#pragma once

#include "calibration.hpp"
#include "policy_engine.hpp"

#include <algorithm>
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
    "output_shock_sd", "inflation_shock_sd", "output_inflation_shock_correlation",
    "growth_shock_sd", "us_growth_shock_sd", "export_shock_sd", "us_export_shock_sd"
  };
}

inline std::vector<std::string> structural_shock_parameter_names() {
  return {
    "output_shock_sd", "inflation_shock_sd", "growth_shock_sd",
    "us_growth_shock_sd", "export_shock_sd", "us_export_shock_sd"
  };
}

inline std::vector<std::string> structural_multiplier_parameter_names() {
  return {
    "fiscal_demand_multiplier", "real_rate_demand_sensitivity",
    "productive_supply_multiplier", "global_growth_sensitivity",
    "phillips_curve_slope", "fx_pass_through", "import_price_pass_through",
    "oil_inflation_sensitivity", "canada_trade_drag_scale",
    "us_retaliation_drag_scale", "tariff_revenue_elasticity_scale"
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

inline bool direct_empirical_structural_kind(const std::string& kind) {
  return kind == "empirical_estimate"
      || kind == "official_assessment"
      || kind == "realized_residual_estimate";
}

inline bool contains_structural_name(
    const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

struct StructuralCalibrationCompleteness {
  int parameter_count = 0;
  int calibration_target_count = 0;
  int direct_empirical_count = 0;
  int provisional_count = 0;
  int shock_target_count = 0;
  int direct_empirical_shock_count = 0;
  int realized_residual_shock_count = 0;
  int multiplier_target_count = 0;
  int direct_empirical_multiplier_count = 0;
  double direct_empirical_coverage = 0.0;
  double shock_coverage = 0.0;
  double multiplier_coverage = 0.0;
  std::string grade = "registry-incomplete";
};

inline StructuralCalibrationCompleteness audit_structural_calibration_completeness(
    const StructuralParameterRegistry& registry) {
  StructuralCalibrationCompleteness out;
  const auto required = required_structural_parameter_names();
  const auto shocks = structural_shock_parameter_names();
  const auto multipliers = structural_multiplier_parameter_names();
  out.parameter_count = static_cast<int>(required.size());
  out.shock_target_count = static_cast<int>(shocks.size());
  out.multiplier_target_count = static_cast<int>(multipliers.size());

  if (!structural_parameter_registry_complete(registry)) return out;

  for (const auto& name : required) {
    const auto* entry = registry.find(name);
    if (!entry) continue;
    // Mandates are policy-framework constants and derived parameters are
    // algebraic constraints. Neither should inflate or depress the empirical
    // calibration denominator.
    if (entry->kind == "mandate" || entry->kind == "derived") continue;
    ++out.calibration_target_count;
    const bool direct = direct_empirical_structural_kind(entry->kind);
    if (direct) ++out.direct_empirical_count;
    else ++out.provisional_count;

    if (contains_structural_name(shocks, name)) {
      if (direct) ++out.direct_empirical_shock_count;
      if (entry->kind == "realized_residual_estimate") ++out.realized_residual_shock_count;
    }
    if (contains_structural_name(multipliers, name) && direct)
      ++out.direct_empirical_multiplier_count;
  }

  if (out.calibration_target_count > 0)
    out.direct_empirical_coverage = 100.0 * out.direct_empirical_count
        / static_cast<double>(out.calibration_target_count);
  if (out.shock_target_count > 0)
    out.shock_coverage = 100.0 * out.direct_empirical_shock_count
        / static_cast<double>(out.shock_target_count);
  if (out.multiplier_target_count > 0)
    out.multiplier_coverage = 100.0 * out.direct_empirical_multiplier_count
        / static_cast<double>(out.multiplier_target_count);

  if (out.direct_empirical_coverage >= 95.0) out.grade = "empirically-calibrated";
  else if (out.direct_empirical_coverage >= 50.0) out.grade = "partially-empirical";
  else out.grade = "mostly-provisional";
  return out;
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
  value("output_inflation_shock_correlation", p.output_inflation_shock_correlation);
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
  const auto completeness = audit_structural_calibration_completeness(registry);
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"registryId\":\"" << calibration_detail::esc(registry.registry_id)
      << "\",\"asOf\":\"" << calibration_detail::esc(registry.as_of)
      << "\",\"loaded\":" << (registry.loaded ? "true" : "false")
      << ",\"complete\":"
      << (structural_parameter_registry_complete(registry) ? "true" : "false")
      << ",\"sampledParameterCount\":"
      << sampled_structural_parameter_count(registry)
      << ",\"calibrationCompleteness\":{\"parameterCount\":" << completeness.parameter_count
      << ",\"calibrationTargetCount\":" << completeness.calibration_target_count
      << ",\"directEmpiricalCount\":" << completeness.direct_empirical_count
      << ",\"provisionalCount\":" << completeness.provisional_count
      << ",\"directEmpiricalCoverage\":" << completeness.direct_empirical_coverage
      << ",\"shockTargetCount\":" << completeness.shock_target_count
      << ",\"directEmpiricalShockCount\":" << completeness.direct_empirical_shock_count
      << ",\"realizedResidualShockCount\":" << completeness.realized_residual_shock_count
      << ",\"shockCoverage\":" << completeness.shock_coverage
      << ",\"multiplierTargetCount\":" << completeness.multiplier_target_count
      << ",\"directEmpiricalMultiplierCount\":" << completeness.direct_empirical_multiplier_count
      << ",\"multiplierCoverage\":" << completeness.multiplier_coverage
      << ",\"grade\":\"" << calibration_detail::esc(completeness.grade) << "\"}"
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
