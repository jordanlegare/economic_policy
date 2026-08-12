#include "welfare_sensitivity.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>

namespace cad {
namespace {

double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }

const Scenario* selected_scenario(const Result& result) {
  for (const auto& scenario : result.scenarios)
    if (scenario.id == result.recommendation.strategy_id) return &scenario;
  return result.scenarios.empty() ? nullptr : &result.scenarios.front();
}

bool same_controls(const Scenario& a, const Scenario& b) {
  return std::abs(a.first_move_bp - b.first_move_bp) < 1e-9
      && std::abs(a.fiscal_impulse - b.fiscal_impulse) < 1e-9
      && std::abs(a.productive_share - b.productive_share) < 1e-9
      && std::abs(a.negotiated_relief - b.negotiated_relief) < 1e-9
      && std::abs(a.targeted_relief - b.targeted_relief) < 1e-9
      && std::abs(a.diversification - b.diversification) < 1e-9;
}

bool same_package(const Scenario& a, const Scenario& b) {
  for (std::size_t i = 0; i < a.applied_us_sector_coverage.size(); ++i) {
    if (std::abs(a.applied_us_sector_coverage[i] - b.applied_us_sector_coverage[i]) > 1e-9)
      return false;
    if (std::abs(a.applied_canada_sector_coverage[i] - b.applied_canada_sector_coverage[i]) > 1e-9)
      return false;
  }
  return true;
}

std::string classify(double rate) {
  if (rate >= .80) return "preference-robust";
  if (rate >= .60) return "moderately-preference-robust";
  if (rate >= .40) return "preference-fragile";
  return "preference-unstable";
}

std::string esc(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

std::vector<WelfarePreferenceProfile> make_welfare_preference_grid(const Economy& economy) {
  const double total = std::max(1e-9, std::max(0.0, economy.canada_priority)
      + std::max(0.0, economy.us_priority));
  const double base_ca = 100.0 * std::max(0.0, economy.canada_priority) / total;
  const double base_risk = clamp(economy.risk_aversion, 0.0, 100.0);
  std::vector<WelfarePreferenceProfile> out;
  for (double priority_shift : {-15.0, 0.0, 15.0}) {
    for (double risk_shift : {-25.0, 0.0, 25.0}) {
      WelfarePreferenceProfile p;
      p.canada_priority = clamp(base_ca + priority_shift, 5.0, 95.0);
      p.us_priority = 100.0 - p.canada_priority;
      p.risk_aversion = clamp(base_risk + risk_shift, 0.0, 100.0);
      p.priority_shift_points = p.canada_priority - base_ca;
      p.risk_shift_points = p.risk_aversion - base_risk;
      if (std::abs(priority_shift) < 1e-9 && std::abs(risk_shift) < 1e-9) {
        p.profile_id = "reference";
      } else {
        std::ostringstream id;
        id << "priority" << (priority_shift >= 0 ? "+" : "")
           << static_cast<int>(priority_shift) << "-risk"
           << (risk_shift >= 0 ? "+" : "") << static_cast<int>(risk_shift);
        p.profile_id = id.str();
      }
      bool duplicate = false;
      for (const auto& q : out)
        duplicate = duplicate || (std::abs(q.canada_priority - p.canada_priority) < 1e-9
            && std::abs(q.risk_aversion - p.risk_aversion) < 1e-9);
      if (!duplicate) out.push_back(p);
    }
  }
  return out;
}

WelfareSensitivitySummary evaluate_welfare_sensitivity(
    const PolicyEngine& engine, const Economy& economy,
    const std::vector<WelfarePreferenceProfile>& profiles) {
  WelfareSensitivitySummary out;
  const Result reference_result = engine.evaluate(economy);
  const Scenario* reference = selected_scenario(reference_result);
  if (!reference) return out;
  out.reference_strategy = reference_result.recommendation.strategy_id;

  const auto active = profiles.empty() ? make_welfare_preference_grid(economy) : profiles;
  out.profile_count = static_cast<int>(active.size());
  if (active.empty()) return out;

  std::map<std::string, int> wins;
  double nearest_priority = 1e100, nearest_risk = 1e100;
  bool fairness_set = false;

  for (const auto& profile : active) {
    Economy candidate = economy;
    candidate.canada_priority = profile.canada_priority;
    candidate.us_priority = profile.us_priority;
    candidate.risk_aversion = profile.risk_aversion;
    const Result result = engine.evaluate(candidate);
    const Scenario* selected = selected_scenario(result);
    if (!selected) continue;

    WelfareSensitivityCase c;
    c.profile_id = profile.profile_id;
    c.strategy_id = result.recommendation.strategy_id;
    c.canada_priority = result.recommendation.canada_priority;
    c.us_priority = result.recommendation.us_priority;
    c.risk_aversion = result.recommendation.risk_aversion;
    c.priority_shift_points = profile.priority_shift_points;
    c.risk_shift_points = profile.risk_shift_points;
    c.fairness_score = std::min(result.recommendation.verified_canada_score,
                                result.recommendation.verified_us_score);
    c.first_move_bp = selected->first_move_bp;
    c.fiscal_impulse = selected->fiscal_impulse;
    c.same_strategy_family = c.strategy_id == out.reference_strategy;
    c.same_reference_controls = c.same_strategy_family && same_controls(*selected, *reference);
    c.same_sector_package = c.same_reference_controls && same_package(*selected, *reference);
    c.growth_constraint_met = result.recommendation.growth_constraint_met;
    c.mandate_weights_fixed = result.recommendation.mandate_weights_fixed;

    if (c.same_reference_controls) ++out.exact_recommendation_wins;
    if (c.same_strategy_family) ++out.strategy_family_wins;
    if (c.same_sector_package) ++out.sector_package_wins;
    if (!c.same_reference_controls) {
      ++out.recommendation_switches;
      if (std::abs(c.risk_shift_points) < 1e-9 && std::abs(c.priority_shift_points) > 1e-9) {
        out.priority_switch_observed = true;
        nearest_priority = std::min(nearest_priority, std::abs(c.priority_shift_points));
      }
      if (std::abs(c.priority_shift_points) < 1e-9 && std::abs(c.risk_shift_points) > 1e-9) {
        out.risk_switch_observed = true;
        nearest_risk = std::min(nearest_risk, std::abs(c.risk_shift_points));
      }
    }
    out.all_growth_constraints_met = out.all_growth_constraints_met && c.growth_constraint_met;
    out.all_mandate_weights_fixed = out.all_mandate_weights_fixed && c.mandate_weights_fixed;
    if (!fairness_set) {
      out.fairness_min = out.fairness_max = c.fairness_score;
      fairness_set = true;
    } else {
      out.fairness_min = std::min(out.fairness_min, c.fairness_score);
      out.fairness_max = std::max(out.fairness_max, c.fairness_score);
    }
    ++wins[c.strategy_id];
    out.cases.push_back(c);
  }

  const double n = static_cast<double>(std::max<std::size_t>(1, out.cases.size()));
  out.exact_recommendation_retention_rate = out.exact_recommendation_wins / n;
  out.strategy_family_retention_rate = out.strategy_family_wins / n;
  out.sector_package_retention_rate = out.sector_package_wins / n;
  if (out.priority_switch_observed) out.nearest_priority_switch_points = nearest_priority;
  if (out.risk_switch_observed) out.nearest_risk_switch_points = nearest_risk;
  out.classification = classify(out.exact_recommendation_retention_rate);

  for (const auto& entry : wins)
    out.alternatives.push_back({entry.first, entry.second, entry.second / n});
  std::sort(out.alternatives.begin(), out.alternatives.end(),
      [](const WelfareAlternativeSupport& a, const WelfareAlternativeSupport& b) {
        return a.wins != b.wins ? a.wins > b.wins : a.strategy_id < b.strategy_id;
      });
  return out;
}

std::string welfare_sensitivity_to_json(const WelfareSensitivitySummary& s) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"methodology\":\"" << esc(s.methodology)
    << "\",\"profileCount\":" << s.profile_count
    << ",\"referenceStrategy\":\"" << esc(s.reference_strategy)
    << "\",\"exactRecommendationRetentionRate\":" << s.exact_recommendation_retention_rate
    << ",\"strategyFamilyRetentionRate\":" << s.strategy_family_retention_rate
    << ",\"sectorPackageRetentionRate\":" << s.sector_package_retention_rate
    << ",\"recommendationSwitches\":" << s.recommendation_switches
    << ",\"prioritySwitchObserved\":" << (s.priority_switch_observed ? "true" : "false")
    << ",\"nearestPrioritySwitchPoints\":" << s.nearest_priority_switch_points
    << ",\"riskSwitchObserved\":" << (s.risk_switch_observed ? "true" : "false")
    << ",\"nearestRiskSwitchPoints\":" << s.nearest_risk_switch_points
    << ",\"fairnessMin\":" << s.fairness_min << ",\"fairnessMax\":" << s.fairness_max
    << ",\"allGrowthConstraintsMet\":" << (s.all_growth_constraints_met ? "true" : "false")
    << ",\"allMandateWeightsFixed\":" << (s.all_mandate_weights_fixed ? "true" : "false")
    << ",\"classification\":\"" << esc(s.classification) << "\",\"alternatives\":[";
  for (std::size_t i = 0; i < s.alternatives.size(); ++i) {
    if (i) o << ',';
    const auto& a = s.alternatives[i];
    o << "{\"strategyId\":\"" << esc(a.strategy_id) << "\",\"wins\":" << a.wins
      << ",\"winRate\":" << a.win_rate << "}";
  }
  o << "],\"cases\":[";
  for (std::size_t i = 0; i < s.cases.size(); ++i) {
    if (i) o << ',';
    const auto& c = s.cases[i];
    o << "{\"profileId\":\"" << esc(c.profile_id) << "\",\"strategyId\":\""
      << esc(c.strategy_id) << "\",\"canadaPriority\":" << c.canada_priority
      << ",\"usPriority\":" << c.us_priority << ",\"riskAversion\":" << c.risk_aversion
      << ",\"priorityShiftPoints\":" << c.priority_shift_points
      << ",\"riskShiftPoints\":" << c.risk_shift_points
      << ",\"fairnessScore\":" << c.fairness_score
      << ",\"sameReferenceControls\":" << (c.same_reference_controls ? "true" : "false")
      << ",\"sameStrategyFamily\":" << (c.same_strategy_family ? "true" : "false")
      << ",\"sameSectorPackage\":" << (c.same_sector_package ? "true" : "false")
      << ",\"growthConstraintMet\":" << (c.growth_constraint_met ? "true" : "false")
      << ",\"mandateWeightsFixed\":" << (c.mandate_weights_fixed ? "true" : "false") << "}";
  }
  o << "]}";
  return o.str();
}

}  // namespace cad
