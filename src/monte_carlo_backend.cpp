#include "monte_carlo_backend.hpp"
#include "monte_carlo_opencl.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace cad::monte_carlo {
namespace {

double clamp_value(double x, double lo, double hi) {
  return std::max(lo, std::min(hi, x));
}

double tail_innovation(double z, double threshold, double scale,
                       double stress_scale, bool stress_regime) {
  double out = z;
  if (std::abs(out) >= std::max(0.5, threshold))
    out *= std::max(1.0, scale);
  if (stress_regime) out *= std::max(1.0, stress_scale);
  return out;
}

void validate(const Input& input, const std::vector<Innovation>& innovations) {
  if (input.draws <= 0) throw std::invalid_argument("Monte Carlo draws must be positive");
  const auto expected = static_cast<std::size_t>(input.draws) * kQuarterCount;
  if (innovations.size() != expected)
    throw std::invalid_argument("Monte Carlo innovation count does not match draws x quarters");
}

DrawResult run_cpu_draw(const Input& in, const Innovation* innovations) {
  const auto& p = in.parameters;
  DrawResult out;
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
        const bool continue_easing = in.move_bp < 0.0
            && gap < -.25 && inf <= p.inflation_target + .35;
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
        + .35 * (in.us_growth - 2.0)
        + 2.0 * in.diversification[q] + z.export_z * p.export_shock_sd;
    us_export_change = 100.0 * (in.us_export_quantity_ratio[q] - 1.0)
        + .30 * (in.gdp_growth - 1.5)
        + 1.5 * in.deescalation[q] + z.us_export_z * p.us_export_shock_sd;

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
        + in.productive_investment[q] * .24
        + z.growth_z * p.growth_shock_sd, -3.0, 5.5);
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
        + in.relief_cost[q] * .55
        + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;
    cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
        + .14 * std::max(0.0, in.wage_growth - growth)
        + .08 * in.import_price[q];
    recession = recession || growth < 0.0;

    out.rates[q] = rate;
    out.inflation[q] = inf;
    out.growth[q] = growth;
    out.us_growth[q] = us_growth;
    out.debt[q] = debt;
    out.cost[q] = cost;
    out.exports[q] = export_change;
    out.us_exports[q] = us_export_change;

    if (q + 1 == kQuarterCount) {
      out.terminal_inflation = inf;
      out.terminal_growth = growth;
      out.terminal_us_growth = us_growth;
      out.terminal_unemployment = unemployment;
      out.terminal_debt = debt;
      out.terminal_housing = housing;
      out.terminal_cost = cost;
      out.terminal_income = growth - cost + in.targeted_relief[q] * .15;
      out.terminal_exports = export_change;
      out.terminal_us_exports = us_export_change;
    }
  }
  out.recession = recession;
  return out;
}

enum class Preference { automatic, cpu, gpu };

Preference preference() {
  const char* raw = std::getenv("CAD_MONTE_CARLO_BACKEND");
  if (!raw || !*raw) return Preference::automatic;
  const std::string value(raw);
  if (value == "cpu" || value == "CPU") return Preference::cpu;
  if (value == "gpu" || value == "GPU" || value == "opencl") return Preference::gpu;
  return Preference::automatic;
}

struct DispatcherState {
  std::mutex mutex;
  bool probed = false;
  bool equivalence_checked = false;
  bool equivalence_passed = false;
  bool performance_checked = false;
  bool performance_passed = false;
  opencl::Probe probe;
  double max_equivalence_error = 0.0;
  double measured_speedup = 0.0;
  double required_speedup = 0.0;
  std::string detail;
  std::atomic<std::uint64_t> gpu_runs{0};
  std::atomic<std::uint64_t> cpu_fallback_runs{0};
};

DispatcherState& dispatcher_state() {
  static DispatcherState state;
  return state;
}

opencl::Probe ensure_probe_locked(DispatcherState& state) {
  if (!state.probed) {
    state.probe = opencl::probe();
    state.probed = true;
    state.detail = state.probe.detail;
  }
  return state.probe;
}

bool equivalent_value(double left, double right) {
  if (left == right) return true;
  if (!std::isfinite(left) || !std::isfinite(right)) return false;
  const double scale = std::max({1.0, std::abs(left), std::abs(right)});
  return std::abs(left - right) <= kGpuEquivalenceTolerance * scale;
}

