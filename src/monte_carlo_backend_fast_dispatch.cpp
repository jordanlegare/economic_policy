#include "monte_carlo_cpu_fast.hpp"
#include "monte_carlo_opencl.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

// Compile the audited detailed CPU/OpenCL dispatcher unchanged, but rename its
// public production entry point and legacy synthetic aggregate helpers. The
// wrapper below reuses its innovation cache, detailed scalar reference and GPU
// qualification machinery while giving production results a compact payload.
namespace cad::monte_carlo::opencl {

bool original_backend_run(const Input& input, const InnovationBank& innovations,
                          BatchResult& output, std::string& error) {
  return run(input, innovations, output, error);
}

}  // namespace cad::monte_carlo::opencl

#define run original_backend_run
#define aggregate_signature original_aggregate_signature
#define for_signature_values original_for_signature_values
#define equivalent_aggregate original_equivalent_aggregate
#define maximum_aggregate_difference original_maximum_aggregate_difference
#include "monte_carlo_backend.cpp"
#undef maximum_aggregate_difference
#undef equivalent_aggregate
#undef for_signature_values
#undef aggregate_signature
#undef run

namespace cad::monte_carlo {
namespace {

bool valid_compact_aggregate(const BatchResult& result) {
  if (!result.aggregate_encoded) return true;
  return result.aggregate.terminal_inflation.size() == result.aggregate.sample_count
      && result.aggregate.terminal_debt.size() == result.aggregate.sample_count;
}

AggregateSignature compact_signature(const BatchResult& result) {
  if (!result.aggregate_encoded) return original_aggregate_signature(result);

  AggregateSignature out;
  const auto& aggregate = result.aggregate;
  out.rates = aggregate.rates;
  out.inflation = aggregate.inflation;
  out.growth = aggregate.growth;
  out.us_growth = aggregate.us_growth;
  out.debt = aggregate.debt;
  out.cost = aggregate.cost;
  out.exports = aggregate.exports;
  out.us_exports = aggregate.us_exports;
  out.terminal_growth = aggregate.terminal_growth;
  out.terminal_us_growth = aggregate.terminal_us_growth;
  out.terminal_unemployment = aggregate.terminal_unemployment;
  out.terminal_housing = aggregate.terminal_housing;
  out.terminal_cost = aggregate.terminal_cost;
  out.terminal_income = aggregate.terminal_income;
  out.terminal_exports = aggregate.terminal_exports;
  out.terminal_us_exports = aggregate.terminal_us_exports;
  out.recessions = aggregate.recessions;

  for (double value : aggregate.terminal_inflation) out.terminal_inflation += value;
  for (double value : aggregate.terminal_debt) out.terminal_debt += value;

  if (!aggregate.terminal_debt.empty()) {
    auto debt = aggregate.terminal_debt;
    auto inflation = aggregate.terminal_inflation;
    const std::size_t index = debt.size() * 9 / 10;
    auto debt_it = debt.begin() + static_cast<std::ptrdiff_t>(index);
    auto inflation_it = inflation.begin() + static_cast<std::ptrdiff_t>(index);
    std::nth_element(debt.begin(), debt_it, debt.end());
    std::nth_element(inflation.begin(), inflation_it, inflation.end());
    out.debt_p90 = *debt_it;
    out.inflation_p90 = *inflation_it;
  }
  return out;
}

bool equivalent_aggregate(const BatchResult& reference, const BatchResult& candidate) {
  if (reference.sample_count() != candidate.sample_count()
      || !valid_compact_aggregate(reference) || !valid_compact_aggregate(candidate)) return false;
  const auto a = compact_signature(reference);
  const auto b = compact_signature(candidate);
  std::vector<double> left;
  std::vector<double> right;
  original_for_signature_values(a, [&](double value) { left.push_back(value); });
  original_for_signature_values(b, [&](double value) { right.push_back(value); });
  if (left.size() != right.size()) return false;
  bool ok = true;
  for (std::size_t i = 0; i < left.size(); ++i)
    ok = ok && equivalent_value(left[i], right[i]);
  return ok;
}

}  // namespace

double maximum_aggregate_difference(const BatchResult& reference,
                                    const BatchResult& candidate) {
  if (reference.sample_count() != candidate.sample_count()
      || !valid_compact_aggregate(reference) || !valid_compact_aggregate(candidate))
    return std::numeric_limits<double>::infinity();
  const auto a = compact_signature(reference);
  const auto b = compact_signature(candidate);
  std::vector<double> left;
  std::vector<double> right;
  original_for_signature_values(a, [&](double value) { left.push_back(value); });
  original_for_signature_values(b, [&](double value) { right.push_back(value); });
  if (left.size() != right.size()) return std::numeric_limits<double>::infinity();
  double maximum = 0.0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (!std::isfinite(left[i]) || !std::isfinite(right[i])) {
      if (left[i] != right[i]) return std::numeric_limits<double>::infinity();
    } else {
      maximum = std::max(maximum, std::abs(left[i] - right[i]));
    }
  }
  return maximum;
}

