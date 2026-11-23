#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DocMatch {
  std::string doc_id;
  double score = 0.0;
  std::string snippet;
};

struct SearchResult { 
  std::vector<DocMatch> results;
  std::vector<std::string> expanded_terms;
  std::int64_t time_ms = 0;
};

struct IndexResult {
  std::size_t documents_indexed = 0;
  std::int64_t time_ms = 0;
  std::size_t threads_used = 0;
};

struct StatsResult {
  std::size_t documents = 0;
  std::int64_t index_build_time_ms = 0;
  std::size_t threads_used = 0;
};