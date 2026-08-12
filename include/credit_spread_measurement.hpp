#pragma once

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace cad {

struct CreditSpreadBenchmark {
  std::string fixture_id;
  std::string decision_date;
  std::string reference_date;
  double spread_percentage_points = 0.0;
  std::string source_id;
  std::string measurement_family;
  std::string methodology;
  std::string reconstruction_vintage;
  std::string notes;
};

namespace credit_spread_detail {

inline bool iso_date(const std::string& value) {
  return value.size() == 10 && value[4] == '-' && value[7] == '-';
}

inline std::vector<std::string> csv_fields(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
        field.push_back('"');
        ++i;
      } else {
        quoted = !quoted;
      }
    } else if (c == ',' && !quoted) {
      fields.push_back(field);
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  fields.push_back(field);
  return fields;
}

inline double number(const std::string& value) {
  try { return std::stod(value); } catch (...) { return 0.0; }
}

}  // namespace credit_spread_detail

inline std::vector<CreditSpreadBenchmark> load_credit_spread_benchmarks(
    const std::string& path) {
  std::vector<CreditSpreadBenchmark> out;
  std::ifstream in(path);
  if (!in) return out;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (header) { header = false; continue; }
    const auto f = credit_spread_detail::csv_fields(line);
    if (f.size() < 9) continue;
    out.push_back({f[0], f[1], f[2], credit_spread_detail::number(f[3]),
                   f[4], f[5], f[6], f[7], f[8]});
  }
  return out;
}

inline bool credit_spread_benchmark_valid(const CreditSpreadBenchmark& record) {
  const bool family_ok = record.measurement_family == "ice-bofa-canada-investment-grade"
      || record.measurement_family == "statistics-canada-corporate-government";
  return !record.fixture_id.empty()
      && credit_spread_detail::iso_date(record.decision_date)
      && credit_spread_detail::iso_date(record.reference_date)
      && record.reference_date <= record.decision_date
      && std::isfinite(record.spread_percentage_points)
      && record.spread_percentage_points > 0.0
      && record.spread_percentage_points < 10.0
      && !record.source_id.empty()
      && family_ok
      && !record.methodology.empty()
      && record.reconstruction_vintage == "public-historical-reconstruction";
}

}  // namespace cad
