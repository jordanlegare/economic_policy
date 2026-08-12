#include "backtest.hpp"
#include "state_measurement.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> input_names(const cad::BacktestFixture& fixture) {
  std::vector<std::string> out;
  out.reserve(fixture.inputs.size());
  for (const auto& datum : fixture.inputs) out.push_back(datum.name);
  return out;
}

void assert_current_fixture_audit(const cad::StateMeasurementRegistry& registry,
                                  const cad::BacktestFixture& fixture) {
  const auto audit = cad::audit_modeled_empirical_state(registry, input_names(fixture));
  assert(audit.modeled_empirical_fields == 18);
  assert(audit.resolved_fields == 16);
  assert(audit.unresolved_fields == 2);
  assert(std::abs(audit.empirical_coverage - 100.0 * 16.0 / 18.0) < 1e-12);
  assert(audit.grade == "measurement-partial");
  assert(audit.unresolved_names.size() == 2);
  assert(audit.unresolved_names[0] == "credit_spread");
  assert(audit.unresolved_names[1] == "housing_gap");
}

}  // namespace

int main() {
  const auto registry = cad::load_state_measurement_registry(
      "data/calibration/state_measurement_registry.csv");
  assert(registry.loaded);
  assert(cad::state_measurement_contract_complete(registry));
  assert(registry.entries.size() == 2);
  assert(cad::ready_state_measurement_count(registry) == 0);
  assert(cad::unresolved_state_measurements(registry).size() == 2);

  const auto* credit = registry.find("credit_spread");
  const auto* housing = registry.find("housing_gap");
  assert(credit);
  assert(housing);
  assert(credit->unit == "percentage_points");
  assert(credit->preferred_source_id == "boc_corporate_spread_research");
  assert(credit->status == "blocked-nonpublic-canonical-series");
  assert(housing->preferred_source_id == "boc_housing_affordability");
  assert(housing->status == "defined-pending-vintage-materialization");
  assert(housing->transformation.find("one-sided benchmark") != std::string::npos);

  const auto oil = cad::load_backtest_fixture("data/backtests/2015-01-20-oil-shock.csv");
  const auto pandemic = cad::load_backtest_fixture("data/backtests/2020-03-03-pandemic-onset.csv");
  const auto inflation = cad::load_backtest_fixture("data/backtests/2022-07-12-inflation-tightening.csv");
  assert_current_fixture_audit(registry, oil);
  assert_current_fixture_audit(registry, pandemic);
  assert_current_fixture_audit(registry, inflation);

  // Merely adding a field name must not increase empirical coverage while its
  // measurement contract remains unresolved.
  auto names = input_names(inflation);
  names.push_back("credit_spread");
  auto unresolved = cad::audit_modeled_empirical_state(registry, names);
  assert(unresolved.resolved_fields == 16);

  // A future measurement is counted only when both the registry marks it ready
  // and the historical fixture actually supplies that measured input.
  auto future = registry;
  auto* future_credit = future.find("credit_spread");
  assert(future_credit);
  future_credit->status = "ready";
  auto one_ready = cad::audit_modeled_empirical_state(future, names);
  assert(one_ready.resolved_fields == 17);
  assert(one_ready.unresolved_fields == 1);
  assert(one_ready.unresolved_names.size() == 1);
  assert(one_ready.unresolved_names[0] == "housing_gap");

  const auto registry_json = cad::state_measurement_registry_to_json(registry);
  const auto audit_json = cad::state_measurement_audit_to_json(
      cad::audit_modeled_empirical_state(registry, input_names(inflation)));
  assert(registry_json.find("\"contractComplete\":true") != std::string::npos);
  assert(registry_json.find("blocked-nonpublic-canonical-series") != std::string::npos);
  assert(audit_json.find("\"resolvedFields\":16") != std::string::npos);
  assert(audit_json.find("\"unresolvedFields\":2") != std::string::npos);
  assert(audit_json.find("\"grade\":\"measurement-partial\"") != std::string::npos);

  std::cout << "state measurement contract tests passed\n";
  return 0;
}
