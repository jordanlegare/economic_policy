#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cad {

struct BacktestDatum {
  std::string name;
  double value = 0.0;
  std::string unit;
  std::string reference_date;
  std::string release_date;
  std::string source_id;
  std::string kind = "observed";
};

struct BacktestSource {
  std::string id;
  std::string agency;
  std::string dataset;
  std::string url;
};

struct BacktestFixture {
  std::string fixture_id = "none";
  std::string decision_date;
  int horizon_quarters = 12;
  std::vector<BacktestDatum> inputs;
  std::vector<BacktestDatum> outcomes;
  std::vector<BacktestDatum> benchmarks;
  std::vector<BacktestSource> sources;
  bool loaded = false;
  bool no_lookahead = false;
  bool provenance_complete = false;
  double input_coverage = 0.0;
  std::string grade = "unusable";
};

struct BacktestMetric {
  bool available = false;
  double initial = 0.0;
  double predicted = 0.0;
  double realized = 0.0;
  double error = 0.0;
  bool direction_match = false;
};

struct BacktestResult {
  std::string fixture_id;
  std::string decision_date;
  int horizon_quarters = 0;
  bool valid = false;
  bool no_lookahead = false;
  bool provenance_complete = false;
  double input_coverage = 0.0;
  std::string fixture_grade;
  std::string recommended_strategy;
  double recommended_first_move_bp = 0.0;
  bool policy_benchmark_available = false;
  double realized_first_move_bp = 0.0;
  double policy_move_error_bp = 0.0;
  bool policy_direction_match = false;
  BacktestMetric inflation;
  BacktestMetric growth;
  BacktestMetric unemployment;
};

namespace backtest_detail {

inline std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
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
      fields.push_back(trim(field));
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  fields.push_back(trim(field));
  return fields;
}

inline double number(const std::string& value, double fallback = 0.0) {
  try { return std::stod(value); } catch (...) { return fallback; }
}

inline bool iso_date(const std::string& value) {
  return value.size() == 10 && value[4] == '-' && value[7] == '-';
}

inline bool released_by(const BacktestDatum& datum, const std::string& decision_date) {
  return iso_date(datum.release_date) && datum.release_date <= decision_date;
}

inline bool released_after(const BacktestDatum& datum, const std::string& decision_date) {
  return iso_date(datum.release_date) && datum.release_date > decision_date;
}

inline const BacktestDatum* find(const std::vector<BacktestDatum>& data,
                                 const std::string& name) {
  for (const auto& datum : data) if (datum.name == name) return &datum;
  return nullptr;
}

inline bool has_source(const BacktestFixture& fixture, const std::string& id) {
  if (id == "backtest_scope") return true;
  for (const auto& source : fixture.sources) if (source.id == id) return true;
  return false;
}

inline std::vector<std::string> required_inputs() {
  return {"policy_rate", "inflation", "core_inflation", "gdp_growth", "unemployment",
          "wage_growth", "us_tariff_canada", "canada_retaliatory_tariff"};
}

inline void finalize(BacktestFixture& fixture) {
  if (!fixture.loaded || !iso_date(fixture.decision_date)) {
    fixture.grade = "unusable";
    return;
  }

  bool temporal_ok = true;
  bool provenance_ok = true;
  for (const auto& datum : fixture.inputs) {
    temporal_ok = temporal_ok && released_by(datum, fixture.decision_date);
    provenance_ok = provenance_ok && !datum.source_id.empty()
        && has_source(fixture, datum.source_id);
  }
  for (const auto& datum : fixture.outcomes) {
    temporal_ok = temporal_ok && released_after(datum, fixture.decision_date);
    provenance_ok = provenance_ok && !datum.source_id.empty()
        && has_source(fixture, datum.source_id);
  }
  for (const auto& datum : fixture.benchmarks) {
    temporal_ok = temporal_ok && released_after(datum, fixture.decision_date);
    provenance_ok = provenance_ok && !datum.source_id.empty()
        && has_source(fixture, datum.source_id);
  }
  fixture.no_lookahead = temporal_ok;
  fixture.provenance_complete = provenance_ok;

  int present = 0;
  const auto required = required_inputs();
  for (const auto& name : required) if (find(fixture.inputs, name)) ++present;
  fixture.input_coverage = required.empty() ? 0.0
      : 100.0 * static_cast<double>(present) / static_cast<double>(required.size());

  if (!fixture.no_lookahead) fixture.grade = "lookahead-failed";
  else if (!fixture.provenance_complete) fixture.grade = "provenance-incomplete";
  else if (fixture.input_coverage >= 99.999) fixture.grade = "vintage-complete";
  else fixture.grade = "vintage-partial";
}

