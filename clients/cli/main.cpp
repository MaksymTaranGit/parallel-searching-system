#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "cli_client.h"

namespace {

enum class Command { com_search, com_health, com_stats, com_index };

std::string get_default_url() {
  const char* env_url = std::getenv("SEARCH_SERVER_URL");
  if (env_url != nullptr && env_url[0] != '\0') {
    return std::string(env_url);
  }
  return "http://localhost:8080";
}

void print_usage(const char* program_name) {
  std::cout << "Usage:\n"
            << "  " << program_name << " \"<query>\" [--mode keyword|expanded] [--url <url>]\n"
            << "  " << program_name << " --index [threads] [--url <url>]\n"
            << "  " << program_name << " --stats [--url <url>]\n"
            << "  " << program_name << " --health [--url <url>]\n\n"
            << "Options:\n"
            << "  -m, --mode <mode>    Search mode: keyword (default) or expanded\n"
            << "  -u, --url <url>      Server base URL (default: http://localhost:8080)\n"
            << "  --index [threads]    Trigger index build with optional thread count\n"
            << "  --stats              Show indexing statistics\n"
            << "  --health             Check server and Ollama availability\n"
            << "  -h, --help           Show this help message\n";
}

bool parse_positive_size(const std::string& text, std::size_t& out) {
  if (text.empty() || text[0] == '-') { //перевірка на від'ємні числа (stoull дозволяє їх, що призводить до переповнення)
    return false;
  }
  try {
    std::size_t parsed_chars = 0;
    const unsigned long long val = std::stoull(text, &parsed_chars);
    if (parsed_chars != text.size() || val == 0) {
      return false;
    }
    out = static_cast<std::size_t>(val);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  Command command = Command::com_search;
  std::string server_url = get_default_url();
  std::string mode = "keyword";
  std::optional<std::size_t> index_threads;
  std::vector<std::string> query_parts;
  
  //фаза читання всіх аргументів з термінала
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    }

    if (arg == "-u" || arg == "--url") {
      if (i + 1 >= argc) {
        std::cerr << "Error: missing value after " << arg << '\n';
        return 1;
      }
      server_url = argv[++i];
      continue;
    }

    if (arg == "-m" || arg == "--mode") {
      if (i + 1 >= argc) {
        std::cerr << "Error: missing value after " << arg << '\n';
        return 1;
      }
      mode = argv[++i];
      if (mode != "keyword" && mode != "expanded") {
        std::cerr << "Error: unsupported mode \"" << mode
                  << "\" (expected \"keyword\" or \"expanded\")\n";
        return 1;
      }
      continue;
    }

    if (arg == "--health") {
      command = Command::com_health;
      continue;
    }

    if (arg == "--stats") {
      command = Command::com_stats;
      continue;
    }

    if (arg == "--index") {
      command = Command::com_index;
      if (i + 1 < argc) {
        std::size_t parsed_threads = 0;
        if (parse_positive_size(argv[i + 1], parsed_threads)) {
          index_threads = parsed_threads;
          ++i;
        }
      }
      continue;
    }

    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Error: unknown option \"" << arg << "\"\n";
      print_usage(argv[0]);
      return 1;
    }

    query_parts.push_back(arg);
  }

  CliClient client(server_url);

  switch (command) {
    case Command::com_health:
      return client.check_health();
    case Command::com_stats:
      return client.get_stats();
    case Command::com_index:
      return client.build_index(index_threads);
    case Command::com_search: {
      if (query_parts.empty()) {
        std::cerr << "Error: search query cannot be empty\n";
        print_usage(argv[0]);
        return 1;
      }

      std::string query;
      for (std::size_t i = 0; i < query_parts.size(); ++i) {
        if (i > 0) {
          query.push_back(' ');
        }
        query += query_parts[i];
      }
      return client.search(query, mode);
    }
  }

  return 0;
}