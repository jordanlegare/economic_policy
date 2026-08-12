#include "policy_engine.hpp"
#include "robustness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace cad {
namespace {

double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
double sq(double x) { return x * x; }

constexpr int kRobustVerificationDraws = 2800;

struct SectorWeights {
  double trade;
  double import;
};

// Keep the directional aggregation weights identical to the verified V1 engine.
constexpr std::array<SectorWeights, 20> kSectorWeights{{
  {.82,.42}, {.88,.18}, {.16,.10}, {.18,.28}, {.94,.76},
  {.68,.58}, {.30,.72}, {.72,.48}, {.34,.30}, {.22,.20},
  {.10,.12}, {.38,.26}, {.20,.18}, {.28,.24}, {.08,.10},
  {.06,.14}, {.14,.16}, {.18,.52}, {.16,.30}, {.04,.08}
}};

void directional_coverage(const Economy& e, double& us_barrier_coverage,
                          double& ca_barrier_coverage) {
  double ca_export_weight = 0.0;
  double us_export_weight = 0.0;
  us_barrier_coverage = 0.0;
  ca_barrier_coverage = 0.0;
  for (std::size_t i = 0; i < kSectorWeights.size(); ++i) {
    ca_export_weight += kSectorWeights[i].trade;
    us_export_weight += kSectorWeights[i].import;
    us_barrier_coverage += kSectorWeights[i].trade
        * clamp(e.us_sector_coverage[i] / 100.0, 0.0, 1.0);
    ca_barrier_coverage += kSectorWeights[i].import
        * clamp(e.canada_sector_coverage[i] / 100.0, 0.0, 1.0);
  }
  us_barrier_coverage /= std::max(1e-9, ca_export_weight);
  ca_barrier_coverage /= std::max(1e-9, us_export_weight);
}

Scenario simulate_parameterized(const Economy& baseline_e, const Scenario& policy,
                                const StructuralParameters& p, std::uint64_t seed,
                                int draws = kRobustVerificationDraws) {
  Economy e = baseline_e;
  e.us_sector_coverage = policy.applied_us_sector_coverage;
  e.canada_sector_coverage = policy.applied_canada_sector_coverage;

  Scenario s = policy;
  s.sectors = policy.sectors;  // Sector package is frozen by the V2 robustness contract.
  s.rates.fill(0.0);
  s.inflation_path.fill(0.0);
  s.growth_path.fill(0.0);
  s.us_growth_path.fill(0.0);
  s.debt_path.fill(0.0);
  s.cost_path.fill(0.0);
  s.export_path.fill(0.0);
  s.us_export_path.fill(0.0);

  const double deescalation = std::min(policy.negotiated_relief / 100.0,
      clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0));

  double us_barrier_coverage = 0.0;
  double ca_barrier_coverage = 0.0;
  directional_coverage(e, us_barrier_coverage, ca_barrier_coverage);

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
  double inf_sum = 0.0, growth_sum = 0.0, us_growth_sum = 0.0, u_sum = 0.0;
  double debt_sum = 0.0, house_sum = 0.0, cost_sum = 0.0, income_sum = 0.0;
  double export_sum = 0.0, us_export_sum = 0.0, recessions = 0.0;
  std::array<double, 12> rp{}, ip{}, gp{}, ugp{}, dp{}, cp{}, xp{}, uxp{};
  std::vector<double> terminal_debt;
  std::vector<double> terminal_inflation;
  terminal_debt.reserve(static_cast<std::size_t>(draws));
  terminal_inflation.reserve(static_cast<std::size_t>(draws));

