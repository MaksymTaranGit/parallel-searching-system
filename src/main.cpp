#include <crow.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

#include "nlp/ollama_client.h"

namespace {

std::string get_env_or_default(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return (value != nullptr && value[0] != '\0') ? std::string(value) : fallback;
}

}  // namespace

int main() {
  const std::string ollama_url = get_env_or_default("OLLAMA_URL", "http://ollama:11434");
  const std::string ollama_model = get_env_or_default("OLLAMA_MODEL", "qwen2.5:0.5b");
  const int port = std::stoi(get_env_or_default("SERVER_PORT", "8080"));

  OllamaClient ollama_client(ollama_url, ollama_model);

  crow::SimpleApp app;

  CROW_ROUTE(app, "/api/health").methods(crow::HTTPMethod::GET)([&ollama_client]() {
    const bool ollama_available = ollama_client.ping();

    nlohmann::json body = {
        {"status", "ok"},
        {"ollama", ollama_available}
    };

    crow::response res(200, body.dump());
    res.set_header("Content-Type", "application/json");
    return res;
  });

  app.port(port).multithreaded().run();
  return 0;
}