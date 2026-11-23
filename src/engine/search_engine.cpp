#include "engine/search_engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

#include "core/hash_table.h"
#include "core/posting.h"
#include "core/thread_pool.h"

namespace {

constexpr double original_term_weight = 1.0;
constexpr double expanded_term_weight = 0.5;
constexpr std::size_t snippet_length = 200;
constexpr std::size_t snippet_lead_context = 60;
constexpr std::size_t snippet_chunk_size = 65536;

inline bool is_ascii_alnum(unsigned char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline char to_ascii_lower(unsigned char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
}

bool is_txt_extension(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  for (char& c : ext) {
    c = to_ascii_lower(static_cast<unsigned char>(c));
  }
  return ext == ".txt";
}

std::string read_file_content(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }

  const std::streampos end_pos = file.tellg();
  if (end_pos <= 0) {
    return {};
  }

  std::string content(static_cast<std::size_t>(end_pos), '\0');
  file.seekg(0, std::ios::beg);
  if (!file.read(content.data(), static_cast<std::streamsize>(content.size()))) {
    content.resize(static_cast<std::size_t>(file.gcount()));
  }
  return content;
}

//За першими бітами символу в ютф8 відразу видно що це за символ(1 байт, 2 байта тощо)
//накладаючи на поточний байт необхідну маску (1000, 1100 тощо) можна перевірити який тип має цей символ
std::string_view align_utf8_slice(std::string_view text, std::size_t start, std::size_t len) {
  const std::size_t total = text.size();
  while (start < total && (static_cast<unsigned char>(text[start]) & 0xC0U) == 0x80U) { //чи знаходимся в середині багатобайтового символу
    ++start;
  }

  std::size_t end = std::min(total, start + len);
  if (end < total && end > start) {
    std::size_t back = end;
    while (back > start && (static_cast<unsigned char>(text[back - 1]) & 0xC0U) == 0x80U) {
      --back;
    }
    if (back > start) {
      const auto lead = static_cast<unsigned char>(text[back - 1]);
      std::size_t expected_len = 1;
      if ((lead & 0xE0U) == 0xC0U) {
        expected_len = 2;
      } else if ((lead & 0xF0U) == 0xE0U) {
        expected_len = 3;
      } else if ((lead & 0xF8U) == 0xF0U) {
        expected_len = 4;
      }
      if ((end - (back - 1)) < expected_len) { //якщо к-сть байтів останнього символу яка влізла у зріз менша за очікувану, убираєм символ
        end = back - 1;
      }
    }
  }

  return (start < end) ? text.substr(start, end - start) : std::string_view{};
}

std::string normalize_whitespace(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  bool prev_space = true;

  for (char ch : raw) {
    const auto uc = static_cast<unsigned char>(ch);
    if (uc <= 0x20U) { //0х20 = 32 - все що менше за 32 в ASCII це керуючі символи
      if (!prev_space) {
        out.push_back(' ');
        prev_space = true;
      }
    } else {
      out.push_back(ch);
      prev_space = false;
    }
  }

  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::size_t find_first_term_match(std::string_view chunk, const std::vector<std::string>& terms) {
  if (chunk.empty() || terms.empty()) {
    return std::string::npos;
  }

  std::string lower;
  lower.resize(chunk.size());
  for (std::size_t i = 0; i < chunk.size(); ++i) {
    lower[i] = to_ascii_lower(static_cast<unsigned char>(chunk[i]));
  }

  std::size_t best_pos = std::string::npos;
  for (const std::string& term : terms) {
    if (term.empty()) {
      continue;
    }

    std::size_t pos = lower.find(term); 
    while (pos != std::string::npos) { //перевірка чи .find знайшов підрядок чи слово повноцінне 
      const bool left_ok =
          (pos == 0) || !is_ascii_alnum(static_cast<unsigned char>(lower[pos - 1]));
      const std::size_t end_pos = pos + term.size();
      const bool right_ok =
          (end_pos == lower.size()) || !is_ascii_alnum(static_cast<unsigned char>(lower[end_pos]));

      if (left_ok && right_ok) {
        if (pos < best_pos) {
          best_pos = pos;
        }
        break;
      }
      pos = lower.find(term, pos + 1);
    }
  }
  return best_pos; //знайдено найраніший збіг серед усіх термінів
}

std::string extract_snippet(const std::string& file_path, const std::vector<std::string>& terms) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::string fallback_head; //треба щоб зберігати початок книги, якщо шукане слово буде рівно на стику 2 чанків
  std::string buffer(snippet_chunk_size, '\0');

  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(snippet_chunk_size));
    const auto bytes_read = static_cast<std::size_t>(file.gcount());
    if (bytes_read == 0) {
      break;
    }

