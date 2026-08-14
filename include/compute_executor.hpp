#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cad::compute {

inline std::size_t hardware_worker_count() {
  const unsigned detected = std::thread::hardware_concurrency();
  return detected == 0 ? 4u : static_cast<std::size_t>(detected);
}

class Executor {
 public:
  explicit Executor(std::size_t workers) {
    workers = std::max<std::size_t>(1, workers);
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
      workers_.emplace_back([this] {
        while (true) {
          std::function<void()> job;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop();
          }
          job();
        }
      });
    }
  }

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  ~Executor() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
  }

  template<class Function>
  auto submit(Function&& function)
      -> std::future<std::invoke_result_t<std::decay_t<Function>>> {
    using Result = std::invoke_result_t<std::decay_t<Function>>;
    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::forward<Function>(function));
    auto future = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) throw std::runtime_error("compute executor is stopping");
      jobs_.push([task] { (*task)(); });
    }
    ready_.notify_one();
    return future;
  }

  std::size_t worker_count() const { return workers_.size(); }

 private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> jobs_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};

inline Executor& global_executor() {
  static Executor executor(hardware_worker_count());
  return executor;
}

inline std::atomic<std::size_t>& worker_limit_storage() {
  static std::atomic<std::size_t> limit{0};
  return limit;
}

inline void set_worker_limit(std::size_t workers) {
  worker_limit_storage().store(workers, std::memory_order_relaxed);
}

inline std::size_t worker_capacity() {
  return global_executor().worker_count();
}

inline std::size_t configured_worker_count() {
  const std::size_t limit = worker_limit_storage().load(std::memory_order_relaxed);
  const std::size_t capacity = worker_capacity();
  return limit == 0 ? capacity : std::max<std::size_t>(1, std::min(limit, capacity));
}

inline std::size_t resolve_parallelism(std::size_t task_count) {
  if (task_count == 0) return 0;
  return std::max<std::size_t>(1,
      std::min(configured_worker_count(), task_count));
}

template<class Function>
void parallel_for(std::size_t task_count, Function&& function) {
  const std::size_t lanes = resolve_parallelism(task_count);
  if (lanes == 0) return;
  if (lanes == 1) {
    for (std::size_t i = 0; i < task_count; ++i) function(i);
    return;
  }

  std::atomic<std::size_t> next{0};
  std::mutex error_mutex;
  std::exception_ptr first_error;
  std::vector<std::future<void>> futures;
  futures.reserve(lanes);
  for (std::size_t lane = 0; lane < lanes; ++lane) {
    futures.push_back(global_executor().submit([&] {
      while (true) {
        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= task_count) return;
        try {
          function(index);
        } catch (...) {
          std::lock_guard<std::mutex> lock(error_mutex);
          if (!first_error) first_error = std::current_exception();
          return;
        }
      }
    }));
  }
  for (auto& future : futures) future.get();
  if (first_error) std::rethrow_exception(first_error);
}

}  // namespace cad::compute
