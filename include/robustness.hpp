#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace cad {

// Draw structural calibrations independently from the stochastic macro shocks.
// The same seed and baseline calibration always produce the same ensemble.
inline std::vector<StructuralParameters> draw_structural_parameters(
    const StructuralParameters& baseline, int draws, std::uint64_t seed) {
  std::vector<StructuralParameters> out;
  if (draws <= 0) return out;
  out.reserve(static_cast<std::size_t>(draws));

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> z(0.0, 1.0);
  const double sigma = std::max(0.0, baseline.uncertainty_scale);

  auto positive = [&](double x, double lo = 1e-6) {
    return std::max(lo, x * std::exp(sigma * z(rng) - 0.5 * sigma * sigma));
  };
  auto bounded = [&](double x, double lo, double hi) {
    return std::clamp(x * (1.0 + sigma * z(rng)), lo, hi);
  };

  for (int i = 0; i < draws; ++i) {
    StructuralParameters p = baseline;
    p.calibration_id = baseline.calibration_id + ":draw-" + std::to_string(i + 1);

    p.neutral_rate = bounded(baseline.neutral_rate, 0.25, 6.0);
    p.rate_inflation_response = positive(baseline.rate_inflation_response);
    p.rate_output_response = positive(baseline.rate_output_response);
    p.max_quarterly_rate_step = positive(baseline.max_quarterly_rate_step);

    p.output_persistence = bounded(baseline.output_persistence, 0.05, 0.98);
    p.fiscal_demand_multiplier = positive(baseline.fiscal_demand_multiplier);
    p.real_rate_demand_sensitivity = positive(baseline.real_rate_demand_sensitivity);
    p.productive_supply_multiplier = positive(baseline.productive_supply_multiplier);
    p.global_growth_sensitivity = positive(baseline.global_growth_sensitivity);

    p.inflation_persistence = bounded(baseline.inflation_persistence, 0.05, 0.98);
    p.inflation_expectations_weight = bounded(baseline.inflation_expectations_weight, 0.01, 0.95);
    p.phillips_curve_slope = positive(baseline.phillips_curve_slope);
    p.fx_pass_through = positive(baseline.fx_pass_through);
    p.import_price_pass_through = positive(baseline.import_price_pass_through);
    p.oil_inflation_sensitivity = positive(baseline.oil_inflation_sensitivity);

    p.canada_trade_drag_scale = positive(baseline.canada_trade_drag_scale);
    p.us_retaliation_drag_scale = positive(baseline.us_retaliation_drag_scale);
    p.tariff_revenue_elasticity_scale = positive(baseline.tariff_revenue_elasticity_scale);

    p.output_shock_sd = positive(baseline.output_shock_sd);
    p.inflation_shock_sd = positive(baseline.inflation_shock_sd);
    p.growth_shock_sd = positive(baseline.growth_shock_sd);
    p.us_growth_shock_sd = positive(baseline.us_growth_shock_sd);
    p.export_shock_sd = positive(baseline.export_shock_sd);
    p.us_export_shock_sd = positive(baseline.us_export_shock_sd);

    out.push_back(std::move(p));
  }
  return out;
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
