#include "calibration.hpp"
#include "finalist_resimulation.hpp"
#include "interactive_frontier.hpp"
#include "negotiation_support.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"
#include "robust_trade_diplomacy.hpp"
#include "trade_diplomacy_platform.hpp"

#include <cmath>
#include <iostream>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ':' << __LINE__ << '\n'; return 1; } } while (0)

namespace {

struct SyntheticFinalistEngine {
  int calls = 0;

  cad::Result evaluate(cad::Economy& economy) {
    ++calls;
    cad::Result out;
    out.recommendation.verification_monte_carlo_draws = 2800;
    cad::Scenario scenario;
    scenario.id = "strategy-tariff-only";
    scenario.name = "Synthetic fixed strategy";
    scenario.negotiated_relief = 40.0;
    scenario.canada_score = 65.0;
    scenario.us_score = 64.0;
    scenario.bilateral_growth_floor = 0.75;
    scenario.recession_risk = 8.0;
    scenario.debt_stress_p90 = 48.0;
    scenario.inflation_stress_p90 = 2.9;
    scenario.sector_verified = true;
    scenario.applied_us_sector_coverage = economy.us_sector_coverage;
    scenario.applied_canada_sector_coverage = economy.canada_sector_coverage;
    out.scenarios.push_back(std::move(scenario));
    return out;
  }
};

cad::NegotiationAnalysis tariff_only_negotiation() {
  cad::NegotiationAnalysis negotiation;
  cad::NegotiationPackage package;
  package.id = "pkg-tariff-only";
  package.pareto_rank = 1;
  package.strategy_id = "strategy-tariff-only";
  package.strategy_name = "Synthetic fixed strategy";
  package.macro_base_verified = true;
  package.sector_posture_verified = true;
  package.bargaining_terms_screened = true;
  package.us_sector_coverage.fill(100.0);
  package.canada_sector_coverage.fill(100.0);
  package.issues = {
      {"us-tariff-relief", "U.S. residual-tariff relief", 0.0, 25.0},
      {"canada-tariff-relief", "Canadian residual retaliatory-tariff relief", 50.0, 0.0},
      {"border-facilitation", "Border and standards facilitation", 0.0, 0.0},
      {"procurement", "Reciprocal procurement access", 0.0, 0.0},
      {"supply-chain", "North American supply-chain commitment", 0.0, 0.0}
  };
  negotiation.recommended = package;
  negotiation.frontier.push_back(package);
  negotiation.pareto_frontier_size = 1;
  return negotiation;
}

cad::RobustRecommendationAnalysis tariff_only_robustness() {
  cad::RobustRecommendationAnalysis robustness;
  robustness.recommended_package_id = "pkg-tariff-only";
  cad::RobustPackageMetrics metrics;
  metrics.package_id = "pkg-tariff-only";
  metrics.strategy_id = "strategy-tariff-only";
  metrics.samples = 5000;
  metrics.joint_clear_probability = 0.91;
  metrics.max_regret = 0.10;
  metrics.clears_probability_gate = true;
  robustness.packages.push_back(metrics);
  return robustness;
}

cad::Result finalist_source_result() {
  cad::Result result;
  result.recommendation.verification_monte_carlo_draws = 2800;
  result.recommendation.baseline_canada_score = 60.0;
  result.recommendation.baseline_us_score = 59.0;
  result.recommendation.independent_us_trade_channel = true;
  result.recommendation.trade_balance_is_objective = false;
  result.recommendation.mandate_weights_fixed = true;
  cad::Scenario source;
  source.id = "strategy-tariff-only";
  source.name = "Synthetic fixed strategy";
  source.negotiated_relief = 40.0;
  source.sector_verified = true;
  source.applied_us_sector_coverage.fill(100.0);
  source.applied_canada_sector_coverage.fill(100.0);
  result.scenarios.push_back(std::move(source));
  return result;
}

}  // namespace

