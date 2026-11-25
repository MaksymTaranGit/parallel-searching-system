#pragma once

#include <cstddef>
#include <optional>
#include <string>

class CliClient {
 public:
  explicit CliClient(std::string server_url);

  // Повертають код завершення процесу (0 — успіх, 1 — помилка)
  int check_health() const;
  int get_stats() const;
  int build_index(std::optional<std::size_t> threads) const;
  int search(const std::string& query, const std::string& mode) const;

 private:
  std::string server_url_;
};