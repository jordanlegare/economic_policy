#include "thread_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

int main() {
  using namespace std::chrono_literals;
  cad::server::ThreadPool pool(2, 4);
  assert(pool.capacity() == 4);
  std::mutex mutex;
  std::condition_variable ready;
  bool release_first = false;
  std::atomic<bool> first_started{false};
  std::atomic<bool> second_completed{false};

  assert(pool.submit([&] {
    first_started = true;
    std::unique_lock<std::mutex> lock(mutex);
    ready.wait(lock, [&] { return release_first; });
  }));

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!first_started && std::chrono::steady_clock::now() < deadline) {}
  assert(first_started);
  assert(pool.active() >= 1);

  assert(pool.submit([&] { second_completed = true; }));
  const auto second_deadline = std::chrono::steady_clock::now() + 2s;
  while (!second_completed && std::chrono::steady_clock::now() < second_deadline) {}
  assert(second_completed);  // second worker progressed while the first was blocked.

  // A bad request/model job must not terminate its worker or the process.
  assert(pool.submit([] { throw std::runtime_error("synthetic worker failure"); }));
  const auto failure_deadline = std::chrono::steady_clock::now() + 2s;
  while (pool.failed() != 1 && std::chrono::steady_clock::now() < failure_deadline) {}
  assert(pool.failed() == 1);

  std::atomic<bool> after_failure_completed{false};
  assert(pool.submit([&] { after_failure_completed = true; }));
  const auto recovery_deadline = std::chrono::steady_clock::now() + 2s;
  while (!after_failure_completed && std::chrono::steady_clock::now() < recovery_deadline) {}
  assert(after_failure_completed);  // the worker pool remains usable after an exception.

  {
    std::lock_guard<std::mutex> lock(mutex);
    release_first = true;
  }
  ready.notify_all();

  const auto idle_deadline = std::chrono::steady_clock::now() + 2s;
  while (pool.active() != 0 && std::chrono::steady_clock::now() < idle_deadline) {}
  assert(pool.active() == 0);

  std::cout << "thread pool concurrency and exception isolation tests passed\n";
  return 0;
}
