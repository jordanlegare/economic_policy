#include "calibration.hpp"
#include "negotiation_support.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"

#include <cassert>
#include <cmath>
#include <numeric>

int main() {
  cad::Economy economy;
  const auto calibration = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  economy = cad::apply_calibration(economy, calibration);

  cad::PolicyEngine engine(20260810);
  const auto result = engine.evaluate(economy);
  const auto negotiation = cad::analyze_negotiation(economy, result);
  assert(!negotiation.frontier.empty());
  assert(negotiation.frontier_complete);

  const auto robust = cad::analyze_robust_recommendations(
      economy, result, negotiation, calibration, 600, 123456);
  assert(robust.second_stage_monte_carlo_draws == 600);
  assert(robust.common_random_numbers);
  assert(robust.parameter_uncertainty_included);
  assert(!robust.political_acceptance_probability_estimated);
  assert(!robust.candidate_set_complete,
      "staged PolicyEngine results must never be promoted as global robust-best");
  assert(!robust.recommended_package_id.empty());
  assert(robust.packages.size() == negotiation.frontier.size());
  assert(!robust.distributions.empty());

  double rank_probability_sum = 0.0;
  bool recommendation_found = false;
  for (const auto& metrics : robust.packages) {
    assert(metrics.samples == 600);
    assert(metrics.canada_ci95_low <= metrics.canada_median_surplus + 1e-9);
    assert(metrics.canada_median_surplus <= metrics.canada_ci95_high + 1e-9);
    assert(metrics.us_ci95_low <= metrics.us_median_surplus + 1e-9);
    assert(metrics.us_median_surplus <= metrics.us_ci95_high + 1e-9);
    assert(metrics.canada_cvar10_surplus <= metrics.canada_mean_surplus + 1e-9);
    assert(metrics.us_cvar10_surplus <= metrics.us_mean_surplus + 1e-9);
    assert(metrics.canada_clear_probability >= 0.0 && metrics.canada_clear_probability <= 1.0);
    assert(metrics.us_clear_probability >= 0.0 && metrics.us_clear_probability <= 1.0);
    assert(metrics.joint_clear_probability >= 0.0 && metrics.joint_clear_probability <= 1.0);
    assert(metrics.rank_win_probability >= 0.0 && metrics.rank_win_probability <= 1.0);
    assert(metrics.mean_regret >= -1e-9);
    assert(metrics.p95_regret + 1e-9 >= metrics.mean_regret || metrics.max_regret + 1e-9 >= metrics.mean_regret);
    assert(metrics.max_regret + 1e-9 >= metrics.p95_regret);
    rank_probability_sum += metrics.rank_win_probability;
    if (metrics.package_id == robust.recommended_package_id) recommendation_found = true;
  }
  assert(recommendation_found);
  assert(std::abs(rank_probability_sum - 1.0) < 1e-9);

  const auto repeat = cad::analyze_robust_recommendations(
      economy, result, negotiation, calibration, 600, 123456);
  assert(repeat.recommended_package_id == robust.recommended_package_id);
  assert(repeat.packages.size() == robust.packages.size());
  for (std::size_t i = 0; i < robust.packages.size(); ++i) {
    assert(std::abs(repeat.packages[i].joint_clear_probability
        - robust.packages[i].joint_clear_probability) < 1e-12);
    assert(std::abs(repeat.packages[i].max_regret - robust.packages[i].max_regret) < 1e-12);
  }

  // Candidate-set completeness is a trust property, not an optimizer side
  // effect. A complete upstream joint search plus complete epsilon-frontier
  // should permit robust promotion without changing the numerical selection.
  auto complete_result = result;
  complete_result.recommendation.global_search_complete = true;
  auto complete_negotiation = negotiation;
  complete_negotiation.frontier_complete = true;
  const auto complete_robust = cad::analyze_robust_recommendations(
      economy, complete_result, complete_negotiation, calibration, 200, 123456);
  assert(complete_robust.candidate_set_complete);
  assert(!complete_robust.recommended_package_id.empty());

  const auto json = cad::robustness_to_json(robust);
  assert(json.find("\"secondStageMonteCarloDraws\":600") != std::string::npos);
  assert(json.find("\"politicalAcceptanceProbabilityEstimated\":false") != std::string::npos);
  assert(json.find("\"candidateSetComplete\":false") != std::string::npos);
  assert(json.find("\"maxRegret\"") != std::string::npos);
  assert(json.find("\"canadaCvar10Surplus\"") != std::string::npos);
  const auto complete_json = cad::robustness_to_json(complete_robust);
  assert(complete_json.find("\"candidateSetComplete\":true") != std::string::npos);
  return 0;
}