#pragma once

#include <crow.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include "engine/search_engine.h"

class ServerController {
 public:
  ServerController(std::shared_ptr<SearchEngine> engine, std::string dataset_path);

  void register_routes();
  void run(int port);

 private:
  crow::response handle_health() const;
  crow::response handle_index(const crow::request& req);
  crow::response handle_search(const crow::request& req) const;
  crow::response handle_stats() const;

  static crow::response json_response(int status_code, const nlohmann::json& body);

  std::shared_ptr<SearchEngine> engine_;
  std::string dataset_path_;
  crow::SimpleApp app_;
};