int main() {
  cad::Economy economy;
  const auto calibration = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  economy = cad::apply_calibration(economy, calibration);
  cad::PolicyEngine engine;
  const auto result = engine.evaluate(economy);
  const auto negotiation = cad::analyze_negotiation(economy, result);
  const auto robustness = cad::analyze_robust_recommendations(
      economy, result, negotiation, calibration, 200, 20260814);
  const auto publication = cad::build_trade_diplomacy_publication(
      economy, result, negotiation, robustness);
  const auto& platform = publication.platform;

  CHECK(!robustness.recommended_package_id.empty());
  CHECK(platform.recommended_robust_package_id == robustness.recommended_package_id);
  CHECK(publication.has_robust_recommended_package);
  CHECK(publication.robust_recommended_package.id == robustness.recommended_package_id);
  CHECK(publication.decision_authority == "second-stage-robustness");
  CHECK(publication.stress_cases_are_diagnostics);
  CHECK(platform.robust_cases == 6);
  CHECK(platform.robustness_cases.size() == 6);
  CHECK(!platform.robust_packages.empty());
  CHECK(platform.robust_packages.size() <= 10);
  CHECK(publication.total_robust_package_count >= platform.robust_packages.size());
  CHECK(platform.robust_packages.front().package_id == platform.recommended_robust_package_id);
  CHECK(std::isfinite(platform.recommended_worst_case_surplus));
  CHECK(platform.operational_readiness >= 0.0 && platform.operational_readiness <= 100.0);

  // The operations heuristic is analytical evidence, not an authorization gate.
  CHECK(std::abs(publication.analytical_readiness_score - platform.operational_readiness) < 1e-9);
  CHECK(publication.mandate_clearance == "not-checked");
  CHECK(publication.legal_clearance == "not-checked");
  CHECK(publication.implementation_readiness == "heuristic-score-only");
  CHECK(publication.overall_readiness == "structured-delegation-review-only");
  CHECK(!publication.offer_ready);
  const auto* recommended_metrics = cad::robust_trade_diplomacy_detail::find_robust_metrics(
      robustness, robustness.recommended_package_id);
  CHECK(recommended_metrics != nullptr);
  CHECK(publication.bargaining_robustness_screen_passed
      == recommended_metrics->clears_probability_gate);

  CHECK(platform.issue_tracks.size() >= 10);
  bool saw_treaty_track = false, saw_parallel_track = false;
  bool saw_market_access = false, saw_origin = false, saw_enforcement = false;
  for (const auto& issue : platform.issue_tracks) {
    saw_treaty_track = saw_treaty_track || !issue.parallel_track;
    saw_parallel_track = saw_parallel_track || issue.parallel_track;
    saw_market_access = saw_market_access || issue.id == "market-access";
    saw_origin = saw_origin || issue.id == "origin-autos";
    saw_enforcement = saw_enforcement || issue.id == "dispute-enforcement";
    CHECK(issue.joint_value >= 0.0 && issue.joint_value <= 100.0);
    CHECK(issue.domestic_sensitivity >= 0.0 && issue.domestic_sensitivity <= 100.0);
    CHECK(!issue.verification.empty());
  }
  CHECK(saw_treaty_track && saw_parallel_track);
  CHECK(saw_market_access && saw_origin && saw_enforcement);

  CHECK(platform.round_plan.size() >= 5);
  for (std::size_t i = 0; i < platform.round_plan.size(); ++i) {
    CHECK(platform.round_plan[i].order == static_cast<int>(i + 1));
    CHECK(!platform.round_plan[i].exit_criteria.empty());
    if (!platform.round_plan[i].package_id.empty())
      CHECK(platform.round_plan[i].package_id == robustness.recommended_package_id);
  }

  CHECK(platform.guardrails.size() >= 4);
  bool saw_deviation_guardrail = false;
  for (const auto& guardrail : platform.guardrails) {
    saw_deviation_guardrail = saw_deviation_guardrail || guardrail.id == "deviation-screen";
    CHECK(!guardrail.evidence.empty());
    CHECK(!guardrail.response.empty());
  }
  CHECK(saw_deviation_guardrail);

  bool canada_authority = false, us_authority = false, joint_implementation = false;
  for (const auto& gate : platform.stakeholder_gates) {
    if (gate.country == "Canada" && gate.formal_authority) canada_authority = true;
    if (gate.country == "United States" && gate.formal_authority) us_authority = true;
    if (gate.country == "Joint" && gate.formal_authority) joint_implementation = true;
  }
  CHECK(canada_authority && us_authority && joint_implementation);
  CHECK(platform.evidence_ledger.size() >= 4);

  const auto base = cad::attach_negotiation_json(cad::to_json(result), negotiation);
  const auto json = cad::attach_published_trade_diplomacy_json(base, publication);
  CHECK(json.find("\"tradeDiplomacy\"") != std::string::npos);
  CHECK(json.find("\"robustRecommendedPackage\"") != std::string::npos);
  CHECK(json.find("\"id\":\"" + robustness.recommended_package_id + "\"") != std::string::npos);
  CHECK(json.find("\"totalPackageCount\"") != std::string::npos);
  CHECK(json.find("\"decisionAuthority\":\"second-stage-robustness\"") != std::string::npos);
  CHECK(json.find("\"stressCasesAreDiagnostics\":true") != std::string::npos);
  CHECK(json.find("\"operationalReadinessSemantics\":\"analytical-implementation-heuristic\"") != std::string::npos);
  CHECK(json.find("\"readiness\"") != std::string::npos);
  CHECK(json.find("\"mandateClearance\":\"not-checked\"") != std::string::npos);
  CHECK(json.find("\"legalClearance\":\"not-checked\"") != std::string::npos);
  CHECK(json.find("\"implementationReadiness\":\"heuristic-score-only\"") != std::string::npos);
  CHECK(json.find("\"overallStatus\":\"structured-delegation-review-only\"") != std::string::npos);
  CHECK(json.find("\"offerReady\":false") != std::string::npos);
  CHECK(json.find("\"issueTracks\"") != std::string::npos);
  CHECK(json.find("\"robustnessCases\"") != std::string::npos);
  CHECK(json.find("\"stakeholderGates\"") != std::string::npos);
  CHECK(json.find("\"evidenceLedger\"") != std::string::npos);

  // The operational layer must never pretend to estimate political acceptance.
  CHECK(json.find("acceptanceProbability") == std::string::npos);

  // The finalist verifier owns only mappings that already have an auditable
  // production target. Residual tariff concessions are materialized before the
  // upstream negotiated-relief path is applied, so they cannot be double counted.
  cad::Economy finalist_economy;
  finalist_economy.us_tariff_canada = 50.0;
  finalist_economy.canada_retaliatory_tariff = 10.0;
  finalist_economy.minimum_bilateral_growth = 0.0;
  auto finalist_result = finalist_source_result();
  auto finalist_negotiation = tariff_only_negotiation();
  auto finalist_robustness = tariff_only_robustness();
  SyntheticFinalistEngine synthetic_engine;
  const auto finalist_analysis = cad::verify_bargaining_finalists(
      synthetic_engine, finalist_economy, finalist_result,
      finalist_negotiation, finalist_robustness, 3);
  CHECK(finalist_analysis.finalists.size() == 1);
  const auto& tariff_record = finalist_analysis.finalists.front();
  CHECK(tariff_record.mapping_complete);
  CHECK(tariff_record.executed);
  CHECK(tariff_record.full_package_resimulated);
  CHECK(tariff_record.verified_win_win);
  CHECK(tariff_record.verification_draws == 2800);
  CHECK(std::abs(tariff_record.materialized_us_headline_tariff - 37.5) < 1e-12);
  CHECK(std::abs(tariff_record.materialized_canada_headline_tariff - 5.0) < 1e-12);
  CHECK(std::abs(tariff_record.source_negotiated_relief - 40.0) < 1e-12);
  CHECK(synthetic_engine.calls == 1);
  CHECK(finalist_negotiation.recommended.bargaining_robustness_passed);
  CHECK(finalist_negotiation.recommended.full_package_resimulated);
  CHECK(finalist_negotiation.recommended.verified_win_win);

  const auto finalist_json = cad::finalist_resimulation_to_json(finalist_analysis);
  CHECK(finalist_json.find("\"methodology\":\"full-production-rerun-source-extraction\"") != std::string::npos);
  CHECK(finalist_json.find("\"termId\":\"procurement\",\"productionMapped\":false") != std::string::npos);
  CHECK(finalist_json.find("\"fullPackageResimulated\":true") != std::string::npos);
  CHECK(finalist_json.find("\"verifiedWinWin\":true") != std::string::npos);

  // Unmapped linked terms fail closed: no production rerun occurs and neither
  // full-package nor final win-win verification can be synthesized.
  auto unmapped_negotiation = tariff_only_negotiation();
  unmapped_negotiation.recommended.id = "pkg-unmapped";
  unmapped_negotiation.frontier.front().id = "pkg-unmapped";
  unmapped_negotiation.recommended.issues[3].canada_move = 25.0;
  unmapped_negotiation.recommended.issues[3].us_move = 25.0;
  unmapped_negotiation.frontier.front().issues = unmapped_negotiation.recommended.issues;
  auto unmapped_robustness = tariff_only_robustness();
  unmapped_robustness.recommended_package_id = "pkg-unmapped";
  unmapped_robustness.packages.front().package_id = "pkg-unmapped";
  SyntheticFinalistEngine unmapped_engine;
  const auto unmapped_analysis = cad::verify_bargaining_finalists(
      unmapped_engine, finalist_economy, finalist_result,
      unmapped_negotiation, unmapped_robustness, 3);
  CHECK(unmapped_analysis.finalists.size() == 1);
  CHECK(!unmapped_analysis.finalists.front().mapping_complete);
  CHECK(!unmapped_analysis.finalists.front().executed);
  CHECK(!unmapped_analysis.finalists.front().full_package_resimulated);
  CHECK(!unmapped_analysis.finalists.front().verified_win_win);
  CHECK(unmapped_analysis.finalists.front().unmapped_terms.size() == 1);
  CHECK(unmapped_analysis.finalists.front().unmapped_terms.front() == "procurement");
  CHECK(unmapped_engine.calls == 0);
  CHECK(!unmapped_negotiation.recommended.full_package_resimulated);
  CHECK(!unmapped_negotiation.recommended.verified_win_win);

  // Simulate a robust winner ranked beyond the first 100. The bounded
  // negotiation serializer must still carry the full package, and the top-level
  // publication remains an independent transport path for the same identity.
  auto large_negotiation = negotiation;
  large_negotiation.frontier.clear();
  for (std::size_t i = 0; i < 101; ++i) {
    auto package = negotiation.recommended;
    package.id = "synthetic-package-" + std::to_string(i + 1);
    package.pareto_rank = i + 1;
    large_negotiation.frontier.push_back(std::move(package));
  }
  large_negotiation.pareto_frontier_size = 101;
  cad::RobustRecommendationAnalysis large_robustness;
  large_robustness.recommended_package_id = large_negotiation.frontier.back().id;

  cad::ensure_robust_package_in_interactive_preview(
      large_negotiation, large_robustness.recommended_package_id);
  bool preview_found = false;
  for (std::size_t i = 0; i < 100; ++i)
    preview_found = preview_found
        || large_negotiation.frontier[i].id == large_robustness.recommended_package_id;
  CHECK(preview_found);
  CHECK(large_negotiation.frontier[99].id == "synthetic-package-101");
  CHECK(large_negotiation.frontier[99].pareto_rank == 101);
  const auto preview_json = cad::interactive_frontier::negotiation_to_json(large_negotiation);
  CHECK(preview_json.find("synthetic-package-101") != std::string::npos);

  const auto large_publication = cad::build_trade_diplomacy_publication(
      economy, result, large_negotiation, large_robustness);
  CHECK(large_publication.has_robust_recommended_package);
  CHECK(large_publication.robust_recommended_package.pareto_rank == 101);
  CHECK(!large_publication.offer_ready);
  const auto large_json = cad::attach_published_trade_diplomacy_json(
      "{\"previewLimit\":100}", large_publication);
  CHECK(large_json.find("synthetic-package-101") != std::string::npos);
  CHECK(large_json.find("\"offerReady\":false") != std::string::npos);
  CHECK(large_publication.platform.robust_packages.size() <= 10);

  std::cout << "trade diplomacy platform tests passed\n";
  return 0;
}
