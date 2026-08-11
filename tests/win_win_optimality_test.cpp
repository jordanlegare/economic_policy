#include "negotiation_support.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

cad::Scenario make_scenario(std::string id, std::string name, double ca, double us, double relief) {
  cad::Scenario s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.boc_score = ca;
  s.federal_score = ca;
  s.us_score = us;
  s.negotiated_relief = relief;
  s.export_change = -2.0;
  s.us_export_change = -1.5;
  s.us_growth = 2.0;
  s.sector_verified = true;
  s.applied_us_sector_coverage.fill(50.0);
  s.applied_canada_sector_coverage.fill(50.0);
  return s;
}

struct BruteCandidate {
  const cad::Scenario* scenario = nullptr;
  cad::negotiation_detail::Terms terms;
  cad::negotiation_detail::EvaluatedTerms evaluated;
  double ca_surplus = 0.0;
  double us_surplus = 0.0;
  double nash = 0.0;
};

double rank(const BruteCandidate& c) {
  return c.nash + 0.030 * c.evaluated.stability_score
      + 0.080 * std::min(c.ca_surplus, c.us_surplus);
}

}  // namespace

int main() {
  cad::Economy economy;
  economy.canada_priority = 65.0;
  economy.us_priority = 35.0;
  economy.cooperation_ceiling = 80.0;
  economy.us_tariff_canada = 30.0;
  economy.canada_retaliatory_tariff = 8.0;

  cad::Result result;
  result.recommendation.independent_us_trade_channel = true;
  result.recommendation.trade_balance_is_objective = false;
  result.recommendation.mandate_weights_fixed = true;
  result.recommendation.verification_monte_carlo_draws = 2800;
  result.scenarios.push_back(make_scenario("statusquo", "Status quo", 42.0, 40.0, 0.0));
  result.scenarios.push_back(make_scenario("compact", "Compact", 88.0, 90.0, 70.0));

  const auto analysis = cad::analyze_negotiation(economy, result);
  assert(analysis.data_integrity_pass);
  assert(analysis.candidates_examined == static_cast<int>(result.scenarios.size()) * 3125);

  const double cap = cad::negotiation_detail::clamp_value(
      economy.cooperation_ceiling / 100.0, 0.0, 1.0);
  const double levels[] = {0.0, 0.25, 0.50, 0.75, 1.0};
  std::vector<BruteCandidate> feasible;

  for (const auto& scenario : result.scenarios) {
    for (double ur : levels) for (double cr : levels) for (double border : levels)
      for (double procurement : levels) for (double supply : levels) {
        BruteCandidate c;
        c.scenario = &scenario;
        c.terms.us_tariff_relief = ur * cap;
        c.terms.canada_tariff_relief = cr * cap;
        c.terms.border_facilitation = border;
        c.terms.procurement_reciprocity = procurement;
        c.terms.supply_chain_commitment = supply;
        c.evaluated = cad::negotiation_detail::evaluate_terms(economy, scenario, c.terms);
        c.ca_surplus = c.evaluated.canada_utility - analysis.canada_reservation;
        c.us_surplus = c.evaluated.us_utility - analysis.us_reservation;
        if (c.ca_surplus < -1e-9 || c.us_surplus < -1e-9) continue;
        c.nash = cad::negotiation_detail::generalized_nash(
            c.ca_surplus, c.us_surplus, economy.canada_priority, economy.us_priority);
        feasible.push_back(c);
      }
  }
  assert(!feasible.empty());

  // Independent O(n^2) dominance check: deliberately different from the
  // production sort-and-scan frontier construction.
  std::vector<const BruteCandidate*> frontier;
  for (const auto& candidate : feasible) {
    bool dominated = false;
    for (const auto& other : feasible) {
      const bool no_worse = other.evaluated.canada_utility + 1e-9 >= candidate.evaluated.canada_utility
          && other.evaluated.us_utility + 1e-9 >= candidate.evaluated.us_utility;
      const bool strictly_better = other.evaluated.canada_utility > candidate.evaluated.canada_utility + 1e-9
          || other.evaluated.us_utility > candidate.evaluated.us_utility + 1e-9;
      if (no_worse && strictly_better) { dominated = true; break; }
    }
    if (!dominated) frontier.push_back(&candidate);
  }
  assert(!frontier.empty());

  const BruteCandidate* brute_best = nullptr;
  double best_rank = -std::numeric_limits<double>::infinity();
  for (const auto* candidate : frontier) {
    const double value = rank(*candidate);
    if (value > best_rank) { best_rank = value; brute_best = candidate; }
  }
  assert(brute_best != nullptr);

  // Production search must return the exact best package under its declared
  // finite search grid and ranking objective.
  assert(analysis.recommended.strategy_id == brute_best->scenario->id);
  assert(std::abs(analysis.recommended.canada_utility
      - brute_best->evaluated.canada_utility) < 1e-9);
  assert(std::abs(analysis.recommended.us_utility
      - brute_best->evaluated.us_utility) < 1e-9);
  assert(std::abs(analysis.recommended.nash_gain - brute_best->nash) < 1e-9);
  assert(analysis.recommended.pareto_efficient);
  assert(analysis.recommended.individually_rational);
  assert(analysis.recommended.verified_win_win);

  return 0;
}
