#pragma once

#include "calibration.hpp"
#include "negotiation_support.hpp"

#include <algorithm>
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

struct ParameterDistribution {
  std::string name;
  double mean = 0.0;
  double standard_deviation = 0.0;
  double lower_bound = 0.0;
  double upper_bound = 0.0;
  std::string evidence_class;
  std::string source;
};

struct RobustPackageMetrics {
  std::string package_id;
  std::string strategy_id;
  int samples = 0;
  double canada_mean_surplus = 0.0;
  double us_mean_surplus = 0.0;
  double canada_median_surplus = 0.0;
  double us_median_surplus = 0.0;
  double canada_ci95_low = 0.0;
  double canada_ci95_high = 0.0;
  double us_ci95_low = 0.0;
  double us_ci95_high = 0.0;
  double canada_cvar10_surplus = 0.0;
  double us_cvar10_surplus = 0.0;
  double canada_clear_probability = 0.0;
  double us_clear_probability = 0.0;
  double joint_clear_probability = 0.0;
  double rank_win_probability = 0.0;
  double mean_regret = 0.0;
  double p95_regret = 0.0;
  double max_regret = 0.0;
  double robust_floor = 0.0;
  bool clears_probability_gate = false;
};

struct RobustRecommendationAnalysis {
  int second_stage_monte_carlo_draws = 5000;
  std::uint64_t seed = 20260811;
  double cvar_tail_probability = 0.10;
  double required_joint_clear_probability = 0.75;
  bool common_random_numbers = true;
  bool parameter_uncertainty_included = true;
  bool political_acceptance_probability_estimated = false;
  bool empirically_calibrated = false;
  bool bounded_memory_two_pass = true;
  // True only when the upstream joint policy/sector search and the entire
  // epsilon-Pareto bargaining set were both complete.
  bool candidate_set_complete = false;
  std::string uncertainty_grade = "model-risk-provisional";
  std::string recommended_package_id;
  std::string selection_rule = "Reservation gate -> minimize maximum regret -> maximize joint-clear probability -> maximize worst-country CVaR surplus";
  std::vector<ParameterDistribution> distributions;
  std::vector<RobustPackageMetrics> packages;
};

namespace robust_detail {

inline double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline const Scenario* find_scenario(const Result& result, const std::string& id) {
  for (const auto& scenario : result.scenarios) if (scenario.id == id) return &scenario;
  return nullptr;
}

inline negotiation_detail::Terms package_terms(const NegotiationPackage& package) {
  negotiation_detail::Terms terms;
  for (const auto& issue : package.issues) {
    if (issue.id == "us-tariff-relief") terms.us_tariff_relief = clamp(issue.us_move / 100.0, 0.0, 1.0);
    else if (issue.id == "canada-tariff-relief") terms.canada_tariff_relief = clamp(issue.canada_move / 100.0, 0.0, 1.0);
    else if (issue.id == "border-facilitation") terms.border_facilitation = clamp(0.5 * (issue.canada_move + issue.us_move) / 100.0, 0.0, 1.0);
    else if (issue.id == "procurement") terms.procurement_reciprocity = clamp(0.5 * (issue.canada_move + issue.us_move) / 100.0, 0.0, 1.0);
    else if (issue.id == "supply-chain") terms.supply_chain_commitment = clamp(0.5 * (issue.canada_move + issue.us_move) / 100.0, 0.0, 1.0);
  }
  return terms;
}

inline double quantile(std::vector<double> values, double probability) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double p = clamp(probability, 0.0, 1.0) * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(p));
  const auto hi = static_cast<std::size_t>(std::ceil(p));
  if (lo == hi) return values[lo];
  const double weight = p - static_cast<double>(lo);
  return values[lo] * (1.0 - weight) + values[hi] * weight;
}

