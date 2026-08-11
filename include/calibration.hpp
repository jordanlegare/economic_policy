#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cad {

struct CalibrationParameter {
  std::string name;
  double value = 0.0;
  std::string unit;
  std::string kind;
  std::string source_id;
  std::string vintage;
  double standard_error = 0.0;
  bool use_in_model = false;
};

struct CalibrationSource {
  std::string id;
  std::string agency;
  std::string dataset;
  std::string vintage;
  std::string url;
  std::string sha256;
  std::string status;
};

struct SectorCalibration {
  int index = -1;
  std::string code;
  std::string name;
  double canada_export_share = 0.0;
  double us_export_share = 0.0;
  double us_effective_tariff = -1.0;
  double canada_effective_tariff = -1.0;
  double trade_elasticity = 0.0;
  double trade_elasticity_se = 0.0;
  double price_pass_through = 0.0;
  double price_pass_through_se = 0.0;
  double origin_utilization = -1.0;
  std::string tariff_kind;
  std::string elasticity_kind;
  std::string pass_through_kind;
};

struct TariffMeasure {
  std::string id;
  std::string jurisdiction;
  std::string instrument;
  std::string announced;
  std::string effective_from;
  std::string effective_to;
  std::string rate;
  std::string scope;
  std::string source_id;
  std::string status;
};

struct CalibrationSnapshot {
  std::string snapshot_id = "none";
  std::string as_of;
  std::string generated_at;
  std::string schema_version = "1";
  std::map<std::string, CalibrationParameter> parameters;
  std::vector<CalibrationSource> sources;
  std::array<SectorCalibration, 20> sectors{};
  std::vector<TariffMeasure> measures;
  bool loaded = false;
  bool official_trade_complete = false;
  bool tariff_lines_complete = false;
  bool input_output_complete = false;
  bool origin_utilization_complete = false;
  bool elasticities_estimated = false;
  bool pass_through_estimated = false;
  double completeness = 0.0;
  std::string grade = "uncalibrated";
};

namespace calibration_detail {

inline std::string trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
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

inline double number(const std::string& value, double fallback = 0.0) {
  if (value.empty() || value == "NA" || value == "na" || value == "null") return fallback;
  try { return std::stod(value); } catch (...) { return fallback; }
}

inline bool yes(const std::string& value) {
  return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

inline std::string esc(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    if (c == '\n') { out += "\\n"; continue; }
    if (c == '\r') { out += "\\r"; continue; }
    if (c == '\t') { out += "\\t"; continue; }
    out.push_back(c);
  }
  return out;
}

inline const CalibrationParameter* parameter(const CalibrationSnapshot& snapshot,
                                              const std::string& name) {
  const auto it = snapshot.parameters.find(name);
  return it == snapshot.parameters.end() ? nullptr : &it->second;
}

inline bool trusted_kind(const std::string& kind) {
  return kind == "observed" || kind == "official-derived" || kind == "empirically-estimated";
}

inline void finalize(CalibrationSnapshot& snapshot) {
  const auto exports = parameter(snapshot, "canada_exports_to_us_cad");
  const auto imports = parameter(snapshot, "canada_imports_from_us_cad");
  const auto export_share = parameter(snapshot, "exports_to_us_share");
  const auto import_share = parameter(snapshot, "imports_from_us_share");
  snapshot.official_trade_complete = exports && imports && export_share && import_share
      && trusted_kind(exports->kind) && trusted_kind(imports->kind)
      && trusted_kind(export_share->kind) && trusted_kind(import_share->kind);

  int sector_rows = 0, tariff_rows = 0, elasticity_rows = 0, pass_rows = 0, origin_rows = 0;
  for (const auto& sector : snapshot.sectors) {
    if (sector.index < 0) continue;
    ++sector_rows;
    if (sector.us_effective_tariff >= 0.0 && sector.canada_effective_tariff >= 0.0
        && trusted_kind(sector.tariff_kind)) ++tariff_rows;
    if (sector.trade_elasticity > 0.0 && sector.trade_elasticity_se >= 0.0
        && sector.elasticity_kind == "empirically-estimated") ++elasticity_rows;
    if (sector.price_pass_through >= 0.0 && sector.price_pass_through_se >= 0.0
        && sector.pass_through_kind == "empirically-estimated") ++pass_rows;
    if (sector.origin_utilization >= 0.0) ++origin_rows;
  }
  snapshot.tariff_lines_complete = sector_rows == 20 && tariff_rows == 20;
  snapshot.elasticities_estimated = sector_rows == 20 && elasticity_rows == 20;
  snapshot.pass_through_estimated = sector_rows == 20 && pass_rows == 20;
  snapshot.origin_utilization_complete = sector_rows == 20 && origin_rows == 20;

  const auto io_flag = parameter(snapshot, "input_output_calibrated");
  snapshot.input_output_complete = io_flag && io_flag->value >= 0.5 && trusted_kind(io_flag->kind);

  snapshot.completeness = 0.0;
  if (snapshot.official_trade_complete) snapshot.completeness += 25.0;
  if (snapshot.tariff_lines_complete) snapshot.completeness += 25.0;
  if (snapshot.input_output_complete) snapshot.completeness += 20.0;
  if (snapshot.origin_utilization_complete) snapshot.completeness += 10.0;
  if (snapshot.elasticities_estimated) snapshot.completeness += 10.0;
  if (snapshot.pass_through_estimated) snapshot.completeness += 10.0;

  if (!snapshot.loaded) snapshot.grade = "uncalibrated";
  else if (snapshot.completeness >= 95.0) snapshot.grade = "empirical-calibrated";
  else if (snapshot.completeness >= 50.0) snapshot.grade = "official-partial";
  else snapshot.grade = "provenance-only";
}

}  // namespace calibration_detail

