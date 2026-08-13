#include "trade_network.hpp"
#include "generated/trade_io_2024.hpp"
#include "generated/trade_io_us_proxy.hpp"

#include <cassert>
#include <cmath>
#include <numeric>
#include <string>

int main() {
  const auto& matrix = cad::canada_trade_input_output_matrix();
  const auto& us_matrix = cad::us_trade_input_output_matrix();

  // The Canadian production network is a frozen aggregation of the 2024
  // StatCan industry-by-industry table, not a hand-built bridge.
  assert(cad::canada_trade_input_output_empirical());
  assert(std::abs(matrix[4][4] - 0.354414317655) < 1e-12);  // manufacturing <- manufacturing
  assert(std::abs(matrix[3][4] - 0.283987709074) < 1e-12);  // construction <- manufacturing
  assert(std::abs(matrix[15][4] - 0.093370094078) < 1e-12); // health care <- manufacturing
  assert(std::abs(matrix[19][15] - 0.131880191344) < 1e-12); // public admin <- health care
  assert(std::abs(cad::maximum_trade_input_share()
      - cad::generated::kStatCanIoMaximumDomesticIntermediateShare) < 1e-10);
  for (const auto& row : matrix) {
    const double sum = std::accumulate(row.begin(), row.end(), 0.0);
    assert(sum > 0.17);
    assert(sum < 0.73);
  }

  // Until a BEA artifact and independent certification marker are both present,
  // the U.S. object must use the U.S.-specific EPA USEEIO proxy rather than
  // silently copying Canada's production coefficients.
  if (!cad::us_trade_input_output_empirical()) {
    assert(std::abs(us_matrix[4][4]
        - cad::generated::kEpaUseeioUsProxyMatrix[4][4]) < 1e-15);
    assert(std::abs(us_matrix[4][4] - 0.241110822962) < 1e-12);
    assert(std::abs(us_matrix[3][4] - 0.190642231341) < 1e-12);
    assert(std::abs(us_matrix[15][4] - 0.066993613516) < 1e-12);
    assert(std::abs(us_matrix[4][4] - matrix[4][4]) > 1e-3);
    assert(std::abs(cad::maximum_us_trade_input_share()
        - cad::generated::kEpaUseeioUsProxyMaximumDomesticIntermediateShare) < 1e-10);
  }
  for (const auto& row : us_matrix) {
    const double sum = std::accumulate(row.begin(), row.end(), 0.0);
    assert(sum > 0.0);
    assert(sum < 1.0);
    if (!cad::us_trade_input_output_empirical()) {
      assert(sum > 0.27);
      assert(sum < 0.58);
    }
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

  // The direct sector layer, macro ledger and first production-network shock
  // share one bounded constant-elasticity quantity response. At 50% with
  // elasticity .65 the loss is exactly 1 - 1.5^(-.65), not tariff*elasticity.
  const double expected_loss = 1.0 - std::pow(1.5, -0.65);
  assert(std::abs(manufacturing.us_tariff.quantity_loss - expected_loss) < 1e-12);
  assert(std::abs(manufacturing.canada_quantity_loss - expected_loss) < 1e-12);
  assert(std::abs(manufacturing.canada_direct_trade_drag
      - (-100.0 * expected_loss * .94 * .72)) < 1e-10);

  // A manufacturing tariff propagates into downstream U.S. industries through
  // the selected U.S. network and into Canadian suppliers through Canada's matrix.
  assert(network.sectors[3].us_upstream_cost > 0.0);   // construction
  assert(network.sectors[6].us_upstream_cost > 0.0);   // retail
  assert(network.sectors[15].us_upstream_cost > 0.0);  // health care
  assert(network.sectors[3].us_indirect_prices > 0.0);
  assert(network.sectors[3].us_indirect_output < 0.0);
  assert(network.sectors[3].us_indirect_jobs < 0.0);
  assert(network.us_supply_chain_drag > 0.0);
  assert(network.us_input_cost_pressure > 0.0);
  assert(std::abs(network.sectors[3].canada_upstream_cost) < 1e-12);

  // Network coefficients are executable structural inputs. Increasing the
  // downstream transmission must increase propagated costs without changing
  // the source tariff incidence/quantity response itself.
  cad::TradeNetworkInput stronger = input;
  stronger.tuning.downstream_cost_transmission = .94;
  stronger.tuning.input_cost_incidence = .98;
  stronger.tuning.supplier_demand_transmission = .42;
  const auto stronger_network = cad::evaluate_trade_network(stronger);
  assert(stronger_network.us_input_cost_pressure > network.us_input_cost_pressure);
  assert(stronger_network.canada_supply_chain_drag > network.canada_supply_chain_drag);
  assert(std::abs(stronger_network.sectors[4].us_tariff.quantity_loss
      - manufacturing.us_tariff.quantity_loss) < 1e-15);

  // Directional sector overrides are independent. A U.S.-side manufacturing
  // pass-through override changes U.S. incidence without changing Canada's
  // retaliatory-tariff incidence mapping.
  cad::TradeNetworkInput directional = input;
  directional.canada_headline_tariff = 5.0;
  directional.canada_coverage[4] = 100.0;
  directional.us_price_pass_through[4] = 1.0;
  directional.canada_price_pass_through[4] = 0.10;
  directional.us_trade_elasticity[4] = 1.10;
  directional.canada_trade_elasticity[4] = 0.40;
  const auto directional_source = cad::evaluate_trade_source(directional, 4, 100.0, 100.0);
  assert(std::abs(directional_source.us_tariff.buyer_pass_through - 50.0) < 1e-9);
  assert(std::abs(directional_source.canada_tariff.buyer_pass_through - 0.5) < 1e-9);
  assert(directional_source.us_tariff.exporter_absorption
      < network.sectors[4].us_tariff.exporter_absorption);
  assert(directional_source.us_tariff.quantity_loss
      > network.sectors[4].us_tariff.quantity_loss);

  // Even a 100% tariff with an extreme elasticity leaves a positive quantity
  // ratio: no linear response or arbitrary quantity floor is reintroduced.
  cad::TradeNetworkInput extreme = input;
  extreme.us_headline_tariff = 100.0;
  extreme.us_trade_elasticity[4] = 20.0;
  const auto extreme_source = cad::evaluate_trade_source(extreme, 4, 100.0, 0.0);
  assert(extreme_source.us_tariff.quantity_loss > .99);
  assert(extreme_source.us_tariff.quantity_loss < 1.0);
  assert(1.0 - extreme_source.us_tariff.quantity_loss > 0.0);

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
  assert(methodology.find("40,364") != std::string::npos);
  assert(methodology.find("213") != std::string::npos);
  assert(methodology.find("BEA") != std::string::npos);
  assert(methodology.find("constant-elasticity") != std::string::npos);
  assert(methodology.find("structural-registry") != std::string::npos);
  if (!cad::us_trade_input_output_empirical()) {
    assert(methodology.find("USEEIO v2.5") != std::string::npos);
    assert(methodology.find("USEEIOv2.5-catbird-22") != std::string::npos);
    assert(methodology.find("A_d") != std::string::npos);
    assert(methodology.find("402") != std::string::npos);
    assert(methodology.find("398") != std::string::npos);
    assert(methodology.find("2017") != std::string::npos);
    assert(methodology.find("commodity-output") != std::string::npos);
    assert(methodology.find("provisional") != std::string::npos);
    assert(methodology.find("current-vintage empirical U.S. IO calibration")
        != std::string::npos);
    assert(methodology.find("certification marker") != std::string::npos);
  }
  return 0;
}