inline void apply_input(Economy& e, const BacktestDatum& d) {
  if (d.name == "policy_rate") e.policy_rate = d.value;
  else if (d.name == "inflation") e.inflation = d.value;
  else if (d.name == "core_inflation") e.core_inflation = d.value;
  else if (d.name == "gdp_growth") e.gdp_growth = d.value;
  else if (d.name == "output_gap") e.output_gap = d.value;
  else if (d.name == "unemployment") e.unemployment = d.value;
  else if (d.name == "wage_growth") e.wage_growth = d.value;
  else if (d.name == "productivity_growth") e.productivity_growth = d.value;
  else if (d.name == "population_growth") e.population_growth = d.value;
  else if (d.name == "inflation_expectations") e.inflation_expectations = d.value;
  else if (d.name == "us_tariff_canada") e.us_tariff_canada = d.value;
  else if (d.name == "canada_retaliatory_tariff") e.canada_retaliatory_tariff = d.value;
  else if (d.name == "global_growth") e.global_growth = d.value;
  else if (d.name == "us_growth") e.us_growth = d.value;
  else if (d.name == "us_inflation") e.us_inflation = d.value;
}

inline BacktestMetric metric(double initial, double predicted,
                             const BacktestDatum* outcome) {
  BacktestMetric m;
  if (!outcome) return m;
  m.available = true;
  m.initial = initial;
  m.predicted = predicted;
  m.realized = outcome->value;
  m.error = predicted - m.realized;
  const double predicted_change = predicted - initial;
  const double realized_change = m.realized - initial;
  const auto sign = [](double x) { return (x > 1e-9) - (x < -1e-9); };
  m.direction_match = sign(predicted_change) == sign(realized_change);
  return m;
}

inline std::string esc(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace backtest_detail

inline BacktestFixture load_backtest_fixture(const std::string& path) {
  BacktestFixture fixture;
  std::ifstream in(path);
  if (!in) return fixture;
  fixture.loaded = true;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto f = backtest_detail::csv_fields(line);
    if (f.empty()) continue;
    if (f[0] == "META" && f.size() >= 3) {
      if (f[1] == "fixture_id") fixture.fixture_id = f[2];
      else if (f[1] == "decision_date") fixture.decision_date = f[2];
      else if (f[1] == "horizon_quarters") fixture.horizon_quarters =
          static_cast<int>(backtest_detail::number(f[2], 12));
    } else if ((f[0] == "INPUT" || f[0] == "OUTCOME" || f[0] == "BENCHMARK")
        && f.size() >= 8) {
      BacktestDatum datum{f[1], backtest_detail::number(f[2]), f[3], f[4], f[5], f[6], f[7]};
      if (f[0] == "INPUT") fixture.inputs.push_back(std::move(datum));
      else if (f[0] == "OUTCOME") fixture.outcomes.push_back(std::move(datum));
      else fixture.benchmarks.push_back(std::move(datum));
    } else if (f[0] == "SOURCE" && f.size() >= 5) {
      fixture.sources.push_back({f[1], f[2], f[3], f[4]});
    }
  }
  backtest_detail::finalize(fixture);
  return fixture;
}

inline Economy apply_backtest_fixture(Economy economy, const BacktestFixture& fixture) {
  if (!fixture.no_lookahead) return economy;
  for (const auto& datum : fixture.inputs) backtest_detail::apply_input(economy, datum);
  return economy;
}

