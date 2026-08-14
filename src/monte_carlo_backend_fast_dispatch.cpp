#include "monte_carlo_cpu_fast.hpp"
#include "monte_carlo_opencl.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>

// Compile the audited detailed CPU/OpenCL dispatcher unchanged, but rename its
// public run entry point. The production wrapper below reuses every private
// equivalence/qualification helper from that translation unit while swapping
// only the CPU throughput/fallback path to the fused implementation.
namespace cad::monte_carlo::opencl {

bool original_backend_run(const Input& input, const InnovationBank& innovations,
                          BatchResult& output, std::string& error) {
  return run(input, innovations, output, error);
}

}  // namespace cad::monte_carlo::opencl

#define run original_backend_run
#include "monte_carlo_backend.cpp"
#undef run

namespace cad::monte_carlo {

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
              return sink.draws.size() == static_cast<std::size_t>(perf_input.draws)
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
