#include "monte_carlo_backend.hpp"
#include "monte_carlo_opencl.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace {

cad::monte_carlo::Input fixture(int draws) {
  cad::monte_carlo::Input in;
  in.draws = draws;
  in.move_bp = 25.0;
  in.productive_share = .75;
  in.policy_rate = 2.75;
  in.core_inflation = 2.85;
  in.output_gap = -.55;
  in.unemployment = 6.5;
  in.federal_debt_gdp = 42.0;
  in.housing_gap = 8.0;
  in.us_growth = 1.9;
  in.gdp_growth = 1.4;
  in.population_growth = 2.0;
  in.credit_spread = 1.5;
  in.fiscal_balance_gdp = -1.3;
  in.wage_growth = 3.4;
  in.headline_inflation = 2.55;
  in.global_growth = 2.65;
  in.inflation_expectations = 2.25;
  in.oil_price = 76.0;
  in.border_friction = 2.5;
  in.fx_pressure = .012;
  in.parameters.neutral_rate = 2.5;
  in.parameters.global_growth_sensitivity = .08;
  for (std::size_t q = 0; q < cad::monte_carlo::kQuarterCount; ++q) {
    const double t = static_cast<double>(q) / 11.0;
    in.fiscal[q] = .22 + .05 * (1.0 - t);
    in.productive_investment[q] = .10 + .15 * t;
    in.targeted_relief[q] = .18 * (1.0 - .75 * t);
    in.diversification[q] = .05 + .20 * t;
    in.deescalation[q] = .10 + .55 * t;
    in.us_tariff[q] = 40.0 - 20.0 * t;
    in.canada_tariff[q] = 10.0 - 5.0 * t;
    in.trade_drag[q] = .55 - .25 * t;
    in.us_supply_chain_drag[q] = .12 - .05 * t;
    in.import_price[q] = .62 - .22 * t;
    in.supply[q] = .02 + .06 * t;
    in.relief_cost[q] = .18 - .08 * t;
    in.canada_export_quantity_ratio[q] = .93 + .04 * t;
    in.us_export_quantity_ratio[q] = .96 + .025 * t;
  }
  return in;
}

void set_env(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

}  // namespace

