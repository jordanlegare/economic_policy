#include "trade_network.hpp"
#include "generated/trade_io_2024.hpp"

#include <cassert>
#include <cmath>
#include <numeric>
#include <string>

int main() {
  const auto& matrix = cad::trade_input_output_matrix();

  // The production network is a frozen aggregation of the 2024 StatCan
  // industry-by-industry table, not a hand-built bridge. Check several
  // economically material cells and the aggregate row bound exactly enough to
  // catch accidental replacement/reorientation of the generated matrix.
  assert(std::abs(matrix[4][4] - 0.354414317655) < 1e-12);  // manufacturing <- manufacturing
  assert(std::abs(matrix[3][4] - 0.283987709074) < 1e-12);  // construction <- manufacturing
  assert(std::abs(matrix[15][4] - 0.093370094078) < 1e-12); // health care <- manufacturing
  assert(std::abs(matrix[19][15] - 0.131880191344) < 1e-12); // public admin <- health care
  // Individual coefficients are frozen to 12 decimals, so a 20-term row sum
  // is compared at 1e-10 rather than against full-precision extraction output.
  assert(std::abs(cad::maximum_trade_input_share()
      - cad::generated::kStatCanIoMaximumDomesticIntermediateShare) < 1e-10);
  for (const auto& row : matrix) {
    const double sum = std::accumulate(row.begin(), row.end(), 0.0);
    assert(sum > 0.17);
    assert(sum < 0.73);
  }

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

  // A manufacturing tariff must propagate into downstream industries through
  // the empirically aggregated direct-requirements matrix.
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

  const std::string methodology = cad::trade_network_methodology();
  assert(methodology.find("36-10-0001-01") != std::string::npos);
  assert(methodology.find("2024 Canada basic-price") != std::string::npos);
  assert(methodology.find("Z_ij") != std::string::npos);
  assert(methodology.find("40,364") != std::string::npos);
  assert(methodology.find("213") != std::string::npos);
  assert(methodology.find("provisional") == std::string::npos);
  return 0;
}
