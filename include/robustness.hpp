#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
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
                           double standardized_draw) {
  if (!rule.sampled || rule.distribution == "fixed"
      || rule.distribution == "derived" || rule.sigma <= 0.0)
    return std::clamp(baseline, rule.lower, rule.upper);
  const double draw = rule.distribution == "lognormal"
      ? baseline * std::exp(rule.sigma * standardized_draw
                            - 0.5 * rule.sigma * rule.sigma)
      : baseline * (1.0 + rule.sigma * standardized_draw);
  return std::clamp(draw, rule.lower, rule.upper);
}

inline std::vector<std::string> sampled_names(
    const StructuralParameterRegistry& registry) {
  std::vector<std::string> names;
  for (const auto& entry : registry.entries) {
    if (entry.sampled && entry.distribution != "fixed"
        && entry.distribution != "derived")
      names.push_back(entry.name);
  }
  return names;
}

inline std::vector<std::vector<double>> correlation_matrix(
    const StructuralParameterRegistry& registry,
    const std::vector<std::string>& names) {
  const std::size_t n = names.size();
  std::vector<std::vector<double>> matrix(n, std::vector<double>(n, 0.0));
  std::map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < n; ++i) {
    matrix[i][i] = 1.0;
    index.emplace(names[i], i);
  }
  for (const auto& pair : registry.correlations) {
    const auto left = index.find(pair.left);
    const auto right = index.find(pair.right);
    if (left == index.end() || right == index.end())
      throw std::invalid_argument("correlation references unsampled structural parameter");
    if (!std::isfinite(pair.correlation) || pair.correlation < -1.0
        || pair.correlation > 1.0)
      throw std::invalid_argument("invalid structural correlation coefficient");
    matrix[left->second][right->second] = pair.correlation;
    matrix[right->second][left->second] = pair.correlation;
  }
  return matrix;
}

inline std::vector<std::vector<double>> cholesky(
    const std::vector<std::vector<double>>& matrix) {
  const std::size_t n = matrix.size();
  std::vector<std::vector<double>> lower(n, std::vector<double>(n, 0.0));
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j <= i; ++j) {
      double value = matrix[i][j];
      for (std::size_t k = 0; k < j; ++k) value -= lower[i][k] * lower[j][k];
      if (i == j) {
        if (value < -1e-10)
          throw std::invalid_argument("structural correlation matrix is not positive semidefinite");
        lower[i][j] = std::sqrt(std::max(0.0, value));
      } else if (lower[j][j] > 1e-12) {
        lower[i][j] = value / lower[j][j];
      } else if (std::abs(value) > 1e-10) {
        throw std::invalid_argument("singular structural correlation matrix is inconsistent");
      }
    }
  }
  return lower;
}

inline bool correlation_matrix_valid(const StructuralParameterRegistry& registry) {
  try {
    const auto names = sampled_names(registry);
    (void)cholesky(correlation_matrix(registry, names));
    return true;
  } catch (...) {
    return false;
  }
}

inline std::string dependence_mode(const StructuralParameterRegistry& registry) {
  return registry.correlations.empty()
      ? "independent-with-derived-constraints"
      : "declared-gaussian-copula-with-derived-constraints";
}

inline std::map<std::string, double> standardized_draws(
    const StructuralParameterRegistry& registry, std::mt19937_64& rng,
    std::normal_distribution<double>& normal) {
  const auto names = sampled_names(registry);
  const auto lower = cholesky(correlation_matrix(registry, names));
  std::vector<double> independent(names.size(), 0.0);
  for (double& value : independent) value = normal(rng);
  std::map<std::string, double> draws;
  for (std::size_t i = 0; i < names.size(); ++i) {
    double value = 0.0;
    for (std::size_t j = 0; j <= i; ++j) value += lower[i][j] * independent[j];
    draws.emplace(names[i], value);
  }
  return draws;
}

inline double standardized_for(const std::map<std::string, double>& draws,
                               const std::string& name,
                               std::mt19937_64& rng,
                               std::normal_distribution<double>& normal) {
  const auto it = draws.find(name);
  return it == draws.end() ? normal(rng) : it->second;
}

}  // namespace robustness_detail

inline bool structural_sampling_correlation_matrix_valid(
    const StructuralParameterRegistry& registry) {
  return robustness_detail::correlation_matrix_valid(registry);
}

inline std::string structural_sampling_dependence_mode(
    const StructuralParameterRegistry& registry) {
  return robustness_detail::dependence_mode(registry);
}

