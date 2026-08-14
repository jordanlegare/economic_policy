#pragma once

#include "compute_executor.hpp"
#include "evaluation_profile.hpp"
#include "robust_recommendation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cad {
namespace robust_fast_detail {

inline double quantile_sorted(const std::vector<double>& values, double probability) {
  if (values.empty()) return 0.0;
  const double p = robust_detail::clamp(probability, 0.0, 1.0)
      * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(p));
  const auto hi = static_cast<std::size_t>(std::ceil(p));
  if (lo == hi) return values[lo];
  const double weight = p - static_cast<double>(lo);
  return values[lo] * (1.0 - weight) + values[hi] * weight;
}

inline double lower_cvar_sorted(const std::vector<double>& values, double alpha) {
  if (values.empty()) return 0.0;
  const std::size_t count = std::max<std::size_t>(1,
      static_cast<std::size_t>(std::ceil(
          robust_detail::clamp(alpha, 0.001, 1.0) * values.size())));
  double sum = 0.0;
  for (std::size_t i = 0; i < count; ++i) sum += values[i];
  return sum / static_cast<double>(count);
}

struct PreparedContext {
  double export_share = 0.0;
  double import_share = 0.0;
  double us_tariff_canada = 0.0;
  double canada_retaliatory_tariff = 0.0;
  double canada_weight = 0.0;
  double us_weight = 0.0;
  double weight_total = 1.0;
};

struct PreparedDraw {
  double canada_relief_capacity = 0.0;
  double us_relief_capacity = 0.0;
  double pass_through = 0.0;
  double friction_delta = 0.0;
  double canada_reservation = 0.0;
  double us_reservation = 0.0;
};

struct ScenarioDrawState {
  double canada_base = 0.01;
  double us_score = 0.01;
};

struct PreparedPackage {
  const NegotiationPackage* package = nullptr;
  std::size_t scenario_slot = std::numeric_limits<std::size_t>::max();
  bool valid = false;
  double export_change = 0.0;
  double us_export_change = 0.0;
  negotiation_detail::Terms terms{};
  double linkage_bonus = 0.0;
  double canada_relief_cost = 0.0;
  double us_relief_cost = 0.0;
  double supply_fiscal_cost = 0.0;
};

inline PreparedContext prepare_context(const Economy& economy) {
  PreparedContext out;
  out.export_share = robust_detail::clamp(
      economy.exports_to_us_share / 100.0, 0.0, 1.0);
  out.import_share = robust_detail::clamp(
      economy.imports_from_us_share / 100.0, 0.0, 1.0);
  out.us_tariff_canada = economy.us_tariff_canada;
  out.canada_retaliatory_tariff = economy.canada_retaliatory_tariff;
  out.canada_weight = robust_detail::clamp(economy.canada_priority, 1.0, 100.0);
  out.us_weight = robust_detail::clamp(economy.us_priority, 1.0, 100.0);
  out.weight_total = out.canada_weight + out.us_weight;
  return out;
}

inline std::vector<PreparedDraw> prepare_draws(
    const std::vector<robust_detail::RobustDraw>& draws,
    const NegotiationAnalysis& negotiation, double reference_border,
    const PreparedContext& context) {
  std::vector<PreparedDraw> prepared(draws.size());
  for (std::size_t i = 0; i < draws.size(); ++i) {
    const auto& draw = draws[i];
    auto& out = prepared[i];
    out.canada_relief_capacity = draw.trade_elasticity
        * context.us_tariff_canada * context.export_share;
    out.us_relief_capacity = draw.trade_elasticity
        * context.canada_retaliatory_tariff * context.import_share;
    out.pass_through = draw.pass_through;
    out.friction_delta = draw.border - reference_border;
    out.canada_reservation = robust_detail::clamp(
        negotiation.canada_reservation
            + 0.28 * draw.canada_growth - 0.20 * draw.canada_inflation
            + draw.reservation_noise,
        0.0, 99.9);
    out.us_reservation = robust_detail::clamp(
        negotiation.us_reservation
            + 0.28 * draw.us_growth - 0.18 * draw.us_inflation
            + draw.reservation_noise,
        0.0, 99.9);
  }
  return prepared;
}

