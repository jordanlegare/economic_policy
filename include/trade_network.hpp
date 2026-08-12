#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace cad {

constexpr std::size_t kTradeSectorCount = 20;

struct TradeSectorProfile {
  const char* code;
  const char* name;
  double trade;
  double import;
  double jobs;
  double cyclical;
};

using TradeInputOutputMatrix =
    std::array<std::array<double, kTradeSectorCount>, kTradeSectorCount>;

struct TariffIncidence {
  double applied_tariff = 0.0;
  double buyer_pass_through = 0.0;
  double importer_absorption = 0.0;
  double exporter_absorption = 0.0;
};

struct TradeNetworkInput {
  double us_headline_tariff = 0.0;
  double canada_headline_tariff = 0.0;
  double negotiated_relief = 0.0;
  double diversification = 0.0;
  double trade_elasticity = 0.65;
  double price_pass_through = 0.24;
  std::array<double, kTradeSectorCount> us_coverage{};
  std::array<double, kTradeSectorCount> canada_coverage{};

  // Optional directional/sector overrides. Non-positive entries deliberately
  // fall back to the aggregate scalar above. This allows the calibration layer
  // to activate sector estimates only when their estimand is production-
  // compatible, without treating reference-only literature as direct evidence.
  // `us_*` describes U.S. imports from Canada (the U.S. tariff direction);
  // `canada_*` describes Canadian imports from the United States.
  std::array<double, kTradeSectorCount> us_trade_elasticity{};
  std::array<double, kTradeSectorCount> canada_trade_elasticity{};
  std::array<double, kTradeSectorCount> us_price_pass_through{};
  std::array<double, kTradeSectorCount> canada_price_pass_through{};
};

struct TradeSourceContribution {
  TariffIncidence us_tariff;
  TariffIncidence canada_tariff;
  std::array<double, kTradeSectorCount> canada_output{};
  std::array<double, kTradeSectorCount> canada_jobs{};
  std::array<double, kTradeSectorCount> canada_prices{};
  std::array<double, kTradeSectorCount> us_output{};
  std::array<double, kTradeSectorCount> us_jobs{};
  std::array<double, kTradeSectorCount> us_prices{};
  std::array<double, kTradeSectorCount> canada_upstream_cost{};
  std::array<double, kTradeSectorCount> us_upstream_cost{};
};

struct TradeNetworkSector {
  TariffIncidence us_tariff;
  TariffIncidence canada_tariff;
  double canada_indirect_output = 0.0;
  double canada_indirect_jobs = 0.0;
  double canada_indirect_prices = 0.0;
  double us_indirect_output = 0.0;
  double us_indirect_jobs = 0.0;
  double us_indirect_prices = 0.0;
  double canada_upstream_cost = 0.0;
  double us_upstream_cost = 0.0;
};

struct TradeNetworkResult {
  std::array<TradeNetworkSector, kTradeSectorCount> sectors{};
  double canada_supply_chain_drag = 0.0;
  double us_supply_chain_drag = 0.0;
  double canada_input_cost_pressure = 0.0;
  double us_input_cost_pressure = 0.0;
};

const std::array<TradeSectorProfile, kTradeSectorCount>& trade_sector_profiles();

// Country-specific direct-requirements matrices. Canada is the certified 2024
// Statistics Canada aggregation. The U.S. accessor is intentionally separate;
// until a BEA artifact is generated and certified it returns the explicitly
// labelled structural proxy rather than pretending the Canadian cells are U.S.
// observations.
const TradeInputOutputMatrix& canada_trade_input_output_matrix();
const TradeInputOutputMatrix& us_trade_input_output_matrix();
bool canada_trade_input_output_empirical();
bool us_trade_input_output_empirical();

// Backward-compatible alias for callers that historically requested the one
// production matrix; it continues to mean the Canadian empirical matrix.
const TradeInputOutputMatrix& trade_input_output_matrix();

TradeSourceContribution evaluate_trade_source(const TradeNetworkInput& input,
                                              std::size_t source,
                                              double us_coverage,
                                              double canada_coverage);
TradeNetworkResult evaluate_trade_network(const TradeNetworkInput& input);
double maximum_trade_input_share();
double maximum_us_trade_input_share();
std::string trade_network_methodology();

}  // namespace cad
