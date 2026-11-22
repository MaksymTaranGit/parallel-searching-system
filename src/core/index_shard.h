#pragma once

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "core/hash_table.h"
#include "core/posting.h"

struct ShardInsertItem {
  std::string_view term;
  std::uint64_t hash = 0;
  int term_freq = 0;
};

class alignas(64) IndexShard {
 public:
  explicit IndexShard(std::size_t initial_capacity = 1024);

  IndexShard(const IndexShard&) = delete;
  IndexShard& operator=(const IndexShard&) = delete;
  IndexShard(IndexShard&&) = delete;
  IndexShard& operator=(IndexShard&&) = delete;
  

  void add_term(std::string_view term, std::uint64_t hash, int doc_id, int term_freq);

  //замість того щоб блокувати мютекс 1 раз на запис 1 слова, можна за 1 блокування шарду зробити запис всіх слів які належать йому
  void add_batch(int doc_id, const std::vector<ShardInsertItem>& items);

  std::vector<Posting> get_postings(std::string_view term, std::uint64_t hash) const;
  void clear();
  std::size_t unique_term_count() const;

 private:
  mutable std::shared_mutex rw_mtx_;
  HashTable<std::vector<Posting>> table_;
};