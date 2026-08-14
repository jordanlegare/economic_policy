#include "server.hpp"
#include "accelerator_status.hpp"
#include "compute_executor.hpp"
#include "monte_carlo_backend.hpp"
#include "search_acceleration.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

bool parse_integer(const std::string& value, int& out) {
  try {
    std::size_t used = 0;
    const int parsed = std::stoi(value, &used);
    if (used != value.size()) return false;
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

std::uint64_t delta(std::uint64_t current, std::uint64_t previous) {
  return current >= previous ? current - previous : 0;
}

double milliseconds(std::uint64_t nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

void report_search_profile(std::atomic<bool>& stop) {
  auto previous = cad::search_acceleration::snapshot();
  auto previous_backend = cad::monte_carlo::status();

  while (!stop.load(std::memory_order_relaxed)) {
    for (int tenth = 0; tenth < 10 && !stop.load(std::memory_order_relaxed); ++tenth)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (stop.load(std::memory_order_relaxed)) break;

    const auto current = cad::search_acceleration::snapshot();
    const std::uint64_t network_calls = delta(current.trade_network_calls, previous.trade_network_calls);
    const std::uint64_t source_calls = delta(current.trade_source_calls, previous.trade_source_calls);
    const std::uint64_t ledger_calls = delta(current.bilateral_ledger_calls, previous.bilateral_ledger_calls);
    const std::uint64_t monte_calls = delta(current.monte_carlo_calls, previous.monte_carlo_calls);
    const std::uint64_t parallel_blocks = delta(current.policy_parallel_blocks, previous.policy_parallel_blocks);
    if (network_calls == 0 && source_calls == 0 && ledger_calls == 0
        && monte_calls == 0 && parallel_blocks == 0) {
      previous = current;
      continue;
    }

    const auto backend = cad::monte_carlo::status();
    const std::uint64_t network_hits = delta(
        current.trade_network_cache_hits, previous.trade_network_cache_hits);
    const std::uint64_t source_hits = delta(
        current.trade_source_cache_hits, previous.trade_source_cache_hits);
    const std::uint64_t network_computations = delta(
        current.trade_network_computations, previous.trade_network_computations);
    const std::uint64_t source_computations = delta(
        current.trade_source_computations, previous.trade_source_computations);
    const std::uint64_t parallel_items = delta(
        current.policy_parallel_items, previous.policy_parallel_items);

    std::ostringstream line;
    line << std::fixed << std::setprecision(1)
         << "GLOBAL SEARCH PROFILE 1s | parallel=" << parallel_blocks
         << " blocks/" << parallel_items << " items, wall="
         << milliseconds(delta(current.policy_parallel_wall_ns,
                               previous.policy_parallel_wall_ns)) << " ms"
         << " | trade-network=" << network_calls << " calls ("
         << network_hits << " cached, " << network_computations << " computed), worker="
         << milliseconds(delta(current.trade_network_compute_ns,
                               previous.trade_network_compute_ns)) << " ms"
         << " | sector-source=" << source_calls << " calls ("
         << source_hits << " cached, " << source_computations << " computed), worker="
         << milliseconds(delta(current.trade_source_compute_ns,
                               previous.trade_source_compute_ns)) << " ms"
         << " | bilateral-ledger=" << ledger_calls << " calls, worker="
         << milliseconds(delta(current.bilateral_ledger_ns,
                               previous.bilateral_ledger_ns)) << " ms"
         << " | MonteCarlo=" << monte_calls << " calls, worker="
         << milliseconds(delta(current.monte_carlo_ns,
                               previous.monte_carlo_ns)) << " ms"
         << " | backend=" << backend.active_backend
         << ", GPU runs=" << delta(backend.gpu_runs, previous_backend.gpu_runs)
         << ", CPU fallbacks=" << delta(backend.cpu_fallback_runs,
                                         previous_backend.cpu_fallback_runs);
    if (backend.performance_checked)
      line << ", qualified speedup=" << backend.measured_speedup << 'x';
    std::cout << line.str() << '\n';

    previous = current;
    previous_backend = backend;
  }
}

}  // namespace

int main(int argc, char** argv) {
  cad::server::ServerOptions options;
#ifdef _WIN32
  options.launch_browser = true;
#else
  options.launch_browser = false;
#endif

  int compute_workers = 0;
  bool port_set = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bind-all") {
      options.bind_all = true;
    } else if (arg == "--no-browser") {
      options.launch_browser = false;
    } else if (arg == "--browser") {
      options.launch_browser = true;
    } else if (arg == "--auth-token") {
      if (i + 1 >= argc) {
        std::cerr << "--auth-token requires a value\n";
        return 2;
      }
      options.auth_token = argv[++i];
    } else if (arg.rfind("--auth-token=", 0) == 0) {
      options.auth_token = arg.substr(std::string("--auth-token=").size());
    } else if (arg == "--workers") {
      if (i + 1 >= argc || !parse_integer(argv[++i], options.workers)) {
        std::cerr << "--workers requires an integer\n";
        return 2;
      }
    } else if (arg.rfind("--workers=", 0) == 0) {
      if (!parse_integer(arg.substr(std::string("--workers=").size()), options.workers)) {
        std::cerr << "--workers requires an integer\n";
        return 2;
      }
    } else if (arg == "--compute-workers") {
      if (i + 1 >= argc || !parse_integer(argv[++i], compute_workers)) {
        std::cerr << "--compute-workers requires a non-negative integer (0 = auto)\n";
        return 2;
      }
    } else if (arg.rfind("--compute-workers=", 0) == 0) {
      if (!parse_integer(arg.substr(std::string("--compute-workers=").size()), compute_workers)) {
        std::cerr << "--compute-workers requires a non-negative integer (0 = auto)\n";
        return 2;
      }
    } else if (!port_set && parse_integer(arg, options.port)) {
      port_set = true;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return 2;
    }
  }

  if (compute_workers < 0) {
    std::cerr << "--compute-workers must be non-negative (0 = auto)\n";
    return 2;
  }
  cad::compute::set_worker_limit(static_cast<std::size_t>(compute_workers));

  if (options.auth_token.empty()) {
    if (const char* token = std::getenv("CAD_POLICY_STUDIO_TOKEN"))
      options.auth_token = token;
  }

  const auto accelerator = cad::accelerator::detect();
  if (accelerator.gpu_present) {
    std::cout << "GPU detected: " << accelerator.provider << " ("
              << accelerator.device_count << " device"
              << (accelerator.device_count == 1 ? "" : "s") << ", "
              << accelerator.total_vram_bytes / (1024ull * 1024ull) << " MiB VRAM)\n";
  } else {
    std::cout << "GPU detected: none\n";
  }

  const auto monte_carlo = cad::monte_carlo::status();
  if (monte_carlo.device_present && monte_carlo.fp64_supported) {
    std::cout << "Monte Carlo accelerator candidate: "
              << (monte_carlo.device_name.empty() ? "OpenCL FP64 device" : monte_carlo.device_name)
              << "; CPU/GPU equivalence and throughput qualification runs on the first large evaluation\n";
  } else {
    std::cout << "Monte Carlo backend: cpu-multicore (" << monte_carlo.detail << ")\n";
  }

  if (cad::search_acceleration::trade_cache_enabled())
    std::cout << "Global-search deterministic trade cache: enabled\n";
  if (cad::search_acceleration::logging_enabled())
    std::cout << "Global-search live profiler: enabled (set CAD_GLOBAL_SEARCH_PROFILE=0 to disable)\n";

  std::atomic<bool> profile_stop{false};
  std::thread profile_thread;
  if (cad::search_acceleration::logging_enabled())
    profile_thread = std::thread(report_search_profile, std::ref(profile_stop));

  const int result = cad::server::run(options);
  profile_stop.store(true, std::memory_order_relaxed);
  if (profile_thread.joinable()) profile_thread.join();
  return result;
}
