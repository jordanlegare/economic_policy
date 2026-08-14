#include "server_session.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

cad::NegotiationAnalysis historical_offer_negotiation() {
  cad::NegotiationAnalysis negotiation;
  cad::NegotiationPackage package;
  package.id = "pkg-history-1";
  package.pareto_rank = 17;
  package.strategy_id = "strategy-history";
  package.strategy_name = "Historical strategy";
  package.canada_utility = 77.25;
  package.us_utility = 74.50;
  package.canada_surplus = 4.75;
  package.us_surplus = 3.25;
  package.macro_base_verified = true;
  package.sector_posture_verified = true;
  package.bargaining_terms_screened = true;
  package.full_package_resimulated = false;
  package.issues = {
      {"us-tariff-relief", "U.S. residual-tariff relief", 0.0, 25.0},
      {"procurement", "Reciprocal procurement access", 50.0, 50.0}
  };
  negotiation.recommended = package;
  negotiation.frontier.push_back(package);
  negotiation.pareto_frontier_size = 1;
  return negotiation;
}

cad::RobustRecommendationAnalysis historical_offer_robustness() {
  cad::RobustRecommendationAnalysis robustness;
  robustness.recommended_package_id = "pkg-history-1";
  cad::RobustPackageMetrics metrics;
  metrics.package_id = "pkg-history-1";
  metrics.strategy_id = "strategy-history";
  metrics.samples = 5000;
  metrics.joint_clear_probability = 0.91;
  metrics.canada_cvar10_surplus = 1.20;
  metrics.us_cvar10_surplus = 0.80;
  metrics.max_regret = 0.12;
  metrics.clears_probability_gate = true;
  robustness.packages.push_back(metrics);
  return robustness;
}

}  // namespace

