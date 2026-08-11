#include "calibration.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

int main() {
  const std::string path = "calibration-test.snapshot.csv";
  {
    std::ofstream out(path);
    out << "META,schema_version,1\n";
    out << "META,snapshot_id,test-complete\n";
    out << "META,as_of,2026-08-11\n";
    out << "META,generated_at,2026-08-11T06:00:00Z\n";
    out << "PARAM,canada_exports_to_us_cad,564.647,CAD_bn,observed,trade,2025,0,true\n";
    out << "PARAM,canada_imports_from_us_cad,361.667,CAD_bn,observed,trade,2025,0,true\n";
    out << "PARAM,exports_to_us_share,78.285,percent,official-derived,trade,2025,0,true\n";
    out << "PARAM,imports_from_us_share,45.840,percent,official-derived,trade,2025,0,true\n";
    out << "PARAM,input_output_calibrated,1,binary,official-derived,io,2024,0,false\n";
    out << "PARAM,trade_elasticity,0.9,elasticity,empirically-estimated,estimate,2025,0.1,true\n";
    out << "SOURCE,trade,Statistics Canada,Bilateral trade,2025,https://example.invalid/trade,abc,verified\n";
    out << "MEASURE,future,United States,Future tariff,2026-07-20,2026-08-19,,50%,selected goods,legal,future\n";
    for (int i = 0; i < 20; ++i) {
      const double us_tariff = i == 4 ? 25.0 : 5.0;
      const double ca_tariff = i == 4 ? 15.0 : 2.0;
      out << "SECTOR," << i << "," << i << ",Sector " << i
          << "," << (5.0) << "," << (5.0)
          << "," << us_tariff << "," << ca_tariff
          << "," << (0.6 + .01 * i) << ",0.05,0.65,0.04,88.0"
          << ",official-derived,empirically-estimated,empirically-estimated\n";
    }
  }

  const auto snapshot = cad::load_calibration_snapshot(path);
  assert(snapshot.loaded);
  assert(snapshot.official_trade_complete);
  assert(snapshot.tariff_lines_complete);
  assert(snapshot.input_output_complete);
  assert(snapshot.origin_utilization_complete);
  assert(snapshot.elasticities_estimated);
  assert(snapshot.pass_through_estimated);
  assert(std::abs(snapshot.completeness - 100.0) < 1e-9);
  assert(snapshot.grade == "empirical-calibrated");

  cad::Economy economy;
  economy.canada_exports_to_us_cad = 1.0;
  economy.canada_imports_from_us_cad = 1.0;
  economy.exports_to_us_share = 1.0;
  economy.imports_from_us_share = 1.0;
  const auto calibrated = cad::apply_calibration(economy, snapshot);
  assert(std::abs(calibrated.canada_exports_to_us_cad - 564.647) < 1e-9);
  assert(std::abs(calibrated.canada_imports_from_us_cad - 361.667) < 1e-9);
  assert(std::abs(calibrated.exports_to_us_share - 78.285) < 1e-9);
  assert(std::abs(calibrated.imports_from_us_share - 45.840) < 1e-9);

  // Compression into max-rate + coverage must exactly preserve each sector's
  // trade-weighted effective tariff rate used by the existing macro engine.
  assert(std::abs(calibrated.us_tariff_canada - 25.0) < 1e-9);
  assert(std::abs(calibrated.canada_retaliatory_tariff - 15.0) < 1e-9);
  for (std::size_t i = 0; i < snapshot.sectors.size(); ++i) {
    const double reconstructed_us = calibrated.us_tariff_canada
        * calibrated.us_sector_coverage[i] / 100.0;
    const double reconstructed_ca = calibrated.canada_retaliatory_tariff
        * calibrated.canada_sector_coverage[i] / 100.0;
    assert(std::abs(reconstructed_us - snapshot.sectors[i].us_effective_tariff) < 1e-9);
    assert(std::abs(reconstructed_ca - snapshot.sectors[i].canada_effective_tariff) < 1e-9);
  }

  // The future legal measure is recorded for diplomats but does not directly
  // alter today's tariff inputs; only reviewed sector-line calibration does.
  assert(snapshot.measures.size() == 1);
  assert(snapshot.measures.front().status == "future");
  assert(std::abs(calibrated.us_tariff_canada - 25.0) < 1e-9);

  const auto json = cad::calibration_to_json(snapshot);
  assert(json.find("\"grade\":\"empirical-calibrated\"") != std::string::npos);
  assert(json.find("\"certifiedForEmpiricalUse\":true") != std::string::npos);
  assert(json.find("\"tariffLines\":true") != std::string::npos);
  assert(json.find("\"effectiveFrom\":\"2026-08-19\"") != std::string::npos);

  const auto partial = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  assert(partial.loaded);
  assert(partial.official_trade_complete);
  assert(!partial.tariff_lines_complete);
  assert(!partial.elasticities_estimated);
  assert(partial.completeness < 95.0);
  assert(partial.grade != "empirical-calibrated");

  std::remove(path.c_str());
  return 0;
}
