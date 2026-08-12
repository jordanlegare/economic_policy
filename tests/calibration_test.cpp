#include "calibration.hpp"
#include "structural_calibration.hpp"

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

  // V2 structural assumptions must be auditable independently of observed-data
  // completeness. Every coefficient has a source classification, vintage and
  // bounded uncertainty rule; mandate/derived parameters are not sampled.
  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  assert(registry.loaded);
  assert(registry.registry_id == "v2-structural-2026-08-12");
  assert(registry.as_of == "2026-08-12");
  assert(cad::structural_parameter_registry_complete(registry));
  assert(registry.entries.size() == cad::required_structural_parameter_names().size());
  assert(cad::sampled_structural_parameter_count(registry) > 0);

  const auto* neutral = registry.find("neutral_rate");
  assert(neutral && neutral->kind == "official_assessment" && neutral->sampled);
  assert(neutral->source_id == "boc_neutral_rate_2026");
  assert(std::abs(neutral->baseline - 2.75) < 1e-12);
  assert(std::abs(neutral->lower_bound - 2.25) < 1e-12);
  assert(std::abs(neutral->upper_bound - 3.25) < 1e-12);
  const auto* target = registry.find("inflation_target");
  assert(target && target->kind == "mandate" && !target->sampled);
  assert(std::abs(target->lower_bound - 2.0) < 1e-12);
  assert(std::abs(target->upper_bound - 2.0) < 1e-12);
  const auto* expectations = registry.find("inflation_expectations_weight");
  assert(expectations && expectations->kind == "derived" && !expectations->sampled);
  const auto* pass_through = registry.find("import_price_pass_through");
  assert(pass_through && pass_through->sampled);
  assert(pass_through->lower_bound < pass_through->baseline);
  assert(pass_through->upper_bound > pass_through->baseline);

  const auto structural = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  assert(structural.calibration_id == registry.registry_id);
  assert(structural.calibration_vintage == registry.as_of);
  assert(structural.uncertainty_registry.loaded);
  assert(std::abs(structural.neutral_rate - 2.75) < 1e-12);
  assert(std::abs(structural.import_price_pass_through - pass_through->baseline) < 1e-12);

  const auto registry_json = cad::structural_parameter_registry_to_json(registry);
  assert(registry_json.find("\"complete\":true") != std::string::npos);
  assert(registry_json.find("\"kind\":\"mandate\"") != std::string::npos);
  assert(registry_json.find("\"distribution\":\"derived\"") != std::string::npos);
  assert(registry_json.find("\"sourceId\":\"boc_neutral_rate_2026\"") != std::string::npos);
  assert(registry_json.find("\"sourceId\":\"internal_model_design_v2\"") != std::string::npos);

  std::remove(path.c_str());
  return 0;
}
