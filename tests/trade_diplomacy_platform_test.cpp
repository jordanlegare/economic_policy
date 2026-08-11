#include "policy_engine.hpp"
#include "negotiation_support.hpp"
#include "trade_diplomacy_platform.hpp"

#include <cmath>
#include <iostream>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ':' << __LINE__ << '\n'; return 1; } } while (0)

int main() {
  cad::Economy economy;
  cad::PolicyEngine engine;
  const auto result = engine.evaluate(economy);
  const auto negotiation = cad::analyze_negotiation(economy, result);
  const auto platform = cad::build_trade_diplomacy_platform(economy, result, negotiation);

  CHECK(!platform.recommended_robust_package_id.empty());
  CHECK(platform.robust_cases == 6);
  CHECK(platform.robustness_cases.size() == 6);
  CHECK(!platform.robust_packages.empty());
  CHECK(platform.robust_packages.front().package_id == platform.recommended_robust_package_id);
  CHECK(std::isfinite(platform.recommended_worst_case_surplus));
  CHECK(platform.operational_readiness >= 0.0 && platform.operational_readiness <= 100.0);

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
  const auto json = cad::attach_trade_diplomacy_json(base, platform);
  CHECK(json.find("\"tradeDiplomacy\"") != std::string::npos);
  CHECK(json.find("\"issueTracks\"") != std::string::npos);
  CHECK(json.find("\"robustnessCases\"") != std::string::npos);
  CHECK(json.find("\"stakeholderGates\"") != std::string::npos);
  CHECK(json.find("\"evidenceLedger\"") != std::string::npos);

  // The operational layer must never pretend to estimate political acceptance.
  CHECK(json.find("acceptanceProbability") == std::string::npos);

  std::cout << "trade diplomacy platform tests passed\n";
  return 0;
}
