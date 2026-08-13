#include "thread_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

int main() {
  using namespace std::chrono_literals;
  cad::server::ThreadPool pool(2, 4);
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

  assert(pool.submit([&] { second_completed = true; }));
  const auto second_deadline = std::chrono::steady_clock::now() + 2s;
  while (!second_completed && std::chrono::steady_clock::now() < second_deadline) {}
  assert(second_completed);  // second worker progressed while the first was blocked.

  {
    std::lock_guard<std::mutex> lock(mutex);
    release_first = true;
  }
  ready.notify_all();

  std::cout << "thread pool concurrency tests passed\n";
  return 0;
}