  for (int d = 0; d < draws; ++d) {
    double rate = e.policy_rate;
    double inf = e.core_inflation;
    double gap = e.output_gap;
    double u = e.unemployment;
    double debt = e.federal_debt_gdp;
    double housing = e.housing_gap;
    double export_change = 0.0;
    double us_export_change = 0.0;
    double cost = e.inflation;
    bool recession = false;

    for (int q = 0; q < 12; ++q) {
      const double coordinated = policy.productive_share * policy.fiscal_impulse;
      const double us_tariff = std::max(
          0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation));
      const double ca_tariff = std::max(
          0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
      const double exposed_exports = e.exports_to_us_share / 100.0
          * (1.0 - clamp(policy.diversification + e.trade_diversification, 0.0, 0.75));
      const double trade_drag = p.canada_trade_drag_scale
          * exposed_exports * e.exports_gdp / 100.0
          * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0;
      const double us_trade_drag = p.us_retaliation_drag_scale
          * e.imports_from_us_share / 100.0 * e.trade_elasticity
          * (ca_tariff + .45 * e.border_friction) / 100.0;
      const double import_price = e.imports_from_us_share / 100.0
          * e.import_content_consumption / 100.0 * ca_tariff;

      const double rate_target = clamp(p.neutral_rate
          + p.rate_inflation_response * (inf - p.inflation_target)
          + p.rate_output_response * gap, .25, 7.0);
      if (q == 0) {
        rate = clamp(rate + policy.first_move_bp / 100.0, 0.0, 8.0);
      } else {
        rate = clamp(rate + clamp(rate_target - rate,
            -p.max_quarterly_rate_step, p.max_quarterly_rate_step), 0.0, 8.0);
      }

      const double demand = policy.fiscal_impulse * (1.0 - policy.productive_share)
          * p.fiscal_demand_multiplier
          - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;
      const double supply = coordinated * p.productive_supply_multiplier
          + e.productivity_growth * .035;

      export_change = -100.0 * trade_drag + .35 * (e.us_growth - 2.0)
          + 2.0 * policy.diversification + shock(rng) * p.export_shock_sd;
      us_export_change = -100.0 * us_trade_drag + .30 * (e.gdp_growth - 1.5)
          + 1.5 * deescalation + shock(rng) * p.us_export_shock_sd;

      gap = p.output_persistence * gap + demand - trade_drag
          + p.global_growth_sensitivity * (e.global_growth - 2.7)
          + shock(rng) * p.output_shock_sd;
      const double fx = (e.usdcad - 1.34) * p.fx_pass_through;
      inf = p.inflation_persistence * inf
          + p.inflation_expectations_weight * e.inflation_expectations
          + p.phillips_curve_slope * gap + fx - supply
          + p.import_price_pass_through * import_price
          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)
          + shock(rng) * p.inflation_shock_sd;
      const double growth = clamp(1.75 + gap - .18 * e.credit_spread
          + coordinated * .24 + shock(rng) * p.growth_shock_sd, -3.0, 5.5);
      const double us_growth = clamp(e.us_growth + .16 * coordinated + .28 * deescalation
          - .010 * us_tariff - .014 * ca_tariff - .04 * e.border_friction
          + shock(rng) * p.us_growth_shock_sd, -3.0, 5.5);
      u = clamp(u - .10 * (growth - 1.7) + shock(rng) * .035, 3.5, 11.0);
      housing = clamp(.78 * housing - 1.15 * (rate - p.neutral_rate)
          + .08 * (e.population_growth - 1.2) + shock(rng) * .5, -15.0, 30.0);
      const double relief_cost = policy.targeted_relief + e.tariff_relief;
      debt += (-e.fiscal_balance_gdp + policy.fiscal_impulse * .8 + relief_cost * .55
          + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;
      cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
          + .14 * std::max(0.0, e.wage_growth - growth) + .08 * import_price;
      recession = recession || growth < 0.0;

      rp[q] += rate;
      ip[q] += inf;
      gp[q] += growth;
      ugp[q] += us_growth;
      dp[q] += debt;
      cp[q] += cost;
      xp[q] += export_change;
      uxp[q] += us_export_change;
      if (q == 11) {
        inf_sum += inf;
        growth_sum += growth;
        us_growth_sum += us_growth;
        u_sum += u;
        debt_sum += debt;
        house_sum += housing;
        cost_sum += cost;
        income_sum += growth - cost + policy.targeted_relief * .15;
        export_sum += export_change;
        us_export_sum += us_export_change;
        terminal_debt.push_back(debt);
        terminal_inflation.push_back(inf);
      }
    }
    if (recession) recessions += 1.0;
  }

  const double denom = static_cast<double>(draws);
  for (int q = 0; q < 12; ++q) {
    s.rates[q] = rp[q] / denom;
    s.inflation_path[q] = ip[q] / denom;
    s.growth_path[q] = gp[q] / denom;
    s.us_growth_path[q] = ugp[q] / denom;
    s.debt_path[q] = dp[q] / denom;
    s.cost_path[q] = cp[q] / denom;
    s.export_path[q] = xp[q] / denom;
    s.us_export_path[q] = uxp[q] / denom;
  }

  s.inflation = inf_sum / denom;
  s.growth = growth_sum / denom;
  s.us_growth = us_growth_sum / denom;
  s.unemployment = u_sum / denom;
  s.bilateral_growth_floor = std::min(
      *std::min_element(s.growth_path.begin(), s.growth_path.end()),
      *std::min_element(s.us_growth_path.begin(), s.us_growth_path.end()));
  s.sustained_bilateral_growth =
      s.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  s.debt_gdp = debt_sum / denom;
  s.housing_gap = house_sum / denom;
  s.recession_risk = 100.0 * recessions / denom;
  s.cost_of_living = cost_sum / denom;
  s.real_income_growth = income_sum / denom;
  s.export_change = export_sum / denom;
  s.us_export_change = us_export_sum / denom;

