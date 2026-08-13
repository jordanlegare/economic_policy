#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace cad::request_json {

enum class Kind { number, boolean, string, null_value };

struct Scalar {
  Kind kind = Kind::null_value;
  double number_value = 0.0;
  bool bool_value = false;
  std::string string_value;
};

struct Object {
  bool valid = false;
  std::string error;
  std::map<std::string, Scalar> values;

  const Scalar* find(const std::string& key) const {
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
  }

  std::optional<double> number(const std::string& key) const {
    const auto* value = find(key);
    if (!value) return std::nullopt;
    if (value->kind != Kind::number) return std::nullopt;
    return value->number_value;
  }

  std::optional<bool> boolean(const std::string& key) const {
    const auto* value = find(key);
    if (!value) return std::nullopt;
    if (value->kind != Kind::boolean) return std::nullopt;
    return value->bool_value;
  }

  std::optional<std::string> string(const std::string& key) const {
    const auto* value = find(key);
    if (!value) return std::nullopt;
    if (value->kind != Kind::string) return std::nullopt;
    return value->string_value;
  }
};

namespace detail {

inline void skip_ws(std::string_view input, std::size_t& pos) {
  while (pos < input.size()) {
    const char c = input[pos];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    ++pos;
  }
}

inline bool hex_digit(char c, unsigned& value) {
  if (c >= '0' && c <= '9') value = static_cast<unsigned>(c - '0');
  else if (c >= 'a' && c <= 'f') value = 10u + static_cast<unsigned>(c - 'a');
  else if (c >= 'A' && c <= 'F') value = 10u + static_cast<unsigned>(c - 'A');
  else return false;
  return true;
}

inline void append_utf8(std::string& out, unsigned cp) {
  if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
  else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
  } else {
    out.push_back(static_cast<char>(0xe0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
  }
}

inline bool parse_string(std::string_view input, std::size_t& pos,
                         std::string& out, std::string& error) {
  if (pos >= input.size() || input[pos] != '"') {
    error = "expected JSON string";
    return false;
  }
  ++pos;
  out.clear();
  while (pos < input.size()) {
    const unsigned char c = static_cast<unsigned char>(input[pos++]);
    if (c == '"') return true;
    if (c < 0x20) {
      error = "control character in JSON string";
      return false;
    }
    if (c != '\\') {
      out.push_back(static_cast<char>(c));
      continue;
    }
    if (pos >= input.size()) {
      error = "unterminated JSON escape";
      return false;
    }
    const char esc = input[pos++];
    switch (esc) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        if (pos + 4 > input.size()) {
          error = "short unicode escape";
          return false;
        }
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
          unsigned digit = 0;
          if (!hex_digit(input[pos++], digit)) {
            error = "invalid unicode escape";
            return false;
          }
          cp = (cp << 4) | digit;
        }
        if (cp >= 0xd800 && cp <= 0xdfff) {
          error = "surrogate unicode escapes are not accepted in request keys/values";
          return false;
        }
        append_utf8(out, cp);
        break;
      }
      default:
        error = "invalid JSON escape";
        return false;
    }
  }
  error = "unterminated JSON string";
  return false;
}

inline bool parse_number(std::string_view input, std::size_t& pos,
                         double& out, std::string& error) {
  const std::size_t start = pos;
  if (pos < input.size() && input[pos] == '-') ++pos;
  if (pos >= input.size()) {
    error = "invalid JSON number";
    return false;
  }
  if (input[pos] == '0') {
    ++pos;
    if (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') {
      error = "leading zero in JSON number";
      return false;
    }
  } else if (input[pos] >= '1' && input[pos] <= '9') {
    while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
  } else {
    error = "invalid JSON number";
    return false;
  }
  if (pos < input.size() && input[pos] == '.') {
    ++pos;
    const std::size_t digits = pos;
    while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
    if (digits == pos) {
      error = "missing fractional digits";
      return false;
    }
  }
  if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
    ++pos;
    if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
    const std::size_t digits = pos;
    while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
    if (digits == pos) {
      error = "missing exponent digits";
      return false;
    }
  }

  const std::string token(input.substr(start, pos - start));
  errno = 0;
  char* end = nullptr;
  out = std::strtod(token.c_str(), &end);
  if (!end || *end != '\0' || errno == ERANGE || !std::isfinite(out)) {
    error = "non-finite or out-of-range JSON number";
    return false;
  }
  return true;
}

