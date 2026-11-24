#include "api/server_controller.h"

#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

ServerController::ServerController(std::shared_ptr<SearchEngine> engine, std::string dataset_path)
    : engine_(std::move(engine)), dataset_path_(std::move(dataset_path)) {}

void ServerController::register_routes() {
  CROW_ROUTE(app_, "/api/health").methods(crow::HTTPMethod::GET)(
    [this]() { return handle_health(); });

  CROW_ROUTE(app_, "/api/index").methods(crow::HTTPMethod::POST)(
    [this](const crow::request& req) { return handle_index(req); });

  CROW_ROUTE(app_, "/api/search").methods(crow::HTTPMethod::POST)(
    [this](const crow::request& req) { return handle_search(req); });

  CROW_ROUTE(app_, "/api/stats").methods(crow::HTTPMethod::GET)(
    [this]() { return handle_stats(); });
} 

void ServerController::run(int port) {
  app_.port(static_cast<std::uint16_t>(port)).multithreaded().run();
}

crow::response ServerController::handle_health() const {
  const nlohmann::json body = {
      {"status", "ok"},
      {"ollama", engine_->check_health()}
  };
  return json_response(200, body);
}

crow::response ServerController::handle_index(const crow::request& req) {
  std::size_t threads = std::thread::hardware_concurrency();
  if (threads == 0) {
    threads = 1;
  }

  if (!req.body.empty()) {
    const auto req_json = nlohmann::json::parse(req.body, nullptr, false);
    if (req_json.is_discarded() || !req_json.is_object()) {
      return json_response(400, {{"error", "Invalid JSON body"}});
    }

    if (req_json.contains("threads")) {
      if (!req_json["threads"].is_number_integer()) {
        return json_response(400, {{"error", "\"threads\" must be an integer"}});
      }
      const auto requested = req_json["threads"].get<std::int64_t>();
      if (requested <= 0) {
        return json_response(400, {{"error", "\"threads\" must be greater than 0"}});
      }
      threads = static_cast<std::size_t>(requested);
    }
  }

  const IndexResult result = engine_->build_index(dataset_path_, threads);

  const nlohmann::json body = {
      {"status", "ok"},
      {"documents_indexed", result.documents_indexed},
      {"time_ms", result.time_ms}
  };
  return json_response(200, body);
}

crow::response ServerController::handle_search(const crow::request& req) const {
  const auto req_json = nlohmann::json::parse(req.body, nullptr, false);
  if (req_json.is_discarded() || !req_json.is_object()) {
    return json_response(400, {{"error", "Invalid JSON body"}});
  }

  if (!req_json.contains("query") || !req_json["query"].is_string()) {
    return json_response(400, {{"error", "Missing or invalid \"query\" field"}});
  }

  const std::string query = req_json["query"].get<std::string>();
  std::string mode = "keyword";

  if (req_json.contains("mode")) {
    if (!req_json["mode"].is_string()) {
      return json_response(400, {{"error", "\"mode\" must be a string"}});
    }
    mode = req_json["mode"].get<std::string>();
    if (mode != "keyword" && mode != "expanded") {
      return json_response(
          400, {{"error", "Unsupported \"mode\" (expected \"keyword\" or \"expanded\")"}});
    }
  }

  const SearchResult result = engine_->search(query, mode);

  nlohmann::json results_json = nlohmann::json::array();
  for (const DocMatch& match : result.results) {
    results_json.push_back({
        {"doc_id", match.doc_id},
        {"score", match.score},
        {"snippet", match.snippet}
    });
  }

  nlohmann::json body = {
      {"results", std::move(results_json)},
      {"time_ms", result.time_ms}
  };

  if (mode == "expanded") {
    body["expanded_terms"] = result.expanded_terms;
  }

  return json_response(200, body);
}

crow::response ServerController::handle_stats() const {
  const StatsResult stats = engine_->get_stats();
  const nlohmann::json body = {
      {"documents", stats.documents},
      {"index_build_time_ms", stats.index_build_time_ms},
      {"threads_used", stats.threads_used}
  };
  return json_response(200, body);
}

crow::response ServerController::json_response(int status_code, const nlohmann::json& body) {
  crow::response res(status_code, body.dump());
  res.set_header("Content-Type", "application/json");
  return res;
}