inline CalibrationSnapshot load_calibration_snapshot(const std::string& path) {
  CalibrationSnapshot snapshot;
  for (auto& sector : snapshot.sectors) sector.index = -1;
  std::ifstream in(path);
  if (!in) {
    calibration_detail::finalize(snapshot);
    return snapshot;
  }
  snapshot.loaded = true;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.empty()) continue;
    if (f[0] == "META" && f.size() >= 3) {
      if (f[1] == "snapshot_id") snapshot.snapshot_id = f[2];
      else if (f[1] == "as_of") snapshot.as_of = f[2];
      else if (f[1] == "generated_at") snapshot.generated_at = f[2];
      else if (f[1] == "schema_version") snapshot.schema_version = f[2];
    } else if (f[0] == "PARAM" && f.size() >= 9) {
      CalibrationParameter p;
      p.name = f[1];
      p.value = calibration_detail::number(f[2]);
      p.unit = f[3];
      p.kind = f[4];
      p.source_id = f[5];
      p.vintage = f[6];
      p.standard_error = calibration_detail::number(f[7]);
      p.use_in_model = calibration_detail::yes(f[8]);
      snapshot.parameters[p.name] = std::move(p);
    } else if (f[0] == "SOURCE" && f.size() >= 8) {
      snapshot.sources.push_back({f[1], f[2], f[3], f[4], f[5], f[6], f[7]});
    } else if (f[0] == "SECTOR" && f.size() >= 16) {
      const int index = static_cast<int>(calibration_detail::number(f[1], -1));
      if (index < 0 || index >= static_cast<int>(snapshot.sectors.size())) continue;
      auto& s = snapshot.sectors[static_cast<std::size_t>(index)];
      s.index = index; s.code = f[2]; s.name = f[3];
      s.canada_export_share = calibration_detail::number(f[4]);
      s.us_export_share = calibration_detail::number(f[5]);
      s.us_effective_tariff = calibration_detail::number(f[6], -1.0);
      s.canada_effective_tariff = calibration_detail::number(f[7], -1.0);
      s.trade_elasticity = calibration_detail::number(f[8]);
      s.trade_elasticity_se = calibration_detail::number(f[9]);
      s.price_pass_through = calibration_detail::number(f[10], -1.0);
      s.price_pass_through_se = calibration_detail::number(f[11]);
      s.origin_utilization = calibration_detail::number(f[12], -1.0);
      s.tariff_kind = f[13]; s.elasticity_kind = f[14]; s.pass_through_kind = f[15];
    } else if (f[0] == "MEASURE" && f.size() >= 11) {
      snapshot.measures.push_back({f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10]});
    }
  }
  calibration_detail::finalize(snapshot);
  return snapshot;
}

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

  // Product-line calibration is represented in the existing engine as a
  // maximum applied rate plus sector-specific coverage ratios. This preserves
  // each sector's observed trade-weighted effective rate without pretending
  // that every tariff line within a sector carries the same legal rate.
  if (snapshot.tariff_lines_complete) {
    double max_us = 0.0, max_ca = 0.0;
    for (const auto& s : snapshot.sectors) {
      max_us = std::max(max_us, s.us_effective_tariff);
      max_ca = std::max(max_ca, s.canada_effective_tariff);
    }
    if (max_us > 1e-9) {
      economy.us_tariff_canada = max_us;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.us_sector_coverage[i] = 100.0 * snapshot.sectors[i].us_effective_tariff / max_us;
    }
    if (max_ca > 1e-9) {
      economy.canada_retaliatory_tariff = max_ca;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.canada_sector_coverage[i] = 100.0 * snapshot.sectors[i].canada_effective_tariff / max_ca;
    }
  }

  if (snapshot.elasticities_estimated) {
    double weighted = 0.0, total = 0.0;
    for (const auto& s : snapshot.sectors) {
      const double weight = std::max(0.0, s.canada_export_share + s.us_export_share);
      weighted += weight * s.trade_elasticity;
      total += weight;
    }
    if (total > 1e-9) economy.trade_elasticity = weighted / total;
  }
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
  if (snapshot.completeness >= 95.0) out << "Empirical calibration checks passed; legal interpretation and model uncertainty still require human review.";
  else out << "Calibration is incomplete. Missing official or estimated layers remain visible and model defaults must not be described as observed data.";
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
                                  std::uint64_t seed = 20260810)
      : base_(seed), snapshot_(load_calibration_snapshot(snapshot_path)), path_(std::move(snapshot_path)) {}

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
