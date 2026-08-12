#pragma once

#include "backtest.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace cad {

struct BacktestSuiteSummary {
  int fixture_count = 0;
  int valid_count = 0;
  int no_lookahead_count = 0;
  int provenance_complete_count = 0;
  int expanded_complete_count = 0;
  double mean_core_input_coverage = 0.0;
  double mean_extended_input_coverage = 0.0;
  double mean_state_coverage = 0.0;
  double minimum_state_coverage = 0.0;

  int policy_benchmark_count = 0;
  int policy_direction_matches = 0;
  double policy_direction_accuracy = 0.0;
  double mean_abs_policy_error_bp = 0.0;

  int inflation_count = 0;
  int inflation_direction_matches = 0;
  double inflation_direction_accuracy = 0.0;
  double mean_abs_inflation_error = 0.0;

  int growth_count = 0;
  int growth_direction_matches = 0;
  double growth_direction_accuracy = 0.0;
  double mean_abs_growth_error = 0.0;

  int unemployment_count = 0;
  int unemployment_direction_matches = 0;
  double unemployment_direction_accuracy = 0.0;
  double mean_abs_unemployment_error = 0.0;

  bool all_valid_no_lookahead = false;
  bool all_expanded_state_complete = false;
  bool aggregate_diagnostics_permitted = false;
};

namespace backtest_suite_detail {

inline double ratio(int numerator, int denominator) {
  return denominator > 0
      ? static_cast<double>(numerator) / static_cast<double>(denominator)
      : 0.0;
}

inline void add_metric(const BacktestMetric& metric, int& count, int& direction_matches,
                       double& abs_error_sum) {
  if (!metric.available) return;
  ++count;
  if (metric.direction_match) ++direction_matches;
  abs_error_sum += std::abs(metric.error);
}

}  // namespace backtest_suite_detail

inline BacktestSuiteSummary summarize_backtests(const std::vector<BacktestResult>& results) {
  BacktestSuiteSummary out;
  out.fixture_count = static_cast<int>(results.size());
  double policy_abs_error = 0.0;
  double inflation_abs_error = 0.0;
  double growth_abs_error = 0.0;
  double unemployment_abs_error = 0.0;
  double core_coverage_sum = 0.0;
  double extended_coverage_sum = 0.0;
  double state_coverage_sum = 0.0;
  out.minimum_state_coverage = results.empty() ? 0.0 : 100.0;

  for (const auto& result : results) {
    if (result.valid) ++out.valid_count;
    if (result.no_lookahead) ++out.no_lookahead_count;
    if (result.provenance_complete) ++out.provenance_complete_count;
    if (result.state_grade == "expanded-complete") ++out.expanded_complete_count;
    core_coverage_sum += result.input_coverage;
    extended_coverage_sum += result.extended_input_coverage;
    state_coverage_sum += result.state_coverage;
    out.minimum_state_coverage = std::min(out.minimum_state_coverage, result.state_coverage);

    if (result.valid && result.policy_benchmark_available) {
      ++out.policy_benchmark_count;
      if (result.policy_direction_match) ++out.policy_direction_matches;
      policy_abs_error += std::abs(result.policy_move_error_bp);
    }
    if (!result.valid) continue;
    backtest_suite_detail::add_metric(result.inflation, out.inflation_count,
        out.inflation_direction_matches, inflation_abs_error);
    backtest_suite_detail::add_metric(result.growth, out.growth_count,
        out.growth_direction_matches, growth_abs_error);
    backtest_suite_detail::add_metric(result.unemployment, out.unemployment_count,
        out.unemployment_direction_matches, unemployment_abs_error);
  }

  if (out.fixture_count) {
    const double n = static_cast<double>(out.fixture_count);
    out.mean_core_input_coverage = core_coverage_sum / n;
    out.mean_extended_input_coverage = extended_coverage_sum / n;
    out.mean_state_coverage = state_coverage_sum / n;
  }

  out.policy_direction_accuracy = backtest_suite_detail::ratio(
      out.policy_direction_matches, out.policy_benchmark_count);
  out.inflation_direction_accuracy = backtest_suite_detail::ratio(
      out.inflation_direction_matches, out.inflation_count);
  out.growth_direction_accuracy = backtest_suite_detail::ratio(
      out.growth_direction_matches, out.growth_count);
  out.unemployment_direction_accuracy = backtest_suite_detail::ratio(
      out.unemployment_direction_matches, out.unemployment_count);

  if (out.policy_benchmark_count)
    out.mean_abs_policy_error_bp = policy_abs_error / out.policy_benchmark_count;
  if (out.inflation_count)
    out.mean_abs_inflation_error = inflation_abs_error / out.inflation_count;
  if (out.growth_count)
    out.mean_abs_growth_error = growth_abs_error / out.growth_count;
  if (out.unemployment_count)
    out.mean_abs_unemployment_error = unemployment_abs_error / out.unemployment_count;

  out.all_valid_no_lookahead = out.fixture_count > 0
      && out.valid_count == out.fixture_count
      && out.no_lookahead_count == out.fixture_count
      && out.provenance_complete_count == out.fixture_count;
  out.all_expanded_state_complete = out.fixture_count > 0
      && out.expanded_complete_count == out.fixture_count;

  // Three independent episodes is the minimum project threshold for showing
  // aggregate diagnostics. This is a reporting guard, not a statistical claim
  // that three observations constitute validation.
  out.aggregate_diagnostics_permitted = out.all_valid_no_lookahead
      && out.fixture_count >= 3;
  return out;
}

