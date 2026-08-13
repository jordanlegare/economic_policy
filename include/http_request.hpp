#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace cad::http {

struct RequestHead {
  bool valid = false;
  std::string error;
  std::string method;
  std::string target;
  std::string path;
  std::string version;
  std::map<std::string, std::string> headers;
  std::size_t content_length = 0;

  std::string header(const std::string& name) const;
};

struct Request : RequestHead {
  std::string body;
};

inline std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

inline std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

inline std::string RequestHead::header(const std::string& name) const {
  const auto it = headers.find(lowercase(name));
  return it == headers.end() ? std::string{} : it->second;
}

inline bool parse_decimal_size(const std::string& value, std::size_t& out) {
  if (value.empty()) return false;
  std::size_t result = 0;
  for (const unsigned char c : value) {
    if (c < '0' || c > '9') return false;
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (result > (static_cast<std::size_t>(-1) - digit) / 10) return false;
    result = result * 10 + digit;
  }
  out = result;
  return true;
}

inline RequestHead parse_head(std::string_view raw_head,
                              std::size_t max_header_bytes = 32 * 1024,
                              std::size_t max_body_bytes = 128 * 1024) {
  RequestHead out;
  if (raw_head.size() > max_header_bytes) {
    out.error = "request headers too large";
    return out;
  }

  const auto first_end = raw_head.find("\r\n");
  const std::string first(raw_head.substr(0, first_end));
  std::istringstream line(first);
  if (!(line >> out.method >> out.target >> out.version)) {
    out.error = "invalid HTTP request line";
    return out;
  }
  std::string extra;
  if (line >> extra) {
    out.error = "invalid HTTP request line";
    return out;
  }
  if (out.version != "HTTP/1.1" && out.version != "HTTP/1.0") {
    out.error = "unsupported HTTP version";
    return out;
  }
  if (out.target.empty() || out.target.front() != '/') {
    out.error = "request target must be origin-form";
    return out;
  }
  const auto query = out.target.find('?');
  out.path = out.target.substr(0, query);

  std::size_t cursor = first_end == std::string_view::npos
      ? raw_head.size() : first_end + 2;
  while (cursor < raw_head.size()) {
    const auto end = raw_head.find("\r\n", cursor);
    const std::string field(raw_head.substr(
        cursor, end == std::string_view::npos ? raw_head.size() - cursor : end - cursor));
    cursor = end == std::string_view::npos ? raw_head.size() : end + 2;
    if (field.empty()) continue;
    const auto colon = field.find(':');
    if (colon == std::string::npos || colon == 0) {
      out.error = "invalid HTTP header";
      return out;
    }
    const std::string name = lowercase(trim(field.substr(0, colon)));
    const std::string value = trim(field.substr(colon + 1));
    if (name.empty()) {
      out.error = "invalid HTTP header name";
      return out;
    }
    if (out.headers.find(name) != out.headers.end()) {
      out.error = "duplicate HTTP header: " + name;
      return out;
    }
    out.headers.emplace(name, value);
  }

  const auto transfer = lowercase(out.header("transfer-encoding"));
  if (!transfer.empty() && transfer != "identity") {
    out.error = "transfer-encoding is not supported";
    return out;
  }
  const auto length = out.header("content-length");
  if (!length.empty()) {
    if (!parse_decimal_size(length, out.content_length)) {
      out.error = "invalid Content-Length";
      return out;
    }
    if (out.content_length > max_body_bytes) {
      out.error = "request body too large";
      return out;
    }
  }
  if ((out.method == "POST" || out.method == "PUT" || out.method == "PATCH")
      && length.empty()) {
    out.content_length = 0;
  }
  out.valid = true;
  return out;
}

inline Request parse_request(const std::string& raw,
                             std::size_t max_header_bytes = 32 * 1024,
                             std::size_t max_body_bytes = 128 * 1024) {
  Request out;
  const auto split = raw.find("\r\n\r\n");
  if (split == std::string::npos) {
    out.error = "incomplete HTTP headers";
    return out;
  }
  const auto head = parse_head(
      std::string_view(raw).substr(0, split), max_header_bytes, max_body_bytes);
  static_cast<RequestHead&>(out) = head;
  if (!head.valid) return out;
  const std::size_t body_start = split + 4;
  if (raw.size() < body_start + head.content_length) {
    out.valid = false;
    out.error = "incomplete HTTP body";
    return out;
  }
  if (raw.size() != body_start + head.content_length) {
    out.valid = false;
    out.error = "unexpected bytes after HTTP body";
    return out;
  }
  out.body.assign(raw.data() + body_start, head.content_length);
  out.valid = true;
  return out;
}

inline bool is_json_content_type(const RequestHead& request) {
  std::string content_type = lowercase(request.header("content-type"));
  const auto semicolon = content_type.find(';');
  if (semicolon != std::string::npos) content_type.resize(semicolon);
  return trim(content_type) == "application/json";
}

inline bool secure_equals(std::string_view left, std::string_view right) {
  const std::size_t n = std::max(left.size(), right.size());
  unsigned char diff = static_cast<unsigned char>(left.size() ^ right.size());
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
    const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
    diff = static_cast<unsigned char>(diff | (a ^ b));
  }
  return diff == 0;
}

}  // namespace cad::http
