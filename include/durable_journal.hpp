#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cad::durable_journal {

inline bool flush_to_disk(std::FILE* file) {
  if (!file || std::fflush(file) != 0) return false;
#ifdef _WIN32
  return _commit(_fileno(file)) == 0;
#else
  return fsync(fileno(file)) == 0;
#endif
}

inline bool append_line(const std::string& path, std::string line) {
  try {
    for (char& c : line) if (c == '\n' || c == '\r') c = ' ';
    const std::filesystem::path file_path(path);
    if (!file_path.parent_path().empty())
      std::filesystem::create_directories(file_path.parent_path());

    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (!file) return false;
    const bool wrote_body = line.empty()
        || std::fwrite(line.data(), 1, line.size(), file) == line.size();
    const bool wrote_newline = std::fwrite("\n", 1, 1, file) == 1;
    const bool durable = wrote_body && wrote_newline && flush_to_disk(file);
    const bool closed = std::fclose(file) == 0;
    return durable && closed;
  } catch (...) {
    return false;
  }
}

inline std::vector<std::string> read_lines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream in(path, std::ios::binary);
  if (!in) return lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(std::move(line));
  return lines;
}

}  // namespace cad::durable_journal
