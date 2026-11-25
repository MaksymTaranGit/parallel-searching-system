#include <cstdlib>
#include <memory>
#include <string>

#include "api/server_controller.h"
#include "core/inverted_index.h"
#include "engine/search_engine.h"
#include "nlp/ollama_client.h"

namespace {

std::string get_env_or_default(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return (value != nullptr && value[0] != '\0') ? std::string(value) : fallback;
}

}

int main() {
  const std::string ollama_url = get_env_or_default("OLLAMA_URL", "http://ollama:11434");
  const std::string ollama_model = get_env_or_default("OLLAMA_MODEL", "qwen2.5:0.5b");
  const std::string dataset_path = get_env_or_default("DATASET_PATH", "/app/data");
  const std::string web_dir = get_env_or_default("WEB_DIR", "/app/web");
  const int port = std::stoi(get_env_or_default("SERVER_PORT", "8080"));

  auto index = std::make_shared<ThreadSafeInvertedIndex>();
  auto ollama = std::make_shared<OllamaClient>(ollama_url, ollama_model);
  auto engine = std::make_shared<SearchEngine>(index, ollama);

  ServerController controller(engine, dataset_path, web_dir);
  controller.register_routes();
  controller.run(port);

  return 0;
}