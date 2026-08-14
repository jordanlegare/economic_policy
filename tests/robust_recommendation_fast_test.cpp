#include "calibration.hpp"
#include "interactive_frontier.hpp"
#include "negotiation_support.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"
#include "robust_recommendation_fast.hpp"
#include "robust_recommendation_hot.hpp"
#include "robustness_runtime.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

void close(double a, double b, double tolerance = 1e-12) {
  assert(std::abs(a - b) <= tolerance);
}

void compare_metrics(const cad::RobustPackageMetrics& expected,
                     const cad::RobustPackageMetrics& actual) {
  assert(expected.package_id == actual.package_id);
  assert(expected.strategy_id == actual.strategy_id);
  assert(expected.samples == actual.samples);
  close(expected.canada_mean_surplus, actual.canada_mean_surplus);
  close(expected.us_mean_surplus, actual.us_mean_surplus);
  close(expected.canada_median_surplus, actual.canada_median_surplus);
  close(expected.us_median_surplus, actual.us_median_surplus);
  close(expected.canada_ci95_low, actual.canada_ci95_low);
  close(expected.canada_ci95_high, actual.canada_ci95_high);
  close(expected.us_ci95_low, actual.us_ci95_low);
  close(expected.us_ci95_high, actual.us_ci95_high);
  close(expected.canada_cvar10_surplus, actual.canada_cvar10_surplus);
  close(expected.us_cvar10_surplus, actual.us_cvar10_surplus);
  close(expected.canada_clear_probability, actual.canada_clear_probability);
  close(expected.us_clear_probability, actual.us_clear_probability);
  close(expected.joint_clear_probability, actual.joint_clear_probability);
  close(expected.rank_win_probability, actual.rank_win_probability);
  close(expected.mean_regret, actual.mean_regret);
  close(expected.p95_regret, actual.p95_regret);
  close(expected.max_regret, actual.max_regret);
  close(expected.robust_floor, actual.robust_floor);
  assert(expected.clears_probability_gate == actual.clears_probability_gate);
}

void compare_analysis(const cad::RobustRecommendationAnalysis& expected,
                      const cad::RobustRecommendationAnalysis& actual) {
  assert(expected.second_stage_monte_carlo_draws == actual.second_stage_monte_carlo_draws);
  assert(expected.seed == actual.seed);
  close(expected.cvar_tail_probability, actual.cvar_tail_probability);
  close(expected.required_joint_clear_probability,
        actual.required_joint_clear_probability);
  assert(expected.common_random_numbers == actual.common_random_numbers);
  assert(expected.parameter_uncertainty_included == actual.parameter_uncertainty_included);
  assert(expected.political_acceptance_probability_estimated
      == actual.political_acceptance_probability_estimated);
  assert(expected.empirically_calibrated == actual.empirically_calibrated);
  assert(expected.bounded_memory_two_pass == actual.bounded_memory_two_pass);
  assert(expected.candidate_set_complete == actual.candidate_set_complete);
  assert(expected.uncertainty_grade == actual.uncertainty_grade);
  assert(expected.recommended_package_id == actual.recommended_package_id);
  assert(expected.selection_rule == actual.selection_rule);
  assert(expected.distributions.size() == actual.distributions.size());
  assert(expected.packages.size() == actual.packages.size());
  for (std::size_t i = 0; i < expected.packages.size(); ++i)
    compare_metrics(expected.packages[i], actual.packages[i]);
}

}  // namespace

int main() {
  cad::Economy economy;
  const auto calibration = cad::load_calibration_snapshot(
      "data/calibration/current.snapshot.csv");
  economy = cad::apply_calibration(economy, calibration);

  cad::PolicyEngine engine(20260810);
  const auto result = engine.evaluate(economy);
  const auto negotiation = cad::analyze_negotiation(economy, result);
  assert(!negotiation.frontier.empty());

  constexpr int draws = 600;
  constexpr std::uint64_t seed = 123456;
  const auto expected = cad::analyze_robust_recommendations(
      economy, result, negotiation, calibration, draws, seed);
  const auto fast = cad::analyze_robust_recommendations_fast(
      economy, result, negotiation, calibration, draws, seed);
  const auto hot = cad::analyze_robust_recommendations_hot(
      economy, result, negotiation, calibration, draws, seed);

  compare_analysis(expected, fast);
  // The hot kernel uses sqrt(CA*US) only for the exact 50/50 mandate. The
  // complete package metrics and selected recommendation must remain within the
  // existing numerical contract against the untouched scalar reference.
  compare_analysis(expected, hot);

  const auto runtime = cad::robustness_runtime::snapshot();
  assert(runtime.active == 0);
  assert(runtime.draws == static_cast<std::uint64_t>(draws));
  assert(runtime.packages == negotiation.frontier.size());
  assert(runtime.completed_evaluations == runtime.total_evaluations);
  assert(runtime.total_evaluations
      == 2u * static_cast<std::uint64_t>(draws)
          * static_cast<std::uint64_t>(negotiation.frontier.size()));

  // The interactive response must be bounded without changing the complete
  // server-side analysis. Always expose the selected recommendation separately.
  assert(negotiation.frontier.size() > 3);
  assert(hot.packages.size() > 3);
  const auto compact_negotiation =
      cad::interactive_frontier::negotiation_to_json(negotiation, 3);
  assert(compact_negotiation.find("\"frontierTotal\":"
      + std::to_string(negotiation.frontier.size())) != std::string::npos);
  assert(compact_negotiation.find("\"frontierReturned\":3") != std::string::npos);
  assert(compact_negotiation.find("\"frontierTruncated\":true") != std::string::npos);
  const auto compact_robustness =
      cad::interactive_frontier::robustness_to_json(hot, 3);
  assert(compact_robustness.find("\"packageTotal\":"
      + std::to_string(hot.packages.size())) != std::string::npos);
  assert(compact_robustness.find("\"packageReturned\":3") != std::string::npos);
  assert(compact_robustness.find("\"packageTruncated\":true") != std::string::npos);
  assert(compact_robustness.find("\"recommendedPackage\":{") != std::string::npos);
  assert(compact_robustness.find("\"recommendedPackageId\":\""
      + hot.recommended_package_id + "\"") != std::string::npos);

  // Repeat the production implementation to ensure scheduling does not affect
  // package ordering, random-number use, or the selected recommendation.
  const auto repeat = cad::analyze_robust_recommendations_hot(
      economy, result, negotiation, calibration, draws, seed);
  compare_analysis(hot, repeat);

  // Unequal diplomatic weights must stay on the general log/exp formula.
  cad::Economy weighted = economy;
  weighted.canada_priority = 60.0;
  weighted.us_priority = 40.0;
  const auto weighted_negotiation = cad::analyze_negotiation(weighted, result);
  const auto weighted_expected = cad::analyze_robust_recommendations(
      weighted, result, weighted_negotiation, calibration, 200, seed + 1);
  const auto weighted_hot = cad::analyze_robust_recommendations_hot(
      weighted, result, weighted_negotiation, calibration, 200, seed + 1);
  compare_analysis(weighted_expected, weighted_hot);

  return 0;
}