bool equivalent(const BatchResult& reference, const BatchResult& candidate) {
  if (reference.draws.size() != candidate.draws.size()) return false;
  for (std::size_t d = 0; d < reference.draws.size(); ++d) {
    const auto& a = reference.draws[d];
    const auto& b = candidate.draws[d];
    for (std::size_t q = 0; q < kQuarterCount; ++q) {
      if (!equivalent_value(a.rates[q], b.rates[q])
          || !equivalent_value(a.inflation[q], b.inflation[q])
          || !equivalent_value(a.growth[q], b.growth[q])
          || !equivalent_value(a.us_growth[q], b.us_growth[q])
          || !equivalent_value(a.debt[q], b.debt[q])
          || !equivalent_value(a.cost[q], b.cost[q])
          || !equivalent_value(a.exports[q], b.exports[q])
          || !equivalent_value(a.us_exports[q], b.us_exports[q])) return false;
    }
    if (!equivalent_value(a.terminal_inflation, b.terminal_inflation)
        || !equivalent_value(a.terminal_growth, b.terminal_growth)
        || !equivalent_value(a.terminal_us_growth, b.terminal_us_growth)
        || !equivalent_value(a.terminal_unemployment, b.terminal_unemployment)
        || !equivalent_value(a.terminal_debt, b.terminal_debt)
        || !equivalent_value(a.terminal_housing, b.terminal_housing)
        || !equivalent_value(a.terminal_cost, b.terminal_cost)
        || !equivalent_value(a.terminal_income, b.terminal_income)
        || !equivalent_value(a.terminal_exports, b.terminal_exports)
        || !equivalent_value(a.terminal_us_exports, b.terminal_us_exports)
        || a.recession != b.recession) return false;
  }
  return true;
}

template<class Function>
double elapsed_microseconds(Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  function();
  const auto stop = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::micro>(stop - start).count();
}

}  // namespace

std::vector<Innovation> generate_innovations(
    std::uint64_t seed, int draws, double output_inflation_correlation,
    double tail_threshold, double tail_scale, double stress_scale,
    bool stress_regime) {
  if (draws <= 0) throw std::invalid_argument("Monte Carlo draws must be positive");
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
  std::vector<Innovation> out(static_cast<std::size_t>(draws) * kQuarterCount);
  const double rho = clamp_value(output_inflation_correlation, -.999, .999);
  const double independent_scale = std::sqrt(std::max(0.0, 1.0 - rho * rho));

  for (int d = 0; d < draws; ++d) {
    for (std::size_t q = 0; q < kQuarterCount; ++q) {
      auto& z = out[static_cast<std::size_t>(d) * kQuarterCount + q];
      z.export_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.us_export_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
      const double raw_output_z = shock(rng);
      const double inflation_independent_z = shock(rng);
      const double raw_inflation_z = rho * raw_output_z
          + independent_scale * inflation_independent_z;
      z.output_z = tail_innovation(raw_output_z, tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.inflation_z = tail_innovation(raw_inflation_z, tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.growth_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.us_growth_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.unemployment_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
      z.housing_z = tail_innovation(shock(rng), tail_threshold, tail_scale,
          stress_scale, stress_regime);
    }
  }
  return out;
}

BatchResult run_cpu(const Input& input, const std::vector<Innovation>& innovations) {
  validate(input, innovations);
  BatchResult result;
  result.backend = "cpu";
  result.draws.resize(static_cast<std::size_t>(input.draws));
  for (int d = 0; d < input.draws; ++d) {
    result.draws[static_cast<std::size_t>(d)] = run_cpu_draw(
        input, innovations.data() + static_cast<std::size_t>(d) * kQuarterCount);
  }
  return result;
}

double maximum_difference(const BatchResult& reference, const BatchResult& candidate) {
  if (reference.draws.size() != candidate.draws.size())
    return std::numeric_limits<double>::infinity();
  double maximum = 0.0;
  const auto record = [&](double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b)) {
      if (a != b) maximum = std::numeric_limits<double>::infinity();
      return;
    }
    maximum = std::max(maximum, std::abs(a - b));
  };
  for (std::size_t d = 0; d < reference.draws.size(); ++d) {
    const auto& a = reference.draws[d];
    const auto& b = candidate.draws[d];
    for (std::size_t q = 0; q < kQuarterCount; ++q) {
      record(a.rates[q], b.rates[q]);
      record(a.inflation[q], b.inflation[q]);
      record(a.growth[q], b.growth[q]);
      record(a.us_growth[q], b.us_growth[q]);
      record(a.debt[q], b.debt[q]);
      record(a.cost[q], b.cost[q]);
      record(a.exports[q], b.exports[q]);
      record(a.us_exports[q], b.us_exports[q]);
    }
    record(a.terminal_inflation, b.terminal_inflation);
    record(a.terminal_growth, b.terminal_growth);
    record(a.terminal_us_growth, b.terminal_us_growth);
    record(a.terminal_unemployment, b.terminal_unemployment);
    record(a.terminal_debt, b.terminal_debt);
    record(a.terminal_housing, b.terminal_housing);
    record(a.terminal_cost, b.terminal_cost);
    record(a.terminal_income, b.terminal_income);
    record(a.terminal_exports, b.terminal_exports);
    record(a.terminal_us_exports, b.terminal_us_exports);
    if (a.recession != b.recession)
      maximum = std::numeric_limits<double>::infinity();
  }
  return maximum;
}

bool run_opencl_for_equivalence_test(
    const Input& input, const std::vector<Innovation>& innovations,
    BatchResult& output, std::string& error) {
  validate(input, innovations);
  return opencl::run(input, innovations, output, error);
}

