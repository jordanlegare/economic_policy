#pragma once

#include "monte_carlo_backend.hpp"

#include <cstddef>
#include <string>

namespace cad::monte_carlo::opencl {

struct Probe {
  bool library_present = false;
  bool device_present = false;
  bool fp64_supported = false;
  std::size_t max_concurrent_runs = 0;
  std::string device_name;
  std::string detail;
};

Probe probe();

bool run(const Input& input, const std::vector<Innovation>& innovations,
         BatchResult& output, std::string& error);

}  // namespace cad::monte_carlo::opencl
