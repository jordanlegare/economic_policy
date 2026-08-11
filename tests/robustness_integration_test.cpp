#include "policy_engine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

int main() {
  cad::Economy economy;

  // Zero requested draws is an explicit no-op around the ordinary evaluation.
  cad::PolicyEngine engine(20260810);
  const auto ordinary = engine.evaluate(economy);
  const auto skipped = engine.evaluate_robust(economy, 0);
  assert(skipped.recommendation.strategy_id == ordinary.recommendation.strategy_id);
  assert(skipped.recommendation.robustness.parameter_draws == 0);
  assert(skipped.recommendation.robustness.classification == "not-evaluated");

  // With zero parameter uncertainty, V2 must recover the verified baseline
  // ranking exactly because it uses the same default coefficients, 2,800 draws,
  // sector packages and common-random-number seed as the verification layer.
  cad::StructuralParameters exact_parameters;
  exact_parameters.uncertainty_scale = 0.0;
  cad::PolicyEngine exact_engine(20260810, exact_parameters);
  const auto exact = exact_engine.evaluate_robust(economy, 2);
  const auto& exact_summary = exact.recommendation.robustness;
  assert(exact_summary.parameter_draws == 2);
  assert(exact_summary.recommendation_wins == 2);
  assert(std::abs(exact_summary.recommendation_win_rate - 1.0) < 1e-15);
  assert(exact_summary.classification == "robust");
  assert(exact_summary.structural_parameters_active);
  assert(exact_summary.common_random_numbers);
  assert(!exact_summary.sector_packages_reoptimized);
  assert(exact_summary.methodology.find("outer-structural-ensemble") != std::string::npos);
  assert(exact.recommendation.explanation.find("V2 structural robustness") != std::string::npos);

  // Structural parameters must actually affect model outcomes. This is the
  // regression guard against the previous scaffold, where PolicyEngine owned a
  // parameter object but evaluate_robust() ultimately called equations that
  // ignored it.
  cad::StructuralParameters shifted_parameters = exact_parameters;
  shifted_parameters.neutral_rate = 4.0;
  shifted_parameters.phillips_curve_slope = 0.22;
  cad::PolicyEngine shifted_engine(20260810, shifted_parameters);
  const auto shifted = shifted_engine.evaluate_robust(economy, 1);
  assert(std::abs(shifted.recommendation.robustness.score_mean
      - exact_summary.score_mean) > 1e-6);

  // A sampled ensemble remains deterministic for a fixed engine seed and
  // calibration. Common random numbers ensure the repeatability covers both
  // parameter draws and macro innovations.
  const auto robust_a = engine.evaluate_robust(economy, 3);
  const auto robust_b = engine.evaluate_robust(economy, 3);
  const auto& a = robust_a.recommendation.robustness;
  const auto& b = robust_b.recommendation.robustness;
  assert(a.parameter_draws == 3);
  assert(a.recommendation_wins >= 0 && a.recommendation_wins <= 3);
  assert(a.recommendation_win_rate >= 0.0 && a.recommendation_win_rate <= 1.0);
  assert(a.score_p10 <= a.score_p90 + 1e-12);
  assert(a.classification != "not-evaluated");
  assert(a.recommendation_wins == b.recommendation_wins);
  assert(std::abs(a.recommendation_win_rate - b.recommendation_win_rate) < 1e-15);
  assert(std::abs(a.score_mean - b.score_mean) < 1e-12);
  assert(std::abs(a.score_p10 - b.score_p10) < 1e-12);
  assert(std::abs(a.score_p90 - b.score_p90) < 1e-12);
  assert(a.classification == b.classification);

  const auto robustness_json = cad::robustness_to_json(robust_a);
  assert(robustness_json.find("\"structuralParametersActive\":true") != std::string::npos);
  assert(robustness_json.find("\"commonRandomNumbers\":true") != std::string::npos);
  assert(robustness_json.find("\"sectorPackagesReoptimized\":false") != std::string::npos);
  assert(robustness_json.find("\"calibrationId\":\"baseline-v1\"") != std::string::npos);

  std::cout << "V2 robust evaluation integration tests passed\n";
  return 0;
}
