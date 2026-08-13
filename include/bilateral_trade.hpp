#pragma once

#include "policy_engine.hpp"
#include "trade_network.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cad {

struct BilateralTradeState {
  double effective_us_tariff = 0.0;
  double effective_canada_tariff = 0.0;
  double canada_bilateral_quantity_ratio = 1.0;
  double us_bilateral_quantity_ratio = 1.0;
  double canada_total_export_quantity_ratio = 1.0;
  double canada_macro_trade_drag = 0.0;
  double us_macro_trade_drag = 0.0;
  double canada_exports_to_us_cad = 0.0;
  double canada_imports_from_us_cad = 0.0;
  double canada_export_redirection_cad = 0.0;
  double us_tariff_revenue_cad = 0.0;
  double canada_tariff_revenue_cad = 0.0;
  double canada_trade_balance_cad = 0.0;
};

namespace bilateral_trade_detail {

inline double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline double sector_elasticity(double candidate, double fallback) {
  return clamp(candidate > 0.0 ? candidate : fallback, 0.10, 20.0);
}

// Constant-elasticity quantity response to an ad-valorem price wedge. Unlike
// the former linear 1-epsilon*tau approximation, this is positive for every
// admissible tariff and remains meaningful in the dashboard's large-shock
// stress cases.
inline double quantity_ratio(double elasticity, double price_wedge) {
  const double wedge = std::max(-0.95, price_wedge);
  return std::pow(1.0 + wedge, -std::max(0.0, elasticity));
}

}  // namespace bilateral_trade_detail

inline BilateralTradeState build_bilateral_trade_state(
    const Economy& economy,
    const Scenario& policy,
    const StructuralParameters& parameters,
    const TradeNetworkResult& network) {
  using bilateral_trade_detail::clamp;
  using bilateral_trade_detail::quantity_ratio;
  using bilateral_trade_detail::sector_elasticity;

  BilateralTradeState out;
  const auto& profiles = trade_sector_profiles();
  const double diversification = clamp(
      policy.diversification + economy.trade_diversification, 0.0, 0.75);
  const double border = std::max(0.0, economy.border_friction) / 100.0;

  double ca_weight = 0.0;
  double us_weight = 0.0;
  double ca_ratio_sum = 0.0;
  double us_ratio_sum = 0.0;
  double us_tariff_sum = 0.0;
  double ca_tariff_sum = 0.0;
  double us_revenue_rate_sum = 0.0;
  double ca_revenue_rate_sum = 0.0;

  for (std::size_t i = 0; i < kTradeSectorCount; ++i) {
    const double ca_w = std::max(0.0, profiles[i].trade);
    const double us_w = std::max(0.0, profiles[i].import);
    const double us_tariff = std::max(0.0, network.sectors[i].us_tariff.applied_tariff) / 100.0;
    const double ca_tariff = std::max(0.0, network.sectors[i].canada_tariff.applied_tariff) / 100.0;
    const double ca_eps = sector_elasticity(
        economy.us_sector_trade_elasticity[i], economy.trade_elasticity)
        * std::max(0.0, parameters.tariff_revenue_elasticity_scale);
    const double us_eps = sector_elasticity(
        economy.canada_sector_trade_elasticity[i], economy.trade_elasticity)
        * std::max(0.0, parameters.tariff_revenue_elasticity_scale);

    const double ca_ratio = quantity_ratio(ca_eps, us_tariff + border);
    const double us_ratio = quantity_ratio(us_eps, ca_tariff + 0.45 * border);

    ca_weight += ca_w;
    us_weight += us_w;
    ca_ratio_sum += ca_w * ca_ratio;
    us_ratio_sum += us_w * us_ratio;
    us_tariff_sum += ca_w * us_tariff;
    ca_tariff_sum += us_w * ca_tariff;
    us_revenue_rate_sum += ca_w * ca_ratio * us_tariff;
    ca_revenue_rate_sum += us_w * us_ratio * ca_tariff;
  }

  ca_weight = std::max(1e-12, ca_weight);
  us_weight = std::max(1e-12, us_weight);
  out.canada_bilateral_quantity_ratio = clamp(ca_ratio_sum / ca_weight, 0.0, 1.5);
  out.us_bilateral_quantity_ratio = clamp(us_ratio_sum / us_weight, 0.0, 1.5);
  out.effective_us_tariff = 100.0 * us_tariff_sum / ca_weight;
  out.effective_canada_tariff = 100.0 * ca_tariff_sum / us_weight;

  // Diversification cushions Canadian aggregate export/GDP exposure by
  // redirecting part of the trade displaced from the U.S. market. It does not
  // fabricate Canada-U.S. bilateral exports: the bilateral ledger below still
  // uses the unadjusted bilateral quantity ratio.
  const double bilateral_loss = std::max(0.0, 1.0 - out.canada_bilateral_quantity_ratio);
  out.canada_total_export_quantity_ratio = clamp(
      1.0 - bilateral_loss * (1.0 - diversification), 0.0, 1.5);

  const double exposed_exports = clamp(economy.exports_to_us_share / 100.0, 0.0, 1.0);
  out.canada_macro_trade_drag = parameters.canada_trade_drag_scale
      * exposed_exports * economy.exports_gdp / 100.0
      * std::max(0.0, 1.0 - out.canada_total_export_quantity_ratio);
  out.us_macro_trade_drag = parameters.us_retaliation_drag_scale
      * clamp(economy.imports_from_us_share / 100.0, 0.0, 1.0)
      * std::max(0.0, 1.0 - out.us_bilateral_quantity_ratio);

  out.canada_exports_to_us_cad = std::max(0.0, economy.canada_exports_to_us_cad)
      * out.canada_bilateral_quantity_ratio;
  out.canada_imports_from_us_cad = std::max(0.0, economy.canada_imports_from_us_cad)
      * out.us_bilateral_quantity_ratio;
  out.canada_export_redirection_cad = std::max(0.0, economy.canada_exports_to_us_cad)
      * bilateral_loss * diversification;

  // Revenue is integrated over the same sector quantity response used above,
  // rather than recomputed from a separate aggregate elasticity equation.
  out.us_tariff_revenue_cad = std::max(0.0, economy.canada_exports_to_us_cad)
      * (us_revenue_rate_sum / ca_weight);
  out.canada_tariff_revenue_cad = std::max(0.0, economy.canada_imports_from_us_cad)
      * (ca_revenue_rate_sum / us_weight);
  out.canada_trade_balance_cad =
      out.canada_exports_to_us_cad - out.canada_imports_from_us_cad;
  return out;
}

}  // namespace cad
