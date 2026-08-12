#include "welfare_sensitivity.hpp"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

int main() {
  cad::Economy economy;
  cad::PolicyEngine engine(20260810);

  const auto grid = cad::make_welfare_preference_grid(economy);
  assert(grid.size() == 9);
  bool found_reference = false;
  for (const auto& p : grid) {
    assert(std::abs(p.canada_priority + p.us_priority - 100.0) < 1e-12);
    assert(p.canada_priority >= 5.0 && p.canada_priority <= 95.0);
    assert(p.risk_aversion >= 0.0 && p.risk_aversion <= 100.0);
    found_reference = found_reference || p.profile_id == "reference";
  }
  assert(found_reference);

  const std::vector<cad::WelfarePreferenceProfile> profiles{
      {"reference", 50.0, 50.0, 50.0, 0.0, 0.0},
      {"canada-tilt", 65.0, 35.0, 50.0, 15.0, 0.0},
      {"risk-averse", 50.0, 50.0, 75.0, 0.0, 25.0}};
  const auto summary = cad::evaluate_welfare_sensitivity(engine, economy, profiles);

  assert(summary.methodology == "delegation-preference-grid-v1");
  assert(summary.profile_count == 3);
  assert(summary.cases.size() == 3);
  assert(!summary.reference_strategy.empty());
  assert(summary.exact_recommendation_retention_rate >= 0.0
      && summary.exact_recommendation_retention_rate <= 1.0);
  assert(summary.strategy_family_retention_rate >= 0.0
      && summary.strategy_family_retention_rate <= 1.0);
  assert(summary.sector_package_retention_rate >= 0.0
      && summary.sector_package_retention_rate <= 1.0);
  assert(summary.fairness_min <= summary.fairness_max + 1e-12);
  assert(std::isfinite(summary.fairness_min));
  assert(std::isfinite(summary.fairness_max));
  assert(summary.all_mandate_weights_fixed);
  assert(summary.classification != "not-evaluated");

  const auto& reference = summary.cases.front();
  assert(reference.profile_id == "reference");
  assert(reference.same_reference_controls);
  assert(reference.same_strategy_family);
  assert(reference.same_sector_package);
  assert(reference.mandate_weights_fixed);

  int alternative_wins = 0;
  for (const auto& alternative : summary.alternatives) {
    alternative_wins += alternative.wins;
    assert(alternative.win_rate >= 0.0 && alternative.win_rate <= 1.0);
  }
  assert(alternative_wins == 3);

  if (summary.priority_switch_observed)
    assert(std::abs(summary.nearest_priority_switch_points - 15.0) < 1e-12);
  if (summary.risk_switch_observed)
    assert(std::abs(summary.nearest_risk_switch_points - 25.0) < 1e-12);

  const auto json = cad::welfare_sensitivity_to_json(summary);
  assert(json.find("\"methodology\":\"delegation-preference-grid-v1\"") != std::string::npos);
  assert(json.find("\"profileCount\":3") != std::string::npos);
  assert(json.find("\"allMandateWeightsFixed\":true") != std::string::npos);
  assert(json.find("\"alternatives\":[") != std::string::npos);
  assert(json.find("\"cases\":[") != std::string::npos);

  return 0;
}
