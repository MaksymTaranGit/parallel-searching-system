#include "core/thread_pool.h"
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t num_threads) {
  if (num_threads == 0) {
    num_threads = 1;
  }

  workers_.reserve(num_threads);
  for (std::size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back(&ThreadPool::worker_loop, this);
  }
}

ThreadPool::~ThreadPool() {
  { //тут мютекс бо коли stop змінюється під тим же мютексом під яким cv перевіряє цей stop, то не буде дедлоку
    std::lock_guard<std::mutex> lock(queue_mtx_);
    stop_ = true;
  }
  task_cv_.notify_all();

  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::enqueue(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    if (stop_) {
      throw std::runtime_error("Cannot enqueue task into a stopped ThreadPool");
    }
    tasks_.push(std::move(task));
  }
  task_cv_.notify_one();
}

void ThreadPool::wait_all() {
  std::unique_lock<std::mutex> lock(queue_mtx_);
  done_cv_.wait(lock, [this]() {
    return tasks_.empty() && active_tasks_ == 0;
  });
}

std::size_t ThreadPool::thread_count() const {
  return workers_.size();
}

void ThreadPool::worker_loop() {
  while (true) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      task_cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });

      if (stop_ && tasks_.empty()) {
        return;
      }

      task = std::move(tasks_.front());
      tasks_.pop();
      ++active_tasks_;
    }

    try {
      task();
    } catch (...) {

    }

    {
      std::lock_guard<std::mutex> lock(queue_mtx_);
      --active_tasks_;
      if (tasks_.empty() && active_tasks_ == 0) {
        done_cv_.notify_all();
      }
    }
  }
}