inline double mean(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

inline double lower_cvar(std::vector<double> values, double alpha) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t count = std::max<std::size_t>(1,
      static_cast<std::size_t>(std::ceil(clamp(alpha, 0.001, 1.0) * values.size())));
  return std::accumulate(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count), 0.0)
      / static_cast<double>(count);
}

inline ParameterDistribution parameter(const CalibrationSnapshot& calibration,
                                       const std::string& key,
                                       double mean_value, double fallback_sigma,
                                       double lo, double hi,
                                       const std::string& fallback_class,
                                       const std::string& fallback_source) {
  ParameterDistribution out{key, mean_value, fallback_sigma, lo, hi, fallback_class, fallback_source};
  const auto it = calibration.parameters.find(key);
  if (it != calibration.parameters.end()) {
    out.evidence_class = it->second.kind;
    out.source = it->second.source_id.empty() ? fallback_source : it->second.source_id;
    if (it->second.standard_error > 1e-9) out.standard_deviation = it->second.standard_error;
  }
  return out;
}

inline ParameterDistribution pass_through_distribution(const CalibrationSnapshot& calibration) {
  if (calibration.pass_through_estimated) {
    double weighted_mean = 0.0, weighted_var = 0.0, weight_sum = 0.0;
    for (const auto& sector : calibration.sectors) {
      if (sector.index < 0 || sector.price_pass_through < 0.0) continue;
      const double weight = std::max(1e-6, sector.canada_export_share + sector.us_export_share);
      weighted_mean += weight * sector.price_pass_through;
      weighted_var += weight * sector.price_pass_through_se * sector.price_pass_through_se;
      weight_sum += weight;
    }
    if (weight_sum > 0.0) {
      return {"price_pass_through", weighted_mean / weight_sum,
              std::sqrt(weighted_var / weight_sum), 0.0, 1.5,
              "empirically-estimated", "sector behavioral estimates"};
    }
  }
  return {"price_pass_through", 0.50, 0.20, 0.0, 1.5,
          "assumption", "uncertified fallback distribution"};
}

inline double decision_value(double canada_surplus, double us_surplus,
                             double canada_weight, double us_weight) {
  if (canada_surplus < 0.0 || us_surplus < 0.0)
    return -25.0 + std::min(canada_surplus, us_surplus);
  return negotiation_detail::generalized_nash(canada_surplus, us_surplus,
      canada_weight, us_weight) + 0.08 * std::min(canada_surplus, us_surplus);
}

inline std::string esc(const std::string& value) {
  return negotiation_detail::escape_json(value);
}

struct RobustDraw {
  double trade_elasticity = 0.0;
  double border = 0.0;
  double pass_through = 0.0;
  double canada_growth = 0.0;
  double us_growth = 0.0;
  double canada_inflation = 0.0;
  double us_inflation = 0.0;
  double reservation_noise = 0.0;
};

struct PackageDrawOutcome {
  double canada_surplus = -100.0;
  double us_surplus = -100.0;
  double value = -125.0;
};