// Scenario shocks are identical for every bargaining package that references the
// same verified policy scenario. Store scenario-major so pass 2 reads contiguous
// state while pass 1 reuses a small working set for each draw.
inline std::vector<ScenarioDrawState> prepare_scenario_draw_states(
    const Result& result, const std::vector<robust_detail::RobustDraw>& draws) {
  const std::size_t samples = draws.size();
  std::vector<ScenarioDrawState> states(result.scenarios.size() * samples);
  compute::parallel_for(result.scenarios.size(), [&](std::size_t scenario_index) {
    const auto& scenario = result.scenarios[scenario_index];
    auto* destination = states.data() + scenario_index * samples;
    for (std::size_t draw_index = 0; draw_index < samples; ++draw_index) {
      const auto& draw = draws[draw_index];
      const double boc_score = robust_detail::clamp(
          scenario.boc_score + 1.4 * draw.canada_growth
              - 1.15 * draw.canada_inflation,
          0.01, 100.0);
      const double federal_score = robust_detail::clamp(
          scenario.federal_score + 1.8 * draw.canada_growth
              - 0.85 * draw.canada_inflation,
          0.01, 100.0);
      destination[draw_index].canada_base = std::sqrt(
          std::max(0.01, boc_score) * std::max(0.01, federal_score));
      destination[draw_index].us_score = robust_detail::clamp(
          scenario.us_score + 1.6 * draw.us_growth - 0.90 * draw.us_inflation,
          0.01, 100.0);
    }
  });
  return states;
}

inline std::vector<PreparedPackage> prepare_packages(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation) {
  std::unordered_map<std::string_view, std::size_t> scenario_slots;
  scenario_slots.reserve(result.scenarios.size());
  for (std::size_t i = 0; i < result.scenarios.size(); ++i)
    scenario_slots.emplace(std::string_view(result.scenarios[i].id), i);

  std::vector<PreparedPackage> prepared(negotiation.frontier.size());
  for (std::size_t i = 0; i < negotiation.frontier.size(); ++i) {
    const auto& package = negotiation.frontier[i];
    auto& out = prepared[i];
    out.package = &package;
    const auto found = scenario_slots.find(std::string_view(package.strategy_id));
    if (found != scenario_slots.end()) {
      out.scenario_slot = found->second;
      out.valid = true;
      out.export_change = result.scenarios[out.scenario_slot].export_change;
      out.us_export_change = result.scenarios[out.scenario_slot].us_export_change;
    }
    out.terms = robust_detail::package_terms(package);

    const double tariff_link = out.terms.us_tariff_relief
        * out.terms.canada_tariff_relief;
    const double implementation_link = out.terms.border_facilitation
        * out.terms.procurement_reciprocity;
    const double resilience_link = out.terms.supply_chain_commitment
        * (0.5 * out.terms.us_tariff_relief
           + 0.5 * out.terms.canada_tariff_relief);
    out.linkage_bonus = 1.25 * tariff_link + 0.85 * implementation_link
        + 0.70 * resilience_link;
    out.canada_relief_cost = out.terms.canada_tariff_relief
        * (0.35 + 0.11 * economy.canada_retaliatory_tariff);
    out.us_relief_cost = out.terms.us_tariff_relief
        * (0.45 + 0.075 * economy.us_tariff_canada);
    out.supply_fiscal_cost = out.terms.supply_chain_commitment
        * (0.45 + 0.10 * std::max(0.0, -economy.fiscal_balance_gdp));
  }
  return prepared;
}

