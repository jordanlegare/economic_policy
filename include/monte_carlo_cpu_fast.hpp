#pragma once

#include "monte_carlo_backend.hpp"

#include <cstddef>
#include <cstdint>

namespace cad::monte_carlo {

struct CpuFastStatus {
  bool avx2_supported = false;
  std::uint64_t runs = 0;
  std::uint64_t avx2_runs = 0;
  std::uint64_t scalar_runs = 0;
};

// Production CPU path: propagate each draw directly into aggregate sums and
// retain only the terminal inflation/debt samples required by the policy P90s.
// The returned BatchResult uses the same aggregate transport contract as the
// reduced OpenCL backend, so the policy engine remains unchanged.
BatchResult run_cpu_fast(const Input& input, const InnovationBank& innovations);
CpuFastStatus cpu_fast_status();

namespace cpu_fast_detail {

bool avx2_runtime_supported();
std::size_t accumulate_avx2_prefix(
    const Input& input, const InnovationBank& innovations, BatchResult& output);

}  // namespace cpu_fast_detail
}  // namespace cad::monte_carlo
