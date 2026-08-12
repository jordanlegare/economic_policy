#pragma once

#include "calibration.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct QuarterlyStructuralEstimate {
  std::string parameter;
  double estimate = 0.0;
  double standard_error = 0.0;
  double lower_bound = 0.0;
  double upper_bound = 0.0;
  int observations = 0;
  std::string sample_start;
  std::string sample_end;
  std::string method;
  bool direct_eligible = false;
  std::string notes;
};

struct QuarterlyStructuralEstimation {
  bool loaded = false;
  std::vector<QuarterlyStructuralEstimate> estimates;
  double output_inflation_residual_correlation = 0.0;
  int residual_covariance_observations = 0;

  const QuarterlyStructuralEstimate* find(const std::string& name) const {
    for (const auto& e : estimates) if (e.parameter == name) return &e;
    return nullptr;
  }
};

inline QuarterlyStructuralEstimation load_quarterly_structural_estimation(
    const std::string& estimates_path, const std::string& covariance_path) {
  QuarterlyStructuralEstimation out;
  std::ifstream estimates(estimates_path);
  if (!estimates) return out;
  std::string line;
  bool header = true;
  while (std::getline(estimates, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 11) continue;
    QuarterlyStructuralEstimate e;
    e.parameter = f[0];
    e.estimate = calibration_detail::number(f[1]);
    e.standard_error = calibration_detail::number(f[2]);
    e.lower_bound = calibration_detail::number(f[3]);
    e.upper_bound = calibration_detail::number(f[4]);
    e.observations = static_cast<int>(calibration_detail::number(f[5]));
    e.sample_start = f[6]; e.sample_end = f[7]; e.method = f[8];
    e.direct_eligible = calibration_detail::yes(f[9]); e.notes = f[10];
    out.estimates.push_back(std::move(e));
  }
  std::ifstream covariance(covariance_path);
  if (!covariance) return out;
  header = true;
  while (std::getline(covariance, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 5) continue;
    if (f[0] == "output_gap_residual" && f[1] == "inflation_residual") {
      out.output_inflation_residual_correlation = calibration_detail::number(f[3]);
      out.residual_covariance_observations = static_cast<int>(calibration_detail::number(f[4]));
    }
  }
  out.loaded = !out.estimates.empty() && out.residual_covariance_observations > 0;
  return out;
}

inline int quarterly_direct_eligible_count(const QuarterlyStructuralEstimation& estimation) {
  int count = 0;
  for (const auto& e : estimation.estimates) if (e.direct_eligible) ++count;
  return count;
}

inline bool quarterly_estimation_valid(const QuarterlyStructuralEstimation& estimation) {
  if (!estimation.loaded || estimation.estimates.empty()
      || estimation.residual_covariance_observations < 60
      || !std::isfinite(estimation.output_inflation_residual_correlation)
      || std::abs(estimation.output_inflation_residual_correlation) > 1.0) return false;
  for (const auto& e : estimation.estimates) {
    if (e.parameter.empty() || e.observations < 60 || e.sample_start.empty()
        || e.sample_end.empty() || e.method.empty() || e.upper_bound < e.lower_bound
        || !std::isfinite(e.estimate) || !std::isfinite(e.standard_error)) return false;
  }
  return true;
}

inline std::string quarterly_estimation_to_json(const QuarterlyStructuralEstimation& e) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"loaded\":" << (e.loaded ? "true" : "false")
      << ",\"valid\":" << (quarterly_estimation_valid(e) ? "true" : "false")
      << ",\"estimateCount\":" << e.estimates.size()
      << ",\"directEligibleCount\":" << quarterly_direct_eligible_count(e)
      << ",\"residualCorrelation\":" << e.output_inflation_residual_correlation
      << ",\"residualObservations\":" << e.residual_covariance_observations << "}";
  return out.str();
}

}  // namespace cad