inline robust_detail::PackageDrawOutcome evaluate_prepared_package_draw(
    const PreparedContext& context, const PreparedPackage& prepared,
    const PreparedDraw& draw, const ScenarioDrawState& scenario) {
  robust_detail::PackageDrawOutcome out;
  if (!prepared.valid) return out;
  const auto& terms = prepared.terms;

  const double canada_export_change = prepared.export_change
      + 0.55 * draw.canada_relief_capacity * terms.us_tariff_relief
      + 1.10 * terms.border_facilitation
      + 0.70 * terms.procurement_reciprocity
      + 0.45 * terms.supply_chain_commitment;
  const double us_export_change = prepared.us_export_change
      + 0.55 * draw.us_relief_capacity * terms.canada_tariff_relief
      + 0.95 * terms.border_facilitation
      + 0.95 * terms.procurement_reciprocity
      + 0.35 * terms.supply_chain_commitment;
  const double canada_trade_gain = canada_export_change - prepared.export_change;
  const double us_trade_gain = us_export_change - prepared.us_export_change;

  const double canada_utility = robust_detail::clamp(scenario.canada_base
      + 0.78 * canada_trade_gain
      + 1.35 * terms.border_facilitation
      + 0.75 * terms.procurement_reciprocity
      + 1.10 * terms.supply_chain_commitment
      + prepared.linkage_bonus
      - prepared.canada_relief_cost
      - prepared.supply_fiscal_cost, 0.0, 100.0);
  const double us_utility = robust_detail::clamp(scenario.us_score
      + 0.82 * us_trade_gain
      + 1.20 * terms.border_facilitation
      + 1.10 * terms.procurement_reciprocity
      + 0.65 * terms.supply_chain_commitment
      + prepared.linkage_bonus
      - prepared.us_relief_cost, 0.0, 100.0);

  const double residual_us_tariff = context.us_tariff_canada
      * (1.0 - terms.us_tariff_relief) * context.export_share;
  const double residual_ca_tariff = context.canada_retaliatory_tariff
      * (1.0 - terms.canada_tariff_relief) * context.import_share;
  const double adjusted_canada_utility = robust_detail::clamp(canada_utility
      - 0.025 * draw.pass_through * residual_us_tariff
      - 0.20 * draw.friction_delta, 0.0, 100.0);
  const double adjusted_us_utility = robust_detail::clamp(us_utility
      - 0.025 * draw.pass_through * residual_ca_tariff
      - 0.16 * draw.friction_delta, 0.0, 100.0);

  out.canada_surplus = adjusted_canada_utility - draw.canada_reservation;
  out.us_surplus = adjusted_us_utility - draw.us_reservation;
  if (out.canada_surplus < 0.0 || out.us_surplus < 0.0) {
    out.value = -25.0 + std::min(out.canada_surplus, out.us_surplus);
  } else {
    double nash = 0.0;
    if (out.canada_surplus > 0.0 && out.us_surplus > 0.0) {
      nash = std::exp((context.canada_weight * std::log(out.canada_surplus)
                      + context.us_weight * std::log(out.us_surplus))
                     / context.weight_total);
    }
    out.value = nash + 0.08 * std::min(out.canada_surplus, out.us_surplus);
  }
  return out;
}

}  // namespace robust_fast_detail

