#include "empirical_calibration.hpp"

#include <cassert>
#include <cmath>
#include <string>

int main() {
  const auto structural = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  const auto empirical = cad::load_empirical_anchor_registry(
      "data/calibration/empirical_anchor_registry.csv");

  assert(structural.loaded);
  assert(cad::structural_parameter_registry_complete(structural));
  assert(empirical.loaded);
  assert(empirical.entries.size() == 7);

  const auto audit = cad::audit_empirical_calibration(structural, empirical);
  assert(audit.structural_parameters == 25);
  assert(audit.estimable_parameters == 23);
  assert(audit.evidence_anchored == 7);
  assert(audit.direct_anchors == 1);
  assert(audit.indirect_anchors == 6);
  assert(std::abs(audit.evidence_anchor_rate - 7.0 / 23.0) < 1e-12);
  assert(audit.evidence_anchor_rate > 0.25);
  assert(audit.exceeds_quarter_threshold);

  const auto* neutral = structural.find("neutral_rate");
  assert(neutral);
  assert(neutral->kind == "official_estimate");
  assert(neutral->source_id == "boc_neutral_rate_2026");
  assert(std::abs(neutral->baseline - 2.75) < 1e-12);
  assert(std::abs(neutral->lower_bound - 2.25) < 1e-12);
  assert(std::abs(neutral->upper_bound - 3.25) < 1e-12);

  cad::StructuralParameters defaults;
  const auto calibrated = cad::apply_structural_parameter_registry(defaults, structural);
  assert(std::abs(calibrated.neutral_rate - 2.75) < 1e-12);
  assert(calibrated.calibration_id == "v2-structural-2026-08-12-empirical");

  const auto json = cad::empirical_calibration_audit_to_json(audit);
  assert(json.find("\"evidenceAnchored\":7") != std::string::npos);
  assert(json.find("\"evidenceAnchorRate\":0.304348") != std::string::npos);
  assert(json.find("\"exceedsQuarterThreshold\":true") != std::string::npos);
  return 0;
}