inline BacktestResult run_backtest(const PolicyEngine& engine,
                                   const BacktestFixture& fixture) {
  BacktestResult out;
  out.fixture_id = fixture.fixture_id;
  out.decision_date = fixture.decision_date;
  out.horizon_quarters = fixture.horizon_quarters;
  out.no_lookahead = fixture.no_lookahead;
  out.provenance_complete = fixture.provenance_complete;
  out.input_coverage = fixture.input_coverage;
  out.fixture_grade = fixture.grade;
  if (!fixture.loaded || !fixture.no_lookahead || !fixture.provenance_complete) return out;

  Economy state;
  state = apply_backtest_fixture(state, fixture);
  const Result result = engine.evaluate(state);
  const Scenario* selected = nullptr;
  for (const auto& scenario : result.scenarios) {
    if (scenario.id == result.recommendation.strategy_id) {
      selected = &scenario;
      break;
    }
  }
  if (!selected) return out;

  out.recommended_strategy = selected->id;
  out.recommended_first_move_bp = selected->first_move_bp;
  if (const auto* actual = backtest_detail::find(fixture.benchmarks, "first_move_bp")) {
    out.policy_benchmark_available = true;
    out.realized_first_move_bp = actual->value;
    out.policy_move_error_bp = out.recommended_first_move_bp - actual->value;
    const auto sign = [](double x) { return (x > 1e-9) - (x < -1e-9); };
    out.policy_direction_match = sign(out.recommended_first_move_bp) == sign(actual->value);
  }
  out.inflation = backtest_detail::metric(state.inflation, selected->inflation,
      backtest_detail::find(fixture.outcomes, "inflation"));
  out.growth = backtest_detail::metric(state.gdp_growth, selected->growth,
      backtest_detail::find(fixture.outcomes, "gdp_growth"));
  out.unemployment = backtest_detail::metric(state.unemployment, selected->unemployment,
      backtest_detail::find(fixture.outcomes, "unemployment"));
  out.valid = true;
  return out;
}

inline std::string backtest_to_json(const BacktestResult& r) {
  using backtest_detail::esc;
  auto metric_json = [&](const BacktestMetric& m) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(6)
      << "{\"available\":" << (m.available ? "true" : "false")
      << ",\"initial\":" << m.initial << ",\"predicted\":" << m.predicted
      << ",\"realized\":" << m.realized << ",\"error\":" << m.error
      << ",\"directionMatch\":" << (m.direction_match ? "true" : "false") << "}";
    return o.str();
  };
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"fixtureId\":\"" << esc(r.fixture_id)
    << "\",\"decisionDate\":\"" << esc(r.decision_date)
    << "\",\"horizonQuarters\":" << r.horizon_quarters
    << ",\"valid\":" << (r.valid ? "true" : "false")
    << ",\"noLookahead\":" << (r.no_lookahead ? "true" : "false")
    << ",\"provenanceComplete\":" << (r.provenance_complete ? "true" : "false")
    << ",\"inputCoverage\":" << r.input_coverage
    << ",\"fixtureGrade\":\"" << esc(r.fixture_grade)
    << "\",\"recommendedStrategy\":\"" << esc(r.recommended_strategy)
    << "\",\"recommendedFirstMoveBp\":" << r.recommended_first_move_bp
    << ",\"policyBenchmarkAvailable\":" << (r.policy_benchmark_available ? "true" : "false")
    << ",\"realizedFirstMoveBp\":" << r.realized_first_move_bp
    << ",\"policyMoveErrorBp\":" << r.policy_move_error_bp
    << ",\"policyDirectionMatch\":" << (r.policy_direction_match ? "true" : "false")
    << ",\"inflation\":" << metric_json(r.inflation)
    << ",\"growth\":" << metric_json(r.growth)
    << ",\"unemployment\":" << metric_json(r.unemployment) << "}";
  return o.str();
}

}  // namespace cad
