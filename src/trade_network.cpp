#include "trade_network.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cad {
namespace {

double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
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

TradeInputOutputMatrix build_matrix() {
  TradeInputOutputMatrix matrix{};
  auto add = [&](std::size_t downstream, std::size_t upstream, double share) {
    matrix[downstream][upstream] += share;
  };

  // Ubiquitous business-service and logistics inputs. These are intentionally
  // conservative bridge coefficients so the network is active without being
  // misrepresented as a direct extraction of every StatCan detail-level cell.
  for (std::size_t downstream = 0; downstream < kTradeSectorCount; ++downstream) {
    if (downstream != 2) add(downstream, 2, .012);   // utilities
    if (downstream != 7) add(downstream, 7, .012);   // transportation
    if (downstream != 8) add(downstream, 8, .010);   // information
    if (downstream != 9) add(downstream, 9, .014);   // finance
    if (downstream != 11) add(downstream, 11, .014); // professional services
    if (downstream != 13) add(downstream, 13, .012); // administrative services
  }

  add(0,4,.080); add(0,2,.020); add(0,7,.030); add(0,9,.020);
  add(1,4,.070); add(1,2,.035); add(1,3,.025); add(1,7,.035); add(1,11,.025);
  add(2,1,.070); add(2,4,.040); add(2,11,.020);
  add(3,4,.130); add(3,1,.040); add(3,5,.025); add(3,7,.020); add(3,9,.025); add(3,11,.035);
  add(4,0,.035); add(4,1,.070); add(4,2,.035); add(4,4,.120); add(4,5,.035); add(4,7,.040); add(4,11,.025);
  add(5,4,.080); add(5,7,.055); add(5,9,.020); add(5,10,.025); add(5,8,.020);
  add(6,4,.060); add(6,5,.100); add(6,7,.035); add(6,10,.040); add(6,9,.025); add(6,8,.020);
  add(7,1,.080); add(7,4,.065); add(7,2,.025); add(7,9,.020); add(7,11,.020);
  add(8,2,.025); add(8,4,.030); add(8,11,.060); add(8,9,.025);
  add(9,8,.050); add(9,10,.025); add(9,11,.055); add(9,13,.025);
  add(10,3,.030); add(10,2,.035); add(10,9,.040); add(10,11,.025); add(10,13,.025);
  add(11,8,.040); add(11,9,.025); add(11,10,.030); add(11,13,.030);
  add(12,8,.030); add(12,9,.040); add(12,11,.060);
  add(13,4,.025); add(13,7,.025); add(13,8,.025); add(13,10,.035);
  add(14,2,.020); add(14,8,.025); add(14,10,.035); add(14,11,.020); add(14,13,.020);
  add(15,4,.070); add(15,5,.040); add(15,2,.020); add(15,11,.025); add(15,13,.025);
  add(16,4,.035); add(16,8,.045); add(16,10,.040); add(16,11,.020);
  add(17,0,.070); add(17,4,.060); add(17,5,.055); add(17,2,.025); add(17,7,.020); add(17,10,.035);
  add(18,4,.040); add(18,5,.025); add(18,2,.020); add(18,10,.035); add(18,11,.025);
  add(19,4,.025); add(19,2,.020); add(19,8,.025); add(19,11,.040); add(19,10,.020);

  return matrix;
}

const TradeInputOutputMatrix kMatrix = build_matrix();

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

}  // namespace

const std::array<TradeSectorProfile, kTradeSectorCount>& trade_sector_profiles() {
  return kProfiles;
}

const TradeInputOutputMatrix& trade_input_output_matrix() {
  return kMatrix;
}

TradeSourceContribution evaluate_trade_source(const TradeNetworkInput& input,
                                              std::size_t source,
                                              double us_coverage,
                                              double canada_coverage) {
  TradeSourceContribution out;
  if (source >= kTradeSectorCount) return out;

  const auto& src = kProfiles[source];
  const double elasticity = clamp(input.trade_elasticity, .10, 5.0);
  const double diversification = clamp(input.diversification, 0.0, .75);
  out.us_tariff = incidence(input.us_headline_tariff, us_coverage,
      input.negotiated_relief, input.price_pass_through, elasticity);
  out.canada_tariff = incidence(input.canada_headline_tariff, canada_coverage,
      input.negotiated_relief, input.price_pass_through, elasticity);

  // Reduced exporter demand feeds backwards to its domestic suppliers. The
  // direct exporting-sector effect remains in policy_engine.cpp; this layer
  // contributes only the supplier spillover so the finite sector search stays
  // additive and auditable.
  const double canada_direct_drag = -100.0 * (out.us_tariff.applied_tariff / 100.0)
      * elasticity * src.trade * (.72 - .28 * diversification);
  const double us_direct_drag = -100.0 * (out.canada_tariff.applied_tariff / 100.0)
      * elasticity * src.import * .46;

  for (std::size_t upstream = 0; upstream < kTradeSectorCount; ++upstream) {
    const double requirement = kMatrix[source][upstream];
    if (requirement <= 0.0) continue;
    out.canada_output[upstream] += canada_direct_drag * requirement * .30;
    out.us_output[upstream] += us_direct_drag * requirement * .30;
  }

  // Tariffs paid by the importing economy also raise the cost of source-sector
  // inputs used downstream. Pass-through is applied before the IO coefficient;
  // firms then absorb part of the marginal-cost shock through margins/output.
  for (std::size_t downstream = 0; downstream < kTradeSectorCount; ++downstream) {
    const double requirement = kMatrix[downstream][source];
    if (requirement <= 0.0) continue;
    const auto& dst = kProfiles[downstream];
    const double us_cost = out.us_tariff.buyer_pass_through * requirement * .85;
    const double canada_cost = out.canada_tariff.buyer_pass_through * requirement * .85;
    out.us_upstream_cost[downstream] += us_cost;
    out.canada_upstream_cost[downstream] += canada_cost;
    out.us_prices[downstream] += .70 * us_cost;
    out.canada_prices[downstream] += .70 * canada_cost;
    out.us_output[downstream] -= us_cost * (.12 + .18 * dst.cyclical);
    out.canada_output[downstream] -= canada_cost * (.12 + .18 * dst.cyclical);
  }

  for (std::size_t sector = 0; sector < kTradeSectorCount; ++sector) {
    const auto& dst = kProfiles[sector];
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
  double maximum = 0.0;
  for (const auto& row : kMatrix)
    maximum = std::max(maximum, std::accumulate(row.begin(), row.end(), 0.0));
  return maximum;
}

std::string trade_network_methodology() {
  return "Linear 20-sector input-output tariff-incidence bridge mapped to the Statistics Canada 2024 sector structure. Direct tariff rates remain user/calibration inputs; the current IO coefficients are transparent provisional model coefficients, not claimed as a cell-for-cell extraction of Table 36-10-0001-01. Tariff pass-through is split between buyers, importers and exporters before downstream input-cost and upstream demand propagation.";
}

}  // namespace cad
