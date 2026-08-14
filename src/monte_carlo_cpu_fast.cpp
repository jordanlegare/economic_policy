#include "monte_carlo_cpu_fast.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace cad::monte_carlo {
namespace {

std::atomic<std::uint64_t> fast_runs{0};
std::atomic<std::uint64_t> avx2_runs{0};
std::atomic<std::uint64_t> scalar_runs{0};

double clamp_value(double x, double lo, double hi) {
  return std::max(lo, std::min(hi, x));
}

void validate_fast(const Input& input, const InnovationBank& innovations) {
  if (input.draws <= 0) throw std::invalid_argument("Monte Carlo draws must be positive");
  const auto expected = static_cast<std::size_t>(input.draws) * kQuarterCount;
  if (innovations.size() != expected)
    throw std::invalid_argument("Monte Carlo innovation count does not match draws x quarters");
}

void accumulate_scalar_draw(const Input& in, const Innovation* innovations,
                            std::size_t draw_index, BatchResult& result) {
  const auto& p = in.parameters;
  auto& aggregate = result.aggregate;

  double rate = in.policy_rate;
  double inf = in.core_inflation;
  double gap = in.output_gap;
  double unemployment = in.unemployment;
  double debt = in.federal_debt_gdp;
  double housing = in.housing_gap;
  double export_change = 0.0;
  double us_export_change = 0.0;
  double cost = in.headline_inflation;
  bool recession = false;

  for (std::size_t q = 0; q < kQuarterCount; ++q) {
    const double rate_target = clamp_value(p.neutral_rate
        + p.rate_inflation_response * (inf - p.inflation_target)
        + p.rate_output_response * gap, .25, 7.0);
    if (q == 0) {
      rate = clamp_value(rate + in.move_bp / 100.0, 0.0, 8.0);
    } else {
      double policy_step = clamp_value(rate_target - rate,
          -p.max_quarterly_rate_step, p.max_quarterly_rate_step);
      if (q == 1 && std::abs(in.move_bp) > 1e-9) {
        const double followup = std::min(.25, p.max_quarterly_rate_step);
        const bool continue_easing = in.move_bp < 0.0 && gap < -.25
            && inf <= p.inflation_target + .35;
        const bool continue_tightening = in.move_bp > 0.0
            && (inf >= p.inflation_target + .50 || gap > .50);
        if (continue_easing) policy_step = std::min(policy_step, -followup);
        if (continue_tightening) policy_step = std::max(policy_step, followup);
      }
      rate = clamp_value(rate + policy_step, 0.0, 8.0);
    }

    const double demand = in.fiscal[q] * (1.0 - in.productive_share)
        * p.fiscal_demand_multiplier
        - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;
    const auto& z = innovations[q];
    export_change = 100.0 * (in.canada_export_quantity_ratio[q] - 1.0)
        + .35 * (in.us_growth - 2.0) + 2.0 * in.diversification[q]
        + z.export_z * p.export_shock_sd;
    us_export_change = 100.0 * (in.us_export_quantity_ratio[q] - 1.0)
        + .30 * (in.gdp_growth - 1.5) + 1.5 * in.deescalation[q]
        + z.us_export_z * p.us_export_shock_sd;
    gap = p.output_persistence * gap + demand - in.trade_drag[q]
        + p.global_growth_sensitivity * (in.global_growth - 2.7)
        + z.output_z * p.output_shock_sd;
    inf = p.inflation_persistence * inf
        + p.inflation_expectations_weight * in.inflation_expectations
        + p.phillips_curve_slope * gap + in.fx_pressure - in.supply[q]
        + p.import_price_pass_through * in.import_price[q]
        - p.oil_inflation_sensitivity * (in.oil_price - 75.0)
        + z.inflation_z * p.inflation_shock_sd;
    const double growth = clamp_value(1.75 + gap - .18 * in.credit_spread
        + in.productive_investment[q] * .24 + z.growth_z * p.growth_shock_sd,
        -3.0, 5.5);
    const double us_growth = clamp_value(in.us_growth
        + .16 * in.productive_investment[q] + .28 * in.deescalation[q]
        - .010 * in.us_tariff[q] - .014 * in.canada_tariff[q]
        - .04 * in.border_friction - .40 * in.us_supply_chain_drag[q]
        + z.us_growth_z * p.us_growth_shock_sd, -3.0, 5.5);
    unemployment = clamp_value(unemployment - .10 * (growth - 1.7)
        + z.unemployment_z * .035, 3.5, 11.0);
    housing = clamp_value(.78 * housing - 1.15 * (rate - p.neutral_rate)
        + .08 * (in.population_growth - 1.2) + z.housing_z * .5,
        -15.0, 30.0);
    debt += (-in.fiscal_balance_gdp + in.fiscal[q] * .8
        + in.relief_cost[q] * .55 + .045 * (rate - p.neutral_rate) * debt
        - .18 * growth) / 4.0;
    cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
        + .14 * std::max(0.0, in.wage_growth - growth)
        + .08 * in.import_price[q];
    recession = recession || growth < 0.0;

    aggregate.rates[q] += rate;
    aggregate.inflation[q] += inf;
    aggregate.growth[q] += growth;
    aggregate.us_growth[q] += us_growth;
    aggregate.debt[q] += debt;
    aggregate.cost[q] += cost;
    aggregate.exports[q] += export_change;
    aggregate.us_exports[q] += us_export_change;

    if (q + 1 == kQuarterCount) {
      aggregate.terminal_inflation[draw_index] = inf;
      aggregate.terminal_debt[draw_index] = debt;
      aggregate.terminal_growth += growth;
      aggregate.terminal_us_growth += us_growth;
      aggregate.terminal_unemployment += unemployment;
      aggregate.terminal_housing += housing;
      aggregate.terminal_cost += cost;
      aggregate.terminal_income += growth - cost + in.targeted_relief[q] * .15;
      aggregate.terminal_exports += export_change;
      aggregate.terminal_us_exports += us_export_change;
    }
  }
  if (recession) aggregate.recessions += 1.0;
}

bool detect_avx2() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int regs[4]{};
  __cpuid(regs, 1);
  const bool osxsave = (regs[2] & (1 << 27)) != 0;
  const bool avx = (regs[2] & (1 << 28)) != 0;
  if (!osxsave || !avx) return false;
  const unsigned __int64 xcr0 = _xgetbv(0);
  if ((xcr0 & 0x6) != 0x6) return false;
  __cpuidex(regs, 7, 0);
  return (regs[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2");
#else
  return false;
#endif
}

}  // namespace

namespace cpu_fast_detail {

bool avx2_runtime_supported() {
  static const bool supported = detect_avx2();
  return supported;
}

}  // namespace cpu_fast_detail

BatchResult run_cpu_fast(const Input& input, const InnovationBank& innovations) {
  validate_fast(input, innovations);
  BatchResult result;
  result.backend = "cpu";
  result.aggregate_encoded = true;
  result.aggregate.sample_count = static_cast<std::size_t>(input.draws);
  result.aggregate.terminal_inflation.resize(result.aggregate.sample_count);
  result.aggregate.terminal_debt.resize(result.aggregate.sample_count);

  fast_runs.fetch_add(1, std::memory_order_relaxed);
  std::size_t processed = 0;
  if (cpu_fast_detail::avx2_runtime_supported() && input.draws >= 4) {
    processed = cpu_fast_detail::accumulate_avx2_prefix(input, innovations, result);
    if (processed > 0) avx2_runs.fetch_add(1, std::memory_order_relaxed);
  }
  if (processed < static_cast<std::size_t>(input.draws)) {
    scalar_runs.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t d = processed; d < static_cast<std::size_t>(input.draws); ++d) {
      accumulate_scalar_draw(input,
          innovations.data() + d * kQuarterCount, d, result);
    }
  }
  return result;
}

CpuFastStatus cpu_fast_status() {
  CpuFastStatus out;
  out.avx2_supported = cpu_fast_detail::avx2_runtime_supported();
  out.runs = fast_runs.load(std::memory_order_relaxed);
  out.avx2_runs = avx2_runs.load(std::memory_order_relaxed);
  out.scalar_runs = scalar_runs.load(std::memory_order_relaxed);
  return out;
}

}  // namespace cad::monte_carlo
