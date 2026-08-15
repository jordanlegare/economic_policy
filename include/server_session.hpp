#pragma once

#include "audited_negotiation_room.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"
#include "server_contracts.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace cad::server {

inline bool valid_session_id(const std::string& value) {
  if (value.empty() || value.size() > 64) return false;
  for (const unsigned char c : value) {
    if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

inline std::string session_id_or_default(const std::string& value) {
  return value.empty() ? "default" : value;
}

inline std::string expose_room_session_id(std::string json,
                                          const std::string& session_id) {
  const std::string old = "\"sessionId\":\"local-room-1\"";
  const std::string replacement = "\"sessionId\":\""
      + json_escape(session_id) + "\"";
  const auto pos = json.find(old);
  if (pos != std::string::npos) json.replace(pos, old.size(), replacement);
  return json;
}

struct SessionState {
  SessionState(std::string session_id, std::filesystem::path event_log_path,
               Economy baseline)
      : id(std::move(session_id)),
        negotiation((event_log_path.parent_path() / "negotiation.events").string()),
        room(event_log_path.string()),
        evaluation_submission_log(
            (event_log_path.parent_path() / "evaluation-submissions.events").string()),
        last_economy(std::move(baseline)) {}

  std::string room_json() const {
    return expose_room_session_id(
        room.json(has_evaluation ? &last_bargaining : nullptr,
                  has_evaluation ? &last_robustness : nullptr), id);
  }

  unsigned long capture_negotiation_revision() const {
    std::lock_guard<std::mutex> lock(mutex);
    return negotiation.revision();
  }

  bool publish_evaluation(unsigned long expected_negotiation_revision,
                          Economy economy,
                          NegotiationAnalysis bargaining,
                          RobustRecommendationAnalysis robustness,
                          std::string input_fingerprint,
                          std::string calibration_snapshot_id = {}) {
    std::lock_guard<std::mutex> lock(mutex);
    if (negotiation.revision() != expected_negotiation_revision) return false;
    last_economy = std::move(economy);
    last_bargaining = std::move(bargaining);
    last_robustness = std::move(robustness);
    last_input_fingerprint = std::move(input_fingerprint);
    last_calibration_snapshot_id = std::move(calibration_snapshot_id);
    last_evaluation_negotiation_revision = expected_negotiation_revision;
    room.set_evaluation_context(last_input_fingerprint, last_calibration_snapshot_id);
    has_evaluation = true;
    return true;
  }

  bool publish_evaluation_with_calibration(
      unsigned long expected_negotiation_revision,
      Economy economy,
      NegotiationAnalysis bargaining,
      RobustRecommendationAnalysis robustness,
      std::string input_fingerprint,
      std::string calibration_snapshot_id) {
    return publish_evaluation(expected_negotiation_revision,
        std::move(economy), std::move(bargaining), std::move(robustness),
        std::move(input_fingerprint), std::move(calibration_snapshot_id));
  }

  std::string id;
  // Stateful evaluations do not overlap within one session. A timed mutex lets
  // the HTTP boundary reject a duplicate run instead of occupying a worker
  // indefinitely behind an already-running evaluation for the same session.
  mutable std::timed_mutex operation_mutex;
  // Short critical sections protect published state and negotiation/room
  // mutations. Expensive model computation does not hold this mutex.
  mutable std::mutex mutex;
  NegotiationState negotiation;
  AuditedNegotiationRoom room;
  std::string evaluation_submission_log;
  NegotiationAnalysis last_bargaining;
  RobustRecommendationAnalysis last_robustness;
  Economy last_economy;
  std::string last_input_fingerprint;
  std::string last_calibration_snapshot_id;
  unsigned long last_evaluation_negotiation_revision = 0;
  bool has_evaluation = false;
};

inline std::size_t session_cache_capacity_from_environment() {
  constexpr std::size_t fallback = 128;
  constexpr std::size_t minimum = 16;
  constexpr std::size_t maximum = 65536;
  const char* raw = std::getenv("CAD_SESSION_CACHE_SIZE");
  if (!raw || !*raw) return fallback;

  try {
    const std::string text(raw);
    std::size_t used = 0;
    const unsigned long long parsed = std::stoull(text, &used, 10);
    if (used != text.size()) return fallback;
    const unsigned long long bounded = std::min<unsigned long long>(parsed, maximum);
    return std::max<std::size_t>(minimum, static_cast<std::size_t>(bounded));
  } catch (...) {
    return fallback;
  }
}

class SessionStore {
 public:
  SessionStore(std::filesystem::path runtime_root, Economy baseline,
               std::size_t max_sessions = 0)
      : sessions_root_(std::move(runtime_root) / "sessions"),
        baseline_(std::move(baseline)),
        max_sessions_(max_sessions == 0
            ? session_cache_capacity_from_environment()
            : std::max<std::size_t>(1, max_sessions)) {}

  std::shared_ptr<SessionState> get(const std::string& raw_id) {
    const std::string id = session_id_or_default(raw_id);
    if (!valid_session_id(id)) throw std::invalid_argument("invalid session id");

    Shard& shard = shard_for(id);
    std::lock_guard<std::mutex> lock(shard.mutex);
    const auto now = Clock::now();
    auto it = shard.sessions.find(id);
    if (it != shard.sessions.end()) {
      it->second.last_touch = now;
      return it->second.state;
    }

    if (session_count_.load(std::memory_order_relaxed) >= max_sessions_)
      prune_locked(shard);

    const auto path = sessions_root_ / id / "negotiation-room.events";
    Entry entry;
    entry.state = std::make_shared<SessionState>(id, path, baseline_);
    entry.last_touch = now;
    auto inserted = shard.sessions.emplace(id, std::move(entry));
    if (inserted.second)
      session_count_.fetch_add(1, std::memory_order_relaxed);
    return inserted.first->second.state;
  }

  std::size_t size() const {
    return session_count_.load(std::memory_order_relaxed);
  }

  std::size_t capacity() const { return max_sessions_; }

 private:
  using Clock = std::chrono::steady_clock;
  struct Entry {
    std::shared_ptr<SessionState> state;
    Clock::time_point last_touch{};
  };

  struct Shard {
    mutable std::mutex mutex;
    std::unordered_map<std::string, Entry> sessions;
  };

  static constexpr std::size_t shard_count = 64;

  Shard& shard_for(const std::string& id) {
    return shards_[std::hash<std::string>{}(id) % shard_count];
  }

  void prune_locked(Shard& shard) {
    while (session_count_.load(std::memory_order_relaxed) >= max_sessions_) {
      auto victim = shard.sessions.end();
      for (auto it = shard.sessions.begin(); it != shard.sessions.end(); ++it) {
        // A state that is currently being used by a worker keeps its map-owned
        // reference plus at least one external reference. Do not evict it and
        // create two live states for the same session id.
        if (it->second.state.use_count() != 1) continue;
        if (victim == shard.sessions.end()
            || it->second.last_touch < victim->second.last_touch) victim = it;
      }
      if (victim == shard.sessions.end()) return;
      shard.sessions.erase(victim);
      session_count_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  std::filesystem::path sessions_root_;
  Economy baseline_;
  std::size_t max_sessions_ = 128;
  std::array<Shard, shard_count> shards_{};
  std::atomic<std::size_t> session_count_{0};
};

}  // namespace cad::server
