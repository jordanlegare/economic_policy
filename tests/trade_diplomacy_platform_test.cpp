#include "calibration.hpp"
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
