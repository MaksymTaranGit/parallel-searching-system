#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/hash_table.h"

class Tokenizer {
 public:
  Tokenizer();

  std::vector<std::string> tokenize(std::string_view text) const;
  HashTable<int> compute_frequencies(std::string_view text) const;
  bool is_stop_word(std::string_view word) const;

 private:
  HashTable<int> stop_words_;
};