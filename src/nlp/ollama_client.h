#pragma once

#include <mutex>
#include <string>
#include <vector>

class OllamaClient {
 public:
  explicit OllamaClient(std::string base_url, std::string model_name);

  bool ping() const;

  std::vector<std::string> expand_query(const std::string& query);

 private:
  std::string base_url_;
  std::string model_name_;
  std::mutex llm_mutex_;
};