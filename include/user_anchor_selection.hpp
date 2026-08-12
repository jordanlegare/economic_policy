#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>

namespace cad {
namespace user_anchor_detail {

inline double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline double canada_score(const Scenario& scenario) {
  return std::sqrt(std::max(0.01, scenario.boc_score)
      * std::max(0.01, scenario.federal_score));
}

inline double min_sector_metric(const Scenario& scenario) {
  double out = std::numeric_limits<double>::infinity();
  for (const auto& sector : scenario.sectors) {
    out = std::min(out, sector.canada_output);
    out = std::min(out, sector.canada_jobs);
    out = std::min(out, -sector.canada_prices);
    out = std::min(out, sector.us_output);
    out = std::min(out, sector.us_jobs);
    out = std::min(out, -sector.us_prices);
  }
  return std::isfinite(out) ? out : 0.0;
}

// Directional weights mirror the policy engine's sectoral trade intensities.
// U.S. barriers are weighted by Canadian export exposure; Canadian retaliation
// is weighted by U.S. sales into Canada. The distance is normalized to a
// headline-equivalent 0-100 scale, so a 25% effective barrier reduction is a
// comparable posture move whether the user's headline tariff is 5% or 50%.
inline double trade_posture_distance(const Economy& anchor, const Scenario& scenario) {
  constexpr std::array<double, 20> ca_export_weight{
      .82,.88,.16,.18,.94,.68,.30,.72,.34,.22,.10,.38,.20,.28,.08,.06,.14,.18,.16,.04};
  constexpr std::array<double, 20> us_export_weight{
      .42,.18,.10,.28,.76,.58,.72,.48,.30,.20,.12,.26,.18,.24,.10,.14,.16,.52,.30,.08};

  const double remaining = 1.0 - clamp(scenario.negotiated_relief / 100.0, 0.0, 1.0);
  double distance = 0.0;
  double weight = 0.0;
  for (std::size_t i = 0; i < ca_export_weight.size(); ++i) {
    if (anchor.us_tariff_canada > 1e-9) {
      const double submitted = clamp(anchor.us_sector_coverage[i] / 100.0, 0.0, 1.0);
      const double selected = remaining
          * clamp(scenario.applied_us_sector_coverage[i] / 100.0, 0.0, 1.0);
      distance += ca_export_weight[i] * 100.0 * std::abs(selected - submitted);
      weight += ca_export_weight[i];
    }
    if (anchor.canada_retaliatory_tariff > 1e-9) {
      const double submitted = clamp(anchor.canada_sector_coverage[i] / 100.0, 0.0, 1.0);
      const double selected = remaining
          * clamp(scenario.applied_canada_sector_coverage[i] / 100.0, 0.0, 1.0);
      distance += us_export_weight[i] * 100.0 * std::abs(selected - submitted);
      weight += us_export_weight[i];
    }
  }
  return weight > 1e-12 ? distance / weight : 0.0;
}

inline bool materially_different(double a, double b, double tolerance = 1e-9) {
  return std::abs(a - b) > tolerance;
}

inline bool user_modified_from_baseline(const Economy& submitted, const Economy& calibrated) {
  if (materially_different(submitted.us_tariff_canada, calibrated.us_tariff_canada)
      || materially_different(submitted.canada_retaliatory_tariff, calibrated.canada_retaliatory_tariff)
      || materially_different(submitted.canada_priority, calibrated.canada_priority)
      || materially_different(submitted.us_priority, calibrated.us_priority)
      || materially_different(submitted.risk_aversion, calibrated.risk_aversion)
      || materially_different(submitted.cooperation_ceiling, calibrated.cooperation_ceiling)) return true;
  for (std::size_t i = 0; i < submitted.us_sector_coverage.size(); ++i) {
    if (materially_different(submitted.us_sector_coverage[i], calibrated.us_sector_coverage[i])
        || materially_different(submitted.canada_sector_coverage[i], calibrated.canada_sector_coverage[i])) return true;
  }
  return false;
}

}  // namespace user_anchor_detail

inline void apply_user_anchor_selection(const Economy& submitted,
                                        const Economy& calibrated_baseline,
                                        Result& result) {
  using namespace user_anchor_detail;
  if (result.scenarios.empty()) return;

  const double baseline_canada = result.recommendation.baseline_canada_score;
  const double baseline_us = result.recommendation.baseline_us_score;
  double best_verified_score = -std::numeric_limits<double>::infinity();

  for (auto& scenario : result.scenarios) {
    scenario.trade_posture_distance = trade_posture_distance(submitted, scenario);
    scenario.anchor_win_win = scenario.sector_verified
        && canada_score(scenario) + 1e-9 >= baseline_canada
        && scenario.us_score + 1e-9 >= baseline_us
        && scenario.bilateral_growth_floor + 1e-9 >= submitted.minimum_bilateral_growth;
    if (scenario.anchor_win_win)
      best_verified_score = std::max(best_verified_score, scenario.score);
  }

  if (!std::isfinite(best_verified_score)) return;
  result.recommendation.best_verified_score = best_verified_score;

  const bool customized = user_modified_from_baseline(submitted, calibrated_baseline);
  if (!customized) return;  // Preserve the globally highest-welfare calibrated opening.

  const double tolerance = result.recommendation.user_anchor_welfare_tolerance;
  auto chosen = result.scenarios.end();
  for (auto it = result.scenarios.begin(); it != result.scenarios.end(); ++it) {
    if (!it->anchor_win_win || it->score + tolerance + 1e-9 < best_verified_score) continue;
    if (chosen == result.scenarios.end()
        || it->trade_posture_distance + 1e-9 < chosen->trade_posture_distance
        || (std::abs(it->trade_posture_distance - chosen->trade_posture_distance) <= 1e-9
            && it->score > chosen->score + 1e-9)) {
      chosen = it;
    }
  }
  if (chosen == result.scenarios.end()) return;

  // Keep legacy consumers simple: scenarios[0] remains the automatically
  // selected package, but the selection is now lexicographic—verified welfare
  // first, then user-posture proximity inside the stated practical score band.
  if (chosen != result.scenarios.begin())
    std::rotate(result.scenarios.begin(), chosen, chosen + 1);

  const auto& selected = result.scenarios.front();
  auto& rec = result.recommendation;
  rec.user_anchor_selection_active = true;
  rec.strategy_id = selected.id;
  rec.us_sector_coverage = selected.applied_us_sector_coverage;
  rec.canada_sector_coverage = selected.applied_canada_sector_coverage;
  rec.verified_canada_score = canada_score(selected);
  rec.verified_us_score = selected.us_score;
  rec.verified_min_sector_metric = min_sector_metric(selected);
  rec.selected_trade_posture_distance = selected.trade_posture_distance;
  rec.growth_constraint_met = selected.bilateral_growth_floor + 1e-9
      >= submitted.minimum_bilateral_growth;
  rec.verified_win_win = selected.anchor_win_win && rec.growth_constraint_met;

  std::ostringstream explanation;
  explanation << rec.explanation
      << " User-steered re-optimization is active: the engine first finds the maximum verified bilateral-welfare score, then among packages within "
      << rec.user_anchor_welfare_tolerance
      << " score point of that maximum selects the package with the smallest trade-posture deviation from the submitted tariffs and sector coverage. The exact submitted posture remains available as the zero-distance starting-posture candidate.";
  rec.explanation = explanation.str();

  result.signal = selected.first_move_bp > 0 ? "Raise 25 bp"
      : selected.first_move_bp < 0 ? "Cut 25 bp" : "Hold & coordinate";
  std::ostringstream rationale;
  rationale << "The " << selected.name
      << " is the closest verified trade posture to the user's submitted scenario among packages within "
      << rec.user_anchor_welfare_tolerance
      << " score point of the maximum verified bilateral-welfare result. It preserves the user's direction where economically near-equivalent while still enforcing the national no-worse test and bilateral growth constraint.";
  result.rationale = rationale.str();
}

}  // namespace cad