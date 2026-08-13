#include "trade_network.hpp"
#include "generated/trade_io_2024.hpp"
#include "generated/trade_io_us_proxy.hpp"

#if __has_include("generated/trade_io_us_bea.hpp") && \
    __has_include("generated/trade_io_us_bea_certified.hpp")
#include "generated/trade_io_us_bea.hpp"
#define CAD_HAS_CERTIFIED_BEA_US_IO 1
#else
#define CAD_HAS_CERTIFIED_BEA_US_IO 0
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace cad {
namespace {

double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

double sector_parameter(double candidate, double fallback, double lo, double hi) {
  return clamp(candidate > 0.0 ? candidate : fallback, lo, hi);
}

double quantity_ratio(double elasticity, double tariff_rate) {
  return std::pow(1.0 + std::max(0.0, tariff_rate), -std::max(0.0, elasticity));
}

const std::array<TradeSectorProfile, kTradeSectorCount> kProfiles{{
  {"11","Agriculture, forestry, fishing & hunting",.82,.42,.72,.65},
  {"21","Mining, quarrying, oil & gas",.88,.18,.32,.75},
  {"22","Utilities",.16,.10,.25,.25},
  {"23","Construction",.18,.28,.82,.88},
  {"31-33","Manufacturing",.94,.76,.68,.92},
  {"42","Wholesale trade",.68,.58,.64,.74},
  {"44-45","Retail trade",.30,.72,.88,.62},
  {"48-49","Transportation & warehousing",.72,.48,.70,.86},
  {"51","Information & cultural industries",.34,.30,.48,.44},
  {"52","Finance & insurance",.22,.20,.34,.55},
  {"53","Real estate, rental & leasing",.10,.12,.30,.78},
  {"54","Professional, scientific & technical services",.38,.26,.58,.48},
  {"55","Management of companies & enterprises",.20,.18,.24,.40},
  {"56","Administrative, support & waste services",.28,.24,.86,.72},
  {"61","Educational services",.08,.10,.82,.18},
  {"62","Health care & social assistance",.06,.14,.94,.16},
  {"71","Arts, entertainment & recreation",.14,.16,.88,.68},
  {"72","Accommodation & food services",.18,.52,.96,.82},
  {"81","Other services (except public administration)",.16,.30,.90,.58},
  {"91","Public administration",.04,.08,.62,.12}
}};

// Canada production coefficients are generated from Statistics Canada Table
// 36-10-0001-01 (2024, Canada, basic prices). Orientation is
// [downstream][upstream], A[j][i] = Z_ij / X_j.
const TradeInputOutputMatrix kCanadaMatrix = generated::kStatCanIo2024Matrix;

#if CAD_HAS_CERTIFIED_BEA_US_IO
const TradeInputOutputMatrix kUsMatrix = generated::kBeaUsIoMatrix;
constexpr bool kUsMatrixEmpirical = true;
#else
const TradeInputOutputMatrix kUsMatrix = generated::kEpaUseeioUsProxyMatrix;
constexpr bool kUsMatrixEmpirical = false;
#endif
constexpr bool kCanadaMatrixEmpirical = true;

TariffIncidence incidence(double headline, double coverage,
                          double negotiated_relief, double pass_through,
                          double elasticity) {
  const double relief = clamp(negotiated_relief / 100.0, 0.0, 1.0);
  const double rate = std::max(0.0, headline) * (1.0 - relief)
      * clamp(coverage / 100.0, 0.0, 1.0);
  const double pass = clamp(pass_through, 0.0, 1.0);
  const double buyer = rate * pass;
  const double absorbed = rate - buyer;
  const double exporter_share = clamp(.50 + .08 * (elasticity - .65), .35, .68);
  TariffIncidence out;
  out.applied_tariff = rate;
  out.buyer_pass_through = buyer;
  out.exporter_absorption = absorbed * exporter_share;
  out.importer_absorption = absorbed - out.exporter_absorption;
  return out;
}

double maximum_row_share(const TradeInputOutputMatrix& matrix) {
  double maximum = 0.0;
  for (const auto& row : matrix)
    maximum = std::max(maximum, std::accumulate(row.begin(), row.end(), 0.0));
  return maximum;
}

std::array<double, kTradeSectorCount> propagate_supplier_demand(
    const TradeInputOutputMatrix& matrix, std::size_t source, double direct_shock) {
  constexpr double transmission = 0.30;
  constexpr int max_rounds = 48;
  constexpr double tolerance = 1e-10;
  std::array<double, kTradeSectorCount> total{};
  std::array<double, kTradeSectorCount> frontier{};
  frontier[source] = direct_shock;

  for (int round = 0; round < max_rounds; ++round) {
    std::array<double, kTradeSectorCount> next{};
    double magnitude = 0.0;
    for (std::size_t downstream = 0; downstream < kTradeSectorCount; ++downstream) {
      if (std::abs(frontier[downstream]) < tolerance) continue;
      for (std::size_t upstream = 0; upstream < kTradeSectorCount; ++upstream) {
        next[upstream] += frontier[downstream] * matrix[downstream][upstream]
            * transmission;
      }
    }
    for (std::size_t i = 0; i < kTradeSectorCount; ++i) {
      total[i] += next[i];
      magnitude += std::abs(next[i]);
    }
    if (magnitude < tolerance) break;
    frontier = next;
  }
  return total;
}

std::array<double, kTradeSectorCount> propagate_input_cost(
    const TradeInputOutputMatrix& matrix, std::size_t source, double source_cost) {
  constexpr double input_incidence = 0.85;
  constexpr double downstream_transmission = 0.85;
  constexpr int max_rounds = 48;
  constexpr double tolerance = 1e-10;
  std::array<double, kTradeSectorCount> total{};
  std::array<double, kTradeSectorCount> frontier{};
  frontier[source] = source_cost * input_incidence;

  for (int round = 0; round < max_rounds; ++round) {
    std::array<double, kTradeSectorCount> next{};
    double magnitude = 0.0;
    for (std::size_t downstream = 0; downstream < kTradeSectorCount; ++downstream) {
      for (std::size_t upstream = 0; upstream < kTradeSectorCount; ++upstream) {
        next[downstream] += matrix[downstream][upstream] * frontier[upstream]
            * downstream_transmission;
      }
    }
    for (std::size_t i = 0; i < kTradeSectorCount; ++i) {
      total[i] += next[i];
      magnitude += std::abs(next[i]);
    }
    if (magnitude < tolerance) break;
    frontier = next;
  }
  return total;
}

}  // namespace

const std::array<TradeSectorProfile, kTradeSectorCount>& trade_sector_profiles() {
  return kProfiles;
}

const TradeInputOutputMatrix& canada_trade_input_output_matrix() {
  return kCanadaMatrix;
}

const TradeInputOutputMatrix& us_trade_input_output_matrix() {
  return kUsMatrix;
}

bool canada_trade_input_output_empirical() {
  return kCanadaMatrixEmpirical;
}

bool us_trade_input_output_empirical() {
  return kUsMatrixEmpirical;
}

const TradeInputOutputMatrix& trade_input_output_matrix() {
  return canada_trade_input_output_matrix();
}

TradeSourceContribution evaluate_trade_source(const TradeNetworkInput& input,
                                              std::size_t source,
                                              double us_coverage,
                                              double canada_coverage) {
  TradeSourceContribution out;
  if (source >= kTradeSectorCount) return out;

  const auto& src = kProfiles[source];
  const double us_elasticity = sector_parameter(
      input.us_trade_elasticity[source], input.trade_elasticity, .10, 20.0);
  const double canada_elasticity = sector_parameter(
      input.canada_trade_elasticity[source], input.trade_elasticity, .10, 20.0);
  const double us_pass = sector_parameter(
      input.us_price_pass_through[source], input.price_pass_through, 0.0, 1.0);
  const double canada_pass = sector_parameter(
      input.canada_price_pass_through[source], input.price_pass_through, 0.0, 1.0);
  const double diversification = clamp(input.diversification, 0.0, .75);

  out.us_tariff = incidence(input.us_headline_tariff, us_coverage,
      input.negotiated_relief, us_pass, us_elasticity);
  out.canada_tariff = incidence(input.canada_headline_tariff, canada_coverage,
      input.negotiated_relief, canada_pass, canada_elasticity);

  const double canada_quantity_loss = 1.0 - quantity_ratio(
      us_elasticity, out.us_tariff.applied_tariff / 100.0);
  const double us_quantity_loss = 1.0 - quantity_ratio(
      canada_elasticity, out.canada_tariff.applied_tariff / 100.0);
  const double canada_direct_drag = -100.0 * canada_quantity_loss
      * src.trade * (.72 - .28 * diversification);
  const double us_direct_drag = -100.0 * us_quantity_loss * src.import * .46;

  const auto canada_supplier = propagate_supplier_demand(
      kCanadaMatrix, source, canada_direct_drag);
  const auto us_supplier = propagate_supplier_demand(
      kUsMatrix, source, us_direct_drag);
  const auto us_costs = propagate_input_cost(
      kUsMatrix, source, out.us_tariff.buyer_pass_through);
  const auto canada_costs = propagate_input_cost(
      kCanadaMatrix, source, out.canada_tariff.buyer_pass_through);

  for (std::size_t sector = 0; sector < kTradeSectorCount; ++sector) {
    const auto& dst = kProfiles[sector];
    out.canada_output[sector] += canada_supplier[sector];
    out.us_output[sector] += us_supplier[sector];
    out.us_upstream_cost[sector] += us_costs[sector];
    out.canada_upstream_cost[sector] += canada_costs[sector];
    out.us_prices[sector] += .70 * us_costs[sector];
    out.canada_prices[sector] += .70 * canada_costs[sector];
    out.us_output[sector] -= us_costs[sector] * (.12 + .18 * dst.cyclical);
    out.canada_output[sector] -= canada_costs[sector] * (.12 + .18 * dst.cyclical);
    out.canada_jobs[sector] = out.canada_output[sector] * (.20 + .35 * dst.jobs);
    out.us_jobs[sector] = out.us_output[sector] * (.20 + .35 * dst.jobs);
  }
  return out;
}

TradeNetworkResult evaluate_trade_network(const TradeNetworkInput& input) {
  TradeNetworkResult result;
  for (std::size_t source = 0; source < kTradeSectorCount; ++source) {
    const auto contribution = evaluate_trade_source(
        input, source, input.us_coverage[source], input.canada_coverage[source]);
    result.sectors[source].us_tariff = contribution.us_tariff;
    result.sectors[source].canada_tariff = contribution.canada_tariff;
    for (std::size_t sector = 0; sector < kTradeSectorCount; ++sector) {
      auto& dst = result.sectors[sector];
      dst.canada_indirect_output += contribution.canada_output[sector];
      dst.canada_indirect_jobs += contribution.canada_jobs[sector];
      dst.canada_indirect_prices += contribution.canada_prices[sector];
      dst.us_indirect_output += contribution.us_output[sector];
      dst.us_indirect_jobs += contribution.us_jobs[sector];
      dst.us_indirect_prices += contribution.us_prices[sector];
      dst.canada_upstream_cost += contribution.canada_upstream_cost[sector];
      dst.us_upstream_cost += contribution.us_upstream_cost[sector];
    }
  }

  double canada_weight = 0.0, us_weight = 0.0;
  double canada_drag = 0.0, us_drag = 0.0;
  double canada_cost = 0.0, us_cost = 0.0;
  for (std::size_t i = 0; i < kTradeSectorCount; ++i) {
    const auto& p = kProfiles[i];
    canada_weight += p.trade;
    us_weight += p.import;
    canada_drag += p.trade * std::max(0.0, -result.sectors[i].canada_indirect_output) / 100.0;
    us_drag += p.import * std::max(0.0, -result.sectors[i].us_indirect_output) / 100.0;
    canada_cost += p.import * result.sectors[i].canada_upstream_cost;
    us_cost += p.import * result.sectors[i].us_upstream_cost;
  }
  result.canada_supply_chain_drag = canada_drag / std::max(1e-9, canada_weight);
  result.us_supply_chain_drag = us_drag / std::max(1e-9, us_weight);
  result.canada_input_cost_pressure = canada_cost / std::max(1e-9, us_weight);
  result.us_input_cost_pressure = us_cost / std::max(1e-9, us_weight);
  return result;
}

double maximum_trade_input_share() {
  return maximum_row_share(kCanadaMatrix);
}

double maximum_us_trade_input_share() {
  return maximum_row_share(kUsMatrix);
}

std::string trade_network_methodology() {
#if CAD_HAS_CERTIFIED_BEA_US_IO
  return "Two-country production-network architecture. Canada uses the empirically aggregated 20-sector direct-requirements matrix from Statistics Canada Table 36-10-0001-01 (2024, Canada, basic prices): 40,364 detailed inter-industry transaction cells and 213 gross-output rows aggregated as A[j][i]=Z_ij/X_j. The U.S. network uses the separately generated and certified BEA Input-Output domestic direct-requirements artifact. Tariff quantity responses are bounded constant-elasticity responses. Supplier-demand losses are propagated upstream and imported-input price shocks downstream with convergent multi-round damped total-requirements iterations rather than a one-hop matrix multiplication. Directional sector-specific trade elasticity and price-pass-through overrides are supported and fall back to declared aggregate anchors when no production-compatible sector estimate is supplied. Imports, taxes, value added and final demand remain outside each domestic direct-requirements matrix.";
#else
  return "Two-country production-network architecture. Canada uses the empirically aggregated 20-sector direct-requirements matrix from Statistics Canada Table 36-10-0001-01 (2024, Canada, basic prices): 40,364 detailed inter-industry transaction cells and 213 gross-output rows aggregated as A[j][i]=Z_ij/X_j. Pending a current-vintage certified BEA artifact, the U.S. network uses a distinct provisional U.S.-specific structural proxy from EPA USEEIO v2.5 model USEEIOv2.5-catbird-22. The selected A_d matrix contains domestic direct requirements, and EPA documents that the detailed v2.5 model uses underlying U.S. input-output data for 2017. Four accounting/adjustment commodities are excluded, leaving 398 of 402 commodities mapped into the simulator's 20 sectors using commodity-output weights. A generated BEA header activates only with a separately committed certification marker, preventing an unreviewed local artifact from silently changing production. This proxy must not be described as current-vintage empirical U.S. IO calibration. Tariff quantity responses are bounded constant-elasticity responses. Supplier-demand losses are propagated upstream and imported-input price shocks downstream with convergent multi-round damped total-requirements iterations rather than a one-hop matrix multiplication. Directional sector-specific trade elasticity and price-pass-through overrides are supported and fall back to declared aggregate anchors when no production-compatible sector estimate is supplied. Imports, taxes, value added and final demand remain outside each domestic direct-requirements matrix.";
#endif
}

}  // namespace cad
