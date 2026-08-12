#pragma once

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct StateMeasurementDefinition {
  std::string name;
  std::string concept;
  std::string unit;
  std::string preferred_source_id;
  std::string transformation;
  std::string status;
  std::string public_reproducibility;
  std::string notes;
};

struct StateMeasurementRegistry {
  bool loaded = false;
  std::vector<StateMeasurementDefinition> entries;

  const StateMeasurementDefinition* find(const std::string& name) const {
    for (const auto& entry : entries) if (entry.name == name) return &entry;
    return nullptr;
  }
};

namespace state_measurement_detail {

inline std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

inline std::vector<std::string> csv_fields(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
        field.push_back('"');
        ++i;
      } else {
        quoted = !quoted;
      }
    } else if (c == ',' && !quoted) {
      fields.push_back(trim(field));
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  fields.push_back(trim(field));
  return fields;
}

inline bool unresolved(const StateMeasurementDefinition& entry) {
  return entry.status != "ready";
}

}  // namespace state_measurement_detail

inline StateMeasurementRegistry load_state_measurement_registry(const std::string& path) {
  StateMeasurementRegistry registry;
  std::ifstream in(path);
  if (!in) return registry;
  registry.loaded = true;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (header) { header = false; continue; }
    const auto f = state_measurement_detail::csv_fields(line);
    if (f.size() < 8) continue;
    registry.entries.push_back({f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]});
  }
  return registry;
}

inline std::vector<std::string> unresolved_state_measurements(
    const StateMeasurementRegistry& registry) {
  std::vector<std::string> out;
  for (const auto& entry : registry.entries)
    if (state_measurement_detail::unresolved(entry)) out.push_back(entry.name);
  return out;
}

inline bool state_measurement_contract_complete(const StateMeasurementRegistry& registry) {
  if (!registry.loaded) return false;
  for (const auto* required : {"credit_spread", "housing_gap"}) {
    const auto* entry = registry.find(required);
    if (!entry || entry->concept.empty() || entry->unit.empty()
        || entry->preferred_source_id.empty() || entry->transformation.empty()
        || entry->status.empty() || entry->public_reproducibility.empty()) return false;
  }
  return true;
}

inline int ready_state_measurement_count(const StateMeasurementRegistry& registry) {
  return static_cast<int>(std::count_if(registry.entries.begin(), registry.entries.end(),
      [](const StateMeasurementDefinition& entry) { return entry.status == "ready"; }));
}

inline std::string state_measurement_registry_to_json(const StateMeasurementRegistry& registry) {
  auto esc = [](const std::string& value) {
    std::string out;
    for (char c : value) {
      if (c == '"' || c == '\\') out.push_back('\\');
      out.push_back(c);
    }
    return out;
  };
  std::ostringstream o;
  o << "{\"loaded\":" << (registry.loaded ? "true" : "false")
    << ",\"contractComplete\":"
    << (state_measurement_contract_complete(registry) ? "true" : "false")
    << ",\"readyCount\":" << ready_state_measurement_count(registry)
    << ",\"unresolvedCount\":" << unresolved_state_measurements(registry).size()
    << ",\"entries\":[";
  for (std::size_t i = 0; i < registry.entries.size(); ++i) {
    if (i) o << ',';
    const auto& e = registry.entries[i];
    o << "{\"name\":\"" << esc(e.name) << "\",\"concept\":\"" << esc(e.concept)
      << "\",\"unit\":\"" << esc(e.unit) << "\",\"preferredSourceId\":\""
      << esc(e.preferred_source_id) << "\",\"transformation\":\""
      << esc(e.transformation) << "\",\"status\":\"" << esc(e.status)
      << "\",\"publicReproducibility\":\"" << esc(e.public_reproducibility)
      << "\",\"notes\":\"" << esc(e.notes) << "\"}";
  }
  o << "]}";
  return o.str();
}

}  // namespace cad
