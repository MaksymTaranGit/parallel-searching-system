#include "nlp/tokenizer.h"

#include <array>
#include <cstddef>

namespace {

constexpr std::size_t min_token_length = 2;
constexpr std::size_t max_token_length = 64;

inline bool is_ascii_alnum(unsigned char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline char to_ascii_lower(unsigned char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
}

constexpr std::array<std::string_view, 124> english_stop_words = {
    "about", "above", "after", "again", "against", "all", "am", "an", "and", "any", "are",
    "as", "at", "be", "because", "been", "before", "being", "below", "between", "both", "but",
    "by", "can", "could", "did", "do", "does", "doing", "down", "during", "each", "few", "for",
    "from", "further", "had", "has", "have", "having", "he", "her", "here", "hers", "herself",
    "him", "himself", "his", "how", "if", "in", "into", "is", "it", "its", "itself", "just",
    "me", "more", "most", "my", "myself", "no", "nor", "not", "now", "of", "off", "on", "once",
    "only", "or", "other", "our", "ours", "ourselves", "out", "over", "own", "same", "she",
    "should", "so", "some", "such", "than", "that", "the", "their", "theirs", "them",
    "themselves", "then", "there", "these", "they", "this", "those", "through", "to", "too",
    "under", "until", "up", "very", "was", "we", "were", "what", "when", "where", "which",
    "while", "who", "whom", "why", "will", "with", "would", "you", "your", "yours", "yourself",
    "yourselves"
};

template <typename Callback>
void extract_tokens(std::string_view text, const HashTable<int>& stop_words, Callback&& on_token) {
  std::string word;
  word.reserve(32);

  auto flush_word = [&]() {
    if (word.size() >= min_token_length && word.size() <= max_token_length &&
        stop_words.find(word) == nullptr) {
      on_token(word);
    }
    word.clear();
  };

  for (char ch : text) {
    const auto uc = static_cast<unsigned char>(ch);
    if (is_ascii_alnum(uc)) {
      if (word.size() <= max_token_length) {
        word.push_back(to_ascii_lower(uc));
      }
    } else if (!word.empty()) {
      flush_word();
    }
  }

  //для останнього слова
  if (!word.empty()) {
    flush_word();
  }
}

}  // namespace

Tokenizer::Tokenizer() : stop_words_(256) {
  for (std::string_view word : english_stop_words) {
    stop_words_.get_or_insert(word) = 1;
  }
}

std::vector<std::string> Tokenizer::tokenize(std::string_view text) const {
  std::vector<std::string> tokens;
  extract_tokens(text, stop_words_, [&tokens](const std::string& word) {
    tokens.push_back(word);
  });
  return tokens;
}

HashTable<int> Tokenizer::compute_frequencies(std::string_view text) const {
  const std::size_t initial_capacity = (text.size() > 16384) ? 2048 : 64;
  HashTable<int> tf_table(initial_capacity);

  extract_tokens(text, stop_words_, [&tf_table](const std::string& word) {
    ++tf_table.get_or_insert(word);
  });

  return tf_table;
}

bool Tokenizer::is_stop_word(std::string_view word) const {
  return stop_words_.find(word) != nullptr;
}