// Production end-to-end robustness evaluator. Numerical semantics intentionally
// match analyze_robust_recommendations(): every draw visits packages in the same
// order, means are accumulated before sorting, and package output order remains
// unchanged. Package strings/objects and scenario/draw invariants are prepared
// once; the O(draws*packages) hot loop performs only required scalar arithmetic.
inline RobustRecommendationAnalysis analyze_robust_recommendations_fast(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation,
    const CalibrationSnapshot& calibration,
    int draws = 5000, std::uint64_t seed = 20260811) {
  using namespace robust_detail;
  using robust_fast_detail::evaluate_prepared_package_draw;
  using robust_fast_detail::lower_cvar_sorted;
  using robust_fast_detail::prepare_context;
  using robust_fast_detail::prepare_draws;
  using robust_fast_detail::prepare_packages;
  using robust_fast_detail::prepare_scenario_draw_states;
  using robust_fast_detail::quantile_sorted;

  RobustRecommendationAnalysis analysis;
  analysis.second_stage_monte_carlo_draws = std::max(200, draws);
  analysis.seed = seed;
  analysis.empirically_calibrated = calibration.completeness >= 95.0;
  analysis.candidate_set_complete = result.recommendation.global_search_complete
      && negotiation.frontier_complete;
  analysis.uncertainty_grade = analysis.empirically_calibrated
      ? "empirical-parameter-uncertainty" : "model-risk-provisional";
  analysis.required_joint_clear_probability = clamp(
      0.65 + 0.25 * economy.risk_aversion / 100.0, 0.65, 0.90);

  const auto trade_elasticity = parameter(calibration, "trade_elasticity",
      economy.trade_elasticity,
      std::max(0.10, 0.20 * std::max(0.10, economy.trade_elasticity)),
      0.05, 3.0, "assumption", "model uncertainty envelope");
  const auto border_friction = parameter(calibration, "border_friction",
      economy.border_friction,
      std::max(0.15, 0.20 * std::max(0.5, economy.border_friction)),
      0.0, 10.0, "assumption", "model uncertainty envelope");
  const auto pass_through = pass_through_distribution(calibration);
  const ParameterDistribution canada_growth{
      "canada_growth_shock", 0.0, 0.35, -1.5, 1.5,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution us_growth{
      "us_growth_shock", 0.0, 0.40, -1.5, 1.5,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution canada_inflation{
      "canada_inflation_shock", 0.0, 0.30, -1.25, 1.25,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution us_inflation{
      "us_inflation_shock", 0.0, 0.30, -1.25, 1.25,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution reservation_noise{
      "reservation_margin_noise", 0.0, 0.20, -0.75, 0.75,
      "model-uncertainty", "outside-option uncertainty"};
  analysis.distributions = {trade_elasticity, border_friction, pass_through,
                            canada_growth, us_growth, canada_inflation,
                            us_inflation, reservation_noise};

  const std::size_t package_count = negotiation.frontier.size();
  const std::size_t sample_count = static_cast<std::size_t>(
      analysis.second_stage_monte_carlo_draws);
  evaluation_profile::record_robustness_shape(sample_count, package_count);
  if (package_count == 0 || result.scenarios.empty()) return analysis;

  const auto sampled = generate_draws(sample_count, seed, trade_elasticity,
      border_friction, pass_through, canada_growth, us_growth,
      canada_inflation, us_inflation, reservation_noise);
  const auto context = prepare_context(economy);
  const auto prepared_draws = prepare_draws(
      sampled, negotiation, border_friction.mean, context);
  const auto prepared_packages = prepare_packages(economy, result, negotiation);
  const auto scenario_states = prepare_scenario_draw_states(result, sampled);

  std::vector<double> best_by_draw(
      sample_count, -std::numeric_limits<double>::infinity());
  std::vector<std::size_t> winner_by_draw(sample_count, 0);
  compute::parallel_for(sample_count, [&](std::size_t draw) {
    std::size_t best_package = 0;
    double best_value = -std::numeric_limits<double>::infinity();
    for (std::size_t p = 0; p < package_count; ++p) {
      const auto& package = prepared_packages[p];
      const auto& scenario = scenario_states[
          package.scenario_slot * sample_count + draw];
      const auto outcome = evaluate_prepared_package_draw(
          context, package, prepared_draws[draw], scenario);
      if (outcome.value > best_value) {
        best_value = outcome.value;
        best_package = p;
      }
    }
    best_by_draw[draw] = best_value;
    winner_by_draw[draw] = best_package;
  });

  std::vector<std::size_t> win_count(package_count, 0);
  for (std::size_t draw = 0; draw < sample_count; ++draw)
    ++win_count[winner_by_draw[draw]];

  analysis.packages.resize(package_count);
  compute::parallel_for(package_count, [&](std::size_t p) {
    std::vector<double> ca_surplus;
    std::vector<double> us_surplus;
    std::vector<double> regret;
    ca_surplus.reserve(sample_count);
    us_surplus.reserve(sample_count);
    regret.reserve(sample_count);
    std::size_t ca_clear = 0;
    std::size_t us_clear = 0;
    std::size_t joint_clear = 0;

    const auto& package = prepared_packages[p];
    const auto* scenario_series = scenario_states.data()
        + package.scenario_slot * sample_count;
    for (std::size_t draw = 0; draw < sample_count; ++draw) {
      const auto outcome = evaluate_prepared_package_draw(
          context, package, prepared_draws[draw], scenario_series[draw]);
      ca_surplus.push_back(outcome.canada_surplus);
      us_surplus.push_back(outcome.us_surplus);
      if (outcome.canada_surplus >= 0.0) ++ca_clear;
      if (outcome.us_surplus >= 0.0) ++us_clear;
      if (outcome.canada_surplus >= 0.0 && outcome.us_surplus >= 0.0)
        ++joint_clear;
      regret.push_back(std::max(0.0, best_by_draw[draw] - outcome.value));
    }

    RobustPackageMetrics metrics;
    metrics.package_id = package.package->id;
    metrics.strategy_id = package.package->strategy_id;
    metrics.samples = analysis.second_stage_monte_carlo_draws;
    metrics.canada_mean_surplus = mean(ca_surplus);
    metrics.us_mean_surplus = mean(us_surplus);
    metrics.mean_regret = mean(regret);
    metrics.canada_clear_probability = static_cast<double>(ca_clear) / sample_count;
    metrics.us_clear_probability = static_cast<double>(us_clear) / sample_count;
    metrics.joint_clear_probability = static_cast<double>(joint_clear) / sample_count;
    metrics.rank_win_probability = static_cast<double>(win_count[p]) / sample_count;

    std::sort(ca_surplus.begin(), ca_surplus.end());
    std::sort(us_surplus.begin(), us_surplus.end());
    std::sort(regret.begin(), regret.end());

    metrics.canada_median_surplus = quantile_sorted(ca_surplus, 0.50);
    metrics.us_median_surplus = quantile_sorted(us_surplus, 0.50);
    metrics.canada_ci95_low = quantile_sorted(ca_surplus, 0.025);
    metrics.canada_ci95_high = quantile_sorted(ca_surplus, 0.975);
    metrics.us_ci95_low = quantile_sorted(us_surplus, 0.025);
    metrics.us_ci95_high = quantile_sorted(us_surplus, 0.975);
    metrics.canada_cvar10_surplus = lower_cvar_sorted(
        ca_surplus, analysis.cvar_tail_probability);
    metrics.us_cvar10_surplus = lower_cvar_sorted(
        us_surplus, analysis.cvar_tail_probability);
    metrics.p95_regret = quantile_sorted(regret, 0.95);
    metrics.max_regret = regret.empty() ? 0.0 : regret.back();
    metrics.robust_floor = std::min(
        metrics.canada_cvar10_surplus, metrics.us_cvar10_surplus);
    metrics.clears_probability_gate = metrics.joint_clear_probability + 1e-12
        >= analysis.required_joint_clear_probability;
    analysis.packages[p] = std::move(metrics);
  });

  auto better = [&](const RobustPackageMetrics& a,
                    const RobustPackageMetrics& b) {
    if (a.clears_probability_gate != b.clears_probability_gate)
      return a.clears_probability_gate;
    if (std::abs(a.max_regret - b.max_regret) > 1e-9)
      return a.max_regret < b.max_regret;
    if (std::abs(a.joint_clear_probability - b.joint_clear_probability) > 1e-9)
      return a.joint_clear_probability > b.joint_clear_probability;
    if (std::abs(a.robust_floor - b.robust_floor) > 1e-9)
      return a.robust_floor > b.robust_floor;
    return a.rank_win_probability > b.rank_win_probability;
  };
  const auto best = std::min_element(analysis.packages.begin(),
      analysis.packages.end(), [&](const RobustPackageMetrics& a,
                                    const RobustPackageMetrics& b) {
        return better(a, b);
      });
  if (best != analysis.packages.end())
    analysis.recommended_package_id = best->package_id;
  return analysis;
}

}  // namespace cad
