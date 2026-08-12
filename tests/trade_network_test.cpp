#include "trade_network.hpp"

#include <cassert>
#include <cmath>
#include <string>

int main() {
  cad::TradeNetworkInput input;
  input.us_headline_tariff = 50.0;
  input.canada_headline_tariff = 0.0;
  input.trade_elasticity = 0.65;
  input.price_pass_through = 0.24;
  input.us_coverage.fill(0.0);
  input.canada_coverage.fill(0.0);
  input.us_coverage[4] = 100.0;  // manufacturing

  const auto network = cad::evaluate_trade_network(input);
  const auto& manufacturing = network.sectors[4];
  assert(std::abs(manufacturing.us_tariff.applied_tariff - 50.0) < 1e-9);
  assert(std::abs(manufacturing.us_tariff.buyer_pass_through - 12.0) < 1e-9);
  assert(manufacturing.us_tariff.exporter_absorption > 0.0);
  assert(manufacturing.us_tariff.importer_absorption > 0.0);

  // A manufacturing tariff must propagate into downstream industries rather
  // than remaining isolated in the manufacturing row.
  assert(network.sectors[3].us_upstream_cost > 0.0);   // construction
  assert(network.sectors[6].us_upstream_cost > 0.0);   // retail
  assert(network.sectors[15].us_upstream_cost > 0.0);  // health care
  assert(network.sectors[3].us_indirect_prices > 0.0);
  assert(network.sectors[3].us_indirect_output < 0.0);
  assert(network.sectors[3].us_indirect_jobs < 0.0);
  assert(network.us_supply_chain_drag > 0.0);
  assert(network.us_input_cost_pressure > 0.0);

  // With no Canadian retaliation there is no Canadian input-cost channel from
  // the manufacturing tariff itself, although Canadian upstream suppliers can
  // still lose demand because U.S. buyers reduce Canadian manufacturing demand.
  assert(std::abs(network.sectors[3].canada_upstream_cost) < 1e-12);

  cad::TradeNetworkInput retaliation = input;
  retaliation.us_headline_tariff = 0.0;
  retaliation.us_coverage.fill(0.0);
  retaliation.canada_headline_tariff = 5.0;
  retaliation.canada_coverage[4] = 100.0;
  const auto canada_network = cad::evaluate_trade_network(retaliation);
  assert(std::abs(canada_network.sectors[4].canada_tariff.applied_tariff - 5.0) < 1e-9);
  assert(canada_network.sectors[3].canada_upstream_cost > 0.0);
  assert(canada_network.sectors[3].canada_indirect_prices > 0.0);
  assert(canada_network.canada_supply_chain_drag > 0.0);

  cad::TradeNetworkInput zero;
  zero.us_coverage.fill(100.0);
  zero.canada_coverage.fill(100.0);
  const auto no_tariff = cad::evaluate_trade_network(zero);
  assert(std::abs(no_tariff.canada_supply_chain_drag) < 1e-12);
  assert(std::abs(no_tariff.us_supply_chain_drag) < 1e-12);

  // The provisional bridge is intentionally bounded so no modeled industry
  // sources an implausibly dominant share of all inputs from the coarse matrix.
  assert(cad::maximum_trade_input_share() > 0.15);
  assert(cad::maximum_trade_input_share() < 0.65);
  const std::string methodology = cad::trade_network_methodology();
  assert(methodology.find("Statistics Canada 2024") != std::string::npos);
  assert(methodology.find("provisional") != std::string::npos);
  return 0;
}
