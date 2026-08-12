#include "empirical_calibration.hpp"
#include "quarterly_empirical_calibration.hpp"
#include "structural_calibration.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

const cad::Scenario* scenario(const cad::Result& result, const std::string& id) {
  for (const auto& s : result.scenarios) if (s.id == id) return &s;
  return nullptr;
}

int main() {
  const auto estimation = cad::load_quarterly_structural_estimation(
      "data/calibration/quarterly_structural_estimates.csv",
      "data/calibration/quarterly_residual_covariance.csv");
  assert(estimation.loaded);
  assert(cad::quarterly_estimation_valid(estimation));
  assert(estimation.estimates.size() == 10);
  assert(cad::quarterly_direct_eligible_count(estimation) == 2);
  assert(estimation.residual_covariance_observations == 75);
  assert(std::abs(estimation.output_inflation_residual_correlation
      - (-0.006249264169)) < 1e-9);

  const auto* output = estimation.find("output_persistence");
  const auto* inflation = estimation.find("inflation_persistence");
  const auto* phillips = estimation.find("phillips_curve_slope");
  assert(output && output->direct_eligible);
  assert(inflation && inflation->direct_eligible);
  assert(phillips && !phillips->direct_eligible);
  assert(phillips->lower_bound < 0.0 && phillips->upper_bound > 0.0);

  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  const auto parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  assert(std::abs(parameters.output_persistence - output->estimate) < 1e-9);
  assert(std::abs(parameters.inflation_persistence - inflation->estimate) < 1e-9);
  assert(std::abs(parameters.inflation_persistence
      + parameters.inflation_expectations_weight - 1.0) < 1e-12);

  // The ordinary production engine must consume StructuralParameters, not only
  // the outer V2 robustness experiment.
  cad::StructuralParameters low = parameters;
  low.output_persistence = 0.20;
  low.uncertainty_scale = 0.0;
  cad::StructuralParameters high = parameters;
  high.output_persistence = 0.90;
  high.uncertainty_scale = 0.0;
  cad::Economy economy;
  economy.us_tariff_canada = 0.0;
  economy.canada_retaliatory_tariff = 0.0;
  const auto low_result = cad::PolicyEngine(20260810, low).evaluate(economy);
  const auto high_result = cad::PolicyEngine(20260810, high).evaluate(economy);
  const auto* low_status = scenario(low_result, "statusquo");
  const auto* high_status = scenario(high_result, "statusquo");
  assert(low_status && high_status);
  assert(std::abs(low_status->growth - high_status->growth) > 1e-5);

  const auto json = cad::quarterly_estimation_to_json(estimation);
  assert(json.find("\"directEligibleCount\":2") != std::string::npos);
  std::cout << "quarterly empirical calibration tests passed\n";
  return 0;
}