int main() {
  using namespace cad;
  using namespace cad::server;

  assert(valid_session_id("abc-123_DEF.9"));
  assert(!valid_session_id(""));
  assert(!valid_session_id("../escape"));
  assert(!valid_session_id(std::string(65, 'a')));

  const auto root = std::filesystem::path("runtime") / "server-session-test";
  std::filesystem::remove_all(root);
  Economy baseline;
  baseline.policy_rate = 2.75;
  const auto update = request_json::parse_object(
      R"({"actor":"canada","operationId":"session-neg-op-1","retaliatoryTariff":12,"canadaPriority":60,"riskAversion":65,"cooperationCeiling":70,"canadaSector0":35})");
  assert(update.valid);
  const std::string historical_offer =
      R"({"schemaVersion":1,"action":"offer","operationId":"offer-snapshot-op-1","side":"canada","packageId":"pkg-history-1","note":"historical offer"})";

  {
    SessionStore store(root, baseline, 4);

    auto a = store.get("session-a");
    auto b = store.get("session-b");
    assert(a != b);
    assert(a->id == "session-a");
    assert(b->id == "session-b");
    assert(a->last_economy.policy_rate == 2.75);
    assert(b->last_economy.policy_rate == 2.75);
    assert(store.size() == 2);

    std::string error;
    {
      std::lock_guard<std::mutex> lock(a->mutex);
      assert(a->negotiation.update(update, error));
      assert(a->negotiation.revision() == 1);

      // Stateful evaluation must derive all negotiation-owned controls from the
      // durable negotiation state rather than a parallel request-body copy.
      Economy authoritative = baseline;
      authoritative.us_tariff_canada = 3.0;
      authoritative.canada_retaliatory_tariff = 1.0;
      authoritative.canada_priority = 1.0;
      authoritative.us_priority = 99.0;
      authoritative.risk_aversion = 1.0;
      authoritative.cooperation_ceiling = 99.0;
      authoritative.canada_sector_coverage.fill(0.0);
      authoritative.us_sector_coverage.fill(0.0);
      a->negotiation.apply_to(authoritative);
      assert(authoritative.us_tariff_canada == 50.0);
      assert(authoritative.canada_retaliatory_tariff == 12.0);
      assert(authoritative.canada_priority == 60.0);
      assert(authoritative.us_priority == 40.0);
      assert(authoritative.risk_aversion == 65.0);
      assert(authoritative.cooperation_ceiling == 70.0);
      assert(authoritative.canada_sector_coverage[0] == 35.0);
      assert(authoritative.canada_sector_coverage[1] == 100.0);
      assert(authoritative.us_sector_coverage[0] == 100.0);
      // Non-negotiation scenario state is not owned by NegotiationState.
      assert(authoritative.policy_rate == 2.75);
    }
    {
      std::lock_guard<std::mutex> lock(b->mutex);
      assert(b->negotiation.revision() == 0);
    }

    const std::string room_a = a->room_json();
    const std::string room_b = b->room_json();
    assert(room_a.find("\"sessionId\":\"session-a\"") != std::string::npos);
    assert(room_b.find("\"sessionId\":\"session-b\"") != std::string::npos);
    assert(room_a.find("local-room-1") == std::string::npos);

    // Re-fetching a live session must preserve state rather than constructing a
    // second room/negotiation object for the same id.
    auto again = store.get("session-a");
    assert(again == a);
    assert(again->capture_negotiation_revision() == 1);

    // New offers persist a self-contained economics snapshot before the room
    // event itself. The snapshot is keyed by the immutable package identity and
    // records the exact published evaluation/calibration context.
    auto offer_session = store.get("offer-a");
    const auto offer_negotiation = historical_offer_negotiation();
    const auto offer_robustness = historical_offer_robustness();
    assert(offer_session->publish_evaluation(
        0, baseline, offer_negotiation, offer_robustness,
        "fnv1a64:offer-evaluation", "calibration-2026-08-12"));
    {
      std::lock_guard<std::mutex> lock(offer_session->mutex);
      assert(offer_session->room.apply_event(
          historical_offer, &offer_session->last_bargaining,
          &offer_session->last_robustness));
      const std::string audited = offer_session->room_json();
      assert(audited.find("\"offerSnapshots\"") != std::string::npos);
      assert(audited.find("\"snapshotCount\":1") != std::string::npos);
      assert(audited.find("\"newOfferSnapshotContract\":\"self-contained\"")
          != std::string::npos);
      assert(audited.find("\"packageId\":\"pkg-history-1\"") != std::string::npos);
      assert(audited.find("\"paretoRankAtOffer\":17") != std::string::npos);
      assert(audited.find("\"evaluationFingerprint\":\"fnv1a64:offer-evaluation\"")
          != std::string::npos);
      assert(audited.find("\"calibrationSnapshotId\":\"calibration-2026-08-12\"")
          != std::string::npos);
      assert(audited.find("\"strategyId\":\"strategy-history\"") != std::string::npos);
      assert(audited.find("\"robustSamples\":5000") != std::string::npos);
      assert(audited.find("\"jointClearProbability\":0.910000") != std::string::npos);
      assert(audited.find("us-tariff-relief:0.000:25.000") != std::string::npos);
      assert(audited.find("\"fullPackageResimulated\":false") != std::string::npos);
    }
  }

  // Destroying the in-memory store and reopening the same runtime root must
  // reconstruct negotiation state, offer snapshots and operation-id dedupe state.
  {
    SessionStore restored_store(root, baseline, 4);
    auto restored_a = restored_store.get("session-a");
    auto restored_b = restored_store.get("session-b");
    std::string error;
    {
      std::lock_guard<std::mutex> lock(restored_a->mutex);
      assert(restored_a->negotiation.revision() == 1);
      assert(restored_a->negotiation.json().find("\"retaliatoryTariff\":12")
          != std::string::npos);
      assert(restored_a->negotiation.json().find("\"riskAversion\":65")
          != std::string::npos);
      assert(restored_a->negotiation.json().find("\"cooperationCeiling\":70")
          != std::string::npos);
      assert(restored_a->negotiation.update(update, error));
      assert(restored_a->negotiation.last_update_replayed());
      assert(restored_a->negotiation.revision() == 1);

      Economy restored_authoritative = baseline;
      restored_a->negotiation.apply_to(restored_authoritative);
      assert(restored_authoritative.canada_retaliatory_tariff == 12.0);
      assert(restored_authoritative.canada_priority == 60.0);
      assert(restored_authoritative.risk_aversion == 65.0);
      assert(restored_authoritative.cooperation_ceiling == 70.0);
      assert(restored_authoritative.canada_sector_coverage[0] == 35.0);
    }
    {
      std::lock_guard<std::mutex> lock(restored_b->mutex);
      assert(restored_b->negotiation.revision() == 0);
    }

    // A historical offer remains self-contained after restart even though no
    // current evaluation is resident. Replaying the same operation ID does not
    // create a second snapshot or rewrite the original economics.
    auto restored_offer = restored_store.get("offer-a");
    {
      std::lock_guard<std::mutex> lock(restored_offer->mutex);
      std::string audited = restored_offer->room_json();
      assert(audited.find("\"snapshotCount\":1") != std::string::npos);
      assert(audited.find("\"evaluationFingerprint\":\"fnv1a64:offer-evaluation\"")
          != std::string::npos);
      assert(audited.find("\"calibrationSnapshotId\":\"calibration-2026-08-12\"")
          != std::string::npos);

      const auto offer_negotiation = historical_offer_negotiation();
      const auto offer_robustness = historical_offer_robustness();
      assert(restored_offer->room.apply_event(
          historical_offer, &offer_negotiation, &offer_robustness));
      assert(restored_offer->room.last_apply_replayed());
      audited = restored_offer->room_json();
      assert(audited.find("\"snapshotCount\":1") != std::string::npos);
      assert(audited.find("\"evaluationFingerprint\":\"fnv1a64:offer-evaluation\"")
          != std::string::npos);
    }

    // A solve admitted against revision 1 must not publish over a delegation
    // change that reached revision 2 while the expensive computation was running.
    const unsigned long admitted_revision = restored_a->capture_negotiation_revision();
    assert(admitted_revision == 1);
    const auto changed = request_json::parse_object(
        R"({"actor":"canada","operationId":"session-neg-op-2","retaliatoryTariff":13,"canadaPriority":61})");
    assert(changed.valid);
    {
      std::lock_guard<std::mutex> lock(restored_a->mutex);
      error.clear();
      assert(restored_a->negotiation.update(changed, error));
      assert(restored_a->negotiation.revision() == 2);
    }

    Economy stale_economy = baseline;
    stale_economy.policy_rate = 9.0;
    assert(!restored_a->publish_evaluation(
        admitted_revision, stale_economy, NegotiationAnalysis{},
        RobustRecommendationAnalysis{}, "fnv1a64:stale"));
    assert(!restored_a->has_evaluation);
    assert(restored_a->last_economy.policy_rate == 2.75);

    const unsigned long current_revision = restored_a->capture_negotiation_revision();
    Economy current_economy = baseline;
    current_economy.policy_rate = 3.25;
    assert(restored_a->publish_evaluation(
        current_revision, current_economy, NegotiationAnalysis{},
        RobustRecommendationAnalysis{}, "fnv1a64:current"));
    assert(restored_a->has_evaluation);
    assert(restored_a->last_economy.policy_rate == 3.25);
    assert(restored_a->last_input_fingerprint == "fnv1a64:current");
    assert(restored_a->last_evaluation_negotiation_revision == 2);
  }

  std::filesystem::remove_all(root);
  std::cout << "server session restart, offer snapshot and stale-publication tests passed\n";
  return 0;
}
