#pragma once

#include "calibration_schema.hpp"
#include "policy_engine.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace cad {

struct DecisionLossCalibrationEntry {
  double baseline = 0.0;
  double sensitivity_low = 0.0;
  double sensitivity_high = 0.0;
};

struct DecisionLossCalibration {
  bool loaded = false;
  bool complete = false;
  int recognized_components = 0;
  DecisionLossWeights weights{};
  std::map<std::string, DecisionLossCalibrationEntry> entries;
};

inline bool assign_decision_loss_weight(DecisionLossWeights& w,
                                        const std::string& name,
                                        double value) {
  if (name == "boc_inflation") w.boc_inflation = value;
  else if (name == "boc_unemployment") w.boc_unemployment = value;
  else if (name == "boc_contraction") w.boc_contraction = value;
  else if (name == "boc_recession") w.boc_recession = value;
  else if (name == "federal_debt") w.federal_debt = value;
  else if (name == "federal_contraction") w.federal_contraction = value;
  else if (name == "federal_unemployment") w.federal_unemployment = value;
  else if (name == "federal_housing") w.federal_housing = value;
  else if (name == "us_exports") w.us_exports = value;
  else if (name == "us_inflation") w.us_inflation = value;
  else if (name == "us_growth") w.us_growth = value;
  else if (name == "us_retaliation") w.us_retaliation = value;
  else return false;
  return true;
}

inline DecisionLossCalibration load_decision_loss_calibration(const std::string& path) {
  DecisionLossCalibration out;
  std::ifstream in(path);
  if (!in) return out;
  out.loaded = true;
  std::string line;
  bool header = true;
  std::set<std::string> recognized;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (header) { header = false; continue; }
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 7) continue;
    const double baseline = calibration_detail::number(f[1]);
    const double low = calibration_detail::number(f[5]);
    const double high = calibration_detail::number(f[6]);
    if (!(baseline >= 0.0) || !(low >= 0.0) || !(high >= low)) continue;
    if (!assign_decision_loss_weight(out.weights, f[0], baseline)) continue;
    out.entries[f[0]] = {baseline, low, high};
    recognized.insert(f[0]);
  }
  out.recognized_components = static_cast<int>(recognized.size());
  out.complete = out.loaded && out.recognized_components == 12;
  return out;
}

inline bool decision_loss_sensitivity_contract_complete(
    const DecisionLossCalibration& calibration) {
  if (!calibration.complete) return false;
  for (const auto& item : calibration.entries) {
    const auto& e = item.second;
    if (!(e.baseline > 0.0)) return false;
    if (std::abs(e.sensitivity_low / e.baseline - 0.80) > 1e-9) return false;
    if (std::abs(e.sensitivity_high / e.baseline - 1.20) > 1e-9) return false;
  }
  return true;
}

}  // namespace cad
