#include "cli_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iomanip>
#include <iostream>
#include <utility>

namespace {

constexpr int connect_timeout_sec = 5;
constexpr int quick_read_timeout_sec = 10;
constexpr int expanded_read_timeout_sec = 60;
constexpr int index_read_timeout_sec = 600;

void print_http_error(const std::string& endpoint, const httplib::Result& res) {
  if (!res) {
    std::cerr << "Error: failed to connect to server on " << endpoint << " ("
              << httplib::to_string(res.error()) << ")\n";
    return;
  }

  std::cerr << "Error: HTTP " << res->status << " from " << endpoint;
  const auto parsed = nlohmann::json::parse(res->body, nullptr, false);
  if (!parsed.is_discarded() && parsed.contains("error") && parsed["error"].is_string()) {
    std::cerr << " - " << parsed["error"].get<std::string>();
  } else if (!res->body.empty()) {
    std::cerr << " - " << res->body;
  }
  std::cerr << '\n';
}

}  // namespace

CliClient::CliClient(std::string server_url) : server_url_(std::move(server_url)) {}

int CliClient::check_health() const {
  httplib::Client client(server_url_);
  client.set_connection_timeout(connect_timeout_sec, 0);
  client.set_read_timeout(quick_read_timeout_sec, 0);

  const auto res = client.Get("/api/health");
  if (!res || res->status != 200) {
    print_http_error("GET /api/health", res);
    return 1;
  }

  const auto json = nlohmann::json::parse(res->body, nullptr, false);
  if (json.is_discarded()) {
    std::cerr << "Error: invalid JSON response from /api/health\n";
    return 1;
  }

  const std::string status = json.value("status", "unknown");
  const bool ollama_ok = json.value("ollama", false);

  std::cout << "Server status : " << status << '\n'
            << "Ollama LLM    : " << (ollama_ok ? "available" : "unavailable") << '\n';
  return 0;
}

int CliClient::get_stats() const {
  httplib::Client client(server_url_);
  client.set_connection_timeout(connect_timeout_sec, 0);
  client.set_read_timeout(quick_read_timeout_sec, 0);

  const auto res = client.Get("/api/stats");
  if (!res || res->status != 200) {
    print_http_error("GET /api/stats", res);
    return 1;
  }

  const auto json = nlohmann::json::parse(res->body, nullptr, false);
  if (json.is_discarded()) {
    std::cerr << "Error: invalid JSON response from /api/stats\n";
    return 1;
  }

  std::cout << "Indexed documents : " << json.value("documents", 0) << '\n'
            << "Last build time   : " << json.value("index_build_time_ms", 0) << " ms\n"
            << "Threads used      : " << json.value("threads_used", 0) << '\n';
  return 0;
}

int CliClient::build_index(std::optional<std::size_t> threads) const {
  httplib::Client client(server_url_);
  client.set_connection_timeout(connect_timeout_sec, 0);
  client.set_read_timeout(index_read_timeout_sec, 0);

  nlohmann::json body = nlohmann::json::object();
  if (threads.has_value()) {
    body["threads"] = *threads;
  }

  std::cout << "Building inverted index";
  if (threads.has_value()) {
    std::cout << " using " << *threads << " thread(s)";
  }
  std::cout << "...\n";

  const auto res = client.Post("/api/index", body.dump(), "application/json");
  if (!res || res->status != 200) {
    print_http_error("POST /api/index", res);
    return 1;
  }

  const auto json = nlohmann::json::parse(res->body, nullptr, false);
  if (json.is_discarded()) {
    std::cerr << "Error: invalid JSON response from /api/index\n";
    return 1;
  }

  std::cout << "Status            : " << json.value("status", "ok") << '\n'
            << "Documents indexed : " << json.value("documents_indexed", 0) << '\n'
            << "Build time        : " << json.value("time_ms", 0) << " ms\n";
  return 0;
}

int CliClient::search(const std::string& query, const std::string& mode) const {
  httplib::Client client(server_url_);
  client.set_connection_timeout(connect_timeout_sec, 0);
  client.set_read_timeout(mode == "expanded" ? expanded_read_timeout_sec : quick_read_timeout_sec, 0);

  const nlohmann::json req_body = {
      {"query", query},
      {"mode", mode}
  };

  const auto res = client.Post("/api/search", req_body.dump(), "application/json");
  if (!res || res->status != 200) {
    print_http_error("POST /api/search", res);
    return 1;
  }

  const auto json = nlohmann::json::parse(res->body, nullptr, false);
  if (json.is_discarded() || !json.contains("results") || !json["results"].is_array()) {
    std::cerr << "Error: invalid JSON response from /api/search\n";
    return 1;
  }

  const auto& results = json["results"];
  const auto time_ms = json.value("time_ms", 0);

  std::cout << "Query   : \"" << query << "\" (mode: " << mode << ")\n";

  if (json.contains("expanded_terms") && json["expanded_terms"].is_array() &&
      !json["expanded_terms"].empty()) {
    std::cout << "Expanded: ";
    bool first = true;
    for (const auto& term : json["expanded_terms"]) {
      if (!term.is_string()) {
        continue;
      }
      if (!first) {
        std::cout << ", ";
      }
      std::cout << term.get<std::string>();
      first = false;
    }
    std::cout << '\n';
  }

  std::cout << "Found   : " << results.size() << " result(s) in " << time_ms << " ms\n";

  if (results.empty()) {
    std::cout << "\nNo matching documents found.\n";
    return 0;
  }

  std::cout << "------------------------------------------------------------\n";
  std::size_t rank = 1;
  for (const auto& item : results) {
    const std::string doc_id = item.value("doc_id", "unknown");
    const double score = item.value("score", 0.0);
    const std::string snippet = item.value("snippet", "");

    std::cout << '[' << rank++ << "] " << doc_id
              << " (score: " << std::fixed << std::setprecision(4) << score << ")\n";
    if (!snippet.empty()) {
      std::cout << "    " << snippet << "\n";
    }
    std::cout << '\n';
  }

  return 0;
}