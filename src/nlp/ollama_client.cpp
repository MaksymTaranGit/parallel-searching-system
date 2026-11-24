#include "nlp/ollama_client.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t max_expanded_terms = 5;
constexpr std::size_t max_term_length = 40;

bool is_ascii_alnum(unsigned char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

char to_ascii_lower(unsigned char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
}

std::string sanitize_term(const std::string& raw) {
  std::size_t start = 0;
  while (start < raw.size() && !is_ascii_alnum(static_cast<unsigned char>(raw[start]))) {
    ++start;
  }

  std::size_t end = raw.size();
  while (end > start && !is_ascii_alnum(static_cast<unsigned char>(raw[end - 1]))) {
    --end;
  }

  if (start >= end || (end - start) > max_term_length) {
    return {};
  }

  std::string cleaned;
  cleaned.reserve(end - start);
  for (std::size_t i = start; i < end; ++i) {
    const auto uc = static_cast<unsigned char>(raw[i]);
    if (is_ascii_alnum(uc) || uc == ' ' || uc == '-') {
      cleaned.push_back(to_ascii_lower(uc));
    }
  }
  return cleaned;
}

void push_unique_term(std::vector<std::string>& terms, const std::string& raw) {
  if (terms.size() >= max_expanded_terms) {
    return;
  }
  std::string cleaned = sanitize_term(raw);
  if (cleaned.size() < 2) {
    return;
  }
  if (std::find(terms.begin(), terms.end(), cleaned) == terms.end()) { //якщо такого слова нема - добавляєм в масив
    terms.push_back(std::move(cleaned));
  }
}

void extract_from_json_array(const nlohmann::json& arr, std::vector<std::string>& out) {
  for (const auto& item : arr) {
    if (item.is_string()) {
      push_unique_term(out, item.get<std::string>());
    }
  }
}

std::vector<std::string> parse_expanded_terms(const std::string& llm_response) {
  std::vector<std::string> terms;

  const auto parsed = nlohmann::json::parse(llm_response, nullptr, false);
  if (!parsed.is_discarded()) {
    if (parsed.is_array()) {
      extract_from_json_array(parsed, terms);
    } else if (parsed.is_object()) {
      if (parsed.contains("terms") && parsed["terms"].is_array()) {
        extract_from_json_array(parsed["terms"], terms);
      } else { //якщо замість terms модель написала іншу назву ключа
        for (const auto& [key, value] : parsed.items()) {
          if (value.is_array()) {
            extract_from_json_array(value, terms);
            break;
          }
        }
      }
    }
  }

  if (!terms.empty()) {
    return terms;
  }
  
  //якщо модель повернула невалідний джсон
  std::string current;
  for (char ch : llm_response) {
    if (ch == ',' || ch == '\n' || ch == ';') {
      push_unique_term(terms, current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  push_unique_term(terms, current);

  return terms;
}

}  // namespace

OllamaClient::OllamaClient(std::string base_url, std::string model_name, int lock_timeout_ms)
    : base_url_(std::move(base_url)), model_name_(std::move(model_name)), lock_timeout_ms_(lock_timeout_ms) {}

bool OllamaClient::ping() const {
  httplib::Client client(base_url_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);

  auto response = client.Get("/api/tags");
  return response && response->status == 200;
}

std::vector<std::string> OllamaClient::expand_query(const std::string& query) const {
  if (query.empty()) {
    return {};
  }

  std::unique_lock<std::timed_mutex> lock(llm_mutex_, std::defer_lock);
  if (lock_timeout_ms_ > 0) {
    if (!lock.try_lock_for(std::chrono::milliseconds(lock_timeout_ms_))) {
      return {};
    }
  } else {
    lock.lock();
  }

  httplib::Client client(base_url_);
  client.set_connection_timeout(5, 0);
  client.set_read_timeout(30, 0);

  const std::string prompt =
      "Return a JSON object with a single key \"terms\" containing an array of 4 to 5 "
      "single-word English synonyms or closely related search keywords for the query: \"" +
      query +
      "\". Example: {\"terms\": [\"cosmos\", \"stars\", \"galaxy\", \"astronomy\"]}.";

  const nlohmann::json request_body = {
      {"model", model_name_},
      {"prompt", prompt},
      {"stream", false},
      {"format", "json"},
      {"options", {
          {"temperature", 0.2},
          {"num_predict", 64}
      }}
  };

  auto response = client.Post("/api/generate", request_body.dump(), "application/json");
  if (!response || response->status != 200) {
    return {};
  }

  const auto body_json = nlohmann::json::parse(response->body, nullptr, false);
  if (body_json.is_discarded() || !body_json.contains("response") ||
      !body_json["response"].is_string()) {
    return {};
  }

  return parse_expanded_terms(body_json["response"].get<std::string>());
}