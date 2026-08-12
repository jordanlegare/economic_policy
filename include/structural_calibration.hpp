#pragma once

#include "calibration.hpp"
#include "policy_engine.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

inline std::vector<std::string> required_structural_parameter_names() {
  return {
    "neutral_rate", "inflation_target", "rate_inflation_response",
    "rate_output_response", "max_quarterly_rate_step", "output_persistence",
    "fiscal_demand_multiplier", "real_rate_demand_sensitivity",
    "productive_supply_multiplier", "global_growth_sensitivity",
    "inflation_persistence", "inflation_expectations_weight",
    "phillips_curve_slope", "fx_pass_through", "import_price_pass_through",
    "oil_inflation_sensitivity", "canada_trade_drag_scale",
    "us_retaliation_drag_scale", "tariff_revenue_elasticity_scale",
    "output_shock_sd", "inflation_shock_sd", "growth_shock_sd",
    "us_growth_shock_sd", "export_shock_sd", "us_export_shock_sd"
  };
}

inline bool structural_parameter_registry_complete(
    const StructuralParameterRegistry& registry) {
  if (!registry.loaded) return false;
  for (const auto& name : required_structural_parameter_names()) {
    const auto* entry = registry.find(name);
    if (!entry || entry->source_id.empty() || entry->vintage.empty()
        || entry->kind.empty() || entry->distribution.empty()
        || entry->upper_bound < entry->lower_bound) return false;
  }
  return true;
}

inline int sampled_structural_parameter_count(
    const StructuralParameterRegistry& registry) {
  int count = 0;
  for (const auto& entry : registry.entries) if (entry.sampled) ++count;
  return count;
}

inline StructuralParameterRegistry load_structural_parameter_registry(
    const std::string& path) {
  StructuralParameterRegistry registry;
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
    } else if (f[0] == "PARAM" && f.size() >= 13) {
      StructuralParameterProvenance p;
      p.name = f[1];
      p.baseline = calibration_detail::number(f[2]);
      p.unit = f[3];
      p.kind = f[4];
      p.source_id = f[5];
      p.vintage = f[6];
      p.lower_bound = calibration_detail::number(f[7]);
      p.upper_bound = calibration_detail::number(f[8]);
      p.distribution = f[9];
      p.relative_sigma = calibration_detail::number(f[10]);
      p.sampled = calibration_detail::yes(f[11]);
      p.notes = f[12];
      registry.entries.push_back(std::move(p));
    }
  }
  return registry;
}

inline StructuralParameters apply_structural_parameter_registry(
    StructuralParameters p, const StructuralParameterRegistry& registry) {
  auto value = [&](const char* name, double& field) {
    const auto* entry = registry.find(name);
    if (entry) field = entry->baseline;
  };
  value("neutral_rate", p.neutral_rate);
  value("inflation_target", p.inflation_target);
  value("rate_inflation_response", p.rate_inflation_response);
  value("rate_output_response", p.rate_output_response);
  value("max_quarterly_rate_step", p.max_quarterly_rate_step);
  value("output_persistence", p.output_persistence);
  value("fiscal_demand_multiplier", p.fiscal_demand_multiplier);
  value("real_rate_demand_sensitivity", p.real_rate_demand_sensitivity);
  value("productive_supply_multiplier", p.productive_supply_multiplier);
  value("global_growth_sensitivity", p.global_growth_sensitivity);
  value("inflation_persistence", p.inflation_persistence);
  value("inflation_expectations_weight", p.inflation_expectations_weight);
  value("phillips_curve_slope", p.phillips_curve_slope);
  value("fx_pass_through", p.fx_pass_through);
  value("import_price_pass_through", p.import_price_pass_through);
  value("oil_inflation_sensitivity", p.oil_inflation_sensitivity);
  value("canada_trade_drag_scale", p.canada_trade_drag_scale);
  value("us_retaliation_drag_scale", p.us_retaliation_drag_scale);
  value("tariff_revenue_elasticity_scale", p.tariff_revenue_elasticity_scale);
  value("output_shock_sd", p.output_shock_sd);
  value("inflation_shock_sd", p.inflation_shock_sd);
  value("growth_shock_sd", p.growth_shock_sd);
  value("us_growth_shock_sd", p.us_growth_shock_sd);
  value("export_shock_sd", p.export_shock_sd);
  value("us_export_shock_sd", p.us_export_shock_sd);
  if (registry.loaded) {
    p.calibration_id = registry.registry_id;
    p.calibration_vintage = registry.as_of;
    p.uncertainty_registry = registry;
  }
  return p;
}

inline std::string structural_parameter_registry_to_json(
    const StructuralParameterRegistry& registry) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"registryId\":\"" << calibration_detail::esc(registry.registry_id)
      << "\",\"asOf\":\"" << calibration_detail::esc(registry.as_of)
      << "\",\"loaded\":" << (registry.loaded ? "true" : "false")
      << ",\"complete\":"
      << (structural_parameter_registry_complete(registry) ? "true" : "false")
      << ",\"sampledParameterCount\":"
      << sampled_structural_parameter_count(registry)
      << ",\"parameters\":[";
  for (std::size_t i = 0; i < registry.entries.size(); ++i) {
    if (i) out << ',';
    const auto& p = registry.entries[i];
    out << "{\"name\":\"" << calibration_detail::esc(p.name)
        << "\",\"baseline\":" << p.baseline
        << ",\"unit\":\"" << calibration_detail::esc(p.unit)
        << "\",\"kind\":\"" << calibration_detail::esc(p.kind)
        << "\",\"sourceId\":\"" << calibration_detail::esc(p.source_id)
        << "\",\"vintage\":\"" << calibration_detail::esc(p.vintage)
        << "\",\"lowerBound\":" << p.lower_bound
        << ",\"upperBound\":" << p.upper_bound
        << ",\"distribution\":\"" << calibration_detail::esc(p.distribution)
        << "\",\"relativeSigma\":" << p.relative_sigma
        << ",\"sampled\":" << (p.sampled ? "true" : "false")
        << ",\"notes\":\"" << calibration_detail::esc(p.notes) << "\"}";
  }
  out << "]}";
  return out.str();
}

}  // namespace cad
