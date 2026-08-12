#pragma once

#include "policy_engine.hpp"

#include <string>
#include <vector>

namespace cad {

struct WelfarePreferenceProfile {
  std::string profile_id;
  double canada_priority = 50.0;
  double us_priority = 50.0;
  double risk_aversion = 50.0;
  double priority_shift_points = 0.0;
  double risk_shift_points = 0.0;
};

struct WelfareSensitivityCase {
  std::string profile_id;
  std::string strategy_id;
  double canada_priority = 50.0;
  double us_priority = 50.0;
  double risk_aversion = 50.0;
  double priority_shift_points = 0.0;
  double risk_shift_points = 0.0;
  double fairness_score = 0.0;
  double first_move_bp = 0.0;
  double fiscal_impulse = 0.0;
  bool same_reference_controls = false;
  bool same_strategy_family = false;
  bool same_sector_package = false;
  bool growth_constraint_met = false;
  bool mandate_weights_fixed = true;
};

struct WelfareAlternativeSupport {
  std::string strategy_id;
  int wins = 0;
  double win_rate = 0.0;
};

struct WelfareSensitivitySummary {
  std::string methodology = "delegation-preference-grid-v1";
  std::string reference_strategy;
  int profile_count = 0;
  int exact_recommendation_wins = 0;
  int strategy_family_wins = 0;
  int sector_package_wins = 0;
  int recommendation_switches = 0;
  double exact_recommendation_retention_rate = 0.0;
  double strategy_family_retention_rate = 0.0;
  double sector_package_retention_rate = 0.0;
  bool priority_switch_observed = false;
  double nearest_priority_switch_points = 0.0;
  bool risk_switch_observed = false;
  double nearest_risk_switch_points = 0.0;
  double fairness_min = 0.0;
  double fairness_max = 0.0;
  bool all_growth_constraints_met = true;
  bool all_mandate_weights_fixed = true;
  std::string classification = "not-evaluated";
  std::vector<WelfareAlternativeSupport> alternatives;
  std::vector<WelfareSensitivityCase> cases;
};

std::vector<WelfarePreferenceProfile> make_welfare_preference_grid(const Economy& economy);
WelfareSensitivitySummary evaluate_welfare_sensitivity(
    const PolicyEngine& engine, const Economy& economy,
    const std::vector<WelfarePreferenceProfile>& profiles = {});
std::string welfare_sensitivity_to_json(const WelfareSensitivitySummary& summary);

}  // namespace cad
