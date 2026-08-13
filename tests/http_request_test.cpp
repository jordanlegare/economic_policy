#include "http_request.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace cad::http;

  const std::string raw =
      "POST /api/evaluate?source=test HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "content-length: 2\r\n"
      "Content-Type: application/json; charset=utf-8\r\n"
      "X-CAD-Session-Id: abc-123\r\n\r\n{}";
  const auto request = parse_request(raw);
  assert(request.valid);
  assert(request.method == "POST");
  assert(request.path == "/api/evaluate");
  assert(request.target == "/api/evaluate?source=test");
  assert(request.content_length == 2);
  assert(request.body == "{}");
  assert(request.header("HOST") == "localhost");
  assert(request.header("x-cad-session-id") == "abc-123");
  assert(is_json_content_type(request));

  const auto duplicate = parse_head(
      "GET / HTTP/1.1\r\nHost: one\r\nhost: two");
  assert(!duplicate.valid);
  assert(duplicate.error.find("duplicate") != std::string::npos);

  const auto chunked = parse_head(
      "POST /api/evaluate HTTP/1.1\r\nTransfer-Encoding: chunked");
  assert(!chunked.valid);
  assert(chunked.error.find("transfer-encoding") != std::string::npos);

  const auto oversized = parse_head(
      "POST /api/evaluate HTTP/1.1\r\nContent-Length: 999999", 32768, 1024);
  assert(!oversized.valid);
  assert(oversized.error == "request body too large");

  const auto extra = parse_request(
      "POST /api/evaluate HTTP/1.1\r\nContent-Length: 2\r\n"
      "Content-Type: application/json\r\n\r\n{}x");
  assert(!extra.valid);
  assert(extra.error.find("unexpected bytes") != std::string::npos);

  assert(secure_equals("Bearer abc", "Bearer abc"));
  assert(!secure_equals("Bearer abc", "Bearer abd"));
  assert(!secure_equals("short", "longer"));

  std::cout << "HTTP request contract tests passed\n";
  return 0;
}
