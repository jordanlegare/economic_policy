#include "policy_engine.hpp"
#include "calibration.hpp"
#include "compute_executor.hpp"
#include "negotiation_support.hpp"
#include "robust_recommendation.hpp"

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

  // Coarse-grained multicore execution must not change any seeded scenario,
  // ordering decision, floating-point reduction, or serialized result. Force a
  // single compute lane and require byte-for-byte equivalence with auto mode.
  if (cad::compute::worker_capacity() > 1) {
    const std::string parallel_json = cad::to_json(baseline);
    cad::compute::set_worker_limit(1);
    const auto serial_baseline = engine.evaluate(defaults);
    assert(cad::to_json(serial_baseline) == parallel_json);
    cad::compute::set_worker_limit(0);
    assert(cad::compute::configured_worker_count()
        == cad::compute::worker_capacity());
  }

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
  assert(!baseline.recommendation.global_search_complete);
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
  // Diversification may report separately identified Canadian export
  // redirection, but it is never added to the Canada-U.S. bilateral ledger.
  const auto& balance = find(baseline, "balance");
  assert(std::abs(balance.us_export_expansion_usd) < 1e-12);
  assert(std::isfinite(balance.canada_export_redirection_cad));
  assert(balance.canada_export_redirection_cad >= 0.0);
  assert(std::abs(balance.us_trade_balance_usd
      + balance.canada_trade_balance_cad / defaults.usdcad) < 1e-9);

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
  assert(json.find("\"globalSearchComplete\":false") != std::string::npos);
  assert(json.find("\"independentUsTradeChannel\":true") != std::string::npos);
  assert(json.find("\"tradeBalanceIsObjective\":false") != std::string::npos);
  assert(json.find("\"mandateWeightsFixed\":true") != std::string::npos);
  assert(json.find("\"allocationsExamined\":1") != std::string::npos);

  // Production startup contract: on the current certified tariff baseline,
  // carry every generated policy mix through the joint sector search. The
  // winner must improve or preserve both national welfare scores relative to
  // the same 2,800-draw starting posture, and the sector safety cap must not bind.
  const auto calibration = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  cad::Economy startup = cad::apply_calibration(cad::Economy{}, calibration);
  startup.exhaustive_policy_search = true;
  const auto initial = engine.evaluate(startup);
  assert(initial.candidates_examined == 288);
  assert(initial.recommendation.policy_candidates_verified == 301);
  assert(initial.scenarios.size() == 302); // 13 expert + 288 generated + baseline fallback.
  assert(initial.recommendation.global_search_complete);
  assert(initial.recommendation.verified_win_win);
  assert(initial.recommendation.growth_constraint_met);
  assert(initial.recommendation.verified_canada_score + 1e-9
      >= initial.recommendation.baseline_canada_score);
  assert(initial.recommendation.verified_us_score + 1e-9
      >= initial.recommendation.baseline_us_score);
  assert(initial.recommendation.sector_finalists_resimulated
      == initial.recommendation.sector_pareto_frontier_size
      || initial.recommendation.sector_pareto_frontier_size == 0);
  const auto initial_json = cad::to_json(initial);
  assert(initial_json.find("\"policyCandidatesVerified\":301") != std::string::npos);
  assert(initial_json.find("\"globalSearchComplete\":true") != std::string::npos);
  assert(initial_json.find("\"baselineCanadaScore\":") != std::string::npos);
  assert(initial_json.find("\"baselineUsScore\":") != std::string::npos);

  // Bargaining enumeration itself searches every linked-issue grid point for
  // every verified policy/sector strategy, so the point-estimate recommendation
  // is selected before the display/robustness retention cap is applied. If the
  // epsilon-frontier exceeds that cap, robustness must report incompleteness
  // rather than invalidating the point-estimate global-search guarantee.
  const auto initial_negotiation = cad::analyze_negotiation(startup, initial);
  assert(!initial_negotiation.frontier.empty());
  assert(initial_negotiation.pareto_frontier_size
      >= static_cast<int>(initial_negotiation.frontier.size()));
  assert(initial_negotiation.recommended.id == initial_negotiation.frontier.front().id);
  const auto initial_robust = cad::analyze_robust_recommendations(
      startup, initial, initial_negotiation, calibration, 200, 424242);
  assert(initial_robust.candidate_set_complete
      == (initial.recommendation.global_search_complete && initial_negotiation.frontier_complete));
  assert(initial_robust.packages.size() == initial_negotiation.frontier.size());
  assert(!initial_robust.recommended_package_id.empty());

  std::cout << "policy engine trust tests passed\n";
  return 0;
}