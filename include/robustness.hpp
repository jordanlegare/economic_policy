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
  DrawRule rule{fallback_lower, fallback_upper,
                std::max(0.0, baseline.uncertainty_scale), true,
                fallback_distribution};
  if (const auto* entry = registry.find(name)) {
    rule.lower = entry->lower_bound;
    rule.upper = entry->upper_bound;
    rule.sampled = entry->sampled;
    rule.distribution = entry->distribution;
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
  const double draw = rule.distribution == "lognormal"
      ? baseline * std::exp(rule.sigma * z(rng) - 0.5 * rule.sigma * rule.sigma)
      : baseline * (1.0 + rule.sigma * z(rng));
  return std::clamp(draw, rule.lower, rule.upper);
}

}  // namespace robustness_detail

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

  auto rule = [&](const char* name, double lo, double hi, const char* distribution) {
    return rule_for(baseline, registry, name, lo, hi, distribution);
  };
  auto draw = [&](double value, const char* name, double lo, double hi,
                  const char* distribution) {
    return sample_value(value, rule(name, lo, hi, distribution), rng, z);
  };

  for (int i = 0; i < draws; ++i) {
    StructuralParameters p = baseline;
    p.calibration_id = baseline.calibration_id + ":draw-" + std::to_string(i + 1);

    p.neutral_rate = draw(baseline.neutral_rate, "neutral_rate", .25, 6.0, "normal");
    p.rate_inflation_response = draw(baseline.rate_inflation_response, "rate_inflation_response", 1e-6, inf, "lognormal");
    p.rate_output_response = draw(baseline.rate_output_response, "rate_output_response", 1e-6, inf, "lognormal");
    p.max_quarterly_rate_step = draw(baseline.max_quarterly_rate_step, "max_quarterly_rate_step", 1e-6, inf, "lognormal");
    p.output_persistence = draw(baseline.output_persistence, "output_persistence", .05, .98, "normal");
    p.fiscal_demand_multiplier = draw(baseline.fiscal_demand_multiplier, "fiscal_demand_multiplier", 1e-6, inf, "lognormal");
    p.real_rate_demand_sensitivity = draw(baseline.real_rate_demand_sensitivity, "real_rate_demand_sensitivity", 1e-6, inf, "lognormal");
    p.productive_supply_multiplier = draw(baseline.productive_supply_multiplier, "productive_supply_multiplier", 1e-6, inf, "lognormal");
    p.global_growth_sensitivity = draw(baseline.global_growth_sensitivity, "global_growth_sensitivity", 1e-6, inf, "lognormal");

    const auto persistence_rule = rule("inflation_persistence", .05, .98, "normal");
    const auto expectations_rule = rule("inflation_expectations_weight", .01, .95, "normal");
    p.inflation_persistence = sample_value(
        baseline.inflation_persistence, persistence_rule, rng, z);
    const double anchor = baseline.inflation_persistence
        + baseline.inflation_expectations_weight;
    if (expectations_rule.distribution == "derived" || !expectations_rule.sampled) {
      const double lo = std::max(persistence_rule.lower, anchor - expectations_rule.upper);
      const double hi = std::min(persistence_rule.upper, anchor - expectations_rule.lower);
      if (lo <= hi) p.inflation_persistence = std::clamp(p.inflation_persistence, lo, hi);
      p.inflation_expectations_weight = anchor - p.inflation_persistence;
    } else {
      p.inflation_expectations_weight = sample_value(
          baseline.inflation_expectations_weight, expectations_rule, rng, z);
      const double sampled_anchor = p.inflation_persistence + p.inflation_expectations_weight;
      if (sampled_anchor > 1e-12) {
        const double scale = anchor / sampled_anchor;
        p.inflation_persistence *= scale;
        p.inflation_expectations_weight *= scale;
      }
    }

    p.phillips_curve_slope = draw(baseline.phillips_curve_slope, "phillips_curve_slope", 1e-6, inf, "lognormal");
    p.fx_pass_through = draw(baseline.fx_pass_through, "fx_pass_through", 1e-6, inf, "lognormal");
    p.import_price_pass_through = draw(baseline.import_price_pass_through, "import_price_pass_through", 1e-6, inf, "lognormal");
    p.oil_inflation_sensitivity = draw(baseline.oil_inflation_sensitivity, "oil_inflation_sensitivity", 1e-6, inf, "lognormal");
    p.canada_trade_drag_scale = draw(baseline.canada_trade_drag_scale, "canada_trade_drag_scale", 1e-6, inf, "lognormal");
    p.us_retaliation_drag_scale = draw(baseline.us_retaliation_drag_scale, "us_retaliation_drag_scale", 1e-6, inf, "lognormal");
    p.tariff_revenue_elasticity_scale = draw(baseline.tariff_revenue_elasticity_scale, "tariff_revenue_elasticity_scale", 1e-6, inf, "lognormal");
    p.output_shock_sd = draw(baseline.output_shock_sd, "output_shock_sd", 1e-6, inf, "lognormal");
    p.inflation_shock_sd = draw(baseline.inflation_shock_sd, "inflation_shock_sd", 1e-6, inf, "lognormal");
    p.growth_shock_sd = draw(baseline.growth_shock_sd, "growth_shock_sd", 1e-6, inf, "lognormal");
    p.us_growth_shock_sd = draw(baseline.us_growth_shock_sd, "us_growth_shock_sd", 1e-6, inf, "lognormal");
    p.export_shock_sd = draw(baseline.export_shock_sd, "export_shock_sd", 1e-6, inf, "lognormal");
    p.us_export_shock_sd = draw(baseline.us_export_shock_sd, "us_export_shock_sd", 1e-6, inf, "lognormal");
    out.push_back(std::move(p));
  }
  return out;
}

inline std::vector<StructuralParameters> draw_structural_parameters(
    const StructuralParameters& baseline, int draws, std::uint64_t seed) {
  return draw_structural_parameters(
      baseline, baseline.uncertainty_registry, draws, seed);
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
