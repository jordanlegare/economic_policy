#include "policy_engine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

namespace {

const cad::Scenario& find(const cad::Result& result, const char* id) {
  for (const auto& scenario : result.scenarios) if (scenario.id == id) return scenario;
  assert(false);
  return result.scenarios.front();
}

}  // namespace

int main() {
  cad::PolicyEngine engine(42);
  cad::Economy defaults;
  const auto baseline = engine.evaluate(defaults);

  assert(baseline.scenarios.size() == 14);
  assert(baseline.candidates_examined == 288);
  assert(baseline.allocations_examined == 1);
  assert(baseline.gdp_floors_examined == 1);
  assert(!baseline.recommendation.strategy_id.empty());
  assert(baseline.recommendation.strategy_id == baseline.scenarios.front().id);
  assert(baseline.recommendation.canada_priority == 50.0);
  assert(baseline.recommendation.us_priority == 50.0);
  assert(baseline.recommendation.mandate_weights_fixed);
  assert(baseline.recommendation.independent_us_trade_channel);
  assert(!baseline.recommendation.trade_balance_is_objective);
  assert(baseline.recommendation.sector_grid_step == 25.0);
  assert(baseline.recommendation.sector_candidates_examined > 0);
  assert(baseline.recommendation.sector_pareto_frontier_size > 0);
  assert(baseline.recommendation.sector_finalists_resimulated > 0);
  assert(baseline.recommendation.sector_finalists_resimulated <= 8);
  assert(baseline.recommendation.base_monte_carlo_draws == 700);
  assert(baseline.recommendation.verification_monte_carlo_draws == 2800);
  assert(baseline.recommendation.verified_win_win);
  assert(baseline.recommendation.growth_constraint_met);

  for (std::size_t i = 0; i < baseline.recommendation.us_sector_coverage.size(); ++i) {
    assert(baseline.recommendation.us_sector_coverage[i] >= 0.0);
    assert(baseline.recommendation.us_sector_coverage[i] <= 100.0);
    assert(baseline.recommendation.canada_sector_coverage[i] >= 0.0);
    assert(baseline.recommendation.canada_sector_coverage[i] <= 100.0);
    assert(baseline.recommendation.us_sector_output[i] >= -1e-9);
    assert(baseline.recommendation.us_sector_output[i] <= 100.0 + 1e-9);
    assert(baseline.recommendation.canada_sector_value[i] >= -1e-9);
    assert(baseline.recommendation.canada_sector_value[i] <= 100.0 + 1e-9);
  }

  for (std::size_t i = 0; i < baseline.scenarios.size(); ++i) {
    const auto& scenario = baseline.scenarios[i];
    assert(scenario.sector_verified);
    assert(scenario.sectors.size() == 20);
    assert(scenario.us_export_path.size() == 12);
    assert(std::isfinite(scenario.us_export_change));
    assert(scenario.us_score > 0.0);
    if (i) assert(baseline.scenarios[i - 1].score >= scenario.score);
  }

  // No scenario may manufacture bilateral flows to hit an accounting target.
  const auto& balance = find(baseline, "balance");
  assert(std::abs(balance.us_export_expansion_usd) < 1e-12);
  assert(std::abs(balance.canada_export_redirection_cad) < 1e-12);

  // U.S. welfare must be independent of Canada's export-dollar accounting
  // baseline. Changing only that bookkeeping input changes the reported trade
  // balance, but not the U.S. export channel, U.S. score or selected strategy.
  cad::Economy accounting_only = defaults;
  accounting_only.canada_exports_to_us_cad *= 1.8;
  const auto accounting_result = engine.evaluate(accounting_only);
  const auto& baseline_statusquo = find(baseline, "statusquo");
  const auto& accounting_statusquo = find(accounting_result, "statusquo");
  assert(std::abs(baseline_statusquo.us_export_change - accounting_statusquo.us_export_change) < 1e-9);
  assert(std::abs(baseline_statusquo.us_score - accounting_statusquo.us_score) < 1e-9);
  assert(std::abs(baseline_statusquo.canada_trade_balance_cad
      - accounting_statusquo.canada_trade_balance_cad) > 1.0);
  assert(baseline.recommendation.strategy_id == accounting_result.recommendation.strategy_id);

  // Delegation priorities are fixed mandate inputs. The engine may optimize
  // deal terms under them, but may not search for a more convenient mandate.
  cad::Economy canada_mandate = defaults;
  canada_mandate.canada_priority = 80.0;
  canada_mandate.us_priority = 20.0;
  const auto mandate_result = engine.evaluate(canada_mandate);
  assert(std::abs(mandate_result.recommendation.canada_priority - 80.0) < 1e-9);
  assert(std::abs(mandate_result.recommendation.us_priority - 20.0) < 1e-9);
  assert(mandate_result.allocations_examined == 1);

  // The cooperation ceiling constrains total tariff relief. With a zero
  // ceiling, neither aggregate rate relief nor sector coverage may move.
  cad::Economy closed = defaults;
  closed.cooperation_ceiling = 0.0;
  const auto closed_result = engine.evaluate(closed);
  for (const auto& scenario : closed_result.scenarios)
    assert(std::abs(scenario.negotiated_relief) < 1e-9);
  for (std::size_t i = 0; i < closed.us_sector_coverage.size(); ++i) {
    assert(std::abs(closed_result.recommendation.us_sector_coverage[i]
        - closed.us_sector_coverage[i]) < 1e-9);
    assert(std::abs(closed_result.recommendation.canada_sector_coverage[i]
        - closed.canada_sector_coverage[i]) < 1e-9);
  }

  // Separate directional tariff coverage must still affect the real sector
  // channels under stress, not just labels in the recommendation UI.
  cad::Economy stress = defaults;
  stress.us_tariff_canada = 60.0;
  stress.canada_retaliatory_tariff = 25.0;
  const auto stressed = engine.evaluate(stress);
  assert(find(stressed, "statusquo").export_change < find(baseline, "statusquo").export_change);
  assert(find(stressed, "statusquo").us_export_change < find(baseline, "statusquo").us_export_change);

  const auto json = cad::to_json(baseline);
  assert(json.find("\"usExports\":") != std::string::npos);
  assert(json.find("\"usExportPath\":[") != std::string::npos);
  assert(json.find("\"sectorVerified\":true") != std::string::npos);
  assert(json.find("\"sectorCandidatesExamined\":") != std::string::npos);
  assert(json.find("\"sectorParetoFrontierSize\":") != std::string::npos);
  assert(json.find("\"verificationMonteCarloDraws\":2800") != std::string::npos);
  assert(json.find("\"verifiedWinWin\":true") != std::string::npos);
  assert(json.find("\"independentUsTradeChannel\":true") != std::string::npos);
  assert(json.find("\"tradeBalanceIsObjective\":false") != std::string::npos);
  assert(json.find("\"mandateWeightsFixed\":true") != std::string::npos);
  assert(json.find("\"allocationsExamined\":1") != std::string::npos);

  std::cout << "policy engine trust tests passed\n";
  return 0;
}