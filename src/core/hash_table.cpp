#include "core/hash_table.h"
#include <utility>
#include "core/hash.h"
#include "core/posting.h"

namespace {
constexpr std::size_t min_capacity = 16;
}

template <typename V>
HashTable<V>::HashTable(std::size_t initial_capacity)
    : initial_capacity_(next_power_of_two(initial_capacity)) {
  slots_.resize(initial_capacity_);
}

template <typename V>
std::size_t HashTable<V>::next_power_of_two(std::size_t n) noexcept {
  std::size_t cap = min_capacity;
  while (cap < n) {
    cap *= 2;
  }
  return cap;
}

template <typename V>
V& HashTable<V>::get_or_insert(std::string_view key) {
  return get_or_insert(key, hash_string(key));
}

template <typename V>
V& HashTable<V>::get_or_insert(std::string_view key, std::uint64_t hash) {

  if ((size_ + 1) * 10 > slots_.size() * 7) {
    rehash(slots_.size() * 2);
  }

  const std::size_t mask = slots_.size() - 1;
  std::size_t idx = static_cast<std::size_t>(hash) & mask;

  while (true) {
    Slot& slot = slots_[idx];
    if (!slot.occupied) {
      slot.key = key;
      slot.value = V{};
      slot.hash = hash;
      slot.occupied = true;
      ++size_;
      return slot.value;
    }

    if (slot.hash == hash && slot.key == key) {
      return slot.value;
    }

    idx = (idx + 1) & mask;
  }
}

template <typename V>
const V* HashTable<V>::find(std::string_view key) const {
  return find(key, hash_string(key));
}

template <typename V>
const V* HashTable<V>::find(std::string_view key, std::uint64_t hash) const {
  const std::size_t mask = slots_.size() - 1;
  std::size_t idx = static_cast<std::size_t>(hash) & mask;

  while (true) {
    const Slot& slot = slots_[idx];
    if (!slot.occupied) {
      return nullptr;
    }
    if (slot.hash == hash && slot.key == key) {
      return &slot.value;
    }
    idx = (idx + 1) & mask;
  }
}

template <typename V>
void HashTable<V>::clear() {
  slots_.assign(initial_capacity_, Slot{});
  size_ = 0;
}

template <typename V>
std::size_t HashTable<V>::size() const noexcept {
  return size_;
}

template <typename V>
std::size_t HashTable<V>::capacity() const noexcept {
  return slots_.size();
}

template <typename V>
bool HashTable<V>::empty() const noexcept {
  return size_ == 0;
}

template <typename V>
const std::vector<typename HashTable<V>::Slot>& HashTable<V>::slots() const noexcept {
  return slots_;
}

template <typename V>
void HashTable<V>::rehash(std::size_t new_capacity) {
  std::vector<Slot> new_slots(new_capacity);
  const std::size_t mask = new_capacity - 1;

  for (Slot& old_slot : slots_) {
    if (!old_slot.occupied) {
      continue;
    }

    std::size_t idx = static_cast<std::size_t>(old_slot.hash) & mask;
    while (new_slots[idx].occupied) {
      idx = (idx + 1) & mask;
    }
    new_slots[idx] = std::move(old_slot);
  }

  slots_ = std::move(new_slots);
}

template class HashTable<int>;
template class HashTable<std::vector<Posting>>;