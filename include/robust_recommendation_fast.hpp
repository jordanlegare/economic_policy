#pragma once

#include "compute_executor.hpp"
#include "evaluation_profile.hpp"
#include "robust_recommendation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

}  // namespace robust_fast_detail

// Production end-to-end robustness evaluator. Numerical semantics intentionally
// match analyze_robust_recommendations(): every draw visits packages in the same
// order, means are accumulated before sorting, and package output order remains
// unchanged. Only independent draw/package work is scheduled concurrently, and
// each sample vector is sorted once instead of repeatedly copying/sorting it.
inline RobustRecommendationAnalysis analyze_robust_recommendations_fast(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation,
    const CalibrationSnapshot& calibration,
    int draws = 5000, std::uint64_t seed = 20260811) {
  using namespace robust_detail;
  using robust_fast_detail::lower_cvar_sorted;
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

  // Pass 1 preserves the original package-order comparison for each draw. Draws
  // themselves are independent, so schedule them concurrently and fold winner
  // counts afterward in deterministic draw order.
  std::vector<double> best_by_draw(
      sample_count, -std::numeric_limits<double>::infinity());
  std::vector<std::size_t> winner_by_draw(sample_count, 0);
  compute::parallel_for(sample_count, [&](std::size_t draw) {
    std::size_t best_package = 0;
    double best_value = -std::numeric_limits<double>::infinity();
    for (std::size_t p = 0; p < package_count; ++p) {
      const auto outcome = evaluate_package_draw(economy, result, negotiation,
          negotiation.frontier[p], sampled[draw], border_friction.mean);
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

  // Pass 2 is independent by package. Preserve each package's draw-order sums,
  // then sort each metric family exactly once for all requested quantiles/CVaR.
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

    for (std::size_t draw = 0; draw < sample_count; ++draw) {
      const auto outcome = evaluate_package_draw(economy, result, negotiation,
          negotiation.frontier[p], sampled[draw], border_friction.mean);
      ca_surplus.push_back(outcome.canada_surplus);
      us_surplus.push_back(outcome.us_surplus);
      if (outcome.canada_surplus >= 0.0) ++ca_clear;
      if (outcome.us_surplus >= 0.0) ++us_clear;
      if (outcome.canada_surplus >= 0.0 && outcome.us_surplus >= 0.0)
        ++joint_clear;
      regret.push_back(std::max(0.0, best_by_draw[draw] - outcome.value));
    }

    RobustPackageMetrics metrics;
    metrics.package_id = negotiation.frontier[p].id;
    metrics.strategy_id = negotiation.frontier[p].strategy_id;
    metrics.samples = analysis.second_stage_monte_carlo_draws;

    // Keep original draw-order summation before changing vector order.
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
