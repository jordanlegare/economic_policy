#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace cad {
namespace robustness_detail {

struct DrawRule {
  double lower = -std::numeric_limits<double>::infinity();
  double upper = std::numeric_limits<double>::infinity();
  double sigma = 0.0;
  bool sampled = true;
  std::string distribution = "normal";
};

inline DrawRule rule_for(const StructuralParameters& baseline,
                         const StructuralParameterRegistry& registry,
                         const std::string& name, double fallback_lower,
                         double fallback_upper, const char* fallback_distribution) {
  DrawRule rule;
  rule.lower = fallback_lower;
  rule.upper = fallback_upper;
  rule.sigma = std::max(0.0, baseline.uncertainty_scale);
  rule.distribution = fallback_distribution;

  if (const auto* entry = registry.find(name)) {
    rule.lower = entry->lower_bound;
    rule.upper = entry->upper_bound;
    rule.sampled = entry->sampled;
    rule.distribution = entry->distribution;
    // Registry widths are calibrated to uncertainty_scale=0.10. The global
    // scale remains a transparent sensitivity multiplier and 0 is an exact
    // baseline-equivalence switch.
    const double multiplier = baseline.uncertainty_scale <= 0.0
        ? 0.0 : baseline.uncertainty_scale / 0.10;
    rule.sigma = std::max(0.0, entry->relative_sigma * multiplier);
  }
  return rule;
}

inline double sample_value(double baseline, const DrawRule& rule,
                           std::mt19937_64& rng,
                           std::normal_distribution<double>& z) {
  if (!rule.sampled || rule.distribution == "fixed"
      || rule.distribution == "derived" || rule.sigma <= 0.0)
    return std::clamp(baseline, rule.lower, rule.upper);

  double draw = baseline;
  if (rule.distribution == "lognormal") {
    draw = baseline * std::exp(rule.sigma * z(rng)
        - 0.5 * rule.sigma * rule.sigma);
  } else {
    draw = baseline * (1.0 + rule.sigma * z(rng));
  }
  return std::clamp(draw, rule.lower, rule.upper);
}

}  // namespace robustness_detail

