#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <csignal>
#endif

namespace cad::server {

#ifdef _WIN32
namespace detail {

inline BOOL WINAPI console_break_bridge(DWORD control_type) {
  if (control_type != CTRL_BREAK_EVENT) return FALSE;
  // CREATE_NEW_PROCESS_GROUP disables CTRL+C delivery, while CTRL+BREAK remains
  // group-addressable. Bridge it into the same minimal SIGINT handler used by
  // run(), which only flips a sig_atomic_t shutdown flag.
  std::raise(SIGINT);
  return TRUE;
}

struct ConsoleBreakBridge {
  ConsoleBreakBridge() { SetConsoleCtrlHandler(console_break_bridge, TRUE); }
  ~ConsoleBreakBridge() { SetConsoleCtrlHandler(console_break_bridge, FALSE); }
};

inline ConsoleBreakBridge console_break_bridge_registration;

}  // namespace detail
#endif

struct ServerOptions {
  int port = 8080;
  bool bind_all = false;
  bool launch_browser = false;
  int workers = 0;
  std::string auth_token;
};

int run(const ServerOptions& options);

}  // namespace cad::server
