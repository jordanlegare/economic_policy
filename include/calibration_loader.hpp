#pragma once

#include "calibration_schema.hpp"

#include <fstream>
#include <string>
#include <utility>

namespace cad {

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

}  // namespace cad