  const double effective_us_rate = std::max(
      0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double effective_ca_rate = std::max(
      0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double ledger_elasticity = e.trade_elasticity * p.tariff_revenue_elasticity_scale;
  const double ca_exports = e.canada_exports_to_us_cad
      * std::max(.05, 1.0 - ledger_elasticity * effective_us_rate);
  const double ca_imports = e.canada_imports_from_us_cad
      * std::max(.05, 1.0 - ledger_elasticity * effective_ca_rate);
  s.us_export_expansion_usd = 0.0;
  s.canada_export_redirection_cad = 0.0;
  s.us_tariff_revenue_cad = effective_us_rate * ca_exports;
  s.us_tariff_revenue_usd = s.us_tariff_revenue_cad / e.usdcad;
  s.canada_tariff_revenue_cad = effective_ca_rate * ca_imports;
  s.canada_tariff_revenue_usd = s.canada_tariff_revenue_cad / e.usdcad;
  s.canada_trade_balance_cad = ca_exports - ca_imports;
  s.us_trade_balance_usd = -s.canada_trade_balance_cad / e.usdcad;
  s.trade_balance_gap_usd = std::abs(s.us_trade_balance_usd);
  const double initial_gap = std::abs(e.canada_exports_to_us_cad - e.canada_imports_from_us_cad)
      / e.usdcad;
  s.trade_balance_progress = 100.0
      * (1.0 - s.trade_balance_gap_usd / std::max(.001, initial_gap));
  s.zero_trade_deficit = s.trade_balance_gap_usd < .05;

  std::sort(terminal_debt.begin(), terminal_debt.end());
  std::sort(terminal_inflation.begin(), terminal_inflation.end());
  const auto p90_index = static_cast<std::size_t>(draws * 9 / 10);
  s.debt_stress_p90 = terminal_debt[p90_index];
  s.inflation_stress_p90 = terminal_inflation[p90_index];

  const double mandate_loss = 3.8 * sq(s.inflation - p.inflation_target)
      + 1.2 * sq(std::max(0.0, s.unemployment - 5.8))
      + .7 * sq(std::min(0.0, s.growth)) + .018 * s.recession_risk;
  const double federal_loss = .32 * sq(std::max(0.0, s.debt_gdp - e.federal_debt_gdp))
      + .7 * sq(std::min(0.0, s.growth))
      + .8 * sq(std::max(0.0, s.unemployment - 6.0)) + .012 * sq(s.housing_gap);
  s.boc_score = 100.0 / (1.0 + mandate_loss);
  s.federal_score = 100.0 / (1.0 + federal_loss);

  const double us_inflation_pressure = std::max(
      0.0, e.us_inflation - 2.0 + e.us_tariff_canada * us_barrier_coverage * .025);
  const double us_loss = .55 * sq(std::max(0.0, -s.us_export_change))
      + .8 * sq(us_inflation_pressure)
      + .55 * sq(std::max(0.0, 1.8 - s.us_growth))
      + .25 * sq(e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
  s.us_score = 100.0 / (1.0 + us_loss);
  s.sector_verified = true;
  return s;
}

double conditional_deal_score(const Scenario& s, const Economy& e) {
  const double canada = std::sqrt(std::max(.01, s.boc_score) * std::max(.01, s.federal_score));
  const double ca_weight = clamp(e.canada_priority, 1.0, 100.0);
  const double us_weight = clamp(e.us_priority, 1.0, 100.0);
  const double total = ca_weight + us_weight;
  const double nash = std::exp((ca_weight * std::log(std::max(.01, canada))
      + us_weight * std::log(std::max(.01, s.us_score))) / total);
  const double floor = std::min(canada, s.us_score);
  const double safety = clamp(e.risk_aversion / 100.0, 0.0, 1.0);
  const double tail_penalty = safety * (.10 * s.recession_risk
      + .35 * std::max(0.0, s.inflation_stress_p90 - 3.0)
      + .08 * std::max(0.0, s.debt_stress_p90 - e.federal_debt_gdp - 5.0));

  double sector_floor = std::numeric_limits<double>::infinity();
  double sector_sum = 0.0;
  std::size_t count = 0;
  for (const auto& x : s.sectors) {
    const std::array<double, 6> metrics{
      x.canada_output, x.canada_jobs, -x.canada_prices,
      x.us_output, x.us_jobs, -x.us_prices
    };
    for (double metric : metrics) {
      sector_floor = std::min(sector_floor, metric);
      sector_sum += metric;
      ++count;
    }
  }
  if (!std::isfinite(sector_floor)) sector_floor = 0.0;
  const double sector_average = count ? sector_sum / static_cast<double>(count) : 0.0;
  return .66 * nash + .26 * floor - .05 * tail_penalty
      + .055 * sector_floor + .018 * sector_average;
}

void rank_parameterized_scenario(Scenario& s, const Economy& e) {
  const double raw_score = conditional_deal_score(s, e);
  const bool growth_ok = s.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  s.score = (growth_ok ? 0.0 : -1e6) + raw_score;
}

std::string json_escape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

}  // namespace

Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {
  Result baseline = evaluate(economy);
  auto& summary = baseline.recommendation.robustness;
  summary.parameter_draws = std::max(0, parameter_draws);
  summary.calibration_id = parameters_.calibration_id;
  summary.calibration_vintage = parameters_.calibration_vintage;
  summary.methodology = "outer-structural-ensemble/fixed-verified-sector-packages/common-random-numbers";
  summary.structural_parameters_active = summary.parameter_draws > 0;
  summary.common_random_numbers = summary.parameter_draws > 0;
  summary.sector_packages_reoptimized = false;

  if (summary.parameter_draws == 0 || baseline.scenarios.empty()) {
    summary.classification = "not-evaluated";
    return baseline;
  }

  const std::string baseline_strategy = baseline.recommendation.strategy_id;
  const auto ensemble = draw_structural_parameters(
      parameters_, summary.parameter_draws,
      static_cast<std::uint64_t>(seed_) ^ 0x9e3779b97f4a7c15ULL);

  std::vector<double> selected_scores;
  selected_scores.reserve(ensemble.size());

  // Common random numbers are deliberate: every structural calibration sees
  // the same innovation sequence, and every candidate policy within that draw
  // sees the same sequence. Winner changes therefore come from structural
  // assumptions rather than accidental Monte Carlo seed changes.
  const std::uint64_t common_macro_seed = static_cast<std::uint64_t>(seed_);

  for (const auto& parameters : ensemble) {
    double best_score = -std::numeric_limits<double>::infinity();
    std::string best_strategy;
    for (const auto& policy : baseline.scenarios) {
      Scenario draw = simulate_parameterized(
          economy, policy, parameters, common_macro_seed, kRobustVerificationDraws);
      rank_parameterized_scenario(draw, economy);
      if (draw.id == baseline_strategy) selected_scores.push_back(draw.score);
      if (draw.score > best_score) {
        best_score = draw.score;
        best_strategy = draw.id;
      }
    }
    if (best_strategy == baseline_strategy) ++summary.recommendation_wins;
  }

  summary.recommendation_win_rate = ensemble.empty() ? 0.0
      : static_cast<double>(summary.recommendation_wins)
          / static_cast<double>(ensemble.size());
  if (!selected_scores.empty()) {
    summary.score_mean = std::accumulate(selected_scores.begin(), selected_scores.end(), 0.0)
        / static_cast<double>(selected_scores.size());
    summary.score_p10 = robustness_quantile(selected_scores, 0.10);
    summary.score_p90 = robustness_quantile(selected_scores, 0.90);
  }
  summary.classification = classify_robustness(summary.recommendation_win_rate);

  std::ostringstream note;
  note << " V2 structural robustness: " << summary.recommendation_wins << "/"
       << summary.parameter_draws << " parameter calibrations retain "
       << baseline_strategy << " (" << std::fixed << std::setprecision(1)
       << 100.0 * summary.recommendation_win_rate << "%, " << summary.classification
       << "). Common random numbers isolate parameter sensitivity; verified sector packages "
       << "are held fixed rather than re-optimized inside each draw.";
  baseline.recommendation.explanation += note.str();
  return baseline;
}

std::string robustness_to_json(const Result& result) {
  const auto& r = result.recommendation.robustness;
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"parameterDraws\":" << r.parameter_draws
    << ",\"recommendationWins\":" << r.recommendation_wins
    << ",\"recommendationWinRate\":" << r.recommendation_win_rate
    << ",\"scoreMean\":" << r.score_mean
    << ",\"scoreP10\":" << r.score_p10
    << ",\"scoreP90\":" << r.score_p90
    << ",\"classification\":\"" << json_escape(r.classification)
    << "\",\"calibrationId\":\"" << json_escape(r.calibration_id)
    << "\",\"calibrationVintage\":\"" << json_escape(r.calibration_vintage)
    << "\",\"methodology\":\"" << json_escape(r.methodology)
    << "\",\"structuralParametersActive\":"
    << (r.structural_parameters_active ? "true" : "false")
    << ",\"commonRandomNumbers\":" << (r.common_random_numbers ? "true" : "false")
    << ",\"sectorPackagesReoptimized\":"
    << (r.sector_packages_reoptimized ? "true" : "false") << "}";
  return o.str();
}

}  // namespace cad
