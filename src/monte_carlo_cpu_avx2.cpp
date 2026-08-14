#include "monte_carlo_cpu_fast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#if defined(CAD_AVX2_COMPILED) \
    && (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#define CAD_CPU_FAST_AVX2 1
#else
#define CAD_CPU_FAST_AVX2 0
#endif

namespace cad::monte_carlo::cpu_fast_detail {

#if CAD_CPU_FAST_AVX2
namespace {

__m256d set4(double a, double b, double c, double d) {
  return _mm256_set_pd(d, c, b, a);
}

__m256d clamp4(__m256d value, double lo, double hi) {
  return _mm256_max_pd(_mm256_set1_pd(lo),
      _mm256_min_pd(value, _mm256_set1_pd(hi)));
}

void add4(__m256d value, double& sum) {
  alignas(32) double lanes[4];
  _mm256_store_pd(lanes, value);
  sum += lanes[0];
  sum += lanes[1];
  sum += lanes[2];
  sum += lanes[3];
}

void add4(__m256d value, std::array<double, kQuarterCount>& sums,
          std::size_t quarter) {
  add4(value, sums[quarter]);
}

__m256d innovation4(const Innovation* a, const Innovation* b,
                    const Innovation* c, const Innovation* d,
                    double Innovation::*member) {
  return set4(a->*member, b->*member, c->*member, d->*member);
}

}  // namespace
#endif

std::size_t accumulate_avx2_prefix(
    const Input& in, const InnovationBank& innovations, BatchResult& result) {
#if !CAD_CPU_FAST_AVX2
  (void)in;
  (void)innovations;
  (void)result;
  return 0;
#else
  const std::size_t draws = static_cast<std::size_t>(in.draws);
  const std::size_t vectorized = draws - draws % 4;
  if (vectorized == 0) return 0;

  const auto& p = in.parameters;
  auto& aggregate = result.draws.back();
  const __m256d zero = _mm256_setzero_pd();
  const __m256d neutral_rate = _mm256_set1_pd(p.neutral_rate);
  const __m256d inflation_target = _mm256_set1_pd(p.inflation_target);
  const __m256d rate_inflation_response = _mm256_set1_pd(p.rate_inflation_response);
  const __m256d rate_output_response = _mm256_set1_pd(p.rate_output_response);
  const __m256d output_persistence = _mm256_set1_pd(p.output_persistence);
  const __m256d real_rate_sensitivity = _mm256_set1_pd(p.real_rate_demand_sensitivity);
  const __m256d inflation_persistence = _mm256_set1_pd(p.inflation_persistence);
  const __m256d expectations_weight = _mm256_set1_pd(p.inflation_expectations_weight);
  const __m256d phillips_slope = _mm256_set1_pd(p.phillips_curve_slope);
  const __m256d output_shock_sd = _mm256_set1_pd(p.output_shock_sd);
  const __m256d inflation_shock_sd = _mm256_set1_pd(p.inflation_shock_sd);
  const __m256d growth_shock_sd = _mm256_set1_pd(p.growth_shock_sd);
  const __m256d us_growth_shock_sd = _mm256_set1_pd(p.us_growth_shock_sd);
  const __m256d export_shock_sd = _mm256_set1_pd(p.export_shock_sd);
  const __m256d us_export_shock_sd = _mm256_set1_pd(p.us_export_shock_sd);

  for (std::size_t base = 0; base < vectorized; base += 4) {
    __m256d rate = _mm256_set1_pd(in.policy_rate);
    __m256d inf = _mm256_set1_pd(in.core_inflation);
    __m256d gap = _mm256_set1_pd(in.output_gap);
    __m256d unemployment = _mm256_set1_pd(in.unemployment);
    __m256d debt = _mm256_set1_pd(in.federal_debt_gdp);
    __m256d housing = _mm256_set1_pd(in.housing_gap);
    __m256d cost = _mm256_set1_pd(in.headline_inflation);
    __m256d export_change = zero;
    __m256d us_export_change = zero;
    __m256d recession_mask = zero;

    for (std::size_t q = 0; q < kQuarterCount; ++q) {
      const auto* z0 = innovations.data() + (base + 0) * kQuarterCount + q;
      const auto* z1 = innovations.data() + (base + 1) * kQuarterCount + q;
      const auto* z2 = innovations.data() + (base + 2) * kQuarterCount + q;
      const auto* z3 = innovations.data() + (base + 3) * kQuarterCount + q;

      const __m256d rate_target = clamp4(_mm256_add_pd(neutral_rate,
          _mm256_add_pd(
              _mm256_mul_pd(rate_inflation_response,
                  _mm256_sub_pd(inf, inflation_target)),
              _mm256_mul_pd(rate_output_response, gap))), .25, 7.0);
      if (q == 0) {
        rate = clamp4(_mm256_add_pd(rate,
            _mm256_set1_pd(in.move_bp / 100.0)), 0.0, 8.0);
      } else {
        __m256d policy_step = clamp4(_mm256_sub_pd(rate_target, rate),
            -p.max_quarterly_rate_step, p.max_quarterly_rate_step);
        if (q == 1 && std::abs(in.move_bp) > 1e-9) {
          const double followup = std::min(.25, p.max_quarterly_rate_step);
          if (in.move_bp < 0.0) {
            const __m256d gap_mask = _mm256_cmp_pd(
                gap, _mm256_set1_pd(-.25), _CMP_LT_OQ);
            const __m256d inf_mask = _mm256_cmp_pd(
                inf, _mm256_set1_pd(p.inflation_target + .35), _CMP_LE_OQ);
            const __m256d easing = _mm256_and_pd(gap_mask, inf_mask);
            const __m256d eased = _mm256_min_pd(
                policy_step, _mm256_set1_pd(-followup));
            policy_step = _mm256_blendv_pd(policy_step, eased, easing);
          } else if (in.move_bp > 0.0) {
            const __m256d inf_mask = _mm256_cmp_pd(
                inf, _mm256_set1_pd(p.inflation_target + .50), _CMP_GE_OQ);
            const __m256d gap_mask = _mm256_cmp_pd(
                gap, _mm256_set1_pd(.50), _CMP_GT_OQ);
            const __m256d tightening = _mm256_or_pd(inf_mask, gap_mask);
            const __m256d tightened = _mm256_max_pd(
                policy_step, _mm256_set1_pd(followup));
            policy_step = _mm256_blendv_pd(policy_step, tightened, tightening);
          }
        }
        rate = clamp4(_mm256_add_pd(rate, policy_step), 0.0, 8.0);
      }

      const __m256d demand = _mm256_sub_pd(
          _mm256_set1_pd(in.fiscal[q] * (1.0 - in.productive_share)
              * p.fiscal_demand_multiplier),
          _mm256_mul_pd(_mm256_sub_pd(rate, neutral_rate), real_rate_sensitivity));
      const __m256d export_z = innovation4(z0, z1, z2, z3, &Innovation::export_z);
      const __m256d us_export_z = innovation4(z0, z1, z2, z3, &Innovation::us_export_z);
      const __m256d output_z = innovation4(z0, z1, z2, z3, &Innovation::output_z);
      const __m256d inflation_z = innovation4(z0, z1, z2, z3, &Innovation::inflation_z);
      const __m256d growth_z = innovation4(z0, z1, z2, z3, &Innovation::growth_z);
      const __m256d us_growth_z = innovation4(z0, z1, z2, z3, &Innovation::us_growth_z);
      const __m256d unemployment_z = innovation4(z0, z1, z2, z3, &Innovation::unemployment_z);
      const __m256d housing_z = innovation4(z0, z1, z2, z3, &Innovation::housing_z);

      export_change = _mm256_add_pd(
          _mm256_set1_pd(100.0 * (in.canada_export_quantity_ratio[q] - 1.0)
              + .35 * (in.us_growth - 2.0) + 2.0 * in.diversification[q]),
          _mm256_mul_pd(export_z, export_shock_sd));
      us_export_change = _mm256_add_pd(
          _mm256_set1_pd(100.0 * (in.us_export_quantity_ratio[q] - 1.0)
              + .30 * (in.gdp_growth - 1.5) + 1.5 * in.deescalation[q]),
          _mm256_mul_pd(us_export_z, us_export_shock_sd));

      gap = _mm256_add_pd(
          _mm256_add_pd(_mm256_mul_pd(output_persistence, gap), demand),
          _mm256_add_pd(
              _mm256_set1_pd(-in.trade_drag[q]
                  + p.global_growth_sensitivity * (in.global_growth - 2.7)),
              _mm256_mul_pd(output_z, output_shock_sd)));
      inf = _mm256_add_pd(
          _mm256_add_pd(
              _mm256_mul_pd(inflation_persistence, inf),
              _mm256_mul_pd(expectations_weight,
                  _mm256_set1_pd(in.inflation_expectations))),
          _mm256_add_pd(
              _mm256_mul_pd(phillips_slope, gap),
              _mm256_add_pd(
                  _mm256_set1_pd(in.fx_pressure - in.supply[q]
                      + p.import_price_pass_through * in.import_price[q]
                      - p.oil_inflation_sensitivity * (in.oil_price - 75.0)),
                  _mm256_mul_pd(inflation_z, inflation_shock_sd))));

      const __m256d growth = clamp4(_mm256_add_pd(
          _mm256_set1_pd(1.75 - .18 * in.credit_spread
              + in.productive_investment[q] * .24),
          _mm256_add_pd(gap, _mm256_mul_pd(growth_z, growth_shock_sd))),
          -3.0, 5.5);
      const __m256d us_growth = clamp4(_mm256_add_pd(
          _mm256_set1_pd(in.us_growth + .16 * in.productive_investment[q]
              + .28 * in.deescalation[q] - .010 * in.us_tariff[q]
              - .014 * in.canada_tariff[q] - .04 * in.border_friction
              - .40 * in.us_supply_chain_drag[q]),
          _mm256_mul_pd(us_growth_z, us_growth_shock_sd)), -3.0, 5.5);

      unemployment = clamp4(_mm256_add_pd(
          _mm256_sub_pd(unemployment,
              _mm256_mul_pd(_mm256_set1_pd(.10),
                  _mm256_sub_pd(growth, _mm256_set1_pd(1.7)))),
          _mm256_mul_pd(unemployment_z, _mm256_set1_pd(.035))), 3.5, 11.0);
      housing = clamp4(_mm256_add_pd(
          _mm256_add_pd(_mm256_mul_pd(_mm256_set1_pd(.78), housing),
              _mm256_mul_pd(_mm256_set1_pd(-1.15),
                  _mm256_sub_pd(rate, neutral_rate))),
          _mm256_add_pd(_mm256_set1_pd(.08 * (in.population_growth - 1.2)),
              _mm256_mul_pd(housing_z, _mm256_set1_pd(.5)))), -15.0, 30.0);

      debt = _mm256_add_pd(debt, _mm256_mul_pd(_mm256_set1_pd(.25),
          _mm256_add_pd(
              _mm256_set1_pd(-in.fiscal_balance_gdp + in.fiscal[q] * .8
                  + in.relief_cost[q] * .55),
              _mm256_add_pd(
                  _mm256_mul_pd(_mm256_set1_pd(.045),
                      _mm256_mul_pd(_mm256_sub_pd(rate, neutral_rate), debt)),
                  _mm256_mul_pd(_mm256_set1_pd(-.18), growth)))));

      const __m256d housing_cost = _mm256_max_pd(zero,
          _mm256_mul_pd(housing, _mm256_set1_pd(.1)));
      const __m256d wage_gap = _mm256_max_pd(zero,
          _mm256_sub_pd(_mm256_set1_pd(in.wage_growth), growth));
      cost = _mm256_add_pd(
          _mm256_add_pd(_mm256_mul_pd(_mm256_set1_pd(.56), inf),
              _mm256_mul_pd(_mm256_set1_pd(.22), housing_cost)),
          _mm256_add_pd(_mm256_mul_pd(_mm256_set1_pd(.14), wage_gap),
              _mm256_set1_pd(.08 * in.import_price[q])));

      recession_mask = _mm256_or_pd(recession_mask,
          _mm256_cmp_pd(growth, zero, _CMP_LT_OQ));

      add4(rate, aggregate.rates, q);
      add4(inf, aggregate.inflation, q);
      add4(growth, aggregate.growth, q);
      add4(us_growth, aggregate.us_growth, q);
      add4(debt, aggregate.debt, q);
      add4(cost, aggregate.cost, q);
      add4(export_change, aggregate.exports, q);
      add4(us_export_change, aggregate.us_exports, q);

      if (q + 1 == kQuarterCount) {
        alignas(32) double terminal_inf[4];
        alignas(32) double terminal_debt[4];
        _mm256_store_pd(terminal_inf, inf);
        _mm256_store_pd(terminal_debt, debt);
        for (std::size_t lane = 0; lane < 4; ++lane) {
          auto& tail = result.draws[base + lane];
          tail.terminal_inflation = terminal_inf[lane];
          tail.terminal_debt = terminal_debt[lane];
        }
        add4(growth, aggregate.terminal_growth);
        add4(us_growth, aggregate.terminal_us_growth);
        add4(unemployment, aggregate.terminal_unemployment);
        add4(housing, aggregate.terminal_housing);
        add4(cost, aggregate.terminal_cost);
        add4(_mm256_add_pd(_mm256_sub_pd(growth, cost),
            _mm256_set1_pd(in.targeted_relief[q] * .15)), aggregate.terminal_income);
        add4(export_change, aggregate.terminal_exports);
        add4(us_export_change, aggregate.terminal_us_exports);
      }
    }

    const int recession_bits = _mm256_movemask_pd(recession_mask);
    for (std::size_t lane = 0; lane < 4; ++lane)
      result.draws[base + lane].recession = (recession_bits & (1 << lane)) != 0;
  }
  return vectorized;
#endif
}

}  // namespace cad::monte_carlo::cpu_fast_detail
