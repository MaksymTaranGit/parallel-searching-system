#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t num_threads);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  void enqueue(std::function<void()> task);
  void wait_all();

  std::size_t thread_count() const;

 private:
  void worker_loop();

  std::queue<std::function<void()>> tasks_;

  std::mutex queue_mtx_;
  std::condition_variable task_cv_;
  std::condition_variable done_cv_;
  std::size_t active_tasks_ = 0;
  bool stop_ = false;

  std::vector<std::thread> workers_;
};