#include "api/server_controller.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

namespace {

std::size_t resolve_ws_pool_size() {
  const std::size_t hw = std::thread::hardware_concurrency();
  return std::max<std::size_t>(4, hw);
}

}

ServerController::ServerController(std::shared_ptr<SearchEngine> engine, std::string dataset_path, std::string web_dir)
    : engine_(std::move(engine)),
      dataset_path_(std::move(dataset_path)),
      web_dir_(std::move(web_dir)),
      ws_pool_(resolve_ws_pool_size()) {}

void ServerController::register_routes() {
  CROW_ROUTE(app_, "/api/health")
      .methods(crow::HTTPMethod::GET)([this]() { return handle_health(); });

  CROW_ROUTE(app_, "/api/index")
      .methods(crow::HTTPMethod::POST)(
          [this](const crow::request& req) { return handle_index(req); });

  CROW_ROUTE(app_, "/api/search")
      .methods(crow::HTTPMethod::POST)(
          [this](const crow::request& req) { return handle_search(req); });

  CROW_ROUTE(app_, "/api/stats")
      .methods(crow::HTTPMethod::GET)([this]() { return handle_stats(); });

  CROW_WEBSOCKET_ROUTE(app_, "/ws/search")
      .onopen([this](crow::websocket::connection& conn) { handle_ws_open(conn); })
      .onclose([this](crow::websocket::connection& conn, auto&&...) {
        handle_ws_close(conn);
      })
      .onmessage([this](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        handle_ws_message(conn, data, is_binary);
      });
  
  CROW_ROUTE(app_, "/")
    .methods(crow::HTTPMethod::GET)([this]() {
      return serve_static_file("index.html", "text/html; charset=utf-8");
    });

  CROW_ROUTE(app_, "/style.css")
    .methods(crow::HTTPMethod::GET)([this]() {
      return serve_static_file("style.css", "text/css; charset=utf-8");
    });

  CROW_ROUTE(app_, "/app.js")
    .methods(crow::HTTPMethod::GET)([this]() {
      return serve_static_file("app.js", "application/javascript; charset=utf-8");
    });
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

  nlohmann::json body = {
      {"results", matches_to_json(result.results)},
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

void ServerController::handle_ws_open(crow::websocket::connection& conn) {
  std::lock_guard<std::mutex> lock(ws_mtx_);
  active_ws_connections_.insert(&conn);
}

void ServerController::handle_ws_close(crow::websocket::connection& conn) {
  std::lock_guard<std::mutex> lock(ws_mtx_);
  active_ws_connections_.erase(&conn);
}

void ServerController::handle_ws_message(crow::websocket::connection& conn, const std::string& data, bool is_binary) {
  if (is_binary) {
    send_ws_json(&conn, {{"stage", "error"}, {"data", {{"message", "Binary frames are not supported"}}}});
    return;
  }

  const auto req_json = nlohmann::json::parse(data, nullptr, false);
  if (req_json.is_discarded() || !req_json.is_object() ||
      !req_json.contains("query") || !req_json["query"].is_string()) {
    send_ws_json(&conn, {{"stage", "error"}, {"data", {{"message", "Expected JSON with string \"query\""}}}});
    return;
  }

  const std::string query = req_json["query"].get<std::string>();
  crow::websocket::connection* conn_ptr = &conn;

  //Запит виконаується у фоновому пулі, щоб не блокувати I/O потік. Якщо викликати expand_query
  // прямо в головному потоці crow, то кожен conn.send_text() буде просто кластися в чергу потоку crow
  //і вони відправляться в кінці пачкою
  ws_pool_.enqueue([this, conn_ptr, query]() {
    SearchStageCallbacks callbacks;

    callbacks.on_query_expansion =
        [this, conn_ptr](const std::string& original, const std::vector<std::string>& expanded_terms) {
          send_ws_json(conn_ptr, {
              {"stage", "query_expansion"},
              {"data", {
                  {"original", original},
                  {"expanded_terms", expanded_terms}
              }}
          });
        };

    callbacks.on_keyword_search =
        [this, conn_ptr](const SearchResult& result) {
          send_ws_json(conn_ptr, {
              {"stage", "keyword_search"},
              {"data", {
                  {"results", matches_to_json(result.results)},
                  {"time_ms", result.time_ms}
              }}
          });
        };

    callbacks.on_done = [this, conn_ptr]() {
      send_ws_json(conn_ptr, {{"stage", "done"}});
    };

    engine_->execute_expanded_search(query, callbacks);
  });
}

void ServerController::send_ws_json(crow::websocket::connection* conn, const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(ws_mtx_);
  if (active_ws_connections_.find(conn) != active_ws_connections_.end()) {
    conn->send_text(payload.dump());
  }
}

nlohmann::json ServerController::matches_to_json(const std::vector<DocMatch>& matches) {
  nlohmann::json arr = nlohmann::json::array();
  for (const DocMatch& match : matches) {
    arr.push_back({
        {"doc_id", match.doc_id},
        {"score", match.score},
        {"snippet", match.snippet}
    });
  }
  return arr;
}

crow::response ServerController::json_response(int status_code, const nlohmann::json& body) {
  crow::response res(status_code, body.dump());
  res.set_header("Content-Type", "application/json");
  return res;
}

crow::response ServerController::serve_static_file(const std::string& filename, const std::string& content_type) const {
  const std::filesystem::path full_path = std::filesystem::path(web_dir_) / filename;
  std::ifstream file(full_path, std::ios::binary);
  if (!file) {
    return crow::response(404, "File not found");
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  crow::response res(200, buffer.str());
  res.set_header("Content-Type", content_type);
  return res;
}