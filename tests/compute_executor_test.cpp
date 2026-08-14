#include "compute_executor.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
  using cad::compute::configured_worker_count;
  using cad::compute::parallel_for;
  using cad::compute::set_worker_limit;
  using cad::compute::worker_capacity;

  assert(worker_capacity() >= 1);
  set_worker_limit(0);
  assert(configured_worker_count() == worker_capacity());

  std::vector<std::size_t> values(2048, 0);
  parallel_for(values.size(), [&](std::size_t i) {
    values[i] = i * i + 17;
  });
  for (std::size_t i = 0; i < values.size(); ++i)
    assert(values[i] == i * i + 17);

  // Concurrent callers must share the same machine-wide executor safely rather
  // than spawning nested per-request pools. Each call owns disjoint output.
  std::vector<std::size_t> left(1024, 0), right(1024, 0);
  auto& executor = cad::compute::global_executor();
  auto first = executor.submit([&] {
    for (std::size_t i = 0; i < left.size(); ++i) left[i] = i + 1;
  });
  auto second = executor.submit([&] {
    for (std::size_t i = 0; i < right.size(); ++i) right[i] = 3 * i + 5;
  });
  first.get();
  second.get();
  for (std::size_t i = 0; i < left.size(); ++i) {
    assert(left[i] == i + 1);
    assert(right[i] == 3 * i + 5);
  }

  set_worker_limit(1);
  assert(configured_worker_count() == 1);
  std::fill(values.begin(), values.end(), 0);
  parallel_for(values.size(), [&](std::size_t i) { values[i] = i + 9; });
  for (std::size_t i = 0; i < values.size(); ++i) assert(values[i] == i + 9);

  set_worker_limit(0);
  std::cout << "compute executor concurrency tests passed\n";
  return 0;
}
