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
  assert(cad::structural_parameter_registry_complete(structural));
  assert(cad::structural_correlation_registry_valid(structural));
  assert(structural.correlations.size() == 2);

  const auto audit = cad::audit_empirical_structural_evidence(evidence);
  // Making hidden network assumptions explicit expands the denominator without
  // fabricating evidence rows for them. Coverage therefore falls conservatively.
  assert(audit.parameter_count == 37);
  assert(audit.statistically_anchored_count == 14);
  assert(audit.reference_only_count == 11);
  assert(audit.direct_mapping_count == 3);
  assert(std::abs(audit.statistically_anchored_coverage - 37.83783783783784) < 1e-10);
  assert(std::abs(audit.direct_mapping_coverage - 8.108108108108109) < 1e-10);

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
  assert(std::abs(parameters.shock_tail_threshold - 2.0) < 1e-12);
  assert(std::abs(parameters.shock_tail_scale - 1.75) < 1e-12);
  assert(std::abs(parameters.stress_regime_shock_scale - 1.35) < 1e-12);

  // Formerly hidden production-network coefficients are now typed structural
  // assumptions with executable provenance and uncertainty bounds.
  assert(std::abs(parameters.network_supplier_demand_transmission - .30) < 1e-12);
  assert(std::abs(parameters.network_input_cost_incidence - .85) < 1e-12);
  assert(std::abs(parameters.network_downstream_cost_transmission - .85) < 1e-12);
  assert(std::abs(parameters.network_price_cost_pass_through - .70) < 1e-12);
  assert(std::abs(parameters.network_output_cost_base - .12) < 1e-12);
  assert(std::abs(parameters.network_output_cost_cyclical - .18) < 1e-12);
  assert(std::abs(parameters.network_jobs_output_base - .20) < 1e-12);
  assert(std::abs(parameters.network_jobs_output_exposure - .35) < 1e-12);

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
  assert(completeness.parameter_count == 37);
  assert(completeness.calibration_target_count == 35);
  assert(completeness.direct_empirical_count == 3);
  assert(completeness.provisional_count == 32);
  assert(std::abs(completeness.direct_empirical_coverage - 8.571428571428571) < 1e-10);
  assert(completeness.multiplier_target_count == 19);
  assert(completeness.direct_empirical_multiplier_count == 0);

  const auto json = cad::empirical_structural_audit_to_json(audit);
  assert(json.find("\"parameterCount\":37") != std::string::npos);
  assert(json.find("\"statisticallyAnchoredCount\":14") != std::string::npos);
  assert(json.find("\"directMappingCount\":3") != std::string::npos);

  const auto registry_json = cad::structural_parameter_registry_to_json(structural);
  assert(registry_json.find("\"correlationPairCount\":2") != std::string::npos);
  assert(registry_json.find("declared-pairwise-gaussian-copula") != std::string::npos);
  assert(registry_json.find("network_supplier_demand_transmission") != std::string::npos);

  std::cout << "empirical structural calibration tests passed\n";
  return 0;
}