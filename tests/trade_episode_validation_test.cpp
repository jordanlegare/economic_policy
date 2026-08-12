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
  return 0;
}
