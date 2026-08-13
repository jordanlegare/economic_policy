#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace cad::server {

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t workers, std::size_t max_queue = 64)
      : max_queue_(max_queue) {
    workers = workers == 0 ? 1 : workers;
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

          active_jobs_.fetch_add(1, std::memory_order_relaxed);
          try {
            job();
          } catch (...) {
            // A request/model exception is an individual job failure, not a
            // process-fatal condition. The request boundary may log/report the
            // error separately; the worker must remain available for later work.
            failed_jobs_.fetch_add(1, std::memory_order_relaxed);
          }
          active_jobs_.fetch_sub(1, std::memory_order_relaxed);
        }
      });
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
  }

  bool submit(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_ || jobs_.size() >= max_queue_) return false;
      jobs_.push(std::move(job));
    }
    ready_.notify_one();
    return true;
  }

  std::size_t worker_count() const { return workers_.size(); }

  std::size_t queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
  }

  std::size_t active() const {
    return active_jobs_.load(std::memory_order_relaxed);
  }

  std::size_t failed() const {
    return failed_jobs_.load(std::memory_order_relaxed);
  }

 private:
  std::size_t max_queue_ = 64;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> jobs_;
  std::vector<std::thread> workers_;
  std::atomic<std::size_t> active_jobs_{0};
  std::atomic<std::size_t> failed_jobs_{0};
  bool stopping_ = false;
};

}  // namespace cad::server
