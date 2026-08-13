#pragma once

#include "request_json.hpp"

#include <cmath>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace cad::room_action {

struct Event {
  bool valid = false;
  std::string error;
  int schema_version = 1;
  std::string action;

  std::optional<int> round;
  std::optional<std::string> phase;
  std::optional<std::string> issue_id;
  std::optional<double> max_canada_move;
  std::optional<double> min_us_move;
  std::optional<std::string> authority;
  std::optional<bool> hard_red_line;
  std::optional<std::string> side;
  std::optional<std::string> package_id;
  std::optional<double> magnitude;
  std::optional<double> estimated_own_cost;
  std::optional<double> estimated_counterpart_value;
  std::optional<bool> reciprocal;
  std::optional<bool> conditional;
  std::optional<std::string> note;
  std::optional<std::string> trigger;
  std::optional<std::string> response;
  std::optional<std::string> summary;
  std::optional<std::string> counterpart_signals;
  std::optional<std::string> unresolved;
  std::optional<std::string> next_actions;
};

inline std::string escape_json(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(hex[(c >> 4) & 0xf]);
          out.push_back(hex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

inline bool allowed_keys(const request_json::Object& object,
                         const std::set<std::string>& allowed,
                         std::string& error) {
  for (const auto& [key, value] : object.values) {
    (void)value;
    if (allowed.find(key) == allowed.end()) {
      error = "unknown room-action field: " + key;
      return false;
    }
  }
  return true;
}

inline bool optional_string(const request_json::Object& object,
                            const std::string& key, std::size_t max_size,
                            std::optional<std::string>& target,
                            std::string& error) {
  const auto* scalar = object.find(key);
  if (!scalar) return true;
  if (scalar->kind != request_json::Kind::string) {
    error = key + " must be a string";
    return false;
  }
  if (scalar->string_value.size() > max_size) {
    error = key + " is too long";
    return false;
  }
  target = scalar->string_value;
  return true;
}

inline bool required_string(const request_json::Object& object,
                            const std::string& key, std::size_t max_size,
                            std::optional<std::string>& target,
                            std::string& error) {
  if (!optional_string(object, key, max_size, target, error)) return false;
  if (!target || target->empty()) {
    error = key + " is required";
    return false;
  }
  return true;
}

inline bool optional_number(const request_json::Object& object,
                            const std::string& key, double lo, double hi,
                            std::optional<double>& target,
                            std::string& error) {
  const auto* scalar = object.find(key);
  if (!scalar) return true;
  if (scalar->kind != request_json::Kind::number) {
    error = key + " must be numeric";
    return false;
  }
  if (scalar->number_value < lo || scalar->number_value > hi) {
    error = key + " is outside its allowed range";
    return false;
  }
  target = scalar->number_value;
  return true;
}

inline bool optional_bool(const request_json::Object& object,
                          const std::string& key,
                          std::optional<bool>& target,
                          std::string& error) {
  const auto* scalar = object.find(key);
  if (!scalar) return true;
  if (scalar->kind != request_json::Kind::boolean) {
    error = key + " must be boolean";
    return false;
  }
  target = scalar->bool_value;
  return true;
}

inline bool schema_version(const request_json::Object& object, Event& event) {
  const auto* scalar = object.find("schemaVersion");
  if (!scalar) return true;  // Legacy valid logs are version 0-on-wire / V1 semantics.
  if (scalar->kind != request_json::Kind::number
      || scalar->number_value != 1.0) {
    event.error = "schemaVersion must be 1";
    return false;
  }
  event.schema_version = 1;
  return true;
}

inline Event parse(const request_json::Object& object) {
  Event out;
  if (!object.valid) {
    out.error = object.error.empty() ? "invalid room-action JSON" : object.error;
    return out;
  }
  if (!schema_version(object, out)) return out;
  const auto action = object.string("action");
  if (!action || action->empty()) {
    out.error = "action is required";
    return out;
  }
  out.action = *action;

  std::set<std::string> allowed{"schemaVersion", "action"};
  auto allow = [&](std::initializer_list<const char*> keys) {
    for (const char* key : keys) allowed.emplace(key);
  };

  if (out.action == "set-round") {
    allow({"round", "phase"});
    const auto* round = object.find("round");
    if (round) {
      if (round->kind != request_json::Kind::number
          || round->number_value < 1.0 || round->number_value > 1000.0
          || std::floor(round->number_value) != round->number_value) {
        out.error = "round must be an integer from 1 to 1000";
        return out;
      }
      out.round = static_cast<int>(round->number_value);
    }
    if (!optional_string(object, "phase", 128, out.phase, out.error)) return out;
    if (!out.round && !out.phase) {
      out.error = "set-round requires round or phase";
      return out;
    }
  } else if (out.action == "set-mandate" || out.action == "red-line") {
    allow({"issueId", "maxCanadaMove", "minUsMove", "authority", "hardRedLine", "note"});
    if (!required_string(object, "issueId", 128, out.issue_id, out.error)) return out;
    if (!optional_number(object, "maxCanadaMove", 0.0, 100.0, out.max_canada_move, out.error)
        || !optional_number(object, "minUsMove", 0.0, 100.0, out.min_us_move, out.error)
        || !optional_string(object, "authority", 128, out.authority, out.error)
        || !optional_bool(object, "hardRedLine", out.hard_red_line, out.error)
        || !optional_string(object, "note", 8192, out.note, out.error)) return out;
  } else if (out.action == "offer") {
    allow({"side", "packageId", "note"});
    if (!required_string(object, "packageId", 256, out.package_id, out.error)
        || !optional_string(object, "side", 16, out.side, out.error)
        || !optional_string(object, "note", 8192, out.note, out.error)) return out;
    if (out.side && *out.side != "canada" && *out.side != "us") {
      out.error = "side must be canada or us";
      return out;
    }
  } else if (out.action == "concession") {
    allow({"side", "issueId", "magnitude", "estimatedOwnCost",
           "estimatedCounterpartValue", "reciprocal", "conditional", "note"});
    if (!required_string(object, "issueId", 128, out.issue_id, out.error)
        || !optional_string(object, "side", 16, out.side, out.error)
        || !optional_number(object, "magnitude", 0.0, 1000000.0, out.magnitude, out.error)
        || !optional_number(object, "estimatedOwnCost", 0.0, 1000000.0, out.estimated_own_cost, out.error)
        || !optional_number(object, "estimatedCounterpartValue", 0.0, 1000000.0, out.estimated_counterpart_value, out.error)
        || !optional_bool(object, "reciprocal", out.reciprocal, out.error)
        || !optional_bool(object, "conditional", out.conditional, out.error)
        || !optional_string(object, "note", 8192, out.note, out.error)) return out;
    if (out.side && *out.side != "canada" && *out.side != "us") {
      out.error = "side must be canada or us";
      return out;
    }
  } else if (out.action == "playbook") {
    allow({"issueId", "trigger", "response", "authority"});
    if (!required_string(object, "issueId", 128, out.issue_id, out.error)
        || !required_string(object, "trigger", 8192, out.trigger, out.error)
        || !required_string(object, "response", 8192, out.response, out.error)
        || !optional_string(object, "authority", 128, out.authority, out.error)) return out;
  } else if (out.action == "debrief") {
    allow({"summary", "counterpartSignals", "unresolved", "nextActions"});
    if (!required_string(object, "summary", 8192, out.summary, out.error)
        || !optional_string(object, "counterpartSignals", 8192, out.counterpart_signals, out.error)
        || !optional_string(object, "unresolved", 8192, out.unresolved, out.error)
        || !optional_string(object, "nextActions", 8192, out.next_actions, out.error)) return out;
  } else {
    out.error = "unsupported room action";
    return out;
  }

  if (!allowed_keys(object, allowed, out.error)) return out;
  out.valid = true;
  return out;
}

inline Event parse(std::string_view body) {
  return parse(request_json::parse_object(body));
}

inline std::string to_json(const Event& event) {
  std::ostringstream out;
  out << std::setprecision(15) << "{\"schemaVersion\":1,\"action\":\""
      << escape_json(event.action) << '"';
  auto string_field = [&](const char* key, const std::optional<std::string>& value) {
    if (value) out << ",\"" << key << "\":\"" << escape_json(*value) << '"';
  };
  auto number_field = [&](const char* key, const std::optional<double>& value) {
    if (value) out << ",\"" << key << "\":" << *value;
  };
  auto bool_field = [&](const char* key, const std::optional<bool>& value) {
    if (value) out << ",\"" << key << "\":" << (*value ? "true" : "false");
  };
  if (event.round) out << ",\"round\":" << *event.round;
  string_field("phase", event.phase);
  string_field("issueId", event.issue_id);
  number_field("maxCanadaMove", event.max_canada_move);
  number_field("minUsMove", event.min_us_move);
  string_field("authority", event.authority);
  bool_field("hardRedLine", event.hard_red_line);
  string_field("side", event.side);
  string_field("packageId", event.package_id);
  number_field("magnitude", event.magnitude);
  number_field("estimatedOwnCost", event.estimated_own_cost);
  number_field("estimatedCounterpartValue", event.estimated_counterpart_value);
  bool_field("reciprocal", event.reciprocal);
  bool_field("conditional", event.conditional);
  string_field("note", event.note);
  string_field("trigger", event.trigger);
  string_field("response", event.response);
  string_field("summary", event.summary);
  string_field("counterpartSignals", event.counterpart_signals);
  string_field("unresolved", event.unresolved);
  string_field("nextActions", event.next_actions);
  out << '}';
  return out.str();
}

}  // namespace cad::room_action