inline std::string backtest_suite_to_json(const BacktestSuiteSummary& s) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"fixtureCount\":" << s.fixture_count
      << ",\"validCount\":" << s.valid_count
      << ",\"noLookaheadCount\":" << s.no_lookahead_count
      << ",\"provenanceCompleteCount\":" << s.provenance_complete_count
      << ",\"expandedCompleteCount\":" << s.expanded_complete_count
      << ",\"meanCoreInputCoverage\":" << s.mean_core_input_coverage
      << ",\"meanExtendedInputCoverage\":" << s.mean_extended_input_coverage
      << ",\"meanStateCoverage\":" << s.mean_state_coverage
      << ",\"minimumStateCoverage\":" << s.minimum_state_coverage
      << ",\"allValidNoLookahead\":" << (s.all_valid_no_lookahead ? "true" : "false")
      << ",\"allExpandedStateComplete\":"
      << (s.all_expanded_state_complete ? "true" : "false")
      << ",\"aggregateDiagnosticsPermitted\":"
      << (s.aggregate_diagnostics_permitted ? "true" : "false")
      << ",\"policy\":{\"count\":" << s.policy_benchmark_count
      << ",\"directionMatches\":" << s.policy_direction_matches
      << ",\"directionAccuracy\":" << s.policy_direction_accuracy
      << ",\"meanAbsoluteErrorBp\":" << s.mean_abs_policy_error_bp << "}"
      << ",\"inflation\":{\"count\":" << s.inflation_count
      << ",\"directionMatches\":" << s.inflation_direction_matches
      << ",\"directionAccuracy\":" << s.inflation_direction_accuracy
      << ",\"meanAbsoluteError\":" << s.mean_abs_inflation_error << "}"
      << ",\"growth\":{\"count\":" << s.growth_count
      << ",\"directionMatches\":" << s.growth_direction_matches
      << ",\"directionAccuracy\":" << s.growth_direction_accuracy
      << ",\"meanAbsoluteError\":" << s.mean_abs_growth_error << "}"
      << ",\"unemployment\":{\"count\":" << s.unemployment_count
      << ",\"directionMatches\":" << s.unemployment_direction_matches
      << ",\"directionAccuracy\":" << s.unemployment_direction_accuracy
      << ",\"meanAbsoluteError\":" << s.mean_abs_unemployment_error << "}}";
  return out.str();
}

}  // namespace cad
