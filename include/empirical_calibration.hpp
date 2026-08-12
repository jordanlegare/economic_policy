#pragma once

#include "calibration.hpp"
#include "structural_calibration.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct EmpiricalAnchor {
  std::string parameter;
  std::string evidence_class;
  std::string source_id;
  std::string vintage;
  std::string estimate_or_range;
  std::string mapping;
  std::string notes;
};

struct EmpiricalCalibrationRegistry {
  bool loaded = false;
  std::vector<EmpiricalAnchor> entries;

  const EmpiricalAnchor* find(const std::string& parameter) const {
    for (const auto& entry : entries) if (entry.parameter == parameter) return &entry;
    return nullptr;
  }
};

struct EmpiricalCalibrationAudit {
  int structural_parameters = 25;
  int mandate_or_derived = 2;
  int estimable_parameters = 23;
  int evidence_anchored = 0;
  int direct_anchors = 0;
  int indirect_anchors = 0;
  double evidence_anchor_rate = 0.0;
  double direct_anchor_rate = 0.0;
  bool exceeds_quarter_threshold = false;
};

inline EmpiricalCalibrationRegistry load_empirical_anchor_registry(const std::string& path) {
  EmpiricalCalibrationRegistry registry;
  std::ifstream in(path);
  if (!in) return registry;
  registry.loaded = true;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (header) { header = false; continue; }
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 7) continue;
    registry.entries.push_back({f[0], f[1], f[2], f[3], f[4], f[5], f[6]});
  }
  return registry;
}

inline EmpiricalCalibrationAudit audit_empirical_calibration(
    const StructuralParameterRegistry& structural,
    const EmpiricalCalibrationRegistry& empirical) {
  EmpiricalCalibrationAudit out;
  if (!structural_parameter_registry_complete(structural) || !empirical.loaded) return out;

  for (const auto& name : required_structural_parameter_names()) {
    const auto* parameter = structural.find(name);
    if (!parameter || parameter->kind == "mandate" || parameter->kind == "derived") continue;
    const auto* anchor = empirical.find(name);
    if (!anchor) continue;
    if (anchor->evidence_class == "direct") ++out.direct_anchors;
    else if (anchor->evidence_class == "indirect") ++out.indirect_anchors;
  }
  out.evidence_anchored = out.direct_anchors + out.indirect_anchors;
  out.evidence_anchor_rate = out.estimable_parameters > 0
      ? static_cast<double>(out.evidence_anchored) / static_cast<double>(out.estimable_parameters)
      : 0.0;
  out.direct_anchor_rate = out.estimable_parameters > 0
      ? static_cast<double>(out.direct_anchors) / static_cast<double>(out.estimable_parameters)
      : 0.0;
  out.exceeds_quarter_threshold = out.evidence_anchor_rate > 0.25;
  return out;
}

inline std::string empirical_calibration_audit_to_json(const EmpiricalCalibrationAudit& audit) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"structuralParameters\":" << audit.structural_parameters
      << ",\"mandateOrDerived\":" << audit.mandate_or_derived
      << ",\"estimableParameters\":" << audit.estimable_parameters
      << ",\"evidenceAnchored\":" << audit.evidence_anchored
      << ",\"directAnchors\":" << audit.direct_anchors
      << ",\"indirectAnchors\":" << audit.indirect_anchors
      << ",\"evidenceAnchorRate\":" << audit.evidence_anchor_rate
      << ",\"directAnchorRate\":" << audit.direct_anchor_rate
      << ",\"exceedsQuarterThreshold\":"
      << (audit.exceeds_quarter_threshold ? "true" : "false") << "}";
  return out.str();
}

}  // namespace cad
