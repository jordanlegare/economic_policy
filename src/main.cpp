#include "server.hpp"
#include "accelerator_status.hpp"
#include "compute_executor.hpp"
#include "monte_carlo_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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

  return cad::server::run(options);
}
