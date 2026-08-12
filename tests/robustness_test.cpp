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

  baseline.uncertainty_scale = 0.0;
  const auto exact = cad::draw_structural_parameters(baseline, 4, 999);
  for (const auto& p : exact) {
    assert(std::abs(p.neutral_rate - baseline.neutral_rate) < 1e-15);
    assert(std::abs(p.output_persistence - baseline.output_persistence) < 1e-15);
    assert(std::abs(p.phillips_curve_slope - baseline.phillips_curve_slope) < 1e-15);
  }

  // Explicit provenance bounds supersede legacy fallback bounds and are used by
  // the ordinary evaluate_robust() sampler through StructuralParameters.
  baseline.uncertainty_scale = 0.10;
  cad::StructuralParameterRegistry registry;
  registry.loaded = true;
  registry.registry_id = "test-bounds";
  registry.as_of = "2026-08-11";
  registry.entries.push_back({
      "neutral_rate", 2.5, "percent", "assumed", "test", "2026-08-11",
      2.49, 2.51, "normal", 2.0, true, "deliberately tight bound"});
  registry.entries.push_back({
      "inflation_persistence", .68, "coefficient", "assumed", "test", "2026-08-11",
      .60, .70, "normal", .5, true, "bounded persistence"});
  registry.entries.push_back({
      "inflation_expectations_weight", .32, "coefficient", "derived", "test", "2026-08-11",
      .30, .40, "derived", 0.0, false, "preserve anchor"});
  baseline.uncertainty_registry = registry;

  const auto bounded = cad::draw_structural_parameters(baseline, 64, 77);
  for (const auto& p : bounded) {
    assert(p.neutral_rate >= 2.49 && p.neutral_rate <= 2.51);
    assert(p.inflation_persistence >= .60 && p.inflation_persistence <= .70);
    assert(p.inflation_expectations_weight >= .30 && p.inflation_expectations_weight <= .40);
    assert(std::abs(p.inflation_persistence + p.inflation_expectations_weight - 1.0) < 1e-12);
  }

  assert(cad::classify_robustness(.85) == "robust");
  assert(cad::classify_robustness(.65) == "moderately-robust");
  assert(cad::classify_robustness(.45) == "fragile");
  assert(cad::classify_robustness(.25) == "unstable");

  std::cout << "robustness sampler tests passed\n";
  return 0;
}