    const std::string_view chunk(buffer.data(), bytes_read);
    if (fallback_head.empty()) {
      fallback_head = normalize_whitespace(align_utf8_slice(chunk, 0, snippet_length));
    }

    const std::size_t match_pos = find_first_term_match(chunk, terms);
    if (match_pos != std::string::npos) {
      const std::size_t start =
          (match_pos > snippet_lead_context) ? (match_pos - snippet_lead_context) : 0;
      const std::string_view slice = align_utf8_slice(chunk, start, snippet_length);
      std::string snippet = normalize_whitespace(slice);

      if (start > 0) {
        snippet = "..." + snippet;
      }
      if (start + slice.size() < bytes_read || !file.eof()) {
        snippet += "...";
      }
      return snippet;
    }
  }

  return fallback_head;
}

}  // namespace

SearchEngine::SearchEngine(std::shared_ptr<ThreadSafeInvertedIndex> index, std::shared_ptr<OllamaClient> ollama)
    : index_(std::move(index)), ollama_(std::move(ollama)) {}

IndexResult SearchEngine::build_index(const std::string& dataset_path, std::size_t threads) {
  std::lock_guard<std::mutex> build_lock(build_mtx_);

  if (threads == 0) {
    threads = 1;
  }

  index_->clear();

  const std::vector<std::string> files = scan_directory(dataset_path);

  const auto start_time = std::chrono::steady_clock::now();

  if (!files.empty()) {
    ThreadPool pool(threads);

    for (const std::string& file_path : files) {
      pool.enqueue([this, file_path]() {
        const std::string content = read_file_content(file_path);
        if (content.empty()) {
          return;
        }

        const HashTable<int> tf_table = tokenizer_.compute_frequencies(content);
        const int doc_id = index_->register_doc(file_path);
        index_->add_document(doc_id, tf_table);
      });
    }

    pool.wait_all();
  }

  const auto end_time = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
  const std::size_t docs_indexed = index_->get_document_count();

  last_build_time_ms_.store(elapsed_ms, std::memory_order_relaxed);
  last_threads_used_.store(threads, std::memory_order_relaxed);

  return IndexResult{docs_indexed, elapsed_ms, threads};
}

SearchResult SearchEngine::search(const std::string& query, const std::string& mode, std::size_t top_k) const {
  const auto start_time = std::chrono::steady_clock::now();

  std::vector<std::string> expanded_terms;
  if (mode == "expanded" && ollama_) {
    expanded_terms = ollama_->expand_query(query);
  }

  const std::vector<WeightedTerm> weighted_terms = build_weighted_terms(query, expanded_terms);
  std::vector<DocMatch> matches = rank_documents(weighted_terms, top_k);

  const auto end_time = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  return SearchResult{std::move(matches), std::move(expanded_terms), elapsed_ms};
}

SearchResult SearchEngine::execute_expanded_search(const std::string& query, const SearchStageCallbacks& callbacks, std::size_t top_k) const {
  std::vector<std::string> expanded_terms;
  if (ollama_) {
    expanded_terms = ollama_->expand_query(query);
  }

  if (callbacks.on_query_expansion) {
    callbacks.on_query_expansion(query, expanded_terms);
  }

  const auto search_start = std::chrono::steady_clock::now();

  const std::vector<WeightedTerm> weighted_terms = build_weighted_terms(query, expanded_terms);
  std::vector<DocMatch> matches = rank_documents(weighted_terms, top_k);

  const auto search_end = std::chrono::steady_clock::now();
  const auto search_ms = std::chrono::duration_cast<std::chrono::milliseconds>(search_end - search_start).count();

  SearchResult result{std::move(matches), std::move(expanded_terms), search_ms};

  if (callbacks.on_keyword_search) {
    callbacks.on_keyword_search(result);
  }

  if (callbacks.on_done) {
    callbacks.on_done();
  }

  return result;
}

bool SearchEngine::check_health() const {
  return ollama_ ? ollama_->ping() : false;
}

