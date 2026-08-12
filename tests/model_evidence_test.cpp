#include "model_evidence.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  const auto empirical = cad::load_empirical_structural_evidence(
      "data/calibration/empirical_structural_evidence.csv");
  assert(registry.loaded);
  assert(cad::structural_parameter_registry_complete(registry));
  assert(empirical.loaded);
  assert(cad::empirical_structural_evidence_complete(empirical));

  const auto parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  cad::PolicyEngine engine(20260810, parameters, registry);

  const std::vector<std::string> fixtures = {
      "data/backtests/2015-01-20-oil-shock.csv",
      "data/backtests/2020-03-03-pandemic-onset.csv",
      "data/backtests/2022-07-12-inflation-tightening.csv"};
  const auto backtests = cad::run_historical_evidence(engine, fixtures);
  const auto status = cad::model_evidence_status(registry, backtests, empirical);
  assert(status.structural_registry_loaded);
  assert(status.structural_registry_complete);
  assert(status.sampled_parameter_count > 0);
  assert(status.empirical_registry_loaded);
  assert(status.empirical_registry_complete);
  assert(status.statistically_anchored_parameter_count == 7);
  assert(status.direct_empirical_mapping_count == 1);
  assert(status.statistically_anchored_coverage == 28.0);
  assert(status.direct_empirical_mapping_coverage == 4.0);
  assert(status.historical_fixture_count == 3);
  assert(status.valid_historical_fixture_count == 3);
  assert(status.historical_aggregate_permitted);

  const auto status_json = cad::model_evidence_status_to_json(status);
  const auto history_json = cad::historical_evidence_to_json(backtests);
  assert(status_json.find("\"structuralRegistryComplete\":true") != std::string::npos);
  assert(status_json.find("\"empiricalRegistryComplete\":true") != std::string::npos);
  assert(status_json.find("\"statisticallyAnchoredCoverage\":28.000000") != std::string::npos);
  assert(status_json.find("\"directEmpiricalMappingCoverage\":4.000000") != std::string::npos);
  assert(history_json.find("\"fixtureCount\":3") != std::string::npos);
  assert(history_json.find("ca-2015-01-20-oil-shock") != std::string::npos);
  assert(history_json.find("ca-2020-03-03-pandemic-onset") != std::string::npos);
  assert(history_json.find("ca-2022-07-12-inflation-tightening") != std::string::npos);

  std::cout << "V2 model evidence contract tests passed\n";
  return 0;
}
