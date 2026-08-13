#include "bilateral_trade.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

cad::TradeNetworkInput input_for(const cad::Economy& economy,
                                 const cad::Scenario& policy) {
  cad::TradeNetworkInput input;
  input.us_headline_tariff = economy.us_tariff_canada;
  input.canada_headline_tariff = economy.canada_retaliatory_tariff;
  input.negotiated_relief = policy.negotiated_relief;
  input.diversification = policy.diversification + economy.trade_diversification;
  input.trade_elasticity = economy.trade_elasticity;
  input.price_pass_through = economy.tariff_price_pass_through;
  input.us_coverage = economy.us_sector_coverage;
  input.canada_coverage = economy.canada_sector_coverage;
  input.us_trade_elasticity = economy.us_sector_trade_elasticity;
  input.canada_trade_elasticity = economy.canada_sector_trade_elasticity;
  input.us_price_pass_through = economy.us_sector_price_pass_through;
  input.canada_price_pass_through = economy.canada_sector_price_pass_through;
  return input;
}

cad::BilateralTradeState state_for(const cad::Economy& economy,
                                   const cad::Scenario& policy) {
  const auto network = cad::evaluate_trade_network(input_for(economy, policy));
  return cad::build_bilateral_trade_state(
      economy, policy, cad::StructuralParameters{}, network);
}

}  // namespace

int main() {
  cad::Economy baseline;
  baseline.us_tariff_canada = 50.0;
  baseline.canada_retaliatory_tariff = 25.0;
  baseline.border_friction = 2.0;
  baseline.trade_elasticity = 0.65;
  baseline.canada_exports_to_us_cad = 600.0;
  baseline.canada_imports_from_us_cad = 400.0;
  baseline.usdcad = 1.40;

  cad::Scenario policy;
  policy.negotiated_relief = 0.0;
  policy.diversification = 0.0;

  const auto low = state_for(baseline, policy);
  assert(low.canada_bilateral_quantity_ratio > 0.0);
  assert(low.canada_bilateral_quantity_ratio < 1.0);
  assert(low.us_bilateral_quantity_ratio > 0.0);
  assert(low.us_bilateral_quantity_ratio < 1.0);
  assert(low.us_tariff_revenue_cad > 0.0);
  assert(low.canada_tariff_revenue_cad > 0.0);
  assert(std::abs(low.canada_trade_balance_cad
      - (low.canada_exports_to_us_cad - low.canada_imports_from_us_cad)) < 1e-9);

  // A production-compatible sector elasticity must alter the aggregate trade
  // state rather than stopping at the sector display/network layer.
  auto high_elasticity = baseline;
  high_elasticity.us_sector_trade_elasticity[0] = 12.5;
  const auto high = state_for(high_elasticity, policy);
  assert(high.canada_bilateral_quantity_ratio
      < low.canada_bilateral_quantity_ratio - 1e-6);
  assert(high.canada_macro_trade_drag > low.canada_macro_trade_drag + 1e-8);

  // Extreme stress tariffs remain strictly positive without an arbitrary 5%
  // quantity floor. This is the behavior the nonlinear response is intended to
  // guarantee.
  auto extreme = baseline;
  extreme.us_tariff_canada = 100.0;
  extreme.border_friction = 0.0;
  extreme.us_sector_trade_elasticity.fill(20.0);
  const auto extreme_state = state_for(extreme, policy);
  assert(extreme_state.canada_bilateral_quantity_ratio > 0.0);
  assert(extreme_state.canada_bilateral_quantity_ratio < 0.05);

  // Diversification cushions aggregate Canadian export/GDP exposure but does
  // not manufacture additional Canada-U.S. bilateral exports.
  cad::Scenario diversified = policy;
  diversified.diversification = 0.50;
  const auto diversified_state = state_for(baseline, diversified);
  assert(std::abs(diversified_state.canada_bilateral_quantity_ratio
      - low.canada_bilateral_quantity_ratio) < 1e-12);
  assert(diversified_state.canada_total_export_quantity_ratio
      > low.canada_total_export_quantity_ratio);
  assert(diversified_state.canada_exports_to_us_cad
      == low.canada_exports_to_us_cad);
  assert(diversified_state.canada_export_redirection_cad > 0.0);

  std::cout << "bilateral trade state tests passed\n";
  return 0;
}
