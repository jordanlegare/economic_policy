#include "robustness.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
  assert(a.size() == b.size() && !a.empty());
  double ma = 0.0, mb = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= static_cast<double>(a.size());
  mb /= static_cast<double>(b.size());
  double cov = 0.0, va = 0.0, vb = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double da = a[i] - ma;
    const double db = b[i] - mb;
    cov += da * db;
    va += da * da;
    vb += db * db;
  }
  return cov / std::sqrt(va * vb);
}

cad::StructuralParameterProvenance parameter(
    std::string name, double baseline, double lo, double hi,
    double sigma = 0.10) {
  return {std::move(name), baseline, "coefficient", "assumed", "test", "2026-08-13",
      lo, hi, "normal", sigma, true, "test structural parameter"};
}

}  // namespace

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
  registry.as_of = "2026-08-13";
  registry.entries.push_back({
      "neutral_rate", 2.5, "percent", "assumed", "test", "2026-08-13",
      2.49, 2.51, "normal", 2.0, true, "deliberately tight bound"});
  registry.entries.push_back({
      "inflation_persistence", .68, "coefficient", "assumed", "test", "2026-08-13",
      .60, .70, "normal", .5, true, "bounded persistence"});
  registry.entries.push_back({
      "inflation_expectations_weight", .32, "coefficient", "derived", "test", "2026-08-13",
      .30, .40, "derived", 0.0, false, "preserve anchor"});
  baseline.uncertainty_registry = registry;

  const auto bounded = cad::draw_structural_parameters(baseline, 64, 77);
  for (const auto& p : bounded) {
    assert(p.neutral_rate >= 2.49 && p.neutral_rate <= 2.51);
    assert(p.inflation_persistence >= .60 && p.inflation_persistence <= .70);
    assert(p.inflation_expectations_weight >= .30 && p.inflation_expectations_weight <= .40);
    assert(std::abs(p.inflation_persistence + p.inflation_expectations_weight - 1.0) < 1e-12);
  }

  // Declared dependence is executable rather than documentation-only. With
  // wide non-binding bounds the observed parameter correlation should converge
  // close to the declared Gaussian-copula correlation.
  cad::StructuralParameterRegistry joint;
  joint.loaded = true;
  joint.registry_id = "joint-test";
  joint.as_of = "2026-08-13";
  joint.entries.push_back(parameter(
      "network_input_cost_incidence", .80, .20, 1.40, .08));
  joint.entries.push_back(parameter(
      "network_downstream_cost_transmission", .70, .20, 1.20, .08));
  joint.correlations.push_back({
      "network_input_cost_incidence", "network_downstream_cost_transmission",
      .70, "assumed", "test", "2026-08-13", "test pair"});
  assert(cad::structural_sampling_correlation_matrix_valid(joint));
  assert(cad::structural_sampling_dependence_mode(joint)
      == "declared-gaussian-copula-with-derived-constraints");
  baseline.uncertainty_registry = joint;
  const auto correlated = cad::draw_structural_parameters(baseline, 4000, 8080);
  std::vector<double> incidence;
  std::vector<double> transmission;
  incidence.reserve(correlated.size());
  transmission.reserve(correlated.size());
  for (const auto& p : correlated) {
    incidence.push_back(p.network_input_cost_incidence);
    transmission.push_back(p.network_downstream_cost_transmission);
  }
  const double observed = correlation(incidence, transmission);
  assert(observed > .62 && observed < .77);

  // A pairwise-valid set can still be globally impossible. The sampler must
  // reject a non-positive-semidefinite correlation matrix rather than silently
  // falling back to independent draws.
  cad::StructuralParameterRegistry invalid;
  invalid.loaded = true;
  invalid.registry_id = "invalid-joint";
  invalid.as_of = "2026-08-13";
  invalid.entries.push_back(parameter("network_supplier_demand_transmission", .30, .05, .80));
  invalid.entries.push_back(parameter("network_input_cost_incidence", .80, .20, 1.40));
  invalid.entries.push_back(parameter("network_downstream_cost_transmission", .70, .20, 1.20));
  invalid.correlations.push_back({"network_supplier_demand_transmission",
      "network_input_cost_incidence", .90, "assumed", "test", "2026-08-13", ""});
  invalid.correlations.push_back({"network_supplier_demand_transmission",
      "network_downstream_cost_transmission", .90, "assumed", "test", "2026-08-13", ""});
  invalid.correlations.push_back({"network_input_cost_incidence",
      "network_downstream_cost_transmission", -.90, "assumed", "test", "2026-08-13", ""});
  assert(!cad::structural_sampling_correlation_matrix_valid(invalid));
  bool threw = false;
  try {
    (void)cad::draw_structural_parameters(baseline, invalid, 2, 9);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);

  assert(cad::classify_robustness(.85) == "robust");
  assert(cad::classify_robustness(.65) == "moderately-robust");
  assert(cad::classify_robustness(.45) == "fragile");
  assert(cad::classify_robustness(.25) == "unstable");

  std::cout << "robustness sampler tests passed\n";
  return 0;
}