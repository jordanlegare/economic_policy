#include "monte_carlo_backend.hpp"
#include "monte_carlo_opencl.hpp"

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

  // Regress the queue architecture itself. All lanes share one context/program,
  // but each submission owns its queue, kernel and buffers. The start barrier
  // makes a process-wide run mutex observable as a failed peak-concurrency check.
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
    std::cerr << "OpenCL runtime did not observe concurrent submissions; peak="
              << concurrent_probe.max_concurrent_runs << '\n';
    return 1;
  }

  // Exercise the production qualification and promotion gate on the same
  // runtime. CAD_OPENCL_ALLOW_CPU=1 lets CI use POCL while production still
  // restricts automatic selection to actual GPU devices.
  set_env("CAD_MONTE_CARLO_BACKEND", "gpu");
  const auto selected = cad::monte_carlo::run(input, innovations);
  const auto status = cad::monte_carlo::status();
  if (!status.equivalence_passed) {
    std::cerr << "OpenCL kernel ran but production equivalence gate rejected it; max error="
              << status.max_equivalence_error << " detail=" << status.detail << '\n';
    return 1;
  }
  assert(selected.backend == "opencl-gpu");
  assert(status.active);
  assert(status.gpu_runs >= 1);
  assert(status.max_concurrent_gpu_runs >= 2);
  std::cout << "OpenCL Monte Carlo equivalence passed; max absolute error=" << maximum
            << "; peak concurrent submissions=" << status.max_concurrent_gpu_runs << '\n';
  return 0;
}
