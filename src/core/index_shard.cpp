#include "core/index_shard.h"

#include <mutex>

IndexShard::IndexShard(std::size_t initial_capacity)
    : table_(initial_capacity) {}

void IndexShard::add_term(std::string_view term, std::uint64_t hash, int doc_id, int term_freq) {
  std::unique_lock<std::shared_mutex> lock(rw_mtx_);
  table_.get_or_insert(term, hash).push_back(Posting{doc_id, term_freq});
}

void IndexShard::add_batch(int doc_id, const std::vector<ShardInsertItem>& items) {
  if (items.empty()) {
    return;
  }

  std::unique_lock<std::shared_mutex> lock(rw_mtx_);
  for (const ShardInsertItem& item : items) {
    table_.get_or_insert(item.term, item.hash).push_back(Posting{doc_id, item.term_freq});
  }
}

std::vector<Posting> IndexShard::get_postings(std::string_view term, std::uint64_t hash) const {
  std::shared_lock<std::shared_mutex> lock(rw_mtx_);
  const auto* postings = table_.find(term, hash);
  if (postings == nullptr) {
    return {};
  }
  return *postings;
}

void IndexShard::clear() {
  std::unique_lock<std::shared_mutex> lock(rw_mtx_);
  table_.clear();
}

std::size_t IndexShard::unique_term_count() const {
  std::shared_lock<std::shared_mutex> lock(rw_mtx_);
  return table_.size();
}