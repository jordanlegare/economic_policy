#include "backtest.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

int main() {
  const auto fixture = cad::load_backtest_fixture(
      "data/backtests/2022-07-12-inflation-tightening.csv");
  assert(fixture.loaded);
  assert(fixture.fixture_id == "ca-2022-07-12-inflation-tightening");
  assert(fixture.decision_date == "2022-07-12");
  assert(fixture.horizon_quarters == 12);
  assert(fixture.no_lookahead);
  assert(fixture.provenance_complete);
  assert(std::abs(fixture.input_coverage - 100.0) < 1e-12);
  assert(fixture.grade == "vintage-complete");

  // Inputs must reflect only information released by the decision cutoff.
  cad::Economy base;
  const auto vintage = cad::apply_backtest_fixture(base, fixture);
  assert(std::abs(vintage.policy_rate - 1.5) < 1e-12);
  assert(std::abs(vintage.inflation - 7.7) < 1e-12);
  assert(std::abs(vintage.core_inflation - 4.15) < 1e-12);
  assert(std::abs(vintage.gdp_growth - 3.2) < 1e-12);
  assert(std::abs(vintage.unemployment - 4.9) < 1e-12);
  assert(std::abs(vintage.wage_growth - 5.2) < 1e-12);
  assert(std::abs(vintage.us_tariff_canada) < 1e-12);
  assert(std::abs(vintage.canada_retaliatory_tariff) < 1e-12);

  // The historical benchmark and realized outcomes are deliberately unavailable
  // at the decision date and may be used only for ex-post diagnostics.
  for (const auto& outcome : fixture.outcomes)
    assert(outcome.release_date > fixture.decision_date);
  for (const auto& benchmark : fixture.benchmarks)
    assert(benchmark.release_date > fixture.decision_date);

  cad::PolicyEngine engine(20260810);
  const auto result = cad::run_backtest(engine, fixture);
  assert(result.valid);
  assert(result.no_lookahead);
  assert(result.provenance_complete);
  assert(result.policy_benchmark_available);
  assert(std::abs(result.realized_first_move_bp - 100.0) < 1e-12);
  assert(result.inflation.available);
  assert(result.growth.available);
  assert(result.unemployment.available);
  assert(std::isfinite(result.recommended_first_move_bp));
  assert(std::isfinite(result.inflation.error));
  assert(std::isfinite(result.growth.error));
  assert(std::isfinite(result.unemployment.error));

  const auto json = cad::backtest_to_json(result);
  assert(json.find("\"fixtureId\":\"ca-2022-07-12-inflation-tightening\"")
      != std::string::npos);
  assert(json.find("\"noLookahead\":true") != std::string::npos);
  assert(json.find("\"inputCoverage\":100.000000") != std::string::npos);
  assert(json.find("\"policyBenchmarkAvailable\":true") != std::string::npos);
  assert(json.find("\"inflation\":{") != std::string::npos);

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
