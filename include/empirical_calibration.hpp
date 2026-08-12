#pragma once

#include "calibration.hpp"
#include "structural_calibration.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct EmpiricalStructuralEvidence {
  std::string parameter;
  std::string evidence_tier;
  std::string mapping_status;
  double anchor_value = 0.0;
  std::string source_id;
  std::string sample_period;
  std::string method;
  std::string notes;
};

struct EmpiricalStructuralEvidenceRegistry {
  bool loaded = false;
  std::string registry_id = "none";
  std::string as_of;
  std::vector<EmpiricalStructuralEvidence> entries;

  const EmpiricalStructuralEvidence* find(const std::string& name) const {
    for (const auto& entry : entries) if (entry.parameter == name) return &entry;
    return nullptr;
  }
};

struct EmpiricalStructuralAudit {
  int parameter_count = 0;
  int statistically_anchored_count = 0;
  int direct_mapping_count = 0;
  int reference_only_count = 0;
  double statistically_anchored_coverage = 0.0;
  double direct_mapping_coverage = 0.0;
};

inline EmpiricalStructuralEvidenceRegistry load_empirical_structural_evidence(
    const std::string& path) {
  EmpiricalStructuralEvidenceRegistry registry;
  std::ifstream in(path);
  if (!in) return registry;
  registry.loaded = true;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.empty()) continue;
    if (f[0] == "META" && f.size() >= 3) {
      if (f[1] == "registry_id") registry.registry_id = f[2];
      else if (f[1] == "as_of") registry.as_of = f[2];
    } else if (f[0] == "EVIDENCE" && f.size() >= 9) {
      registry.entries.push_back({
          f[1], f[2], f[3], calibration_detail::number(f[4]),
          f[5], f[6], f[7], f[8]});
    }
  }
  return registry;
}

inline bool statistical_evidence_tier(const std::string& tier) {
  return tier == "official-model-estimate" || tier == "empirical-estimate";
}

inline EmpiricalStructuralAudit audit_empirical_structural_evidence(
    const EmpiricalStructuralEvidenceRegistry& registry) {
  EmpiricalStructuralAudit out;
  const auto required = required_structural_parameter_names();
  out.parameter_count = static_cast<int>(required.size());
  for (const auto& name : required) {
    const auto* evidence = registry.find(name);
    if (!evidence || !statistical_evidence_tier(evidence->evidence_tier)) continue;
    ++out.statistically_anchored_count;
    if (evidence->mapping_status == "direct") ++out.direct_mapping_count;
    else if (evidence->mapping_status == "reference-only") ++out.reference_only_count;
  }
  if (out.parameter_count > 0) {
    out.statistically_anchored_coverage = 100.0 * out.statistically_anchored_count
        / static_cast<double>(out.parameter_count);
    out.direct_mapping_coverage = 100.0 * out.direct_mapping_count
        / static_cast<double>(out.parameter_count);
  }
  return out;
}

inline bool empirical_structural_evidence_complete(
    const EmpiricalStructuralEvidenceRegistry& registry) {
  if (!registry.loaded || registry.registry_id == "none" || registry.as_of.empty()) return false;
  for (const auto& entry : registry.entries) {
    if (entry.parameter.empty() || entry.evidence_tier.empty()
        || entry.mapping_status.empty() || entry.source_id.empty()
        || entry.sample_period.empty() || entry.method.empty()) return false;
  }
  return true;
}

inline std::string empirical_structural_audit_to_json(const EmpiricalStructuralAudit& audit) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"parameterCount\":" << audit.parameter_count
      << ",\"statisticallyAnchoredCount\":" << audit.statistically_anchored_count
      << ",\"directMappingCount\":" << audit.direct_mapping_count
      << ",\"referenceOnlyCount\":" << audit.reference_only_count
      << ",\"statisticallyAnchoredCoverage\":" << audit.statistically_anchored_coverage
      << ",\"directMappingCoverage\":" << audit.direct_mapping_coverage << "}";
  return out.str();
}

}  // namespace cad
