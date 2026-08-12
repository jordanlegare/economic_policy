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

double input_value(const cad::BacktestFixture& fixture, const std::string& name) {
  for (const auto& datum : fixture.inputs) if (datum.name == name) return datum.value;
  return 0.0;
}

void assert_current_fixture_audit(const cad::StateMeasurementRegistry& registry,
                                  const cad::BacktestFixture& fixture) {
  const auto audit = cad::audit_modeled_empirical_state(registry, input_names(fixture));
  assert(audit.modeled_empirical_fields == 18);
  assert(audit.resolved_fields == 17);
  assert(audit.unresolved_fields == 1);
  assert(std::abs(audit.empirical_coverage - 100.0 * 17.0 / 18.0) < 1e-12);
  assert(audit.grade == "measurement-partial");
  assert(audit.unresolved_names.size() == 1);
  assert(audit.unresolved_names[0] == "credit_spread");
}

}  // namespace

int main() {
  const auto registry = cad::load_state_measurement_registry(
      "data/calibration/state_measurement_registry.csv");
  assert(registry.loaded);
  assert(cad::state_measurement_contract_complete(registry));
  assert(registry.entries.size() == 2);
  assert(cad::ready_state_measurement_count(registry) == 1);
  assert(cad::unresolved_state_measurements(registry).size() == 1);
  assert(cad::unresolved_state_measurements(registry)[0] == "credit_spread");

  const auto* credit = registry.find("credit_spread");
  const auto* housing = registry.find("housing_gap");
  assert(credit);
  assert(housing);
  assert(credit->unit == "percentage_points");
  assert(credit->preferred_source_id == "boc_corporate_spread_research");
  assert(credit->status == "blocked-nonpublic-canonical-series");
  assert(housing->preferred_source_id == "boc_housing_affordability");
  assert(housing->status == "ready");
  assert(housing->transformation.find("median(HAI_{t-20}...HAI_{t-1})") != std::string::npos);
  assert(housing->transformation.find("excluded") != std::string::npos);

  const auto benchmarks = cad::load_housing_affordability_benchmarks(
      "data/calibration/housing_affordability_benchmarks.csv");
  assert(benchmarks.size() == 3);
  for (const auto& benchmark : benchmarks) {
    assert(cad::housing_affordability_benchmark_valid(benchmark));
    assert(benchmark.source_update_date <= benchmark.decision_date);
    assert(benchmark.benchmark_quarters == 20);
  }
  assert(std::abs(benchmarks[0].housing_gap_percent - 0.621118012422) < 1e-12);
  assert(std::abs(benchmarks[1].housing_gap_percent + 1.388888888889) < 1e-12);
  assert(std::abs(benchmarks[2].housing_gap_percent - 23.731138545953) < 1e-12);

  const auto oil = cad::load_backtest_fixture("data/backtests/2015-01-20-oil-shock.csv");
  const auto pandemic = cad::load_backtest_fixture("data/backtests/2020-03-03-pandemic-onset.csv");
  const auto inflation = cad::load_backtest_fixture("data/backtests/2022-07-12-inflation-tightening.csv");
  assert_current_fixture_audit(registry, oil);
  assert_current_fixture_audit(registry, pandemic);
  assert_current_fixture_audit(registry, inflation);

  assert(std::abs(input_value(oil, "housing_gap") - benchmarks[0].housing_gap_percent) < 1e-12);
  assert(std::abs(input_value(pandemic, "housing_gap") - benchmarks[1].housing_gap_percent) < 1e-12);
  assert(std::abs(input_value(inflation, "housing_gap") - benchmarks[2].housing_gap_percent) < 1e-12);

  // Merely adding the still-blocked credit-spread field cannot raise empirical
  // coverage. The registry must explicitly certify the measurement as ready.
  auto names = input_names(inflation);
  names.push_back("credit_spread");
  auto unresolved = cad::audit_modeled_empirical_state(registry, names);
  assert(unresolved.resolved_fields == 17);
  assert(unresolved.unresolved_fields == 1);

  // A future credit-spread measurement would reach 18/18 only when both the
  // registry marks it ready and the historical fixture supplies the input.
  auto future = registry;
  auto* future_credit = future.find("credit_spread");
  assert(future_credit);
  future_credit->status = "ready";
  auto complete = cad::audit_modeled_empirical_state(future, names);
  assert(complete.resolved_fields == 18);
  assert(complete.unresolved_fields == 0);
  assert(complete.grade == "empirically-complete");

  const auto registry_json = cad::state_measurement_registry_to_json(registry);
  const auto audit_json = cad::state_measurement_audit_to_json(
      cad::audit_modeled_empirical_state(registry, input_names(inflation)));
  assert(registry_json.find("\"contractComplete\":true") != std::string::npos);
  assert(registry_json.find("\"readyCount\":1") != std::string::npos);
  assert(registry_json.find("blocked-nonpublic-canonical-series") != std::string::npos);
  assert(audit_json.find("\"resolvedFields\":17") != std::string::npos);
  assert(audit_json.find("\"unresolvedFields\":1") != std::string::npos);
  assert(audit_json.find("\"empiricalCoverage\":94.444444") != std::string::npos);
  assert(audit_json.find("\"grade\":\"measurement-partial\"") != std::string::npos);

  std::cout << "state measurement contract tests passed\n";
  return 0;
}