BatchResult run(const Input& input, const std::vector<Innovation>& innovations) {
  validate(input, innovations);
  auto& state = dispatcher_state();
  const Preference requested = preference();
  if (requested == Preference::cpu
      || (requested == Preference::automatic && input.draws < kAutoGpuMinimumDraws)) {
    state.cpu_fallback_runs.fetch_add(1, std::memory_order_relaxed);
    return run_cpu(input, innovations);
  }

  bool qualified = false;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto probe = ensure_probe_locked(state);
    if (probe.device_present && probe.fp64_supported && !state.equivalence_checked) {
      const int gate_draws = std::min(input.draws, 64);
      Input gate_input = input;
      gate_input.draws = gate_draws;
      std::vector<Innovation> gate_innovations(
          innovations.begin(), innovations.begin()
              + static_cast<std::ptrdiff_t>(gate_draws * static_cast<int>(kQuarterCount)));
      const auto reference = run_cpu(gate_input, gate_innovations);
      BatchResult candidate;
      std::string error;
      const bool ran = opencl::run(gate_input, gate_innovations, candidate, error);
      state.max_equivalence_error = ran
          ? maximum_difference(reference, candidate)
          : std::numeric_limits<double>::infinity();
      state.equivalence_passed = ran && equivalent(reference, candidate);
      state.equivalence_checked = true;
      if (state.equivalence_passed) {
        state.detail = "OpenCL FP64 backend passed deterministic CPU equivalence gate";
      } else if (!ran) {
        state.detail = "OpenCL backend rejected during equivalence gate: " + error;
      } else {
        state.detail = "OpenCL backend rejected: CPU/GPU equivalence tolerance exceeded";
      }
    }

    if (requested == Preference::automatic
        && state.equivalence_passed && !state.performance_checked) {
      constexpr int repetitions = 3;
      double cpu_us = 0.0;
      double gpu_us = 0.0;
      bool gpu_ok = true;
      BatchResult sink;
      std::string error;
      for (int i = 0; i < repetitions; ++i) {
        cpu_us += elapsed_microseconds([&] { sink = run_cpu(input, innovations); });
      }
      for (int i = 0; i < repetitions; ++i) {
        gpu_us += elapsed_microseconds([&] {
          BatchResult candidate;
          if (!opencl::run(input, innovations, candidate, error)) {
            gpu_ok = false;
            return;
          }
          sink = std::move(candidate);
        });
        if (!gpu_ok) break;
      }
      state.performance_checked = true;
      state.measured_speedup = gpu_ok && gpu_us > 0.0
          ? cpu_us / gpu_us : 0.0;
      const double cpu_lanes = static_cast<double>(
          std::max<std::size_t>(1, input.expected_cpu_parallelism));
      state.required_speedup = kAutoGpuMinimumSpeedupPerCpuLane * cpu_lanes;
      state.performance_passed = gpu_ok
          && state.measured_speedup >= state.required_speedup;
      if (!gpu_ok) {
        state.detail = "OpenCL backend rejected during throughput gate: " + error;
      } else if (state.performance_passed) {
        state.detail = "OpenCL FP64 backend passed equivalence and aggregate-throughput gates; measured speedup="
            + std::to_string(state.measured_speedup) + "x, required="
            + std::to_string(state.required_speedup) + "x";
      } else {
        state.detail = "OpenCL backend retained as available but CPU stayed active; measured speedup="
            + std::to_string(state.measured_speedup) + "x is below aggregate CPU opportunity threshold="
            + std::to_string(state.required_speedup) + "x";
      }
    }

    qualified = state.equivalence_passed
        && (requested == Preference::gpu
            || (state.performance_checked && state.performance_passed));
  }

  if (qualified) {
    BatchResult accelerated;
    std::string error;
    if (opencl::run(input, innovations, accelerated, error)) {
      state.gpu_runs.fetch_add(1, std::memory_order_relaxed);
      return accelerated;
    }
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.detail = "OpenCL runtime failure; falling back to CPU: " + error;
    }
  }

  state.cpu_fallback_runs.fetch_add(1, std::memory_order_relaxed);
  return run_cpu(input, innovations);
}

BackendStatus status() {
  auto& state = dispatcher_state();
  std::lock_guard<std::mutex> lock(state.mutex);
  const auto probe = ensure_probe_locked(state);
  const Preference requested = preference();
  BackendStatus out;
  out.opencl_library_present = probe.library_present;
  out.device_present = probe.device_present;
  out.fp64_supported = probe.fp64_supported;
  out.equivalence_checked = state.equivalence_checked;
  out.equivalence_passed = state.equivalence_passed;
  out.performance_checked = state.performance_checked;
  out.performance_passed = state.performance_passed;
  out.active = state.equivalence_passed
      && (requested == Preference::gpu || state.performance_passed);
  out.active_backend = out.active ? "opencl-gpu" : "cpu-multicore";
  out.device_name = probe.device_name;
  out.detail = state.detail.empty() ? probe.detail : state.detail;
  out.max_equivalence_error = state.max_equivalence_error;
  out.measured_speedup = state.measured_speedup;
  out.required_speedup = state.required_speedup;
  out.gpu_runs = state.gpu_runs.load(std::memory_order_relaxed);
  out.cpu_fallback_runs = state.cpu_fallback_runs.load(std::memory_order_relaxed);
  return out;
}

}  // namespace cad::monte_carlo
