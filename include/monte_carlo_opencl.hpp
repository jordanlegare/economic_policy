#pragma once

#include "monte_carlo_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cad::monte_carlo::opencl {

struct Probe {
  bool library_present = false;
  bool device_present = false;
  bool fp64_supported = false;
  std::size_t max_concurrent_runs = 0;
  std::size_t pooled_lanes = 0;
  std::size_t resident_innovation_banks = 0;
  std::uint64_t innovation_uploads = 0;
  std::uint64_t lane_reuses = 0;
  std::uint64_t reduced_dispatches = 0;
  std::uint64_t batched_scenarios = 0;
  std::size_t max_batch_scenarios = 0;
  std::uint64_t host_values_read = 0;
  std::string device_name;
  std::string detail;
};

Probe probe();

// Detailed path is retained for deterministic device qualification/tests.
bool run(const Input& input, const InnovationBank& innovations,
         BatchResult& output, std::string& error);

// Production path. Concurrent scenario calls sharing a CRN bank/draw count are
// opportunistically coalesced into one two-dimensional scenario x draw launch,
// then reduced on-device before the compact aggregate encoding is returned.
bool run_reduced(const Input& input, const InnovationBank& innovations,
                 BatchResult& output, std::string& error);

}  // namespace cad::monte_carlo::opencl