inline std::vector<RobustDraw> generate_draws(
    std::size_t count, std::uint64_t seed,
    const ParameterDistribution& trade_elasticity,
    const ParameterDistribution& border_friction,
    const ParameterDistribution& pass_through,
    const ParameterDistribution& canada_growth,
    const ParameterDistribution& us_growth,
    const ParameterDistribution& canada_inflation,
    const ParameterDistribution& us_inflation,
    const ParameterDistribution& reservation_noise) {
  std::vector<RobustDraw> draws;
  draws.reserve(count);
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> normal(0.0, 1.0);
  for (std::size_t i = 0; i < count; ++i) {
    RobustDraw d;
    d.trade_elasticity = clamp(trade_elasticity.mean
        + trade_elasticity.standard_deviation * normal(rng),
        trade_elasticity.lower_bound, trade_elasticity.upper_bound);
    d.border = clamp(border_friction.mean
        + border_friction.standard_deviation * normal(rng),
        border_friction.lower_bound, border_friction.upper_bound);
    d.pass_through = clamp(pass_through.mean
        + pass_through.standard_deviation * normal(rng),
        pass_through.lower_bound, pass_through.upper_bound);
    d.canada_growth = clamp(canada_growth.mean
        + canada_growth.standard_deviation * normal(rng),
        canada_growth.lower_bound, canada_growth.upper_bound);
    d.us_growth = clamp(us_growth.mean + us_growth.standard_deviation * normal(rng),
        us_growth.lower_bound, us_growth.upper_bound);
    d.canada_inflation = clamp(canada_inflation.mean
        + canada_inflation.standard_deviation * normal(rng),
        canada_inflation.lower_bound, canada_inflation.upper_bound);
    d.us_inflation = clamp(us_inflation.mean
        + us_inflation.standard_deviation * normal(rng),
        us_inflation.lower_bound, us_inflation.upper_bound);
    d.reservation_noise = clamp(reservation_noise.mean
        + reservation_noise.standard_deviation * normal(rng),
        reservation_noise.lower_bound, reservation_noise.upper_bound);
    draws.push_back(d);
  }
  return draws;
}

inline PackageDrawOutcome evaluate_package_draw(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation,
    const NegotiationPackage& package,
    const RobustDraw& draw, double reference_border) {
  PackageDrawOutcome out;
  const Scenario* base = find_scenario(result, package.strategy_id);
  if (!base) return out;

  Economy draw_economy = economy;
  draw_economy.trade_elasticity = draw.trade_elasticity;
  draw_economy.border_friction = draw.border;
  Scenario scenario = *base;
  scenario.growth += draw.canada_growth;
  scenario.us_growth += draw.us_growth;
  scenario.inflation += draw.canada_inflation;
  scenario.boc_score = clamp(scenario.boc_score + 1.4 * draw.canada_growth
      - 1.15 * draw.canada_inflation, 0.01, 100.0);
  scenario.federal_score = clamp(scenario.federal_score + 1.8 * draw.canada_growth
      - 0.85 * draw.canada_inflation, 0.01, 100.0);
  scenario.us_score = clamp(scenario.us_score + 1.6 * draw.us_growth
      - 0.90 * draw.us_inflation, 0.01, 100.0);

  const auto terms = package_terms(package);
  const auto evaluated = negotiation_detail::evaluate_terms(draw_economy, scenario, terms);
  const double residual_us_tariff = draw_economy.us_tariff_canada
      * (1.0 - terms.us_tariff_relief)
      * clamp(draw_economy.exports_to_us_share / 100.0, 0.0, 1.0);
  const double residual_ca_tariff = draw_economy.canada_retaliatory_tariff
      * (1.0 - terms.canada_tariff_relief)
      * clamp(draw_economy.imports_from_us_share / 100.0, 0.0, 1.0);
  const double friction_delta = draw.border - reference_border;
  const double canada_utility = clamp(evaluated.canada_utility
      - 0.025 * draw.pass_through * residual_us_tariff
      - 0.20 * friction_delta, 0.0, 100.0);
  const double us_utility = clamp(evaluated.us_utility
      - 0.025 * draw.pass_through * residual_ca_tariff
      - 0.16 * friction_delta, 0.0, 100.0);

  const double ca_reservation = clamp(negotiation.canada_reservation
      + 0.28 * draw.canada_growth - 0.20 * draw.canada_inflation
      + draw.reservation_noise, 0.0, 99.9);
  const double us_reservation = clamp(negotiation.us_reservation
      + 0.28 * draw.us_growth - 0.18 * draw.us_inflation
      + draw.reservation_noise, 0.0, 99.9);
  out.canada_surplus = canada_utility - ca_reservation;
  out.us_surplus = us_utility - us_reservation;
  out.value = decision_value(out.canada_surplus, out.us_surplus,
      economy.canada_priority, economy.us_priority);
  return out;
}

}  // namespace robust_detail

