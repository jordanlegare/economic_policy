#include "backtest.hpp"
#include "credit_spread_measurement.hpp"
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

void assert_complete_audit(const cad::StateMeasurementRegistry& registry,
                           const cad::BacktestFixture& fixture) {
  const auto audit = cad::audit_modeled_empirical_state(registry, input_names(fixture));
  assert(audit.modeled_empirical_fields == 18);
  assert(audit.resolved_fields == 18);
  assert(audit.unresolved_fields == 0);
  assert(std::abs(audit.empirical_coverage - 100.0) < 1e-12);
  assert(audit.grade == "empirically-complete");
  assert(audit.unresolved_names.empty());
}

}  // namespace

int main() {
  const auto registry = cad::load_state_measurement_registry(
      "data/calibration/state_measurement_registry.csv");
  assert(registry.loaded);
  assert(cad::state_measurement_contract_complete(registry));
  assert(registry.entries.size() == 2);
  assert(cad::ready_state_measurement_count(registry) == 2);
  assert(cad::unresolved_state_measurements(registry).empty());

  const auto* credit = registry.find("credit_spread");
  const auto* housing = registry.find("housing_gap");
  assert(credit);
  assert(housing);
  assert(credit->unit == "percentage_points");
  assert(credit->preferred_source_id == "boc_corporate_spread_research");
  assert(credit->status == "ready");
  assert(credit->public_reproducibility == "public-reconstructed");
  assert(credit->transformation.find("corporate-minus-government") != std::string::npos);
  assert(housing->status == "ready");

  const auto credit_benchmarks = cad::load_credit_spread_benchmarks(
      "data/calibration/credit_spread_benchmarks.csv");
  assert(credit_benchmarks.size() == 3);
  for (const auto& benchmark : credit_benchmarks)
    assert(cad::credit_spread_benchmark_valid(benchmark));
  assert(std::abs(credit_benchmarks[0].spread_percentage_points - 1.26) < 1e-12);
  assert(std::abs(credit_benchmarks[1].spread_percentage_points - 1.50) < 1e-12);
  assert(std::abs(credit_benchmarks[2].spread_percentage_points - 1.37) < 1e-12);

  const auto housing_benchmarks = cad::load_housing_affordability_benchmarks(
      "data/calibration/housing_affordability_benchmarks.csv");
  assert(housing_benchmarks.size() == 3);
  for (const auto& benchmark : housing_benchmarks)
    assert(cad::housing_affordability_benchmark_valid(benchmark));

  const auto oil = cad::load_backtest_fixture("data/backtests/2015-01-20-oil-shock.csv");
  const auto pandemic = cad::load_backtest_fixture("data/backtests/2020-03-03-pandemic-onset.csv");
  const auto inflation = cad::load_backtest_fixture("data/backtests/2022-07-12-inflation-tightening.csv");
  assert_complete_audit(registry, oil);
  assert_complete_audit(registry, pandemic);
  assert_complete_audit(registry, inflation);

  assert(std::abs(input_value(oil, "credit_spread") - credit_benchmarks[0].spread_percentage_points) < 1e-12);
  assert(std::abs(input_value(pandemic, "credit_spread") - credit_benchmarks[1].spread_percentage_points) < 1e-12);
  assert(std::abs(input_value(inflation, "credit_spread") - credit_benchmarks[2].spread_percentage_points) < 1e-12);
  assert(std::abs(input_value(oil, "housing_gap") - housing_benchmarks[0].housing_gap_percent) < 1e-12);
  assert(std::abs(input_value(pandemic, "housing_gap") - housing_benchmarks[1].housing_gap_percent) < 1e-12);
  assert(std::abs(input_value(inflation, "housing_gap") - housing_benchmarks[2].housing_gap_percent) < 1e-12);

  const auto registry_json = cad::state_measurement_registry_to_json(registry);
  const auto audit_json = cad::state_measurement_audit_to_json(
      cad::audit_modeled_empirical_state(registry, input_names(inflation)));
  assert(registry_json.find("\"readyCount\":2") != std::string::npos);
  assert(registry_json.find("\"unresolvedCount\":0") != std::string::npos);
  assert(audit_json.find("\"resolvedFields\":18") != std::string::npos);
  assert(audit_json.find("\"unresolvedFields\":0") != std::string::npos);
  assert(audit_json.find("\"empiricalCoverage\":100.000000") != std::string::npos);
  assert(audit_json.find("\"grade\":\"empirically-complete\"") != std::string::npos);

  std::cout << "state measurement contract tests passed\n";
  return 0;
}
