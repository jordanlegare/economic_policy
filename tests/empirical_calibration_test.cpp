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
  assert(audit.parameter_count == 26);
  assert(audit.statistically_anchored_count == 14);
  assert(audit.reference_only_count == 11);
  assert(audit.direct_mapping_count == 3);
  assert(std::abs(audit.statistically_anchored_coverage - 53.84615384615385) < 1e-10);
  assert(std::abs(audit.direct_mapping_coverage - 11.53846153846154) < 1e-10);

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
  assert(std::abs(parameters.growth_shock_sd - 0.25) < 1e-12);

  const auto* realized_growth = evidence.find("growth_shock_sd");
  assert(realized_growth);
  assert(realized_growth->mapping_status == "reference-only");
  assert(std::abs(realized_growth->anchor_value - 2.3455131813) < 1e-10);

  const auto* residual_correlation = structural.find("output_inflation_shock_correlation");
  assert(residual_correlation);
  assert(residual_correlation->kind == "calibrated");
  assert(!residual_correlation->sampled);
  assert(std::abs(parameters.output_inflation_shock_correlation + 0.006249264169) < 1e-12);
  const auto* residual_correlation_evidence = evidence.find("output_inflation_shock_correlation");
  assert(residual_correlation_evidence);
  assert(residual_correlation_evidence->mapping_status == "reference-only");

  const auto completeness = cad::audit_structural_calibration_completeness(structural);
  assert(completeness.parameter_count == 26);
  assert(completeness.calibration_target_count == 24);
  assert(completeness.direct_empirical_count == 3);
  assert(completeness.provisional_count == 21);
  assert(std::abs(completeness.direct_empirical_coverage - 12.5) < 1e-12);

  const auto json = cad::empirical_structural_audit_to_json(audit);
  assert(json.find("\"statisticallyAnchoredCount\":14") != std::string::npos);
  assert(json.find("\"directMappingCount\":3") != std::string::npos);

  std::cout << "empirical structural calibration tests passed\n";
  return 0;
}
