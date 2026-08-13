#include "server.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool parse_integer(const std::string& value, int& out) {
  try {
    std::size_t used = 0;
    const int parsed = std::stoi(value, &used);
    if (used != value.size()) return false;
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  cad::server::ServerOptions options;
#ifdef _WIN32
  options.launch_browser = true;
#else
  options.launch_browser = false;
#endif

  bool port_set = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bind-all") {
      options.bind_all = true;
    } else if (arg == "--no-browser") {
      options.launch_browser = false;
    } else if (arg == "--browser") {
      options.launch_browser = true;
    } else if (arg == "--auth-token") {
      if (i + 1 >= argc) {
        std::cerr << "--auth-token requires a value\n";
        return 2;
      }
      options.auth_token = argv[++i];
    } else if (arg.rfind("--auth-token=", 0) == 0) {
      options.auth_token = arg.substr(std::string("--auth-token=").size());
    } else if (arg == "--workers") {
      if (i + 1 >= argc || !parse_integer(argv[++i], options.workers)) {
        std::cerr << "--workers requires an integer\n";
        return 2;
      }
    } else if (arg.rfind("--workers=", 0) == 0) {
      if (!parse_integer(arg.substr(std::string("--workers=").size()), options.workers)) {
        std::cerr << "--workers requires an integer\n";
        return 2;
      }
    } else if (!port_set && parse_integer(arg, options.port)) {
      port_set = true;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return 2;
    }
  }

  if (options.auth_token.empty()) {
    if (const char* token = std::getenv("CAD_POLICY_STUDIO_TOKEN"))
      options.auth_token = token;
  }

  return cad::server::run(options);
}
