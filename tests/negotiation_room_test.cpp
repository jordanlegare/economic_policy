#include "calibration.hpp"
#include "negotiation_room.hpp"
#include "negotiation_support.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
  const std::string path = "negotiation-room-test.events";
  std::remove(path.c_str());

  cad::Economy economy;
  const auto calibration = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  economy = cad::apply_calibration(economy, calibration);
  cad::PolicyEngine engine(20260810);
  const auto result = engine.evaluate(economy);
  const auto negotiation = cad::analyze_negotiation(economy, result);
  const auto robust = cad::analyze_robust_recommendations(
      economy, result, negotiation, calibration, 400, 98765);
  assert(!negotiation.frontier.empty());
  assert(!robust.recommended_package_id.empty());

  {
    cad::NegotiationRoom room(path);
    assert(room.apply_event("{\"action\":\"set-round\",\"round\":3,\"phase\":\"package round\"}"));
    assert(room.apply_event("{\"action\":\"set-mandate\",\"issueId\":\"procurement\",\"maxCanadaMove\":60,\"minUsMove\":40,\"authority\":\"senior_approval_required\",\"note\":\"No unilateral procurement opening\"}"));
    assert(room.apply_event("{\"action\":\"red-line\",\"issueId\":\"canada-tariff-relief\",\"maxCanadaMove\":75,\"minUsMove\":0,\"authority\":\"ministerial\",\"hardRedLine\":true,\"note\":\"Do not exceed without new mandate\"}"));
    assert(room.apply_event("{\"action\":\"concession\",\"side\":\"canada\",\"issueId\":\"border-facilitation\",\"magnitude\":20,\"estimatedOwnCost\":1.5,\"estimatedCounterpartValue\":2.0,\"reciprocal\":false,\"conditional\":true,\"note\":\"Conditional tranche\"}"));
    assert(room.apply_event("{\"action\":\"playbook\",\"issueId\":\"procurement\",\"trigger\":\"U.S. asks for full opening\",\"response\":\"Offer reciprocal 40% tranche with review\",\"authority\":\"senior_approval_required\"}"));
    assert(room.apply_event("{\"action\":\"debrief\",\"summary\":\"Counterpart focused on procurement and autos\",\"counterpartSignals\":\"Flexible on sequencing\",\"unresolved\":\"Autos origin verification\",\"nextActions\":\"Prepare conditional bridge\"}"));
    assert(room.apply_event(std::string("{\"action\":\"offer\",\"side\":\"canada\",\"packageId\":\"")
        + robust.recommended_package_id + "\",\"note\":\"Opening package\"}", &negotiation, &robust));

    const auto suggestions = room.counteroffers(&negotiation, &robust);
    assert(!suggestions.empty());
    for (const auto& suggestion : suggestions) {
      assert(!suggestion.package_id.empty());
      assert(suggestion.joint_clear_probability >= 0.0 && suggestion.joint_clear_probability <= 1.0);
      assert(suggestion.authority_status != "blocked-by-red-line");
    }

    const auto json = room.json(&negotiation, &robust);
    assert(json.find("\"round\":3") != std::string::npos);
    assert(json.find("package round") != std::string::npos);
    assert(json.find("senior_approval_required") != std::string::npos);
    assert(json.find("\"hardRedLine\":true") != std::string::npos);
    assert(json.find("Opening package") != std::string::npos);
    assert(json.find("Conditional tranche") != std::string::npos);
    assert(json.find("Counterpart focused on procurement") != std::string::npos);
    assert(json.find("secureForProtectedInformation\":false") != std::string::npos);
  }

  {
    cad::NegotiationRoom restored(path);
    const auto json = restored.json(&negotiation, &robust);
    assert(json.find("\"round\":3") != std::string::npos);
    assert(json.find("package round") != std::string::npos);
    assert(json.find("Opening package") != std::string::npos);
    assert(json.find("Conditional tranche") != std::string::npos);
    assert(json.find("U.S. asks for full opening") != std::string::npos);
    assert(json.find("Counterpart focused on procurement") != std::string::npos);
  }

  std::remove(path.c_str());
  return 0;
}
