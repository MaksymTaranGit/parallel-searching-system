#pragma once

#include <crow.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "core/thread_pool.h"
#include "engine/search_engine.h"

class ServerController {
 public:
  ServerController(std::shared_ptr<SearchEngine> engine, std::string dataset_path, std::string web_dir);

  void register_routes();
  void run(int port);

 private:
  crow::response serve_static_file(const std::string& filename, const std::string& content_type) const;
  crow::response handle_health() const;
  crow::response handle_index(const crow::request& req);
  crow::response handle_search(const crow::request& req) const;
  crow::response handle_stats() const;

  void handle_ws_open(crow::websocket::connection& conn);
  void handle_ws_close(crow::websocket::connection& conn);
  void handle_ws_message(crow::websocket::connection& conn, const std::string& data, bool is_binary);
  void send_ws_json(crow::websocket::connection* conn, const nlohmann::json& payload);

  static nlohmann::json matches_to_json(const std::vector<DocMatch>& matches);
  static crow::response json_response(int status_code, const nlohmann::json& body);

  std::shared_ptr<SearchEngine> engine_;
  std::string dataset_path_;
  std::string web_dir_;

  std::mutex ws_mtx_;
  std::unordered_set<crow::websocket::connection*> active_ws_connections_; //потрібен щоб перед відправкою(send) дивитись чи існує з'єднання

  crow::SimpleApp app_;
  ThreadPool ws_pool_;
};