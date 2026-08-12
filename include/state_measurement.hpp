#pragma once

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct StateMeasurementDefinition {
  std::string name;
  std::string economic_concept;
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

  StateMeasurementDefinition* find(const std::string& name) {
    for (auto& entry : entries) if (entry.name == name) return &entry;
    return nullptr;
  }

  const StateMeasurementDefinition* find(const std::string& name) const {
    for (const auto& entry : entries) if (entry.name == name) return &entry;
    return nullptr;
  }
};

struct StateMeasurementAudit {
  int modeled_empirical_fields = 18;
  int resolved_fields = 0;
  int unresolved_fields = 0;
  double empirical_coverage = 0.0;
  std::string grade = "unusable";
  std::vector<std::string> unresolved_names;
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

inline bool contains(const std::vector<std::string>& names, const std::string& value) {
  return std::find(names.begin(), names.end(), value) != names.end();
}

inline std::string esc(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
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
    if (!entry || entry->economic_concept.empty() || entry->unit.empty()
        || entry->preferred_source_id.empty() || entry->transformation.empty()
        || entry->status.empty() || entry->public_reproducibility.empty()) return false;
  }
  return true;
}

inline int ready_state_measurement_count(const StateMeasurementRegistry& registry) {
  return static_cast<int>(std::count_if(registry.entries.begin(), registry.entries.end(),
      [](const StateMeasurementDefinition& entry) { return entry.status == "ready"; }));
}

// The historical suite already reconstructs 16 declared fields. This audit
// adds the two model-specific financial/housing states without redefining the
// backward-compatible 16-field contract. A registry entry counts as resolved
// only when its measurement status is `ready` and the fixture supplies it.
inline StateMeasurementAudit audit_modeled_empirical_state(
    const StateMeasurementRegistry& registry,
    const std::vector<std::string>& fixture_input_names,
    int resolved_declared_fields = 16) {
  StateMeasurementAudit out;
  out.resolved_fields = std::max(0, std::min(resolved_declared_fields,
      out.modeled_empirical_fields));

  for (const auto* required : {"credit_spread", "housing_gap"}) {
    const auto* entry = registry.find(required);
    const bool ready = entry && entry->status == "ready"
        && state_measurement_detail::contains(fixture_input_names, required);
    if (ready) ++out.resolved_fields;
    else out.unresolved_names.push_back(required);
  }

  out.unresolved_fields = out.modeled_empirical_fields - out.resolved_fields;
  out.empirical_coverage = 100.0 * static_cast<double>(out.resolved_fields)
      / static_cast<double>(out.modeled_empirical_fields);
  if (!state_measurement_contract_complete(registry)) out.grade = "measurement-contract-incomplete";
  else if (out.unresolved_fields == 0) out.grade = "empirically-complete";
  else out.grade = "measurement-partial";
  return out;
}

inline std::string state_measurement_audit_to_json(const StateMeasurementAudit& audit) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"modeledEmpiricalFields\":" << audit.modeled_empirical_fields
    << ",\"resolvedFields\":" << audit.resolved_fields
    << ",\"unresolvedFields\":" << audit.unresolved_fields
    << ",\"empiricalCoverage\":" << audit.empirical_coverage
    << ",\"grade\":\"" << state_measurement_detail::esc(audit.grade)
    << "\",\"unresolvedNames\":[";
  for (std::size_t i = 0; i < audit.unresolved_names.size(); ++i) {
    if (i) o << ',';
    o << '"' << state_measurement_detail::esc(audit.unresolved_names[i]) << '"';
  }
  o << "]}";
  return o.str();
}

inline std::string state_measurement_registry_to_json(const StateMeasurementRegistry& registry) {
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
    o << "{\"name\":\"" << state_measurement_detail::esc(e.name)
      << "\",\"concept\":\"" << state_measurement_detail::esc(e.economic_concept)
      << "\",\"unit\":\"" << state_measurement_detail::esc(e.unit)
      << "\",\"preferredSourceId\":\""
      << state_measurement_detail::esc(e.preferred_source_id)
      << "\",\"transformation\":\"" << state_measurement_detail::esc(e.transformation)
      << "\",\"status\":\"" << state_measurement_detail::esc(e.status)
      << "\",\"publicReproducibility\":\""
      << state_measurement_detail::esc(e.public_reproducibility)
      << "\",\"notes\":\"" << state_measurement_detail::esc(e.notes) << "\"}";
  }
  o << "]}";
  return o.str();
}

}  // namespace cad