BatchResult run(const Input& input, const InnovationBank& innovations) {
  validate(input, innovations);
  auto& state = dispatcher_state();
  const Preference requested = preference();
  if (requested == Preference::cpu
      || (requested == Preference::automatic && input.draws < kAutoGpuMinimumDraws)) {
    state.cpu_fallback_runs.fetch_add(1, std::memory_order_relaxed);
    return run_cpu_fast(input, innovations);
  }

  bool qualified = false;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto probe = ensure_probe_locked(state);
    if (probe.device_present && probe.fp64_supported && !state.equivalence_checked) {
      const int gate_draws = std::min(input.draws, 64);
      Input gate_input = input;
      gate_input.draws = gate_draws;
      const auto gate_innovations = innovations.prefix(gate_draws);
      const auto reference = run_cpu(gate_input, gate_innovations);

      BatchResult detailed_candidate;
      std::string detailed_error;
      const bool detailed_ran = opencl::run(
          gate_input, gate_innovations, detailed_candidate, detailed_error);
      const bool detailed_passed = detailed_ran && equivalent(reference, detailed_candidate);
      const double detailed_error_max = detailed_ran
          ? maximum_difference(reference, detailed_candidate)
          : std::numeric_limits<double>::infinity();

      BatchResult reduced_candidate;
      std::string reduced_error;
      const bool reduced_ran = detailed_passed && opencl::run_reduced(
          gate_input, gate_innovations, reduced_candidate, reduced_error);
      const bool reduced_passed = reduced_ran
          && reduced_candidate.aggregate_encoded
          && equivalent_aggregate(reference, reduced_candidate);
      const double reduced_error_max = reduced_ran
          ? maximum_aggregate_difference(reference, reduced_candidate)
          : std::numeric_limits<double>::infinity();

      state.max_equivalence_error = std::max(detailed_error_max, reduced_error_max);
      state.equivalence_passed = detailed_passed && reduced_passed;
      state.equivalence_checked = true;
      if (state.equivalence_passed) {
        state.detail = "OpenCL FP64 backend passed detailed and device-reduction equivalence gates";
      } else if (!detailed_ran) {
        state.detail = "OpenCL backend rejected during detailed equivalence gate: " + detailed_error;
      } else if (!detailed_passed) {
        state.detail = "OpenCL backend rejected: detailed CPU/GPU equivalence tolerance exceeded";
      } else if (!reduced_ran) {
        state.detail = "OpenCL backend rejected during device-reduction gate: " + reduced_error;
      } else {
        state.detail = "OpenCL backend rejected: device-reduced aggregates exceeded equivalence tolerance";
      }
    }

    if (requested == Preference::automatic && state.equivalence_passed
        && !state.performance_checked) {
      constexpr int repetitions = 2;
      constexpr std::size_t max_measured_lanes = 16;
      constexpr int sample_draws = 1024;
      const std::size_t host_lanes = std::max<std::size_t>(1, input.expected_cpu_parallelism);
      const std::size_t lanes = std::min(host_lanes, max_measured_lanes);
      const int perf_draws = std::min(input.draws, sample_draws);
      Input perf_input = input;
      perf_input.draws = perf_draws;
      const auto perf_innovations = innovations.prefix(perf_draws);

      BatchResult warm;
      std::string warm_error;
      bool gpu_ok = opencl::run_reduced(
          perf_input, perf_innovations, warm, warm_error);
      double cpu_us = 0.0;
      double gpu_us = 0.0;
      std::string error = warm_error;
      for (int rep = 0; rep < repetitions && gpu_ok; ++rep) {
        bool cpu_ok = false;
        std::string cpu_error;
        cpu_us += concurrent_elapsed_microseconds(lanes,
            [&](std::size_t, std::string&) {
              auto sink = run_cpu_fast(perf_input, perf_innovations);
              return sink.sample_count() == static_cast<std::size_t>(perf_input.draws)
                  && sink.aggregate_encoded;
            }, cpu_ok, cpu_error);
        if (!cpu_ok) {
          gpu_ok = false;
          error = "Fused CPU reference failed during concurrent throughput gate: " + cpu_error;
          break;
        }

        bool batch_gpu_ok = false;
        std::string batch_gpu_error;
        gpu_us += concurrent_elapsed_microseconds(lanes,
            [&](std::size_t, std::string& lane_error) {
              BatchResult candidate;
              return opencl::run_reduced(
                         perf_input, perf_innovations, candidate, lane_error)
                  && candidate.aggregate_encoded;
            }, batch_gpu_ok, batch_gpu_error);
        if (!batch_gpu_ok) {
          gpu_ok = false;
          error = batch_gpu_error;
        }
      }
      state.performance_checked = true;
      state.performance_lanes = lanes;
      state.performance_draws = perf_draws;
      state.measured_speedup = gpu_ok && gpu_us > 0.0 ? cpu_us / gpu_us : 0.0;
      const double unmeasured_cpu_scale = static_cast<double>(host_lanes)
          / static_cast<double>(lanes);
      state.required_speedup = kAutoGpuMinimumConcurrentSpeedup * unmeasured_cpu_scale;
      state.performance_passed = gpu_ok
          && state.measured_speedup >= state.required_speedup;
      if (!gpu_ok) {
        state.detail = "OpenCL backend rejected during batched concurrent-throughput gate: "
            + error;
      } else if (state.performance_passed) {
        state.detail = "OpenCL FP64 backend passed equivalence and batched concurrent-throughput gates against fused CPU; measured speedup="
            + std::to_string(state.measured_speedup) + "x across "
            + std::to_string(lanes) + " scenario lanes, required="
            + std::to_string(state.required_speedup) + "x";
      } else {
        state.detail = "OpenCL backend retained as available but fused CPU stayed active; batched concurrent speedup="
            + std::to_string(state.measured_speedup) + "x across "
            + std::to_string(lanes) + " scenario lanes is below required="
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
    if (opencl::run_reduced(input, innovations, accelerated, error)) {
      state.gpu_runs.fetch_add(1, std::memory_order_relaxed);
      return accelerated;
    }
    std::lock_guard<std::mutex> lock(state.mutex);
    state.detail = "OpenCL reduced runtime failure; falling back to fused CPU: " + error;
  }
  state.cpu_fallback_runs.fetch_add(1, std::memory_order_relaxed);
  return run_cpu_fast(input, innovations);
}

}  // namespace cad::monte_carlo