inline bool consume(std::string_view input, std::size_t& pos, std::string_view token) {
  if (input.substr(pos, token.size()) != token) return false;
  pos += token.size();
  return true;
}

}  // namespace detail

inline Object parse_object(std::string_view input) {
  Object out;
  std::size_t pos = 0;
  detail::skip_ws(input, pos);
  if (pos >= input.size() || input[pos] != '{') {
    out.error = "request body must be a JSON object";
    return out;
  }
  ++pos;
  detail::skip_ws(input, pos);
  if (pos < input.size() && input[pos] == '}') {
    ++pos;
    detail::skip_ws(input, pos);
    out.valid = pos == input.size();
    if (!out.valid) out.error = "trailing data after JSON object";
    return out;
  }

  while (pos < input.size()) {
    std::string key;
    if (!detail::parse_string(input, pos, key, out.error)) return out;
    if (out.values.find(key) != out.values.end()) {
      out.error = "duplicate JSON key: " + key;
      return out;
    }
    detail::skip_ws(input, pos);
    if (pos >= input.size() || input[pos] != ':') {
      out.error = "expected ':' after JSON key";
      return out;
    }
    ++pos;
    detail::skip_ws(input, pos);
    if (pos >= input.size()) {
      out.error = "missing JSON value";
      return out;
    }

    Scalar value;
    if (input[pos] == '"') {
      value.kind = Kind::string;
      if (!detail::parse_string(input, pos, value.string_value, out.error)) return out;
    } else if (input[pos] == '-' || (input[pos] >= '0' && input[pos] <= '9')) {
      value.kind = Kind::number;
      if (!detail::parse_number(input, pos, value.number_value, out.error)) return out;
    } else if (detail::consume(input, pos, "true")) {
      value.kind = Kind::boolean;
      value.bool_value = true;
    } else if (detail::consume(input, pos, "false")) {
      value.kind = Kind::boolean;
      value.bool_value = false;
    } else if (detail::consume(input, pos, "null")) {
      value.kind = Kind::null_value;
    } else {
      out.error = "request values must be scalar JSON values";
      return out;
    }
    out.values.emplace(std::move(key), std::move(value));

    detail::skip_ws(input, pos);
    if (pos >= input.size()) {
      out.error = "unterminated JSON object";
      return out;
    }
    if (input[pos] == '}') {
      ++pos;
      detail::skip_ws(input, pos);
      if (pos != input.size()) {
        out.error = "trailing data after JSON object";
        return out;
      }
      out.valid = true;
      return out;
    }
    if (input[pos] != ',') {
      out.error = "expected ',' between JSON members";
      return out;
    }
    ++pos;
    detail::skip_ws(input, pos);
  }

  out.error = "unterminated JSON object";
  return out;
}

inline bool number_in_range(const Object& object, const std::string& key,
                            double lo, double hi, double& target,
                            std::string& error) {
  const auto* scalar = object.find(key);
  if (!scalar) return true;
  if (scalar->kind != Kind::number) {
    error = key + " must be numeric";
    return false;
  }
  if (scalar->number_value < lo || scalar->number_value > hi) {
    error = key + " is outside its allowed range";
    return false;
  }
  target = scalar->number_value;
  return true;
}

inline bool boolean_value(const Object& object, const std::string& key,
                          bool fallback, bool& target, std::string& error) {
  const auto* scalar = object.find(key);
  if (!scalar) {
    target = fallback;
    return true;
  }
  if (scalar->kind != Kind::boolean) {
    error = key + " must be boolean";
    return false;
  }
  target = scalar->bool_value;
  return true;
}

}  // namespace cad::request_json
