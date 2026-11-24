#pragma once

#include <mutex>
#include <string>
#include <vector>

class OllamaClient {
 public:
  static constexpr int default_lock_timeout_ms = 15000;
  explicit OllamaClient(std::string base_url, std::string model_name, int lock_timeout_ms = default_lock_timeout_ms);

  bool ping() const;

  std::vector<std::string> expand_query(const std::string& query) const;

 private:
  std::string base_url_;
  std::string model_name_;
  int lock_timeout_ms_;
  mutable std::timed_mutex llm_mutex_;
};