#pragma once

#include "negotiation_room.hpp"
#include "policy_engine.hpp"
#include "robust_recommendation.hpp"
#include "server_contracts.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

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
        room(event_log_path.string()),
        last_economy(std::move(baseline)) {}

  std::string room_json() const {
    return expose_room_session_id(
        room.json(has_evaluation ? &last_bargaining : nullptr,
                  has_evaluation ? &last_robustness : nullptr), id);
  }

  std::string id;
  // Stateful evaluations do not overlap within one session. Lock acquisition
  // defines their serialization order; other sessions use distinct operation
  // mutexes and remain concurrent.
  mutable std::mutex operation_mutex;
  // Short critical sections protect published state and negotiation/room
  // mutations. Expensive model computation does not hold this mutex.
  mutable std::mutex mutex;
  NegotiationState negotiation;
  NegotiationRoom room;
  NegotiationAnalysis last_bargaining;
  RobustRecommendationAnalysis last_robustness;
  Economy last_economy;
  bool has_evaluation = false;
};

class SessionStore {
 public:
  SessionStore(std::filesystem::path runtime_root, Economy baseline,
               std::size_t max_sessions = 128)
      : sessions_root_(std::move(runtime_root) / "sessions"),
        baseline_(std::move(baseline)),
        max_sessions_(std::max<std::size_t>(1, max_sessions)) {}

  std::shared_ptr<SessionState> get(const std::string& raw_id) {
    const std::string id = session_id_or_default(raw_id);
    if (!valid_session_id(id)) throw std::invalid_argument("invalid session id");

    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
      it->second.last_touch = now;
      return it->second.state;
    }

    prune_locked();
    const auto path = sessions_root_ / id / "negotiation-room.events";
    Entry entry;
    entry.state = std::make_shared<SessionState>(id, path, baseline_);
    entry.last_touch = now;
    auto inserted = sessions_.emplace(id, std::move(entry));
    return inserted.first->second.state;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
  }

 private:
  using Clock = std::chrono::steady_clock;
  struct Entry {
    std::shared_ptr<SessionState> state;
    Clock::time_point last_touch{};
  };

  void prune_locked() {
    while (sessions_.size() >= max_sessions_) {
      auto victim = sessions_.end();
      for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        // A state that is currently being used by a worker keeps its map-owned
        // reference plus at least one external reference. Do not evict it and
        // create two live states for the same session id.
        if (it->second.state.use_count() != 1) continue;
        if (victim == sessions_.end()
            || it->second.last_touch < victim->second.last_touch) victim = it;
      }
      if (victim == sessions_.end()) return;
      sessions_.erase(victim);
    }
  }

  std::filesystem::path sessions_root_;
  Economy baseline_;
  std::size_t max_sessions_ = 128;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> sessions_;
};

}  // namespace cad::server
