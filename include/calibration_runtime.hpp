#pragma once

#include "calibration_loader.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace cad {

inline Economy apply_calibration(Economy economy, const CalibrationSnapshot& snapshot) {
  auto apply = [&](const char* key, double& field) {
    const auto* p = calibration_detail::parameter(snapshot, key);
    if (p && p->use_in_model && calibration_detail::trusted_kind(p->kind)) field = p->value;
  };
  apply("canada_exports_to_us_cad", economy.canada_exports_to_us_cad);
  apply("canada_imports_from_us_cad", economy.canada_imports_from_us_cad);
  apply("exports_to_us_share", economy.exports_to_us_share);
  apply("imports_from_us_share", economy.imports_from_us_share);
  apply("trade_elasticity", economy.trade_elasticity);
  apply("border_friction", economy.border_friction);

  // The engine represents the frozen legal/effective tariff calibration as a
  // maximum applied goods rate plus per-sector coverage. Non-merchandise
  // sectors are explicitly zero rather than receiving fabricated tariff lines.
  if (snapshot.tariff_lines_complete) {
    double max_us = 0.0, max_ca = 0.0;
    for (const auto& s : snapshot.sectors) {
      max_us = std::max(max_us, s.us_effective_tariff);
      max_ca = std::max(max_ca, s.canada_effective_tariff);
    }
    if (max_us > 1e-9) {
      economy.us_tariff_canada = max_us;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.us_sector_coverage[i] = 100.0 * std::max(0.0, snapshot.sectors[i].us_effective_tariff) / max_us;
    }
    if (max_ca > 1e-9) {
      economy.canada_retaliatory_tariff = max_ca;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.canada_sector_coverage[i] = 100.0 * std::max(0.0, snapshot.sectors[i].canada_effective_tariff) / max_ca;
    }
  }

  // Only direct production mappings should change the aggregate model scalar.
  // Sector-level literature estimates can certify the evidence layer without
  // silently redefining the engine's aggregate trade-elasticity estimand.
  return economy;
}

inline std::string calibration_to_json(const CalibrationSnapshot& snapshot) {
  using calibration_detail::esc;
  std::ostringstream out;
  out << std::fixed << std::setprecision(4);
  out << "{\"snapshotId\":\"" << esc(snapshot.snapshot_id)
      << "\",\"asOf\":\"" << esc(snapshot.as_of)
      << "\",\"generatedAt\":\"" << esc(snapshot.generated_at)
      << "\",\"schemaVersion\":\"" << esc(snapshot.schema_version)
      << "\",\"loaded\":" << (snapshot.loaded ? "true" : "false")
      << ",\"grade\":\"" << esc(snapshot.grade)
      << "\",\"completeness\":" << snapshot.completeness
      << ",\"certifiedForEmpiricalUse\":" << (snapshot.completeness >= 95.0 ? "true" : "false")
      << ",\"calibrationScope\":\"merchandise-primary-and-manufacturing\""
      << ",\"checks\":{\"officialTrade\":" << (snapshot.official_trade_complete ? "true" : "false")
      << ",\"tariffLines\":" << (snapshot.tariff_lines_complete ? "true" : "false")
      << ",\"inputOutput\":" << (snapshot.input_output_complete ? "true" : "false")
      << ",\"originUtilization\":" << (snapshot.origin_utilization_complete ? "true" : "false")
      << ",\"elasticitiesEstimated\":" << (snapshot.elasticities_estimated ? "true" : "false")
      << ",\"passThroughEstimated\":" << (snapshot.pass_through_estimated ? "true" : "false") << "}"
      << ",\"sources\":[";
  for (std::size_t i = 0; i < snapshot.sources.size(); ++i) {
    if (i) out << ',';
    const auto& s = snapshot.sources[i];
    out << "{\"id\":\"" << esc(s.id) << "\",\"agency\":\"" << esc(s.agency)
        << "\",\"dataset\":\"" << esc(s.dataset) << "\",\"vintage\":\"" << esc(s.vintage)
        << "\",\"url\":\"" << esc(s.url) << "\",\"sha256\":\"" << esc(s.sha256)
        << "\",\"status\":\"" << esc(s.status) << "\"}";
  }
  out << "],\"measures\":[";
  for (std::size_t i = 0; i < snapshot.measures.size(); ++i) {
    if (i) out << ',';
    const auto& m = snapshot.measures[i];
    out << "{\"id\":\"" << esc(m.id) << "\",\"jurisdiction\":\"" << esc(m.jurisdiction)
        << "\",\"instrument\":\"" << esc(m.instrument) << "\",\"announced\":\"" << esc(m.announced)
        << "\",\"effectiveFrom\":\"" << esc(m.effective_from) << "\",\"effectiveTo\":\"" << esc(m.effective_to)
        << "\",\"rate\":\"" << esc(m.rate) << "\",\"scope\":\"" << esc(m.scope)
        << "\",\"sourceId\":\"" << esc(m.source_id) << "\",\"status\":\"" << esc(m.status) << "\"}";
  }
  out << "],\"warning\":\"";
  if (snapshot.completeness >= 95.0)
    out << "Observed-data calibration checks passed; legal interpretation and model uncertainty still require human review.";
  else
    out << "Calibration is incomplete. Missing official or estimated layers remain visible and model defaults must not be described as observed data.";
  out << "\"}";
  return out.str();
}

inline std::string attach_calibration_json(std::string base_json,
                                           const CalibrationSnapshot& snapshot) {
  if (base_json.size() < 2 || base_json.front() != '{' || base_json.back() != '}') return base_json;
  base_json.pop_back();
  base_json += ",\"calibration\":" + calibration_to_json(snapshot) + "}";
  return base_json;
}

class CalibratedPolicyEngine {
 public:
  explicit CalibratedPolicyEngine(std::string snapshot_path,
                                  std::uint64_t seed = 20260810,
                                  StructuralParameters structural_parameters = {},
                                  StructuralParameterRegistry structural_registry = {})
      : base_(seed, std::move(structural_parameters), std::move(structural_registry)),
        snapshot_(load_calibration_snapshot(snapshot_path)), path_(std::move(snapshot_path)) {}

  Result evaluate(Economy& economy) const {
    economy = apply_calibration(economy, snapshot_);
    return base_.evaluate(economy);
  }

  const CalibrationSnapshot& snapshot() const { return snapshot_; }
  const std::string& snapshot_path() const { return path_; }

 private:
  PolicyEngine base_;
  CalibrationSnapshot snapshot_;
  std::string path_;
};

}  // namespace cad
