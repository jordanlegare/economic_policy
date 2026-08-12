#include "policy_engine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

int main() {
  cad::Economy economy;

  cad::PolicyEngine engine(20260810);
  const auto ordinary = engine.evaluate(economy);
  const auto skipped = engine.evaluate_robust(economy, 0);
  assert(skipped.recommendation.strategy_id == ordinary.recommendation.strategy_id);
  assert(skipped.recommendation.robustness.parameter_draws == 0);
  assert(skipped.recommendation.robustness.classification == "not-evaluated");
  assert(!skipped.recommendation.robustness.sector_packages_reoptimized);

  // With zero structural uncertainty, the nested V2 optimizer must recover the
  // production sector packages and ranking exactly. This is the strongest guard
  // against drift between the V1 sector optimizer and the nested robustness path.
  cad::StructuralParameters exact_parameters;
  exact_parameters.uncertainty_scale = 0.0;
  cad::PolicyEngine exact_engine(20260810, exact_parameters);
  const auto exact = exact_engine.evaluate_robust(economy, 1);
  const auto& exact_summary = exact.recommendation.robustness;
  assert(exact_summary.parameter_draws == 1);
  assert(exact_summary.recommendation_wins == 1);
  assert(std::abs(exact_summary.recommendation_win_rate - 1.0) < 1e-15);
  assert(exact_summary.classification == "robust");
  assert(exact_summary.structural_parameters_active);
  assert(exact_summary.common_random_numbers);
  assert(exact_summary.sector_packages_reoptimized);
  assert(exact_summary.sector_frontiers_built
      == static_cast<int>(ordinary.scenarios.size()));
  assert(exact_summary.nested_sector_optimizations == ordinary.scenarios.size());
  assert(exact_summary.nested_sector_candidates_examined > 0);
  assert(exact_summary.nested_sector_finalists_resimulated > 0);
  assert(exact_summary.sector_package_changes == 0);
  assert(std::abs(exact_summary.reference_package_retention_rate - 1.0) < 1e-15);
  assert(exact_summary.methodology.find("nested-sector-pareto-reoptimization")
      != std::string::npos);
  assert(exact.recommendation.explanation.find("V2 nested structural robustness")
      != std::string::npos);

  // Structural parameters must still move the nested result rather than merely
  // rerunning the same package with a different label.
  cad::StructuralParameters shifted_parameters = exact_parameters;
  shifted_parameters.neutral_rate = 4.0;
  shifted_parameters.phillips_curve_slope = 0.22;
  shifted_parameters.canada_trade_drag_scale = 1.35;
  shifted_parameters.us_retaliation_drag_scale = 0.70;
  cad::PolicyEngine shifted_engine(20260810, shifted_parameters);
  const auto shifted = shifted_engine.evaluate_robust(economy, 1);
  assert(std::abs(shifted.recommendation.robustness.score_mean
      - exact_summary.score_mean) > 1e-6);
  assert(shifted.recommendation.robustness.sector_packages_reoptimized);

  // The complete nested experiment remains deterministic for a fixed engine
  // seed, including structural draws, finalist simulations and package choices.
  const auto robust_a = engine.evaluate_robust(economy, 2);
  const auto robust_b = engine.evaluate_robust(economy, 2);
  const auto& a = robust_a.recommendation.robustness;
  const auto& b = robust_b.recommendation.robustness;
  assert(a.parameter_draws == 2);
  assert(a.recommendation_wins >= 0 && a.recommendation_wins <= 2);
  assert(a.recommendation_win_rate >= 0.0 && a.recommendation_win_rate <= 1.0);
  assert(a.score_p10 <= a.score_p90 + 1e-12);
  assert(a.classification != "not-evaluated");
  assert(a.sector_packages_reoptimized);
  assert(a.nested_sector_optimizations
      == 2u * static_cast<std::uint64_t>(ordinary.scenarios.size()));
  assert(a.recommendation_wins == b.recommendation_wins);
  assert(std::abs(a.recommendation_win_rate - b.recommendation_win_rate) < 1e-15);
  assert(std::abs(a.score_mean - b.score_mean) < 1e-12);
  assert(std::abs(a.score_p10 - b.score_p10) < 1e-12);
  assert(std::abs(a.score_p90 - b.score_p90) < 1e-12);
  assert(a.classification == b.classification);
  assert(a.sector_package_changes == b.sector_package_changes);
  assert(a.nested_sector_candidates_examined == b.nested_sector_candidates_examined);
  assert(a.nested_sector_finalists_resimulated == b.nested_sector_finalists_resimulated);
  assert(std::abs(a.reference_package_retention_rate
      - b.reference_package_retention_rate) < 1e-15);

  const auto robustness_json = cad::robustness_to_json(robust_a);
  assert(robustness_json.find("\"structuralParametersActive\":true") != std::string::npos);
  assert(robustness_json.find("\"commonRandomNumbers\":true") != std::string::npos);
  assert(robustness_json.find("\"sectorPackagesReoptimized\":true") != std::string::npos);
  assert(robustness_json.find("\"nestedSectorOptimizations\":") != std::string::npos);
  assert(robustness_json.find("\"referencePackageRetentionRate\":") != std::string::npos);
  assert(robustness_json.find("\"calibrationId\":\"baseline-v1\"") != std::string::npos);

  std::cout << "V2 nested robust evaluation integration tests passed\n";
  return 0;
}