// Draw structural calibrations independently from the stochastic macro shocks.
// The same seed, baseline calibration and provenance registry always produce the
// same ensemble. When a registry is supplied, its declared bounds and sampling
// rules supersede the legacy hard-coded fallback ranges.
inline std::vector<StructuralParameters> draw_structural_parameters(
    const StructuralParameters& baseline,
    const StructuralParameterRegistry& registry,
    int draws, std::uint64_t seed) {
  using robustness_detail::rule_for;
  using robustness_detail::sample_value;

  std::vector<StructuralParameters> out;
  if (draws <= 0) return out;
  out.reserve(static_cast<std::size_t>(draws));

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> z(0.0, 1.0);
  const double inf = std::numeric_limits<double>::infinity();

  for (int i = 0; i < draws; ++i) {
    StructuralParameters p = baseline;
    p.calibration_id = baseline.calibration_id + ":draw-" + std::to_string(i + 1);

    p.neutral_rate = sample_value(baseline.neutral_rate,
        rule_for(baseline, registry, "neutral_rate", 0.25, 6.0, "normal"), rng, z);
    p.rate_inflation_response = sample_value(baseline.rate_inflation_response,
        rule_for(baseline, registry, "rate_inflation_response", 1e-6, inf, "lognormal"), rng, z);
    p.rate_output_response = sample_value(baseline.rate_output_response,
        rule_for(baseline, registry, "rate_output_response", 1e-6, inf, "lognormal"), rng, z);
    p.max_quarterly_rate_step = sample_value(baseline.max_quarterly_rate_step,
        rule_for(baseline, registry, "max_quarterly_rate_step", 1e-6, inf, "lognormal"), rng, z);

    p.output_persistence = sample_value(baseline.output_persistence,
        rule_for(baseline, registry, "output_persistence", 0.05, 0.98, "normal"), rng, z);
    p.fiscal_demand_multiplier = sample_value(baseline.fiscal_demand_multiplier,
        rule_for(baseline, registry, "fiscal_demand_multiplier", 1e-6, inf, "lognormal"), rng, z);
    p.real_rate_demand_sensitivity = sample_value(baseline.real_rate_demand_sensitivity,
        rule_for(baseline, registry, "real_rate_demand_sensitivity", 1e-6, inf, "lognormal"), rng, z);
    p.productive_supply_multiplier = sample_value(baseline.productive_supply_multiplier,
        rule_for(baseline, registry, "productive_supply_multiplier", 1e-6, inf, "lognormal"), rng, z);
    p.global_growth_sensitivity = sample_value(baseline.global_growth_sensitivity,
        rule_for(baseline, registry, "global_growth_sensitivity", 1e-6, inf, "lognormal"), rng, z);

    const auto persistence_rule = rule_for(
        baseline, registry, "inflation_persistence", 0.05, 0.98, "normal");
    const auto expectations_rule = rule_for(
        baseline, registry, "inflation_expectations_weight", 0.01, 0.95, "normal");
    p.inflation_persistence = sample_value(
        baseline.inflation_persistence, persistence_rule, rng, z);
    const double baseline_anchor = baseline.inflation_persistence
        + baseline.inflation_expectations_weight;
    if (expectations_rule.distribution == "derived" || !expectations_rule.sampled) {
      const double persistence_lo = std::max(
          persistence_rule.lower, baseline_anchor - expectations_rule.upper);
      const double persistence_hi = std::min(
          persistence_rule.upper, baseline_anchor - expectations_rule.lower);
      if (persistence_lo <= persistence_hi)
        p.inflation_persistence = std::clamp(
            p.inflation_persistence, persistence_lo, persistence_hi);
      p.inflation_expectations_weight = baseline_anchor - p.inflation_persistence;
    } else {
      p.inflation_expectations_weight = sample_value(
          baseline.inflation_expectations_weight, expectations_rule, rng, z);
      const double sampled_anchor = p.inflation_persistence
          + p.inflation_expectations_weight;
      if (sampled_anchor > 1e-12) {
        const double scale = baseline_anchor / sampled_anchor;
        p.inflation_persistence *= scale;
        p.inflation_expectations_weight *= scale;
      }
    }

    p.phillips_curve_slope = sample_value(baseline.phillips_curve_slope,
        rule_for(baseline, registry, "phillips_curve_slope", 1e-6, inf, "lognormal"), rng, z);
    p.fx_pass_through = sample_value(baseline.fx_pass_through,
        rule_for(baseline, registry, "fx_pass_through", 1e-6, inf, "lognormal"), rng, z);
    p.import_price_pass_through = sample_value(baseline.import_price_pass_through,
        rule_for(baseline, registry, "import_price_pass_through", 1e-6, inf, "lognormal"), rng, z);
    p.oil_inflation_sensitivity = sample_value(baseline.oil_inflation_sensitivity,
        rule_for(baseline, registry, "oil_inflation_sensitivity", 1e-6, inf, "lognormal"), rng, z);

    p.canada_trade_drag_scale = sample_value(baseline.canada_trade_drag_scale,
        rule_for(baseline, registry, "canada_trade_drag_scale", 1e-6, inf, "lognormal"), rng, z);
    p.us_retaliation_drag_scale = sample_value(baseline.us_retaliation_drag_scale,
        rule_for(baseline, registry, "us_retaliation_drag_scale", 1e-6, inf, "lognormal"), rng, z);
    p.tariff_revenue_elasticity_scale = sample_value(baseline.tariff_revenue_elasticity_scale,
        rule_for(baseline, registry, "tariff_revenue_elasticity_scale", 1e-6, inf, "lognormal"), rng, z);

    p.output_shock_sd = sample_value(baseline.output_shock_sd,
        rule_for(baseline, registry, "output_shock_sd", 1e-6, inf, "lognormal"), rng, z);
    p.inflation_shock_sd = sample_value(baseline.inflation_shock_sd,
        rule_for(baseline, registry, "inflation_shock_sd", 1e-6, inf, "lognormal"), rng, z);
    p.growth_shock_sd = sample_value(baseline.growth_shock_sd,
        rule_for(baseline, registry, "growth_shock_sd", 1e-6, inf, "lognormal"), rng, z);
    p.us_growth_shock_sd = sample_value(baseline.us_growth_shock_sd,
        rule_for(baseline, registry, "us_growth_shock_sd", 1e-6, inf, "lognormal"), rng, z);
    p.export_shock_sd = sample_value(baseline.export_shock_sd,
        rule_for(baseline, registry, "export_shock_sd", 1e-6, inf, "lognormal"), rng, z);
    p.us_export_shock_sd = sample_value(baseline.us_export_shock_sd,
        rule_for(baseline, registry, "us_export_shock_sd", 1e-6, inf, "lognormal"), rng, z);

    out.push_back(std::move(p));
  }
  return out;
}

inline std::vector<StructuralParameters> draw_structural_parameters(
    const StructuralParameters& baseline, int draws, std::uint64_t seed) {
  return draw_structural_parameters(
      baseline, StructuralParameterRegistry{}, draws, seed);
}

inline double robustness_quantile(std::vector<double> values, double probability) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  probability = std::clamp(probability, 0.0, 1.0);
  const double index = probability * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(index));
  const auto hi = static_cast<std::size_t>(std::ceil(index));
  if (lo == hi) return values[lo];
  const double weight = index - static_cast<double>(lo);
  return values[lo] * (1.0 - weight) + values[hi] * weight;
}

inline std::string classify_robustness(double win_rate) {
  if (win_rate >= 0.80) return "robust";
  if (win_rate >= 0.60) return "moderately-robust";
  if (win_rate >= 0.40) return "fragile";
  return "unstable";
}

}  // namespace cad
