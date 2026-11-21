#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

template <typename V>
class HashTable {
 public:
  struct Slot {
  std::string key;
  std::uint64_t hash = 0;
  V value{};
  bool occupied = false;
};

  explicit HashTable(std::size_t initial_capacity = 64);

  V& get_or_insert(std::string_view key, std::uint64_t hash);
  V& get_or_insert(std::string_view key);

  const V* find(std::string_view key, std::uint64_t hash) const;
  const V* find(std::string_view key) const;

  void clear();

  std::size_t size() const noexcept;
  std::size_t capacity() const noexcept;
  bool empty() const noexcept;
  const std::vector<Slot>& slots() const noexcept;

 private:
  static std::size_t next_power_of_two(std::size_t n) noexcept;
  void rehash(std::size_t new_capacity);

  std::vector<Slot> slots_;
  std::size_t initial_capacity_;
  std::size_t size_ = 0;
};