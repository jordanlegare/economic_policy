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
  DecisionLossWeights loss_weights{};
  bool internal_weights_changed = false;
  std::string internal_weight_dimension = "reference";
  double internal_weight_scale = 1.0;
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
  bool internal_component_weights_fixed = true;
  std::string internal_weight_dimension = "reference";
  double internal_weight_scale = 1.0;
};

struct WelfareAlternativeSupport {
  std::string strategy_id;
  int wins = 0;
  double win_rate = 0.0;
};

struct WelfareSensitivitySummary {
  std::string methodology = "delegation-and-internal-weight-grid-v2";
  std::string reference_strategy;
  int profile_count = 0;
  int internal_weight_profile_count = 0;
  int exact_recommendation_wins = 0;
  int strategy_family_wins = 0;
  int sector_package_wins = 0;
  int recommendation_switches = 0;
  int internal_weight_switches = 0;
  double exact_recommendation_retention_rate = 0.0;
  double strategy_family_retention_rate = 0.0;
  double sector_package_retention_rate = 0.0;
  bool priority_switch_observed = false;
  double nearest_priority_switch_points = 0.0;
  bool risk_switch_observed = false;
  double nearest_risk_switch_points = 0.0;
  bool internal_weight_switch_observed = false;
  double fairness_min = 0.0;
  double fairness_max = 0.0;
  bool all_growth_constraints_met = true;
  // The legal/institutional mandate remains fixed across every profile. The
  // separately reported internal-component flag distinguishes sensitivity of
  // model-design coefficients from changing the mandate itself.
  bool all_mandate_weights_fixed = true;
  bool all_internal_component_weights_fixed = true;
  std::string classification = "not-evaluated";
  std::vector<WelfareAlternativeSupport> alternatives;
  std::vector<WelfareSensitivityCase> cases;
};

std::vector<WelfarePreferenceProfile> make_welfare_preference_grid(const Economy& economy);
WelfareSensitivitySummary evaluate_welfare_sensitivity(
    const PolicyEngine& engine, const Economy& economy,
    const std::vector<WelfarePreferenceProfile>& profiles = {});
WelfareSensitivitySummary evaluate_welfare_sensitivity(
    const PolicyEngine& engine, const Economy& economy,
    const std::vector<WelfarePreferenceProfile>& profiles,
    EvaluationOptions options);
std::string welfare_sensitivity_to_json(const WelfareSensitivitySummary& summary);

}  // namespace cad
