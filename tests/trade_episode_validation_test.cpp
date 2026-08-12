#include "policy_engine.hpp"
#include "trade_network.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

std::map<std::string, double> load_numeric_fixture(const std::string& path) {
  std::ifstream in(path);
  assert(in.good());
  std::map<std::string, double> values;
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const auto first = line.find(',');
    if (first == std::string::npos) continue;
    const auto second = line.find(',', first + 1);
    if (second == std::string::npos) continue;
    const std::string key = line.substr(0, first);
    const std::string raw = line.substr(first + 1, second - first - 1);
    try {
      values[key] = std::stod(raw);
    } catch (...) {
      // Metadata rows such as episode_id are intentionally non-numeric.
    }
  }
  return values;
}

const cad::Scenario& find_scenario(const cad::Result& result, const char* id) {
  for (const auto& scenario : result.scenarios)
    if (scenario.id == id) return scenario;
  assert(false && "expected scenario not found");
  return result.scenarios.front();
}

const cad::SectorImpact& manufacturing(const cad::Scenario& scenario) {
  for (const auto& sector : scenario.sectors)
    if (sector.code == "31-33") return sector;
  assert(false && "manufacturing sector not found");
  return scenario.sectors.front();
}

}  // namespace

int main() {
  const auto evidence = load_numeric_fixture(
      "data/backtests/2018-section232-trade-validation.csv");
  assert(std::abs(evidence.at("us_steel_tariff") - 25.0) < 1e-12);
  assert(std::abs(evidence.at("tariffed_steel_aluminum_export_change") + 50.0) < 1e-12);
  assert(std::abs(evidence.at("us_importer_tariff_cost_pass_through") - 1.0) < 1e-12);

  cad::TradeNetworkInput input;
  input.us_headline_tariff = evidence.at("us_steel_tariff");
  input.trade_elasticity = 0.65;
  input.price_pass_through = 0.24;
  input.us_coverage.fill(0.0);
  input.canada_coverage.fill(0.0);
  input.us_coverage[4] = 100.0;  // manufacturing proxy for treated metals

  const auto baseline = cad::evaluate_trade_source(input, 4, 100.0, 0.0);
  assert(std::abs(baseline.us_tariff.applied_tariff - 25.0) < 1e-12);
  assert(std::abs(baseline.us_tariff.buyer_pass_through - 6.0) < 1e-12);
  assert(baseline.canada_output[4] < 0.0);

  const auto network = cad::evaluate_trade_network(input);
  assert(network.canada_supply_chain_drag > 0.0);
  assert(network.us_input_cost_pressure > 0.0);

  // The 2018 treated-product evidence is deliberately a falsification/stress
  // benchmark rather than a forced production calibration. It indicates full
  // tariff-cost pass-through for treated steel/aluminum imports, materially
  // above the model's current 0.24 aggregate/shared anchor.
  cad::TradeNetworkInput stress = input;
  stress.us_price_pass_through[4] = evidence.at("us_importer_tariff_cost_pass_through");
  const auto observed_pass = cad::evaluate_trade_source(stress, 4, 100.0, 0.0);
  assert(std::abs(observed_pass.us_tariff.buyer_pass_through - 25.0) < 1e-12);
  assert(observed_pass.us_tariff.buyer_pass_through
      > baseline.us_tariff.buyer_pass_through + 1e-9);
  assert(observed_pass.us_upstream_cost[3] > baseline.us_upstream_cost[3]);

  // Country provenance is explicit: Canada is empirical; the U.S. matrix is a
  // separately replaceable proxy until the BEA artifact is certified.
  assert(cad::canada_trade_input_output_empirical());
  assert(!cad::us_trade_input_output_empirical());
  assert(cad::maximum_trade_input_share() > 0.0);
  assert(cad::maximum_us_trade_input_share() > 0.0);
  const auto method = cad::trade_network_methodology();
  assert(method.find("Statistics Canada") != std::string::npos);
  assert(method.find("BEA") != std::string::npos);
  assert(method.find("provisional") != std::string::npos);

  // Production integration: a direct U.S.-side sector pass-through override
  // must flow through PolicyEngine, not only the standalone network helper.
  cad::Economy production_economy;
  production_economy.cooperation_ceiling = 0.0;  // preserve submitted coverage
  cad::PolicyEngine production_engine(20260810);
  const auto production_baseline = production_engine.evaluate(production_economy);
  const auto& baseline_statusquo = find_scenario(production_baseline, "statusquo");
  const double baseline_manufacturing_pass = manufacturing(baseline_statusquo).us_buyer_pass_through;
  assert(std::abs(baseline_manufacturing_pass - 12.0) < 1e-9);

  production_economy.us_sector_price_pass_through[4] = 1.0;
  const auto sector_override = production_engine.evaluate(production_economy);
  const auto& override_statusquo = find_scenario(sector_override, "statusquo");
  assert(std::abs(manufacturing(override_statusquo).us_buyer_pass_through - 50.0) < 1e-9);
  assert(manufacturing(override_statusquo).us_buyer_pass_through
      > baseline_manufacturing_pass + 1e-9);

  // Dynamic monetary path: the declared search still chooses only the first
  // move, but quarter 2 can continue that direction when the incoming state
  // remains consistent with the initial action.
  cad::Economy weak = production_economy;
  weak.us_sector_price_pass_through.fill(0.0);
  weak.core_inflation = 2.10;
  weak.inflation = 2.10;
  weak.inflation_expectations = 2.0;
  weak.output_gap = -1.20;
  weak.gdp_growth = 0.6;
  const auto weak_result = production_engine.evaluate(weak);
  const auto& easing = find_scenario(weak_result, "relief");
  assert(easing.first_move_bp < 0.0);
  assert(easing.rates[1] < easing.rates[0] - 0.05);

  cad::Economy hot = production_economy;
  hot.us_sector_price_pass_through.fill(0.0);
  hot.core_inflation = 3.40;
  hot.inflation = 3.30;
  hot.inflation_expectations = 3.0;
  hot.output_gap = 0.70;
  hot.gdp_growth = 2.8;
  const auto hot_result = production_engine.evaluate(hot);
  const auto& tightening = find_scenario(hot_result, "guardrail");
  assert(tightening.first_move_bp > 0.0);
  assert(tightening.rates[1] > tightening.rates[0] + 0.05);

  // The measured residual dependence is active in production. Setting rho to
  // zero with the same seed preserves the marginal shock scales but changes the
  // joint output/inflation draw, so terminal inflation must move deterministically.
  cad::StructuralParameters zero_corr;
  zero_corr.output_inflation_shock_correlation = 0.0;
  cad::StructuralParameters high_corr = zero_corr;
  high_corr.output_inflation_shock_correlation = 0.80;
  cad::PolicyEngine zero_corr_engine(20260810, zero_corr);
  cad::PolicyEngine high_corr_engine(20260810, high_corr);
  const auto zero_corr_result = zero_corr_engine.evaluate(production_economy);
  const auto high_corr_result = high_corr_engine.evaluate(production_economy);
  const double zero_inf = find_scenario(zero_corr_result, "statusquo").inflation;
  const double high_inf = find_scenario(high_corr_result, "statusquo").inflation;
  assert(std::abs(zero_inf - high_inf) > 1e-6);
  return 0;
}