inline std::vector<StructuralParameters> draw_structural_parameters(
    const StructuralParameters& baseline,
    const StructuralParameterRegistry& registry,
    int draws, std::uint64_t seed) {
  using robustness_detail::rule_for;
  using robustness_detail::sample_value;
  std::vector<StructuralParameters> out;
  if (draws <= 0) return out;
  if (!structural_sampling_correlation_matrix_valid(registry))
    throw std::invalid_argument("invalid structural uncertainty correlation matrix");
  out.reserve(static_cast<std::size_t>(draws));
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> normal(0.0, 1.0);
  const double inf = std::numeric_limits<double>::infinity();

  auto rule = [&](const char* name, double lo, double hi, const char* distribution) {
    return rule_for(baseline, registry, name, lo, hi, distribution);
  };

  for (int i = 0; i < draws; ++i) {
    StructuralParameters p = baseline;
    p.calibration_id = baseline.calibration_id + ":draw-" + std::to_string(i + 1);
    const auto z = robustness_detail::standardized_draws(registry, rng, normal);
    auto draw = [&](double value, const char* name, double lo, double hi,
                    const char* distribution) {
      return sample_value(value, rule(name, lo, hi, distribution),
          robustness_detail::standardized_for(z, name, rng, normal));
    };

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
        baseline.inflation_persistence, persistence_rule,
        robustness_detail::standardized_for(z, "inflation_persistence", rng, normal));
    const double anchor = baseline.inflation_persistence
        + baseline.inflation_expectations_weight;
    if (expectations_rule.distribution == "derived" || !expectations_rule.sampled) {
      const double lo = std::max(persistence_rule.lower, anchor - expectations_rule.upper);
      const double hi = std::min(persistence_rule.upper, anchor - expectations_rule.lower);
      if (lo <= hi) p.inflation_persistence = std::clamp(p.inflation_persistence, lo, hi);
      p.inflation_expectations_weight = anchor - p.inflation_persistence;
    } else {
      p.inflation_expectations_weight = sample_value(
          baseline.inflation_expectations_weight, expectations_rule,
          robustness_detail::standardized_for(z, "inflation_expectations_weight", rng, normal));
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

    p.network_supplier_demand_transmission = draw(baseline.network_supplier_demand_transmission, "network_supplier_demand_transmission", 0.0, .95, "normal");
    p.network_input_cost_incidence = draw(baseline.network_input_cost_incidence, "network_input_cost_incidence", 0.0, 1.0, "normal");
    p.network_downstream_cost_transmission = draw(baseline.network_downstream_cost_transmission, "network_downstream_cost_transmission", 0.0, .95, "normal");
    p.network_price_cost_pass_through = draw(baseline.network_price_cost_pass_through, "network_price_cost_pass_through", 0.0, 1.5, "normal");
    p.network_output_cost_base = draw(baseline.network_output_cost_base, "network_output_cost_base", 1e-6, 1.0, "lognormal");
    p.network_output_cost_cyclical = draw(baseline.network_output_cost_cyclical, "network_output_cost_cyclical", 1e-6, 1.0, "lognormal");
    p.network_jobs_output_base = draw(baseline.network_jobs_output_base, "network_jobs_output_base", 0.0, 1.0, "normal");
    p.network_jobs_output_exposure = draw(baseline.network_jobs_output_exposure, "network_jobs_output_exposure", 0.0, 1.5, "normal");

    p.output_shock_sd = draw(baseline.output_shock_sd, "output_shock_sd", 1e-6, inf, "lognormal");
    p.inflation_shock_sd = draw(baseline.inflation_shock_sd, "inflation_shock_sd", 1e-6, inf, "lognormal");
    p.growth_shock_sd = draw(baseline.growth_shock_sd, "growth_shock_sd", 1e-6, inf, "lognormal");
    p.us_growth_shock_sd = draw(baseline.us_growth_shock_sd, "us_growth_shock_sd", 1e-6, inf, "lognormal");
    p.export_shock_sd = draw(baseline.export_shock_sd, "export_shock_sd", 1e-6, inf, "lognormal");
    p.us_export_shock_sd = draw(baseline.us_export_shock_sd, "us_export_shock_sd", 1e-6, inf, "lognormal");
    p.shock_tail_threshold = draw(baseline.shock_tail_threshold, "shock_tail_threshold", .5, 5.0, "normal");
    p.shock_tail_scale = draw(baseline.shock_tail_scale, "shock_tail_scale", 1.0, 5.0, "lognormal");
    p.stress_regime_shock_scale = draw(baseline.stress_regime_shock_scale, "stress_regime_shock_scale", 1.0, 4.0, "lognormal");
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