StatsResult SearchEngine::get_stats() const {
  return StatsResult{
      index_->get_document_count(),
      last_build_time_ms_.load(std::memory_order_relaxed),
      last_threads_used_.load(std::memory_order_relaxed)
  };
}

std::vector<SearchEngine::WeightedTerm> SearchEngine::build_weighted_terms(const std::string& query,
    const std::vector<std::string>& expanded_terms) const {

  std::vector<WeightedTerm> result;
  HashTable<int> seen(32);

  for (const std::string& token : tokenizer_.tokenize(query)) {
    if (seen.find(token) == nullptr) {
      seen.get_or_insert(token) = 1;
      result.push_back(WeightedTerm{token, original_term_weight});
    }
  }

  for (const std::string& raw_term : expanded_terms) { //оскільки в OllamaClient дозволені слова з пробілами та дефісами, треба огорнути в ще 1 цикл
    for (const std::string& token : tokenizer_.tokenize(raw_term)) {
      if (seen.find(token) == nullptr) {
        seen.get_or_insert(token) = 1;
        result.push_back(WeightedTerm{token, expanded_term_weight});
      }
    }
  }

  return result;
}

//TF-IDF = W * TF * IDF
// TF(частота слова в 1 докі) = 1 + ln(term_freq)
// IDF(показує унікальність слова) = ln(1 + N/df) -> N - всі доки, df - к-сть книг з цим словом
std::vector<DocMatch> SearchEngine::rank_documents(const std::vector<WeightedTerm>& weighted_terms, std::size_t top_k) const {
  const std::size_t total_docs = index_->get_document_count();
  if (weighted_terms.empty() || total_docs == 0 || top_k == 0) {
    return {};
  }

  std::vector<double> scores(total_docs, 0.0);
  std::vector<int> matched_docs;

  for (const WeightedTerm& wt : weighted_terms) {
    const std::vector<Posting> postings = index_->search_term(wt.term);
    if (postings.empty()) {
      continue;
    }

    const double idf = std::log(1.0 + static_cast<double>(total_docs) / static_cast<double>(postings.size()));

    for (const Posting& posting : postings) {
      if (posting.doc_id < 0 || static_cast<std::size_t>(posting.doc_id) >= total_docs) {
        continue;
      }

      const auto idx = static_cast<std::size_t>(posting.doc_id);
      const double tf = 1.0 + std::log(static_cast<double>(posting.term_freq));
      const double contribution = wt.weight * tf * idf; //внесок конкретного слова в оцінку цієї конкретної книги

      if (scores[idx] == 0.0) {
        matched_docs.push_back(posting.doc_id);
      }
      scores[idx] += contribution;
    }
  }

  if (matched_docs.empty()) {
    return {};
  }

  const std::size_t limit = std::min(top_k, matched_docs.size());
  std::partial_sort(matched_docs.begin(), matched_docs.begin() + static_cast<std::ptrdiff_t>(limit), matched_docs.end(),
      [&scores](int a, int b) {
        const double score_a = scores[static_cast<std::size_t>(a)];
        const double score_b = scores[static_cast<std::size_t>(b)];
        if (score_a != score_b) {
          return score_a > score_b;
        }
        return a < b;
      });

  std::vector<std::string> snippet_terms;
  snippet_terms.reserve(weighted_terms.size());
  for (const WeightedTerm& wt : weighted_terms) {
    snippet_terms.push_back(wt.term);
  }

  std::vector<DocMatch> results;
  results.reserve(limit);
  for (std::size_t i = 0; i < limit; ++i) {
    const int doc_id = matched_docs[i];
    const std::string file_path = index_->get_doc_path(doc_id);
    const std::string doc_name = std::filesystem::path(file_path).filename().string();
    const double raw_score = scores[static_cast<std::size_t>(doc_id)];
    const double rounded_score = std::round(raw_score * 10000.0) / 10000.0;

    results.push_back(DocMatch{
        doc_name,
        rounded_score,
        extract_snippet(file_path, snippet_terms)
    });
  }

  return results;
}

std::vector<std::string> SearchEngine::scan_directory(const std::string& path) {
  std::vector<std::string> files;
  std::error_code ec;

  if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
    return files;
  }

  const auto options = std::filesystem::directory_options::skip_permission_denied; //якщо прав нема - скіп
  for (const auto& entry : std::filesystem::recursive_directory_iterator(path, options, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_regular_file(ec) && is_txt_extension(entry.path())) {
      files.push_back(entry.path().string());
    }
  }

  return files;
}