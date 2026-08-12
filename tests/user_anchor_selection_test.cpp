#include "user_anchor_selection.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

cad::Scenario candidate(std::string id, double score, double coverage,
                        double canada, double us, double relief = 0.0) {
  cad::Scenario scenario;
  scenario.id = std::move(id);
  scenario.name = scenario.id;
  scenario.score = score;
  scenario.boc_score = canada;
  scenario.federal_score = canada;
  scenario.us_score = us;
  scenario.negotiated_relief = relief;
  scenario.bilateral_growth_floor = 0.5;
  scenario.sector_verified = true;
  scenario.applied_us_sector_coverage.fill(coverage);
  scenario.applied_canada_sector_coverage.fill(coverage);
  scenario.sectors.resize(20);
  return scenario;
}

cad::Result result_with(double baseline_score) {
  cad::Result result;
  result.recommendation.baseline_canada_score = 70.0;
  result.recommendation.baseline_us_score = 70.0;
  result.recommendation.verified_win_win = true;
  result.recommendation.growth_constraint_met = true;
  result.recommendation.global_search_complete = true;
  result.recommendation.strategy_id = "max-welfare";
  result.scenarios.push_back(candidate("max-welfare", 90.0, 5.0, 82.0, 84.0));
  result.scenarios.push_back(candidate("near-user", 89.7, 38.0, 81.0, 83.0));
  result.scenarios.push_back(candidate("baseline", baseline_score, 40.0, 70.0, 70.0));
  return result;
}

}  // namespace

int main() {
  cad::Economy calibrated;
  calibrated.us_tariff_canada = 5.0;
  calibrated.canada_retaliatory_tariff = 1.5;
  calibrated.us_sector_coverage.fill(100.0);
  calibrated.canada_sector_coverage.fill(100.0);

  cad::Economy submitted = calibrated;
  submitted.us_sector_coverage.fill(40.0);
  submitted.canada_sector_coverage.fill(40.0);
  assert(cad::user_anchor_detail::user_modified_from_baseline(submitted, calibrated));

  // Welfare remains the gate. The 89.7 package is inside the 0.5-point band
  // around the 90.0 maximum and is much closer to the submitted 40% coverage,
  // so it becomes the visible automatic package.
  auto steered = result_with(88.0);
  cad::apply_user_anchor_selection(submitted, calibrated, steered);
  assert(steered.recommendation.user_anchor_selection_active);
  assert(steered.scenarios.front().id == "near-user");
  assert(steered.recommendation.strategy_id == "near-user");
  assert(steered.recommendation.verified_win_win);
  assert(std::abs(steered.recommendation.best_verified_score - 90.0) < 1e-9);
  assert(steered.scenarios.front().score + steered.recommendation.user_anchor_welfare_tolerance
      + 1e-9 >= steered.recommendation.best_verified_score);
  assert(steered.recommendation.selected_trade_posture_distance
      < steered.scenarios[1].trade_posture_distance);

  // If the exact submitted posture is itself near-best, do not move the user at
  // all: the zero-distance starting posture wins the proximity tie-break.
  auto keep_user = result_with(89.8);
  cad::apply_user_anchor_selection(submitted, calibrated, keep_user);
  assert(keep_user.scenarios.front().id == "baseline");
  assert(std::abs(keep_user.recommendation.selected_trade_posture_distance) < 1e-9);

  // The certified initial opening is still pure maximum welfare. User-anchor
  // proximity becomes active only after the submitted scenario differs from the
  // calibrated opening state.
  auto opening = result_with(89.8);
  cad::apply_user_anchor_selection(calibrated, calibrated, opening);
  assert(!opening.recommendation.user_anchor_selection_active);
  assert(opening.scenarios.front().id == "max-welfare");

  // A materially worse package must never be selected merely because it is
  // closer to the user's posture.
  auto welfare_gate = result_with(89.4);
  welfare_gate.scenarios[1].score = 88.0;
  cad::apply_user_anchor_selection(submitted, calibrated, welfare_gate);
  assert(welfare_gate.scenarios.front().id == "max-welfare");

  return 0;
}