inline RobustRecommendationAnalysis analyze_robust_recommendations(
    const Economy& economy, const Result& result, const NegotiationAnalysis& negotiation,
    const CalibrationSnapshot& calibration, int draws = 5000, std::uint64_t seed = 20260811) {
  using namespace robust_detail;
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

  const auto trade_elasticity = parameter(calibration, "trade_elasticity", economy.trade_elasticity,
      std::max(0.10, 0.20 * std::max(0.10, economy.trade_elasticity)), 0.05, 3.0,
      "assumption", "model uncertainty envelope");
  const auto border_friction = parameter(calibration, "border_friction", economy.border_friction,
      std::max(0.15, 0.20 * std::max(0.5, economy.border_friction)), 0.0, 10.0,
      "assumption", "model uncertainty envelope");
  const auto pass_through = pass_through_distribution(calibration);
  const ParameterDistribution canada_growth{"canada_growth_shock", 0.0, 0.35, -1.5, 1.5,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution us_growth{"us_growth_shock", 0.0, 0.40, -1.5, 1.5,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution canada_inflation{"canada_inflation_shock", 0.0, 0.30, -1.25, 1.25,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution us_inflation{"us_inflation_shock", 0.0, 0.30, -1.25, 1.25,
      "model-uncertainty", "second-stage macro uncertainty"};
  const ParameterDistribution reservation_noise{"reservation_margin_noise", 0.0, 0.20, -0.75, 0.75,
      "model-uncertainty", "outside-option uncertainty"};
  analysis.distributions = {trade_elasticity, border_friction, pass_through,
                            canada_growth, us_growth, canada_inflation,
                            us_inflation, reservation_noise};

  const std::size_t package_count = negotiation.frontier.size();
  if (package_count == 0 || result.scenarios.empty()) return analysis;
  const std::size_t sample_count = static_cast<std::size_t>(analysis.second_stage_monte_carlo_draws);
  const auto sampled = generate_draws(sample_count, seed, trade_elasticity,
      border_friction, pass_through, canada_growth, us_growth,
      canada_inflation, us_inflation, reservation_noise);

  // Pass 1: evaluate every candidate under the same random draw and retain only
  // the best value/winner for that draw. This is O(draws + packages) memory and
  // removes the former package-count cap without sacrificing common random
  // numbers or regret calculations.
  std::vector<double> best_by_draw(sample_count, -std::numeric_limits<double>::infinity());
  std::vector<std::size_t> win_count(package_count, 0);
  for (std::size_t draw = 0; draw < sample_count; ++draw) {
    std::size_t best_package = 0;
    for (std::size_t p = 0; p < package_count; ++p) {
      const auto outcome = evaluate_package_draw(economy, result, negotiation,
          negotiation.frontier[p], sampled[draw], border_friction.mean);
      if (outcome.value > best_by_draw[draw]) {
        best_by_draw[draw] = outcome.value;
        best_package = p;
      }
    }
    ++win_count[best_package];
  }

  // Pass 2: replay exactly the same stored draws one package at a time. Only
  // one package's sample vectors are resident, while all candidates still get
  // full distribution, CVaR, probability and regret metrics.
  analysis.packages.reserve(package_count);
  for (std::size_t p = 0; p < package_count; ++p) {
    std::vector<double> ca_surplus;
    std::vector<double> us_surplus;
    std::vector<double> regret;
    ca_surplus.reserve(sample_count);
    us_surplus.reserve(sample_count);
    regret.reserve(sample_count);
    std::size_t ca_clear = 0, us_clear = 0, joint_clear = 0;

    for (std::size_t draw = 0; draw < sample_count; ++draw) {
      const auto outcome = evaluate_package_draw(economy, result, negotiation,
          negotiation.frontier[p], sampled[draw], border_friction.mean);
      ca_surplus.push_back(outcome.canada_surplus);
      us_surplus.push_back(outcome.us_surplus);
      if (outcome.canada_surplus >= 0.0) ++ca_clear;
      if (outcome.us_surplus >= 0.0) ++us_clear;
      if (outcome.canada_surplus >= 0.0 && outcome.us_surplus >= 0.0) ++joint_clear;
      regret.push_back(std::max(0.0, best_by_draw[draw] - outcome.value));
    }

    RobustPackageMetrics metrics;
    metrics.package_id = negotiation.frontier[p].id;
    metrics.strategy_id = negotiation.frontier[p].strategy_id;
    metrics.samples = analysis.second_stage_monte_carlo_draws;
    metrics.canada_mean_surplus = mean(ca_surplus);
    metrics.us_mean_surplus = mean(us_surplus);
    metrics.canada_median_surplus = quantile(ca_surplus, 0.50);
    metrics.us_median_surplus = quantile(us_surplus, 0.50);
    metrics.canada_ci95_low = quantile(ca_surplus, 0.025);
    metrics.canada_ci95_high = quantile(ca_surplus, 0.975);
    metrics.us_ci95_low = quantile(us_surplus, 0.025);
    metrics.us_ci95_high = quantile(us_surplus, 0.975);
    metrics.canada_cvar10_surplus = lower_cvar(ca_surplus, analysis.cvar_tail_probability);
    metrics.us_cvar10_surplus = lower_cvar(us_surplus, analysis.cvar_tail_probability);
    metrics.canada_clear_probability = static_cast<double>(ca_clear) / sample_count;
    metrics.us_clear_probability = static_cast<double>(us_clear) / sample_count;
    metrics.joint_clear_probability = static_cast<double>(joint_clear) / sample_count;
    metrics.rank_win_probability = static_cast<double>(win_count[p]) / sample_count;
    metrics.robust_floor = std::min(metrics.canada_cvar10_surplus, metrics.us_cvar10_surplus);
    metrics.clears_probability_gate = metrics.joint_clear_probability + 1e-12
        >= analysis.required_joint_clear_probability;
    metrics.mean_regret = mean(regret);
    metrics.p95_regret = quantile(regret, 0.95);
    metrics.max_regret = *std::max_element(regret.begin(), regret.end());
    analysis.packages.push_back(metrics);
  }

  auto better = [&](const RobustPackageMetrics& a, const RobustPackageMetrics& b) {
    if (a.clears_probability_gate != b.clears_probability_gate) return a.clears_probability_gate;
    if (std::abs(a.max_regret - b.max_regret) > 1e-9) return a.max_regret < b.max_regret;
    if (std::abs(a.joint_clear_probability - b.joint_clear_probability) > 1e-9)
      return a.joint_clear_probability > b.joint_clear_probability;
    if (std::abs(a.robust_floor - b.robust_floor) > 1e-9) return a.robust_floor > b.robust_floor;
    return a.rank_win_probability > b.rank_win_probability;
  };
  const auto best = std::min_element(analysis.packages.begin(), analysis.packages.end(),
      [&](const RobustPackageMetrics& a, const RobustPackageMetrics& b) { return better(a, b); });
  if (best != analysis.packages.end()) analysis.recommended_package_id = best->package_id;
  return analysis;
}

inline std::string robustness_to_json(const RobustRecommendationAnalysis& analysis) {
  using robust_detail::esc;
  std::ostringstream out;
  out << std::fixed << std::setprecision(4);
  out << "{\"secondStageMonteCarloDraws\":" << analysis.second_stage_monte_carlo_draws
      << ",\"seed\":" << analysis.seed
      << ",\"cvarTailProbability\":" << analysis.cvar_tail_probability
      << ",\"requiredJointClearProbability\":" << analysis.required_joint_clear_probability
      << ",\"commonRandomNumbers\":" << (analysis.common_random_numbers ? "true" : "false")
      << ",\"parameterUncertaintyIncluded\":" << (analysis.parameter_uncertainty_included ? "true" : "false")
      << ",\"politicalAcceptanceProbabilityEstimated\":" << (analysis.political_acceptance_probability_estimated ? "true" : "false")
      << ",\"empiricallyCalibrated\":" << (analysis.empirically_calibrated ? "true" : "false")
      << ",\"boundedMemoryTwoPass\":" << (analysis.bounded_memory_two_pass ? "true" : "false")
      << ",\"candidateSetComplete\":" << (analysis.candidate_set_complete ? "true" : "false")
      << ",\"uncertaintyGrade\":\"" << esc(analysis.uncertainty_grade)
      << "\",\"recommendedPackageId\":\"" << esc(analysis.recommended_package_id)
      << "\",\"selectionRule\":\"" << esc(analysis.selection_rule) << "\",\"parameterDistributions\":[";
  for (std::size_t i = 0; i < analysis.distributions.size(); ++i) {
    if (i) out << ',';
    const auto& d = analysis.distributions[i];
    out << "{\"name\":\"" << esc(d.name) << "\",\"mean\":" << d.mean
        << ",\"standardDeviation\":" << d.standard_deviation
        << ",\"lowerBound\":" << d.lower_bound << ",\"upperBound\":" << d.upper_bound
        << ",\"evidenceClass\":\"" << esc(d.evidence_class)
        << "\",\"source\":\"" << esc(d.source) << "\"}";
  }
  out << "],\"packages\":[";
  for (std::size_t i = 0; i < analysis.packages.size(); ++i) {
    if (i) out << ',';
    const auto& p = analysis.packages[i];
    out << "{\"packageId\":\"" << esc(p.package_id) << "\",\"strategyId\":\"" << esc(p.strategy_id)
        << "\",\"samples\":" << p.samples
        << ",\"canadaMeanSurplus\":" << p.canada_mean_surplus
        << ",\"usMeanSurplus\":" << p.us_mean_surplus
        << ",\"canadaMedianSurplus\":" << p.canada_median_surplus
        << ",\"usMedianSurplus\":" << p.us_median_surplus
        << ",\"canadaCi95\":[" << p.canada_ci95_low << ',' << p.canada_ci95_high << ']'
        << ",\"usCi95\":[" << p.us_ci95_low << ',' << p.us_ci95_high << ']'
        << ",\"canadaCvar10Surplus\":" << p.canada_cvar10_surplus
        << ",\"usCvar10Surplus\":" << p.us_cvar10_surplus
        << ",\"canadaClearProbability\":" << p.canada_clear_probability
        << ",\"usClearProbability\":" << p.us_clear_probability
        << ",\"jointClearProbability\":" << p.joint_clear_probability
        << ",\"rankWinProbability\":" << p.rank_win_probability
        << ",\"meanRegret\":" << p.mean_regret << ",\"p95Regret\":" << p.p95_regret
        << ",\"maxRegret\":" << p.max_regret << ",\"robustFloor\":" << p.robust_floor
        << ",\"clearsProbabilityGate\":" << (p.clears_probability_gate ? "true" : "false") << '}';
  }
  out << "],\"interpretation\":\"Probabilities describe model outcomes conditional on declared uncertainty distributions; they are not estimates of political acceptance. candidateSetComplete must be true before the robust package is described as the best package on the declared startup search grid. The complete epsilon-Pareto set is evaluated with a bounded-memory two-pass common-random-number algorithm.\"}";
  return out.str();
}

inline std::string attach_robustness_json(std::string base_json,
                                          const RobustRecommendationAnalysis& analysis) {
  if (base_json.empty() || base_json.back() != '}') return base_json;
  base_json.pop_back();
  base_json += ",\"robustness\":" + robustness_to_json(analysis) + "}";
  return base_json;
}

}  // namespace cad