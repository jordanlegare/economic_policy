#include "model_evidence.hpp"
#include "welfare_sensitivity.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  assert(registry.loaded);
  assert(cad::structural_parameter_registry_complete(registry));

  const auto parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  cad::PolicyEngine engine(20260810, parameters, registry);

  const std::vector<std::string> fixtures = {
      "data/backtests/2015-01-20-oil-shock.csv",
      "data/backtests/2020-03-03-pandemic-onset.csv",
      "data/backtests/2022-07-12-inflation-tightening.csv"};
  const auto backtests = cad::run_historical_evidence(engine, fixtures);
  const auto status = cad::model_evidence_status(registry, backtests);
  assert(status.structural_registry_loaded);
  assert(status.structural_registry_complete);
  assert(status.sampled_parameter_count > 0);
  assert(status.historical_fixture_count == 3);
  assert(status.valid_historical_fixture_count == 3);
  assert(status.historical_aggregate_permitted);

  const auto status_json = cad::model_evidence_status_to_json(status);
  const auto history_json = cad::historical_evidence_to_json(backtests);
  assert(status_json.find("\"structuralRegistryComplete\":true") != std::string::npos);
  assert(history_json.find("\"fixtureCount\":3") != std::string::npos);
  assert(history_json.find("ca-2015-01-20-oil-shock") != std::string::npos);
  assert(history_json.find("ca-2020-03-03-pandemic-onset") != std::string::npos);
  assert(history_json.find("ca-2022-07-12-inflation-tightening") != std::string::npos);

  cad::Economy economy;
  const auto reference = engine.evaluate(economy);
  std::vector<cad::WelfarePreferenceProfile> one_profile = {{
      "reference", economy.canada_priority, economy.us_priority,
      economy.risk_aversion, 0.0, 0.0}};
  const auto welfare = cad::evaluate_welfare_sensitivity(engine, economy, one_profile);
  assert(welfare.profile_count == 1);
  assert(welfare.exact_recommendation_wins == 1);
  assert(welfare.reference_strategy == reference.recommendation.strategy_id);
  assert(welfare.all_mandate_weights_fixed);

  const auto robust = engine.evaluate_robust(economy, 1);
  assert(robust.recommendation.robustness.parameter_draws == 1);
  assert(robust.recommendation.robustness.parameter_provenance_complete);
  assert(robust.recommendation.robustness.policy_controls_reoptimized);
  assert(robust.recommendation.robustness.sector_packages_reoptimized);

  std::cout << "V2 model evidence contract tests passed\n";
  return 0;
}
