#include "empirical_calibration.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  const auto structural = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  const auto evidence = cad::load_empirical_structural_evidence(
      "data/calibration/empirical_structural_evidence.csv");

  assert(structural.loaded);
  assert(evidence.loaded);
  assert(cad::empirical_structural_evidence_complete(evidence));

  const auto audit = cad::audit_empirical_structural_evidence(evidence);
  assert(audit.parameter_count == 25);
  assert(audit.statistically_anchored_count == 12);
  assert(audit.reference_only_count == 9);
  assert(audit.direct_mapping_count == 3);
  assert(std::abs(audit.statistically_anchored_coverage - 48.0) < 1e-12);
  assert(std::abs(audit.direct_mapping_coverage - 12.0) < 1e-12);

  const auto* neutral = structural.find("neutral_rate");
  assert(neutral);
  assert(neutral->source_id == "boc_neutral_rate_2026");
  assert(neutral->kind == "official_assessment");
  assert(std::abs(neutral->baseline - 2.75) < 1e-12);
  assert(std::abs(neutral->lower_bound - 2.25) < 1e-12);
  assert(std::abs(neutral->upper_bound - 3.25) < 1e-12);

  const auto parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, structural);
  assert(std::abs(parameters.neutral_rate - 2.75) < 1e-12);

  // Reference-only evidence must not silently overwrite parameters whose
  // statistical object does not match the production equation one-for-one.
  assert(std::abs(parameters.rate_inflation_response - 0.75) < 1e-12);
  assert(std::abs(parameters.rate_output_response - 0.25) < 1e-12);
  assert(std::abs(parameters.output_persistence - 0.8802835399) < 1e-10);
  assert(std::abs(parameters.inflation_persistence - 0.7371310581) < 1e-10);
  assert(std::abs(parameters.inflation_expectations_weight - 0.2628689419) < 1e-10);
  assert(std::abs(parameters.fx_pass_through - 0.35) < 1e-12);
  assert(std::abs(parameters.import_price_pass_through - 0.022) < 1e-12);

  const auto json = cad::empirical_structural_audit_to_json(audit);
  assert(json.find("\"statisticallyAnchoredCount\":12") != std::string::npos);
  assert(json.find("\"statisticallyAnchoredCoverage\":48.000000") != std::string::npos);
  assert(json.find("\"directMappingCount\":3") != std::string::npos);

  std::cout << "empirical structural calibration tests passed\n";
  return 0;
}
