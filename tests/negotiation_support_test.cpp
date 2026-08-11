#include "negotiation_support.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

cad::Scenario scenario(std::string id, std::string name, double boc, double federal, double us,
                       double relief, double ca_exports, double us_exports, double us_growth) {
  cad::Scenario s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.boc_score = boc;
  s.federal_score = federal;
  s.us_score = us;
  s.negotiated_relief = relief;
  s.export_change = ca_exports;
  s.us_export_change = us_exports;
  s.us_growth = us_growth;
  s.growth = 1.8;
  s.inflation = 2.2;
  s.recession_risk = 12.0;
  s.sector_verified = true;
  s.trade_balance_gap_usd = 15.0;
  s.applied_us_sector_coverage.fill(50.0);
  s.applied_canada_sector_coverage.fill(25.0);
  return s;
}

cad::Result synthetic_result() {
  cad::Result result;
  result.recommendation.independent_us_trade_channel = true;
  result.recommendation.trade_balance_is_objective = false;
  result.recommendation.mandate_weights_fixed = true;
  result.recommendation.verification_monte_carlo_draws = 2800;
  result.scenarios.push_back(scenario("statusquo", "Status quo", 58.0, 61.0, 54.0, 0.0, -6.0, -4.0, 1.6));
  result.scenarios.push_back(scenario("diversify", "Diversification BATNA", 70.0, 74.0, 61.0, 10.0, -3.5, -2.2, 1.8));
  result.scenarios.push_back(scenario("compact", "Negotiated compact", 82.0, 80.0, 83.0, 75.0, -1.5, -0.8, 2.4));
  return result;
}

}  // namespace

int main() {
  cad::Economy economy;
  economy.cooperation_ceiling = 80.0;
  economy.risk_aversion = 50.0;
  economy.us_tariff_canada = 35.0;
  economy.canada_retaliatory_tariff = 10.0;
  economy.canada_priority = 60.0;
  economy.us_priority = 40.0;

  auto result = synthetic_result();
  const auto analysis = cad::analyze_negotiation(economy, result);
  assert(analysis.candidates_examined == 3 * 3125);
  assert(analysis.bargaining_grid_levels == 5);
  assert(analysis.individually_rational_count > 0);
  assert(analysis.pareto_frontier_size > 0);
  assert(!analysis.frontier.empty());
  assert(analysis.canada_reservation >= analysis.canada_batna);
  assert(analysis.us_reservation >= analysis.us_batna);
  assert(analysis.recommended.individually_rational);
  assert(analysis.recommended.pareto_efficient);
  assert(analysis.recommended.issues.size() == 5);
  assert(analysis.recommended.sector_verified);
  assert(analysis.recommended.verified_win_win);
  assert(analysis.data_integrity_pass);
  assert(analysis.independent_us_trade_channel);
  assert(!analysis.trade_balance_is_objective);
  assert(analysis.mandate_weights_fixed);
  assert(analysis.sector_verification_draws == 2800);

  // The recommended diplomatic package carries the exact sector schedule that
  // was re-simulated by the policy engine rather than a disconnected UI hint.
  for (std::size_t i = 0; i < analysis.recommended.us_sector_coverage.size(); ++i) {
    assert(std::abs(analysis.recommended.us_sector_coverage[i] - 50.0) < 1e-9);
    assert(std::abs(analysis.recommended.canada_sector_coverage[i] - 25.0) < 1e-9);
  }

  // Canada's and the United States' export channels are distinct. The package
  // must not collapse asymmetric country trade outcomes into one proxy.
  assert(std::abs(analysis.recommended.canada_export_change
      - analysis.recommended.us_export_change) > 1e-6);

  // Bilateral accounting balance is report-only. Changing only the reported
  // trade gap must not change the bargain, utilities or Nash ranking.
  auto accounting_only = result;
  for (auto& s : accounting_only.scenarios) s.trade_balance_gap_usd += 5000.0;
  const auto accounting_analysis = cad::analyze_negotiation(economy, accounting_only);
  assert(accounting_analysis.recommended.strategy_id == analysis.recommended.strategy_id);
  assert(std::abs(accounting_analysis.recommended.canada_utility
      - analysis.recommended.canada_utility) < 1e-9);
  assert(std::abs(accounting_analysis.recommended.us_utility
      - analysis.recommended.us_utility) < 1e-9);
  assert(std::abs(accounting_analysis.recommended.nash_gain
      - analysis.recommended.nash_gain) < 1e-9);

  // The generalized Nash bargain must respect the fixed diplomatic mandate.
  cad::Economy canada_first = economy;
  canada_first.canada_priority = 90.0;
  canada_first.us_priority = 10.0;
  const auto canada_weighted = cad::analyze_negotiation(canada_first, result);
  assert(canada_weighted.candidates_examined == analysis.candidates_examined);
  assert(canada_weighted.recommended.individually_rational);

  const auto negotiation_json = cad::negotiation_to_json(analysis);
  assert(negotiation_json.find("\"batna\"") != std::string::npos);
  assert(negotiation_json.find("\"reservation\"") != std::string::npos);
  assert(negotiation_json.find("\"paretoFrontierSize\"") != std::string::npos);
  assert(negotiation_json.find("\"canadaExportChange\"") != std::string::npos);
  assert(negotiation_json.find("\"usExportChange\"") != std::string::npos);
  assert(negotiation_json.find("\"usSectorCoverage\"") != std::string::npos);
  assert(negotiation_json.find("\"canadaSectorCoverage\"") != std::string::npos);
  assert(negotiation_json.find("\"tradeBalanceIsObjective\":false") != std::string::npos);
  assert(negotiation_json.find("\"dataIntegrityPass\":true") != std::string::npos);
  assert(negotiation_json.find("\"verifiedWinWin\":true") != std::string::npos);

  const auto combined = cad::attach_negotiation_json("{\"policy\":1}", analysis);
  assert(combined.find("\"negotiation\"") != std::string::npos);

  // A zero cooperation ceiling must bind tariff-relief concessions even though
  // implementation and supply-chain issues can still be linked.
  economy.cooperation_ceiling = 0.0;
  const auto closed = cad::analyze_negotiation(economy, result);
  for (const auto& package : closed.frontier) {
    assert(package.issues.size() >= 2);
    assert(std::abs(package.issues[0].us_move) < 1e-9);
    assert(std::abs(package.issues[1].canada_move) < 1e-9);
  }

  // Trust certification must fail if a package did not pass sector-level
  // stochastic re-simulation, even if its reduced-form bargaining scores look good.
  auto unverified = synthetic_result();
  for (auto& s : unverified.scenarios) s.sector_verified = false;
  const auto unverified_analysis = cad::analyze_negotiation(economy, unverified);
  assert(!unverified_analysis.data_integrity_pass);
  assert(!unverified_analysis.recommended.verified_win_win);

  return 0;
}
