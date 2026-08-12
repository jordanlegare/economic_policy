#include "backtest.hpp"
#include "backtest_suite.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void assert_fixture_integrity(const cad::BacktestFixture& fixture) {
  assert(fixture.loaded);
  assert(fixture.horizon_quarters == 12);
  assert(fixture.no_lookahead);
  assert(fixture.provenance_complete);
  assert(std::abs(fixture.input_coverage - 100.0) < 1e-12);
  assert(fixture.grade == "vintage-complete");
  for (const auto& input : fixture.inputs)
    assert(input.release_date <= fixture.decision_date);
  for (const auto& outcome : fixture.outcomes)
    assert(outcome.release_date > fixture.decision_date);
  for (const auto& benchmark : fixture.benchmarks)
    assert(benchmark.release_date > fixture.decision_date);
}

void assert_result_finite(const cad::BacktestResult& result) {
  assert(result.valid);
  assert(result.no_lookahead);
  assert(result.provenance_complete);
  assert(result.policy_benchmark_available);
  assert(result.inflation.available);
  assert(result.growth.available);
  assert(result.unemployment.available);
  assert(std::isfinite(result.recommended_first_move_bp));
  assert(std::isfinite(result.policy_move_error_bp));
  assert(std::isfinite(result.inflation.error));
  assert(std::isfinite(result.growth.error));
  assert(std::isfinite(result.unemployment.error));
}

}  // namespace

int main() {
  const auto oil = cad::load_backtest_fixture(
      "data/backtests/2015-01-20-oil-shock.csv");
  const auto pandemic = cad::load_backtest_fixture(
      "data/backtests/2020-03-03-pandemic-onset.csv");
  const auto inflation = cad::load_backtest_fixture(
      "data/backtests/2022-07-12-inflation-tightening.csv");

  assert_fixture_integrity(oil);
  assert_fixture_integrity(pandemic);
  assert_fixture_integrity(inflation);
  assert(oil.fixture_id == "ca-2015-01-20-oil-shock");
  assert(pandemic.fixture_id == "ca-2020-03-03-pandemic-onset");
  assert(inflation.fixture_id == "ca-2022-07-12-inflation-tightening");

  // Vintage-state spot checks prevent fixture drift and ensure headline/core
  // inflation remain distinct where the contemporaneous sources distinguish them.
  cad::Economy base;
  const auto oil_state = cad::apply_backtest_fixture(base, oil);
  assert(std::abs(oil_state.policy_rate - 1.0) < 1e-12);
  assert(std::abs(oil_state.inflation - 2.0) < 1e-12);
  assert(std::abs(oil_state.core_inflation - 2.1) < 1e-12);
  assert(std::abs(oil_state.gdp_growth - 2.8) < 1e-12);
  assert(std::abs(oil_state.unemployment - 6.6) < 1e-12);
  assert(std::abs(oil_state.wage_growth - 1.88) < 1e-12);

  const auto pandemic_state = cad::apply_backtest_fixture(base, pandemic);
  assert(std::abs(pandemic_state.policy_rate - 1.75) < 1e-12);
  assert(std::abs(pandemic_state.inflation - 2.4) < 1e-12);
  assert(std::abs(pandemic_state.core_inflation - 2.0) < 1e-12);
  assert(std::abs(pandemic_state.gdp_growth - 0.4) < 1e-12);
  assert(std::abs(pandemic_state.unemployment - 5.5) < 1e-12);
  assert(std::abs(pandemic_state.wage_growth - 4.24) < 1e-12);

  const auto inflation_state = cad::apply_backtest_fixture(base, inflation);
  assert(std::abs(inflation_state.policy_rate - 1.5) < 1e-12);
  assert(std::abs(inflation_state.inflation - 7.7) < 1e-12);
  assert(std::abs(inflation_state.core_inflation - 4.15) < 1e-12);
  assert(std::abs(inflation_state.gdp_growth - 3.2) < 1e-12);
  assert(std::abs(inflation_state.unemployment - 4.9) < 1e-12);
  assert(std::abs(inflation_state.wage_growth - 5.2) < 1e-12);

  cad::PolicyEngine engine(20260810);
  const auto oil_result = cad::run_backtest(engine, oil);
  const auto pandemic_result = cad::run_backtest(engine, pandemic);
  const auto inflation_result = cad::run_backtest(engine, inflation);
  assert_result_finite(oil_result);
  assert_result_finite(pandemic_result);
  assert_result_finite(inflation_result);
  assert(std::abs(oil_result.realized_first_move_bp + 25.0) < 1e-12);
  assert(std::abs(pandemic_result.realized_first_move_bp + 50.0) < 1e-12);
  assert(std::abs(inflation_result.realized_first_move_bp - 100.0) < 1e-12);

  const std::vector<cad::BacktestResult> results{
      oil_result, pandemic_result, inflation_result};
  const auto suite = cad::summarize_backtests(results);
  assert(suite.fixture_count == 3);
  assert(suite.valid_count == 3);
  assert(suite.no_lookahead_count == 3);
  assert(suite.provenance_complete_count == 3);
  assert(suite.policy_benchmark_count == 3);
  assert(suite.inflation_count == 3);
  assert(suite.growth_count == 3);
  assert(suite.unemployment_count == 3);
  assert(suite.all_valid_no_lookahead);
  assert(suite.aggregate_diagnostics_permitted);
  assert(suite.policy_direction_accuracy >= 0.0 && suite.policy_direction_accuracy <= 1.0);
  assert(suite.inflation_direction_accuracy >= 0.0 && suite.inflation_direction_accuracy <= 1.0);
  assert(suite.growth_direction_accuracy >= 0.0 && suite.growth_direction_accuracy <= 1.0);
  assert(suite.unemployment_direction_accuracy >= 0.0 && suite.unemployment_direction_accuracy <= 1.0);
  assert(std::isfinite(suite.mean_abs_policy_error_bp));
  assert(std::isfinite(suite.mean_abs_inflation_error));
  assert(std::isfinite(suite.mean_abs_growth_error));
  assert(std::isfinite(suite.mean_abs_unemployment_error));

  const auto suite_json = cad::backtest_suite_to_json(suite);
  assert(suite_json.find("\"fixtureCount\":3") != std::string::npos);
  assert(suite_json.find("\"allValidNoLookahead\":true") != std::string::npos);
  assert(suite_json.find("\"aggregateDiagnosticsPermitted\":true") != std::string::npos);
  assert(suite_json.find("\"meanAbsoluteErrorBp\":") != std::string::npos);

  // CI guard: a future release inserted into the input set invalidates the
  // entire fixture. The engine must not silently drop or consume it.
  const std::string invalid_path = "backtest-lookahead-test.csv";
  {
    std::ofstream out(invalid_path);
    out << "META,fixture_id,lookahead-test\n";
    out << "META,decision_date,2022-07-12\n";
    out << "META,horizon_quarters,12\n";
    out << "INPUT,policy_rate,1.5,percent,2022-06-01,2022-06-01,source,observed\n";
    out << "INPUT,inflation,8.1,percent_yoy,2022-06-30,2022-07-20,source,observed\n";
    out << "SOURCE,source,Test agency,Deliberately invalid future release,https://example.invalid\n";
  }
  const auto invalid = cad::load_backtest_fixture(invalid_path);
  assert(invalid.loaded);
  assert(!invalid.no_lookahead);
  assert(invalid.grade == "lookahead-failed");
  assert(!cad::run_backtest(engine, invalid).valid);
  std::remove(invalid_path.c_str());

  return 0;
}
