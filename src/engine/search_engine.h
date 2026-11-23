#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/inverted_index.h"
#include "engine/models.h"
#include "nlp/ollama_client.h"
#include "nlp/tokenizer.h"

struct SearchStageCallbacks {
  std::function<void(const std::string& original, const std::vector<std::string>& expanded_terms)> on_query_expansion;
  std::function<void(const SearchResult& result)> on_keyword_search;
  std::function<void()> on_done;
};

class SearchEngine {
 public:
  static constexpr std::size_t default_top_k = 10;

  SearchEngine(std::shared_ptr<ThreadSafeInvertedIndex> index, std::shared_ptr<OllamaClient> ollama);

  IndexResult build_index(const std::string& dataset_path, std::size_t threads);

  SearchResult search(const std::string& query, const std::string& mode = "keyword", std::size_t top_k = default_top_k) const;

  SearchResult execute_expanded_search(const std::string& query, const SearchStageCallbacks& callbacks, std::size_t top_k = default_top_k) const;

  bool check_health() const;
  StatsResult get_stats() const;

 private:
  struct WeightedTerm {
    std::string term;
    double weight = 1.0;
  };

  std::vector<WeightedTerm> build_weighted_terms(const std::string& query, const std::vector<std::string>& expanded_terms) const;
  std::vector<DocMatch> rank_documents(const std::vector<WeightedTerm>& weighted_terms, std::size_t top_k) const;
  static std::vector<std::string> scan_directory(const std::string& path);

  std::shared_ptr<ThreadSafeInvertedIndex> index_;
  std::shared_ptr<OllamaClient> ollama_;
  Tokenizer tokenizer_;

  mutable std::mutex build_mtx_;
  std::atomic<std::int64_t> last_build_time_ms_{0};
  std::atomic<std::size_t> last_threads_used_{0};
};