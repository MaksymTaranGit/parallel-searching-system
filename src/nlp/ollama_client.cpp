#include "nlp/ollama_client.h"
#include <httplib.h>

#include <utility>

OllamaClient::OllamaClient(std::string base_url, std::string model_name)
    : base_url_(std::move(base_url)), model_name_(std::move(model_name)) {}

bool OllamaClient::ping() const {
  httplib::Client client(base_url_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);

  auto response = client.Get("/api/tags");
  return response && response->status == 200;
}

std::vector<std::string> OllamaClient::expand_query(const std::string& query) {
  std::lock_guard<std::mutex> lock(llm_mutex_);
  return {query};
}