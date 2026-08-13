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
    out << "PARAM,input_output_model_sector_coverage,20,count,official-derived,io,2024,0,false\n";
    out << "PARAM,us_effective_tariff_goods,25,percent,official-derived,tariff,2026,0,false\n";
    out << "PARAM,canada_effective_tariff_goods,15,percent,official-derived,tariff,2026,0,false\n";
    out << "PARAM,cusma_origin_utilization_proxy,88,percent,official-derived,origin,2026,0,false\n";
    out << "PARAM,tariff_price_pass_through_anchor,0.65,ratio,empirically-estimated,pass,2025,0.04,false\n";
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
  assert(std::abs(calibrated.trade_elasticity - 0.9) < 1e-9);

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
  assert(json.find("\"calibrationScope\":\"merchandise-primary-and-manufacturing\"") != std::string::npos);
  assert(json.find("\"effectiveState\":") != std::string::npos);
  assert(json.find("\"usTariff\":25.0000") != std::string::npos);
  assert(json.find("\"retaliatoryTariff\":15.0000") != std::string::npos);
  assert(json.find("\"effectiveFrom\":\"2026-08-19\"") != std::string::npos);

  const auto certified = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  assert(certified.loaded);
  assert(certified.snapshot_id == "ca-us-2026-08-12-certified-trade");
  assert(certified.as_of == "2026-08-12");
  assert(certified.official_trade_complete);
  assert(certified.tariff_lines_complete);
  assert(certified.input_output_complete);
  assert(certified.origin_utilization_complete);
  assert(certified.elasticities_estimated);
  assert(certified.pass_through_estimated);
  assert(std::abs(certified.completeness - 100.0) < 1e-9);
  assert(certified.grade == "empirical-calibrated");
  assert(certified.parameters.at("input_output_model_sector_coverage").value == 20.0);
  assert(certified.parameters.at("trade_elasticity").kind == "empirically-estimated");
  assert(certified.parameters.at("trade_elasticity").source_id == "imf_trade_flows_mr_2012");
  assert(certified.parameters.at("trade_elasticity").use_in_model);
  assert(certified.sectors[0].elasticity_kind == "empirically-estimated");
  assert(certified.sectors[4].pass_through_kind == "empirical-research-anchor");
  assert(certified.sectors[2].tariff_kind == "not-applicable");
  assert(certified.sectors[2].origin_utilization < 0.0);

  cad::Economy current_economy;
  const auto current_calibrated = cad::apply_calibration(current_economy, certified);
  assert(std::abs(current_calibrated.us_tariff_canada - 5.0) < 1e-9);
  assert(std::abs(current_calibrated.canada_retaliatory_tariff - 1.5) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_coverage[0] - 100.0) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_coverage[1] - 100.0) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_coverage[4] - 100.0) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_coverage[2]) < 1e-9);
  assert(std::abs(current_calibrated.trade_elasticity - 0.65) < 1e-9);
  assert(std::abs(current_calibrated.tariff_price_pass_through - 0.24) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_trade_elasticity[0] - 5.705) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_trade_elasticity[1] - 12.510) < 1e-9);
  assert(std::abs(current_calibrated.canada_sector_trade_elasticity[4] - 7.167) < 1e-9);
  assert(std::abs(current_calibrated.canada_sector_price_pass_through[0] - 0.24) < 1e-9);
  assert(std::abs(current_calibrated.us_sector_price_pass_through[0]) < 1e-9);

  const auto certified_json = cad::calibration_to_json(certified);
  assert(certified_json.find("\"certifiedForEmpiricalUse\":true") != std::string::npos);
  assert(certified_json.find("\"tariffLines\":true") != std::string::npos);
  assert(certified_json.find("\"inputOutput\":true") != std::string::npos);
  assert(certified_json.find("\"originUtilization\":true") != std::string::npos);
  assert(certified_json.find("\"elasticitiesEstimated\":true") != std::string::npos);
  assert(certified_json.find("\"passThroughEstimated\":true") != std::string::npos);
  assert(certified_json.find("\"effectiveState\":") != std::string::npos);
  assert(certified_json.find("\"usTariff\":5.0000") != std::string::npos);
  assert(certified_json.find("\"retaliatoryTariff\":1.5000") != std::string::npos);
  assert(certified_json.find("\"sectorElasticityOverrideCount\":3") != std::string::npos);
  assert(certified_json.find("\"canadaPassThroughOverrideCount\":3") != std::string::npos);

  bool saw_future_section338 = false;
  for (const auto& measure : certified.measures) {
    if (measure.id == "us_section338_20260819") {
      saw_future_section338 = true;
      assert(measure.effective_from == "2026-08-19");
      assert(measure.status.find("future") == 0);
    }
  }
  assert(saw_future_section338);

  // V3 structural assumptions must be auditable independently of observed-data
  // completeness. Every coefficient has a source classification, vintage and
  // bounded uncertainty rule; mandate/derived parameters are not sampled, and
  // declared parameter dependence is explicit rather than hidden in code.
  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  assert(registry.loaded);
  assert(registry.registry_id == "v3-structural-network-2026-08-13");
  assert(registry.as_of == "2026-08-13");
  assert(cad::structural_parameter_registry_complete(registry));
  assert(cad::structural_correlation_registry_valid(registry));
  assert(registry.entries.size() == cad::required_structural_parameter_names().size());
  assert(registry.correlations.size() == 2);
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
  const auto* network_transmission = registry.find("network_supplier_demand_transmission");
  assert(network_transmission && network_transmission->kind == "assumed");
  assert(network_transmission->source_id == "internal_model_design_v3");
  assert(network_transmission->sampled);

  const auto structural = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  assert(structural.calibration_id == registry.registry_id);
  assert(structural.calibration_vintage == registry.as_of);
  assert(structural.uncertainty_registry.loaded);
  assert(std::abs(structural.neutral_rate - 2.75) < 1e-12);
  assert(std::abs(structural.import_price_pass_through - pass_through->baseline) < 1e-12);
  assert(std::abs(structural.network_supplier_demand_transmission
      - network_transmission->baseline) < 1e-12);

  const auto registry_json = cad::structural_parameter_registry_to_json(registry);
  assert(registry_json.find("\"complete\":true") != std::string::npos);
  assert(registry_json.find("\"correlationPairCount\":2") != std::string::npos);
  assert(registry_json.find("\"correlationRegistryValid\":true") != std::string::npos);
  assert(registry_json.find("\"kind\":\"mandate\"") != std::string::npos);
  assert(registry_json.find("\"distribution\":\"derived\"") != std::string::npos);
  assert(registry_json.find("\"sourceId\":\"boc_neutral_rate_2026\"") != std::string::npos);
  assert(registry_json.find("\"sourceId\":\"internal_model_design_v3\"") != std::string::npos);

  std::remove(path.c_str());
  return 0;
}