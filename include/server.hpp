#pragma once

#include <string>

namespace cad::server {

struct ServerOptions {
  int port = 8080;
  bool bind_all = false;
  bool launch_browser = false;
  int workers = 0;
  std::string auth_token;
};

int run(const ServerOptions& options);

}  // namespace cad::server