int main() {
  auto input = fixture(64);
  const auto innovations = cad::monte_carlo::generate_innovations(
      20260810, input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto reference = cad::monte_carlo::run_cpu(input, innovations);

  cad::monte_carlo::BatchResult device;
  std::string error;
  if (!cad::monte_carlo::run_opencl_for_equivalence_test(
          input, innovations, device, error)) {
    const char* required = std::getenv("CAD_OPENCL_REQUIRED");
    if (required && std::string(required) == "1") {
      std::cerr << "OpenCL equivalence runtime required but unavailable: " << error << '\n';
      return 1;
    }
    std::cout << "OpenCL equivalence test skipped: " << error << '\n';
    return 0;
  }

  const double maximum = cad::monte_carlo::maximum_difference(reference, device);
  assert(std::isfinite(maximum));
  assert(!device.aggregate_encoded);
  const auto after_first = cad::monte_carlo::opencl::probe();
  assert(after_first.innovation_uploads == 1);
  assert(after_first.resident_innovation_banks == 1);

  cad::monte_carlo::BatchResult reduced;
  std::string reduced_error;
  assert(cad::monte_carlo::run_opencl_reduced_for_equivalence_test(
      input, innovations, reduced, reduced_error));
  assert(reduced.aggregate_encoded);
  const double reduced_maximum = cad::monte_carlo::maximum_aggregate_difference(reference, reduced);
  assert(std::isfinite(reduced_maximum));
  const auto after_reduced = cad::monte_carlo::opencl::reduced_probe();
  assert(after_reduced.innovation_uploads == 1);
  assert(after_reduced.resident_innovation_banks == 1);
  assert(after_reduced.reduced_dispatches >= 1);
  assert(after_reduced.batched_scenarios >= 1);
  assert(after_reduced.host_values_read >= 105 + 2 * static_cast<std::uint64_t>(input.draws));

  auto concurrent_input = fixture(256);
  const auto concurrent_innovations = cad::monte_carlo::generate_innovations(
      20260811, concurrent_input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto concurrent_reference = cad::monte_carlo::run_cpu(
      concurrent_input, concurrent_innovations);
  constexpr std::size_t lanes = 4;
  std::array<cad::monte_carlo::BatchResult, lanes> concurrent_results{};
  std::array<std::string, lanes> concurrent_errors{};
  std::array<bool, lanes> concurrent_ok{};
  std::atomic<std::size_t> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> workers;
  workers.reserve(lanes);
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    workers.emplace_back([&, lane] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      concurrent_ok[lane] = cad::monte_carlo::run_opencl_for_equivalence_test(
          concurrent_input, concurrent_innovations,
          concurrent_results[lane], concurrent_errors[lane]);
    });
  }
  while (ready.load(std::memory_order_acquire) < lanes) std::this_thread::yield();
  go.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    if (!concurrent_ok[lane]) {
      std::cerr << "Concurrent OpenCL lane " << lane << " failed: "
                << concurrent_errors[lane] << '\n';
      return 1;
    }
    assert(std::isfinite(cad::monte_carlo::maximum_difference(
        concurrent_reference, concurrent_results[lane])));
  }
  const auto concurrent_probe = cad::monte_carlo::opencl::probe();
  if (concurrent_probe.max_concurrent_runs < 2) {
    std::cerr << "OpenCL runtime did not observe concurrent detailed submissions; peak="
              << concurrent_probe.max_concurrent_runs << '\n';
    return 1;
  }
  assert(concurrent_probe.pooled_lanes >= 2);
  assert(concurrent_probe.resident_innovation_banks == 2);
  assert(concurrent_probe.innovation_uploads == 2);

  cad::monte_carlo::BatchResult reused;
  std::string reuse_error;
  assert(cad::monte_carlo::run_opencl_for_equivalence_test(
      input, innovations, reused, reuse_error));
  assert(std::isfinite(cad::monte_carlo::maximum_difference(reference, reused)));
  const auto reuse_probe = cad::monte_carlo::opencl::probe();
  assert(reuse_probe.innovation_uploads == 2);
  assert(reuse_probe.lane_reuses >= 1);

  // The production search fans out 700-draw scenarios concurrently. The new
  // reduced path must coalesce those requests into a two-dimensional launch
  // and return policy-equivalent aggregates while retaining only debt and
  // inflation tails per draw on the host.
  auto batch_input = fixture(cad::monte_carlo::kSharedInnovationBankMinimumDraws);
  const auto batch_innovations = cad::monte_carlo::generate_innovations(
      20260812, batch_input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto batch_reference = cad::monte_carlo::run_cpu(batch_input, batch_innovations);
  const auto before_batch = cad::monte_carlo::opencl::reduced_probe();
  std::array<cad::monte_carlo::Input, lanes> batch_inputs{};
  std::array<cad::monte_carlo::BatchResult, lanes> batch_references{};
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    batch_inputs[lane] = batch_input;
    batch_inputs[lane].move_bp += static_cast<double>(lane) * 5.0;
    batch_references[lane] = cad::monte_carlo::run_cpu(batch_inputs[lane], batch_innovations);
  }
  std::array<cad::monte_carlo::BatchResult, lanes> batch_results{};
  std::array<std::string, lanes> batch_errors{};
  std::array<bool, lanes> batch_ok{};
  ready.store(0, std::memory_order_relaxed);
  go.store(false, std::memory_order_relaxed);
  workers.clear();
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    workers.emplace_back([&, lane] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      batch_ok[lane] = cad::monte_carlo::run_opencl_reduced_for_equivalence_test(
          batch_inputs[lane], batch_innovations, batch_results[lane], batch_errors[lane]);
      if (batch_ok[lane]) {
        const double diff = cad::monte_carlo::maximum_aggregate_difference(
            batch_references[lane], batch_results[lane]);
        if (!std::isfinite(diff)) batch_ok[lane] = false;
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < lanes) std::this_thread::yield();
  go.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    if (!batch_ok[lane]) {
      std::cerr << "Batched reduced OpenCL lane " << lane << " failed: "
                << batch_errors[lane] << '\n';
      return 1;
    }
    assert(batch_results[lane].aggregate_encoded);
  }
  (void)batch_reference;
  const auto batch_probe = cad::monte_carlo::opencl::reduced_probe();
  assert(batch_probe.innovation_uploads == before_batch.innovation_uploads + 1);
  assert(batch_probe.max_batch_scenarios >= 2);
  assert(batch_probe.batched_scenarios >= before_batch.batched_scenarios + lanes);
  assert(batch_probe.reduced_dispatches < before_batch.reduced_dispatches + lanes);
  const std::uint64_t compact_values_per_scenario =
      105 + 2 * static_cast<std::uint64_t>(batch_input.draws);
  assert(batch_probe.host_values_read
      >= before_batch.host_values_read + compact_values_per_scenario * lanes);

  set_env("CAD_MONTE_CARLO_BACKEND", "gpu");
  const auto selected = cad::monte_carlo::run(input, innovations);
  const auto status = cad::monte_carlo::status();
  if (!status.equivalence_passed) {
    std::cerr << "OpenCL kernel/reduction ran but production equivalence gate rejected it; max error="
              << status.max_equivalence_error << " detail=" << status.detail << '\n';
    return 1;
  }
  assert(selected.backend == "opencl-gpu");
  assert(selected.aggregate_encoded);
  assert(status.active);
  assert(status.gpu_runs >= 1);
  assert(status.max_concurrent_gpu_runs >= 2);
  assert(status.gpu_innovation_uploads == 2);
  assert(status.pooled_opencl_lanes >= 2);
  assert(status.resident_innovation_banks == 2);
  assert(status.opencl_lane_reuses >= 1);
  const auto final_reduced_probe = cad::monte_carlo::opencl::reduced_probe();
  assert(final_reduced_probe.innovation_uploads == 2);
  assert(final_reduced_probe.reduced_dispatches >= 1);
  assert(final_reduced_probe.batched_scenarios >= lanes);
  assert(final_reduced_probe.max_batch_scenarios >= 2);
  assert(final_reduced_probe.host_values_read >= compact_values_per_scenario * lanes);
  std::cout << "OpenCL Monte Carlo equivalence passed; max detailed error=" << maximum
            << "; max aggregate error=" << reduced_maximum
            << "; peak detailed submissions=" << status.max_concurrent_gpu_runs
            << "; max scenario batch=" << final_reduced_probe.max_batch_scenarios
            << "; reduced dispatches=" << final_reduced_probe.reduced_dispatches << '\n';
  return 0;
}
