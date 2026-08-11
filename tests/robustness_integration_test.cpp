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

  // A small ensemble is enough for an integration contract test; production
  // callers can request a larger ensemble when compute budget permits.
  const auto robust_a = engine.evaluate_robust(economy, 3);
  const auto robust_b = engine.evaluate_robust(economy, 3);
  const auto& a = robust_a.recommendation.robustness;
  const auto& b = robust_b.recommendation.robustness;

  assert(a.parameter_draws == 3);
  assert(a.recommendation_wins >= 0 && a.recommendation_wins <= 3);
  assert(a.recommendation_win_rate >= 0.0 && a.recommendation_win_rate <= 1.0);
  assert(a.score_p10 <= a.score_p90 + 1e-12);
  assert(a.classification != "not-evaluated");

  // The complete outer uncertainty experiment must be reproducible for the
  // same baseline calibration and engine seed.
  assert(a.recommendation_wins == b.recommendation_wins);
  assert(std::abs(a.recommendation_win_rate - b.recommendation_win_rate) < 1e-15);
  assert(std::abs(a.score_mean - b.score_mean) < 1e-12);
  assert(std::abs(a.score_p10 - b.score_p10) < 1e-12);
  assert(std::abs(a.score_p90 - b.score_p90) < 1e-12);
  assert(a.classification == b.classification);

  std::cout << "robust evaluation integration tests passed\n";
  return 0;
}
