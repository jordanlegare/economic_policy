#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
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

// Customs tariffs and preferential origin rules apply to merchandise, not to
// every service-producing NAICS sector. The coarse 20-sector model therefore
// certifies the primary-goods and manufacturing sectors and records services as
// explicitly not-applicable rather than fabricating tariff/origin observations.
inline bool tariff_relevant_sector(const SectorCalibration& sector) {
  return sector.index == 0 || sector.index == 1 || sector.index == 4;
}

inline bool empirical_pass_through_kind(const std::string& kind) {
  return kind == "empirically-estimated" || kind == "empirical-research-anchor";
}

inline void finalize(CalibrationSnapshot& snapshot) {
  const auto exports = parameter(snapshot, "canada_exports_to_us_cad");
  const auto imports = parameter(snapshot, "canada_imports_from_us_cad");
  const auto export_share = parameter(snapshot, "exports_to_us_share");
  const auto import_share = parameter(snapshot, "imports_from_us_share");
  snapshot.official_trade_complete = exports && imports && export_share && import_share
      && trusted_kind(exports->kind) && trusted_kind(imports->kind)
      && trusted_kind(export_share->kind) && trusted_kind(import_share->kind);

  int sector_rows = 0, relevant_rows = 0, tariff_rows = 0;
  int elasticity_rows = 0, pass_rows = 0, origin_rows = 0;
  for (const auto& sector : snapshot.sectors) {
    if (sector.index < 0) continue;
    ++sector_rows;
    if (!tariff_relevant_sector(sector)) continue;
    ++relevant_rows;
    if (sector.us_effective_tariff >= 0.0 && sector.canada_effective_tariff >= 0.0
        && trusted_kind(sector.tariff_kind)) ++tariff_rows;
    if (sector.trade_elasticity > 0.0 && sector.trade_elasticity_se >= 0.0
        && sector.elasticity_kind == "empirically-estimated") ++elasticity_rows;
    if (sector.price_pass_through >= 0.0 && sector.price_pass_through <= 1.0
        && sector.price_pass_through_se >= 0.0
        && empirical_pass_through_kind(sector.pass_through_kind)) ++pass_rows;
    if (sector.origin_utilization >= 0.0 && sector.origin_utilization <= 100.0) ++origin_rows;
  }
  const bool scope_complete = sector_rows == 20 && relevant_rows == 3;
  snapshot.tariff_lines_complete = scope_complete && tariff_rows == relevant_rows;
  snapshot.elasticities_estimated = scope_complete && elasticity_rows == relevant_rows;
  snapshot.pass_through_estimated = scope_complete && pass_rows == relevant_rows;
  snapshot.origin_utilization_complete = scope_complete && origin_rows == relevant_rows;

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
}  // namespace cad
