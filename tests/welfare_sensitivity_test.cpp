#include "welfare_sensitivity.hpp"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

int main() {
  cad::Economy economy;
  cad::PolicyEngine engine(20260810);

  const auto grid = cad::make_welfare_preference_grid(economy);
  assert(grid.size() == 15);
  bool found_reference = false;
  int internal_profiles = 0;
  for (const auto& p : grid) {
    assert(std::abs(p.canada_priority + p.us_priority - 100.0) < 1e-12);
    assert(p.canada_priority >= 5.0 && p.canada_priority <= 95.0);
    assert(p.risk_aversion >= 0.0 && p.risk_aversion <= 100.0);
    found_reference = found_reference || p.profile_id == "reference";
    if (p.internal_weights_changed) {
      ++internal_profiles;
      assert(p.internal_weight_dimension != "reference");
      assert(std::abs(p.internal_weight_scale - 1.0) > 1e-12);
    }
  }
  assert(found_reference);
  assert(internal_profiles == 6);

  cad::WelfarePreferenceProfile reference;
  reference.profile_id = "reference";
  reference.loss_weights = economy.loss_weights;
  cad::WelfarePreferenceProfile canada_tilt = reference;
  canada_tilt.profile_id = "canada-tilt";
  canada_tilt.canada_priority = 65.0;
  canada_tilt.us_priority = 35.0;
  canada_tilt.priority_shift_points = 15.0;
  cad::WelfarePreferenceProfile risk_averse = reference;
  risk_averse.profile_id = "risk-averse";
  risk_averse.risk_aversion = 75.0;
  risk_averse.risk_shift_points = 25.0;
  cad::WelfarePreferenceProfile internal = reference;
  internal.profile_id = "boc-inflation-high";
  internal.internal_weights_changed = true;
  internal.internal_weight_dimension = "boc_inflation";
  internal.internal_weight_scale = 1.2;
  internal.loss_weights.boc_inflation *= 1.2;

  const std::vector<cad::WelfarePreferenceProfile> profiles{
      reference, canada_tilt, risk_averse, internal};
  const auto summary = cad::evaluate_welfare_sensitivity(engine, economy, profiles);

  assert(summary.methodology == "delegation-and-internal-weight-grid-v2");
  assert(summary.profile_count == 4);
  assert(summary.internal_weight_profile_count == 1);
  assert(summary.cases.size() == 4);
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
  assert(!summary.all_internal_component_weights_fixed);
  assert(summary.classification != "not-evaluated");

  const auto& reference_case = summary.cases.front();
  assert(reference_case.profile_id == "reference");
  assert(reference_case.same_reference_controls);
  assert(reference_case.same_strategy_family);
  assert(reference_case.same_sector_package);
  assert(reference_case.mandate_weights_fixed);
  assert(reference_case.internal_component_weights_fixed);

  const auto& internal_case = summary.cases.back();
  assert(internal_case.internal_weight_dimension == "boc_inflation");
  assert(std::abs(internal_case.internal_weight_scale - 1.2) < 1e-12);
  assert(!internal_case.internal_component_weights_fixed);

  int alternative_wins = 0;
  for (const auto& alternative : summary.alternatives) {
    alternative_wins += alternative.wins;
    assert(alternative.win_rate >= 0.0 && alternative.win_rate <= 1.0);
  }
  assert(alternative_wins == 4);

  if (summary.priority_switch_observed)
    assert(std::abs(summary.nearest_priority_switch_points - 15.0) < 1e-12);
  if (summary.risk_switch_observed)
    assert(std::abs(summary.nearest_risk_switch_points - 25.0) < 1e-12);
  if (summary.internal_weight_switch_observed)
    assert(summary.internal_weight_switches > 0);

  const auto json = cad::welfare_sensitivity_to_json(summary);
  assert(json.find("\"methodology\":\"delegation-and-internal-weight-grid-v2\"") != std::string::npos);
  assert(json.find("\"profileCount\":4") != std::string::npos);
  assert(json.find("\"internalWeightProfileCount\":1") != std::string::npos);
  assert(json.find("\"allMandateWeightsFixed\":true") != std::string::npos);
  assert(json.find("\"allInternalComponentWeightsFixed\":false") != std::string::npos);
  assert(json.find("\"internalWeightDimension\":\"boc_inflation\"") != std::string::npos);
  assert(json.find("\"alternatives\":[") != std::string::npos);
  assert(json.find("\"cases\":[") != std::string::npos);

  return 0;
}
