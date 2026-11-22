#pragma once

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/hash_table.h"
#include "core/index_shard.h"
#include "core/posting.h"

class ThreadSafeInvertedIndex {
 public:
  static constexpr std::size_t default_shard_count = 64;

  explicit ThreadSafeInvertedIndex(std::size_t num_shards = default_shard_count);

  ThreadSafeInvertedIndex(const ThreadSafeInvertedIndex&) = delete;
  ThreadSafeInvertedIndex& operator=(const ThreadSafeInvertedIndex&) = delete;

  int register_doc(std::string path);
  std::string get_doc_path(int doc_id) const;
  void add_document(int doc_id, const HashTable<int>& tf_table);
  std::vector<Posting> search_term(std::string_view term) const;

  void clear();

  std::size_t get_document_count() const;
  std::size_t get_unique_term_count() const;
  std::size_t shard_count() const noexcept;

 private:
  static std::size_t normalize_shard_count(std::size_t num_shards) noexcept;
  static unsigned int compute_shard_shift(std::size_t actual_shards) noexcept;
  std::size_t shard_index(std::uint64_t hash) const noexcept;

  std::vector<IndexShard> shards_;
  unsigned int shard_shift_ = 0;

  std::vector<std::string> doc_registry_;
  mutable std::shared_mutex reg_mtx_;
};