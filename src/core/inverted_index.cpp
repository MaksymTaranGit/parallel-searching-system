#include "core/inverted_index.h"

#include <mutex>
#include <utility>

#include "core/hash.h"

std::size_t ThreadSafeInvertedIndex::normalize_shard_count(std::size_t num_shards) noexcept {
  constexpr std::size_t max_shards = 4096;
  std::size_t actual_shards = 1;
  while (actual_shards < num_shards && actual_shards < max_shards) {
    actual_shards *= 2;
  }
  return actual_shards;
}

unsigned int ThreadSafeInvertedIndex::compute_shard_shift(std::size_t actual_shards) noexcept {
  if (actual_shards <= 1) {
    return 0;
  }

  unsigned int bits = 0;
  while (actual_shards > 1) {
    actual_shards /= 2;
    ++bits;
  }
  return 64 - bits;
}

ThreadSafeInvertedIndex::ThreadSafeInvertedIndex(std::size_t num_shards)
    : shards_(normalize_shard_count(num_shards)),
      shard_shift_(compute_shard_shift(normalize_shard_count(num_shards))) {}

std::size_t ThreadSafeInvertedIndex::shard_index(std::uint64_t hash) const noexcept {
  if (shards_.size() == 1) {
    return 0;
  }
  return static_cast<std::size_t>(hash >> shard_shift_);
}

int ThreadSafeInvertedIndex::register_doc(std::string path) {
  std::unique_lock<std::shared_mutex> lock(reg_mtx_);
  const int doc_id = static_cast<int>(doc_registry_.size());
  doc_registry_.push_back(std::move(path));
  return doc_id;
}

std::string ThreadSafeInvertedIndex::get_doc_path(int doc_id) const {
  std::shared_lock<std::shared_mutex> lock(reg_mtx_);
  if (doc_id < 0 || static_cast<std::size_t>(doc_id) >= doc_registry_.size()) {
    return {};
  }
  return doc_registry_[static_cast<std::size_t>(doc_id)];
}

void ThreadSafeInvertedIndex::add_document(int doc_id, const HashTable<int>& tf_table) {
  if (tf_table.empty()) {
    return;
  }

  //для кожного шарду йде відбір масиву айтемів, щоб за 1 блокування мютекса заповнити весь шард
  std::vector<std::vector<ShardInsertItem>> batches(shards_.size());
  const auto& slots = tf_table.slots();

  for (const auto& slot : slots) {
    if (!slot.occupied) {
      continue;
    }
    const std::size_t idx = shard_index(slot.hash);
    batches[idx].push_back(ShardInsertItem{slot.key, slot.hash, slot.value});
  }

  for (std::size_t i = 0; i < shards_.size(); ++i) {
    if (!batches[i].empty()) {
      shards_[i].add_batch(doc_id, batches[i]);
    }
  }
}

std::vector<Posting> ThreadSafeInvertedIndex::search_term(std::string_view term) const {
  if (term.empty()) {
    return {};
  }
  const std::uint64_t hash = hash_string(term);
  const std::size_t idx = shard_index(hash);
  return shards_[idx].get_postings(term, hash);
}

void ThreadSafeInvertedIndex::clear() {
  {
    std::unique_lock<std::shared_mutex> lock(reg_mtx_);
    doc_registry_.clear();
  }
  for (auto& shard : shards_) {
    shard.clear();
  }
}

std::size_t ThreadSafeInvertedIndex::get_document_count() const {
  std::shared_lock<std::shared_mutex> lock(reg_mtx_);
  return doc_registry_.size();
}

std::size_t ThreadSafeInvertedIndex::get_unique_term_count() const {
  std::size_t total = 0;
  for (const auto& shard : shards_) {
    total += shard.unique_term_count();
  }
  return total;
}

std::size_t ThreadSafeInvertedIndex::shard_count() const noexcept {
  return shards_.size();
}