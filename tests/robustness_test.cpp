#include "robustness.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  cad::StructuralParameters baseline;
  baseline.uncertainty_scale = 0.10;

  const auto a = cad::draw_structural_parameters(baseline, 32, 12345);
  const auto b = cad::draw_structural_parameters(baseline, 32, 12345);
  const auto c = cad::draw_structural_parameters(baseline, 32, 54321);

  assert(a.size() == 32);
  assert(b.size() == a.size());
  assert(c.size() == a.size());

  // Deterministic under a fixed structural-uncertainty seed.
  for (std::size_t i = 0; i < a.size(); ++i) {
    assert(a[i].calibration_id == b[i].calibration_id);
    assert(std::abs(a[i].neutral_rate - b[i].neutral_rate) < 1e-15);
    assert(std::abs(a[i].phillips_curve_slope - b[i].phillips_curve_slope) < 1e-15);
    assert(a[i].output_persistence > 0.0 && a[i].output_persistence < 1.0);
    assert(a[i].inflation_persistence > 0.0 && a[i].inflation_persistence < 1.0);
    assert(a[i].output_shock_sd > 0.0);
    assert(a[i].inflation_shock_sd > 0.0);
  }

  bool seed_changes_draw = false;
  for (std::size_t i = 0; i < a.size(); ++i)
    seed_changes_draw = seed_changes_draw
        || std::abs(a[i].neutral_rate - c[i].neutral_rate) > 1e-12;
  assert(seed_changes_draw);

  // Zero uncertainty must reproduce the baseline calibration exactly.
  baseline.uncertainty_scale = 0.0;
  const auto exact = cad::draw_structural_parameters(baseline, 4, 999);
  for (const auto& p : exact) {
    assert(std::abs(p.neutral_rate - baseline.neutral_rate) < 1e-15);
    assert(std::abs(p.output_persistence - baseline.output_persistence) < 1e-15);
    assert(std::abs(p.phillips_curve_slope - baseline.phillips_curve_slope) < 1e-15);
  }

  assert(cad::classify_robustness(.85) == "robust");
  assert(cad::classify_robustness(.65) == "moderately-robust");
  assert(cad::classify_robustness(.45) == "fragile");
  assert(cad::classify_robustness(.25) == "unstable");

  std::cout << "robustness sampler tests passed\n";
  return 0;
}
