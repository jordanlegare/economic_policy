#include "server_session.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

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
      R"({"actor":"canada","operationId":"session-neg-op-1","retaliatoryTariff":12,"canadaPriority":60})");
  assert(update.valid);

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
  }

  // Destroying the in-memory store and reopening the same runtime root must
  // reconstruct negotiation state and its operation-id dedupe ledger from disk.
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
      assert(restored_a->negotiation.update(update, error));
      assert(restored_a->negotiation.last_update_replayed());
      assert(restored_a->negotiation.revision() == 1);
    }
    {
      std::lock_guard<std::mutex> lock(restored_b->mutex);
      assert(restored_b->negotiation.revision() == 0);
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
  std::cout << "server session restart and stale-publication tests passed\n";
  return 0;
}
