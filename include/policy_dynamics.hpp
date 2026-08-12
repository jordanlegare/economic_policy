#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace cad {

struct PolicyImplementationPaths {
  std::array<double, 12> fiscal{};
  std::array<double, 12> productive_investment{};
  std::array<double, 12> negotiated_relief{};  // percentage points
  std::array<double, 12> targeted_relief{};
  std::array<double, 12> diversification{};
};

inline PolicyImplementationPaths build_policy_implementation_paths(
    double fiscal, double productive_share, double negotiated_relief_percent,
    double targeted_relief, double diversification) {
  // Deterministic implementation rules turn optimized amplitudes into quarterly
  // policy paths. They are model-design assumptions, not extra search controls.
  constexpr std::array<double, 12> fiscal_profile{
      .65, .85, 1.00, 1.00, .95, .90, .80, .70, .60, .50, .40, .30};
  constexpr std::array<double, 12> productive_profile{
      .20, .40, .60, .80, 1.00, 1.00, 1.00, .95, .90, .85, .80, .75};
  constexpr std::array<double, 12> relief_profile{
      .35, .65, .85, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00};
  constexpr std::array<double, 12> targeted_profile{
      .80, 1.00, .95, .85, .75, .65, .55, .45, .35, .25, .15, .10};
  constexpr std::array<double, 12> diversification_profile{
      .15, .30, .45, .60, .75, .85, .92, .97, 1.00, 1.00, 1.00, 1.00};

  PolicyImplementationPaths out;
  const double productive = std::clamp(productive_share, 0.0, 1.0);
  for (std::size_t q = 0; q < out.fiscal.size(); ++q) {
    out.fiscal[q] = fiscal * fiscal_profile[q];
    out.productive_investment[q] = fiscal * productive * productive_profile[q];
    out.negotiated_relief[q] = negotiated_relief_percent * relief_profile[q];
    out.targeted_relief[q] = targeted_relief * targeted_profile[q];
    out.diversification[q] = diversification * diversification_profile[q];
  }
  return out;
}

inline bool macro_stress_regime(const Economy& economy) {
  return economy.credit_spread >= 2.25
      || economy.gdp_growth < 0.0
      || economy.us_tariff_canada >= 30.0
      || economy.canada_retaliatory_tariff >= 15.0;
}

inline double regime_tail_innovation(double z, const StructuralParameters& parameters,
                                     bool stress_regime) {
  double out = z;
  if (std::abs(out) >= std::max(0.5, parameters.shock_tail_threshold))
    out *= std::max(1.0, parameters.shock_tail_scale);
  if (stress_regime)
    out *= std::max(1.0, parameters.stress_regime_shock_scale);
  return out;
}

}  // namespace cad
