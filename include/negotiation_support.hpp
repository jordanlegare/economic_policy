#pragma once

#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct NegotiationIssueMove {
  std::string id;
  std::string label;
  double canada_move = 0.0;
  double us_move = 0.0;
};

struct NegotiationPackage {
  std::string id;
  std::string strategy_id;
  std::string strategy_name;
  double canada_utility = 0.0;
  double us_utility = 0.0;
  double canada_surplus = 0.0;
  double us_surplus = 0.0;
  double nash_gain = 0.0;
  double stability_score = 0.0;
  double canada_deviation_gain = 0.0;
  double us_deviation_gain = 0.0;
  double canada_export_change = 0.0;
  double us_export_change = 0.0;
  double trade_balance_gap_usd = 0.0;
  bool individually_rational = false;
  bool pareto_efficient = false;
  bool stable = false;
  bool sector_verified = false;
  bool verified_win_win = false;
  std::array<double, 20> us_sector_coverage{};
  std::array<double, 20> canada_sector_coverage{};
  std::vector<NegotiationIssueMove> issues;
};

struct NegotiationAnalysis {
  std::string canada_batna_strategy;
  std::string us_batna_strategy;
  double canada_batna = 0.0;
  double us_batna = 0.0;
  double canada_reservation = 0.0;
  double us_reservation = 0.0;
  int candidates_examined = 0;
  int individually_rational_count = 0;
  int pareto_frontier_size = 0;
  int bargaining_grid_levels = 5;
  int sector_verification_draws = 0;
  bool independent_us_trade_channel = false;
  bool trade_balance_is_objective = true;
  bool mandate_weights_fixed = false;
  bool sector_schedule_verified = false;
  bool data_integrity_pass = false;
  NegotiationPackage recommended;
  std::vector<NegotiationPackage> frontier;
};

namespace negotiation_detail {

inline double clamp_value(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline double canada_payoff(const Scenario& scenario) {
  return std::sqrt(std::max(0.01, scenario.boc_score) * std::max(0.01, scenario.federal_score));
}

inline bool outside_option_candidate(const Scenario& scenario) {
  return scenario.negotiated_relief <= 20.0 && scenario.id != "custom" && scenario.id != "balance";
}

struct Terms {
  double us_tariff_relief = 0.0;
  double canada_tariff_relief = 0.0;
  double border_facilitation = 0.0;
  double procurement_reciprocity = 0.0;
  double supply_chain_commitment = 0.0;
};

struct EvaluatedTerms {
  double canada_utility = 0.0;
  double us_utility = 0.0;
  double canada_export_change = 0.0;
  double us_export_change = 0.0;
  double canada_deviation_gain = 0.0;
  double us_deviation_gain = 0.0;
  double stability_score = 0.0;
};

inline EvaluatedTerms evaluate_terms(const Economy& e, const Scenario& scenario, const Terms& terms) {
  EvaluatedTerms out;
  const double canada_base = canada_payoff(scenario);
  const double us_base = scenario.us_score;

  // The stochastic policy engine has already propagated the selected 20-sector
  // tariff-coverage schedule through two independent trade channels. The
  // bargaining layer therefore starts from those verified country-specific
  // export outcomes instead of reconstructing U.S. welfare from Canadian data.
  const double canada_relief_capacity = e.trade_elasticity * e.us_tariff_canada
      * clamp_value(e.exports_to_us_share / 100.0, 0.0, 1.0);
  const double us_relief_capacity = e.trade_elasticity * e.canada_retaliatory_tariff
      * clamp_value(e.imports_from_us_share / 100.0, 0.0, 1.0);

  out.canada_export_change = scenario.export_change
      + 0.55 * canada_relief_capacity * terms.us_tariff_relief
      + 1.10 * terms.border_facilitation
      + 0.70 * terms.procurement_reciprocity
      + 0.45 * terms.supply_chain_commitment;
  out.us_export_change = scenario.us_export_change
      + 0.55 * us_relief_capacity * terms.canada_tariff_relief
      + 0.95 * terms.border_facilitation
      + 0.95 * terms.procurement_reciprocity
      + 0.35 * terms.supply_chain_commitment;

  const double canada_trade_gain = out.canada_export_change - scenario.export_change;
  const double us_trade_gain = out.us_export_change - scenario.us_export_change;

  const double tariff_link = terms.us_tariff_relief * terms.canada_tariff_relief;
  const double implementation_link = terms.border_facilitation * terms.procurement_reciprocity;
  const double resilience_link = terms.supply_chain_commitment
      * (0.5 * terms.us_tariff_relief + 0.5 * terms.canada_tariff_relief);
  const double linkage_bonus = 1.25 * tariff_link + 0.85 * implementation_link
      + 0.70 * resilience_link;

  const double canada_relief_cost = terms.canada_tariff_relief
      * (0.35 + 0.11 * e.canada_retaliatory_tariff);
  const double us_relief_cost = terms.us_tariff_relief
      * (0.45 + 0.075 * e.us_tariff_canada);
  const double supply_fiscal_cost = terms.supply_chain_commitment
      * (0.45 + 0.10 * std::max(0.0, -e.fiscal_balance_gdp));

  out.canada_utility = clamp_value(canada_base
      + 0.78 * canada_trade_gain
      + 1.35 * terms.border_facilitation
      + 0.75 * terms.procurement_reciprocity
      + 1.10 * terms.supply_chain_commitment
      + linkage_bonus
      - canada_relief_cost
      - supply_fiscal_cost, 0.0, 100.0);
  out.us_utility = clamp_value(us_base
      + 0.82 * us_trade_gain
      + 1.20 * terms.border_facilitation
      + 1.10 * terms.procurement_reciprocity
      + 0.65 * terms.supply_chain_commitment
      + linkage_bonus
      - us_relief_cost, 0.0, 100.0);

  const double implementation_penalty = 0.55 + 0.012 * e.cooperation_ceiling
      + 0.35 * terms.border_facilitation + 0.25 * terms.procurement_reciprocity;
  const double canada_commitment_cost = canada_relief_cost + supply_fiscal_cost
      + 0.35 * terms.procurement_reciprocity;
  const double us_commitment_cost = us_relief_cost
      + 0.30 * terms.procurement_reciprocity + 0.20 * terms.border_facilitation;
  const double canada_reciprocity_value = 0.20 * canada_trade_gain
      + 0.45 * terms.border_facilitation + 0.25 * terms.procurement_reciprocity;
  const double us_reciprocity_value = 0.20 * us_trade_gain
      + 0.45 * terms.border_facilitation + 0.25 * terms.procurement_reciprocity;

  out.canada_deviation_gain = canada_commitment_cost - canada_reciprocity_value - implementation_penalty;
  out.us_deviation_gain = us_commitment_cost - us_reciprocity_value - implementation_penalty;
  const double positive_deviation = std::max(0.0, out.canada_deviation_gain)
      + std::max(0.0, out.us_deviation_gain);
  out.stability_score = clamp_value(100.0 - 14.0 * positive_deviation, 0.0, 100.0);
  return out;
}

inline double generalized_nash(double canada_surplus, double us_surplus,
                               double canada_weight, double us_weight) {
  if (canada_surplus <= 0.0 || us_surplus <= 0.0) return 0.0;
  const double ca = clamp_value(canada_weight, 1.0, 100.0);
  const double us = clamp_value(us_weight, 1.0, 100.0);
  const double total = ca + us;
  return std::exp((ca * std::log(canada_surplus) + us * std::log(us_surplus)) / total);
}

struct Candidate {
  const Scenario* scenario = nullptr;
  Terms terms;
  EvaluatedTerms evaluated;
  double canada_surplus = 0.0;
  double us_surplus = 0.0;
  double nash_gain = 0.0;
};

inline NegotiationPackage make_package(const Candidate& candidate, std::size_t rank) {
  NegotiationPackage package;
  package.id = "pareto-" + std::to_string(rank + 1);
  package.strategy_id = candidate.scenario->id;
  package.strategy_name = candidate.scenario->name;
  package.canada_utility = candidate.evaluated.canada_utility;
  package.us_utility = candidate.evaluated.us_utility;
  package.canada_surplus = candidate.canada_surplus;
  package.us_surplus = candidate.us_surplus;
  package.nash_gain = candidate.nash_gain;
  package.stability_score = candidate.evaluated.stability_score;
  package.canada_deviation_gain = candidate.evaluated.canada_deviation_gain;
  package.us_deviation_gain = candidate.evaluated.us_deviation_gain;
  package.canada_export_change = candidate.evaluated.canada_export_change;
  package.us_export_change = candidate.evaluated.us_export_change;
  package.trade_balance_gap_usd = candidate.scenario->trade_balance_gap_usd;
  package.individually_rational = candidate.canada_surplus >= -1e-9 && candidate.us_surplus >= -1e-9;
  package.pareto_efficient = true;
  package.stable = candidate.evaluated.canada_deviation_gain <= 0.5
      && candidate.evaluated.us_deviation_gain <= 0.5;
  package.sector_verified = candidate.scenario->sector_verified;
  package.verified_win_win = package.individually_rational && package.pareto_efficient
      && package.sector_verified;
  package.us_sector_coverage = candidate.scenario->applied_us_sector_coverage;
  package.canada_sector_coverage = candidate.scenario->applied_canada_sector_coverage;
  package.issues = {
      {"us-tariff-relief", "U.S. tariff relief", 0.0, 100.0 * candidate.terms.us_tariff_relief},
      {"canada-tariff-relief", "Canadian retaliatory-tariff relief", 100.0 * candidate.terms.canada_tariff_relief, 0.0},
      {"border-facilitation", "Border and standards facilitation", 100.0 * candidate.terms.border_facilitation, 100.0 * candidate.terms.border_facilitation},
      {"procurement", "Reciprocal procurement access", 100.0 * candidate.terms.procurement_reciprocity, 100.0 * candidate.terms.procurement_reciprocity},
      {"supply-chain", "North American supply-chain commitment", 100.0 * candidate.terms.supply_chain_commitment, 100.0 * candidate.terms.supply_chain_commitment}
  };
  return package;
}

inline std::string escape_json(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    if (c == '\n') { out += "\\n"; continue; }
    if (c == '\r') { out += "\\r"; continue; }
    if (c == '\t') { out += "\\t"; continue; }
    out.push_back(c);
  }
  return out;
}

template<std::size_t N>
inline void array_json(std::ostringstream& out, const std::array<double, N>& values) {
  out << '[';
  for (std::size_t i = 0; i < N; ++i) {
    if (i) out << ',';
    out << values[i];
  }
  out << ']';
}

inline void package_json(std::ostringstream& out, const NegotiationPackage& package) {
  out << "{\"id\":\"" << escape_json(package.id)
      << "\",\"strategyId\":\"" << escape_json(package.strategy_id)
      << "\",\"strategyName\":\"" << escape_json(package.strategy_name)
      << "\",\"canadaUtility\":" << package.canada_utility
      << ",\"usUtility\":" << package.us_utility
      << ",\"canadaSurplus\":" << package.canada_surplus
      << ",\"usSurplus\":" << package.us_surplus
      << ",\"nashGain\":" << package.nash_gain
      << ",\"stabilityScore\":" << package.stability_score
      << ",\"canadaDeviationGain\":" << package.canada_deviation_gain
      << ",\"usDeviationGain\":" << package.us_deviation_gain
      << ",\"canadaExportChange\":" << package.canada_export_change
      << ",\"usExportChange\":" << package.us_export_change
      << ",\"tradeBalanceGapUsd\":" << package.trade_balance_gap_usd
      << ",\"individuallyRational\":" << (package.individually_rational ? "true" : "false")
      << ",\"paretoEfficient\":" << (package.pareto_efficient ? "true" : "false")
      << ",\"stable\":" << (package.stable ? "true" : "false")
      << ",\"sectorVerified\":" << (package.sector_verified ? "true" : "false")
      << ",\"verifiedWinWin\":" << (package.verified_win_win ? "true" : "false")
      << ",\"usSectorCoverage\":";
  array_json(out, package.us_sector_coverage);
  out << ",\"canadaSectorCoverage\":";
  array_json(out, package.canada_sector_coverage);
  out << ",\"issues\":[";
  for (std::size_t i = 0; i < package.issues.size(); ++i) {
    if (i) out << ',';
    const auto& issue = package.issues[i];
    out << "{\"id\":\"" << escape_json(issue.id)
        << "\",\"label\":\"" << escape_json(issue.label)
        << "\",\"canadaMove\":" << issue.canada_move
        << ",\"usMove\":" << issue.us_move << '}';
  }
  out << "]}";
}

}  // namespace negotiation_detail

inline NegotiationAnalysis analyze_negotiation(const Economy& economy, const Result& result) {
  using namespace negotiation_detail;
  NegotiationAnalysis analysis;
  analysis.independent_us_trade_channel = result.recommendation.independent_us_trade_channel;
  analysis.trade_balance_is_objective = result.recommendation.trade_balance_is_objective;
  analysis.mandate_weights_fixed = result.recommendation.mandate_weights_fixed;
  analysis.sector_verification_draws = result.recommendation.verification_monte_carlo_draws;
  if (result.scenarios.empty()) return analysis;

  const Scenario* canada_batna_scenario = &result.scenarios.front();
  const Scenario* us_batna_scenario = &result.scenarios.front();
  double canada_batna = -std::numeric_limits<double>::infinity();
  double us_batna = -std::numeric_limits<double>::infinity();
  for (const auto& scenario : result.scenarios) {
    if (!outside_option_candidate(scenario)) continue;
    const double canada = canada_payoff(scenario);
    if (canada > canada_batna) { canada_batna = canada; canada_batna_scenario = &scenario; }
    if (scenario.us_score > us_batna) { us_batna = scenario.us_score; us_batna_scenario = &scenario; }
  }
  if (!std::isfinite(canada_batna)) canada_batna = canada_payoff(*canada_batna_scenario);
  if (!std::isfinite(us_batna)) us_batna = us_batna_scenario->us_score;

  analysis.canada_batna = canada_batna;
  analysis.us_batna = us_batna;
  analysis.canada_batna_strategy = canada_batna_scenario->name;
  analysis.us_batna_strategy = us_batna_scenario->name;
  const double caution = clamp_value(economy.risk_aversion / 100.0, 0.0, 1.0);
  analysis.canada_reservation = clamp_value(canada_batna
      + (100.0 - canada_batna) * (0.008 + 0.008 * caution), 0.0, 99.8);
  analysis.us_reservation = clamp_value(us_batna
      + (100.0 - us_batna) * (0.008 + 0.008 * caution), 0.0, 99.8);

  const double cooperation_cap = clamp_value(economy.cooperation_ceiling / 100.0, 0.0, 1.0);
  const double levels[] = {0.0, 0.25, 0.50, 0.75, 1.0};
  constexpr int combinations_per_scenario = 3125;
  std::vector<Candidate> feasible;
  feasible.reserve(result.scenarios.size() * combinations_per_scenario);

  for (const auto& scenario : result.scenarios) {
    for (double ur : levels) for (double cr : levels) for (double border : levels)
      for (double procurement : levels) for (double supply : levels) {
        Candidate candidate;
        candidate.scenario = &scenario;
        candidate.terms.us_tariff_relief = ur * cooperation_cap;
        candidate.terms.canada_tariff_relief = cr * cooperation_cap;
        candidate.terms.border_facilitation = border;
        candidate.terms.procurement_reciprocity = procurement;
        candidate.terms.supply_chain_commitment = supply;
        candidate.evaluated = evaluate_terms(economy, scenario, candidate.terms);
        candidate.canada_surplus = candidate.evaluated.canada_utility - analysis.canada_reservation;
        candidate.us_surplus = candidate.evaluated.us_utility - analysis.us_reservation;
        candidate.nash_gain = generalized_nash(candidate.canada_surplus, candidate.us_surplus,
            economy.canada_priority, economy.us_priority);
        ++analysis.candidates_examined;
        if (candidate.canada_surplus >= -1e-9 && candidate.us_surplus >= -1e-9)
          feasible.push_back(candidate);
      }
  }
  analysis.individually_rational_count = static_cast<int>(feasible.size());

  std::sort(feasible.begin(), feasible.end(), [](const Candidate& a, const Candidate& b) {
    if (std::abs(a.evaluated.canada_utility - b.evaluated.canada_utility) > 1e-9)
      return a.evaluated.canada_utility > b.evaluated.canada_utility;
    return a.evaluated.us_utility > b.evaluated.us_utility;
  });

  // Preserve distinct bargaining packages that occupy the same nondominated
  // utility coordinate. The previous one-pass skyline kept only the first
  // candidate at a tied Canada/U.S. point, which made Pareto package IDs vanish
  // when empirical tariff calibration caused more utilities to clamp at 100.
  // A candidate is dominated only when another package is at least as good for
  // both principals and strictly better for one; exact utility ties remain
  // Pareto-efficient alternatives because their concession bundles can differ.
  std::vector<Candidate> frontier;
  double best_us_at_higher_canada = -std::numeric_limits<double>::infinity();
  constexpr double pareto_eps = 1e-9;
  std::size_t group_begin = 0;
  while (group_begin < feasible.size()) {
    std::size_t group_end = group_begin + 1;
    const double canada_utility = feasible[group_begin].evaluated.canada_utility;
    double group_best_us = feasible[group_begin].evaluated.us_utility;
    while (group_end < feasible.size()
        && std::abs(feasible[group_end].evaluated.canada_utility - canada_utility) <= pareto_eps) {
      group_best_us = std::max(group_best_us, feasible[group_end].evaluated.us_utility);
      ++group_end;
    }

    if (group_best_us > best_us_at_higher_canada + pareto_eps) {
      for (std::size_t i = group_begin; i < group_end; ++i) {
        if (std::abs(feasible[i].evaluated.us_utility - group_best_us) <= pareto_eps)
          frontier.push_back(feasible[i]);
      }
    }
    best_us_at_higher_canada = std::max(best_us_at_higher_canada, group_best_us);
    group_begin = group_end;
  }
  analysis.pareto_frontier_size = static_cast<int>(frontier.size());

  std::sort(frontier.begin(), frontier.end(), [](const Candidate& a, const Candidate& b) {
    const double a_score = a.nash_gain + 0.030 * a.evaluated.stability_score
        + 0.080 * std::min(a.canada_surplus, a.us_surplus);
    const double b_score = b.nash_gain + 0.030 * b.evaluated.stability_score
        + 0.080 * std::min(b.canada_surplus, b.us_surplus);
    return a_score > b_score;
  });

  if (frontier.empty()) {
    Candidate fallback;
    fallback.scenario = canada_batna_scenario;
    fallback.evaluated = evaluate_terms(economy, *fallback.scenario, fallback.terms);
    fallback.canada_surplus = fallback.evaluated.canada_utility - analysis.canada_reservation;
    fallback.us_surplus = fallback.evaluated.us_utility - analysis.us_reservation;
    fallback.nash_gain = 0.0;
    frontier.push_back(fallback);
  }

  const std::size_t keep = std::min<std::size_t>(12, frontier.size());
  analysis.frontier.reserve(keep);
  for (std::size_t i = 0; i < keep; ++i) analysis.frontier.push_back(make_package(frontier[i], i));
  analysis.recommended = analysis.frontier.front();
  analysis.sector_schedule_verified = analysis.recommended.sector_verified;
  analysis.data_integrity_pass = analysis.independent_us_trade_channel
      && !analysis.trade_balance_is_objective
      && analysis.mandate_weights_fixed
      && analysis.recommended.individually_rational
      && analysis.recommended.pareto_efficient
      && analysis.recommended.sector_verified;
  analysis.recommended.verified_win_win = analysis.data_integrity_pass;
  return analysis;
}

inline std::string negotiation_to_json(const NegotiationAnalysis& analysis) {
  using namespace negotiation_detail;
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << "{\"candidatesExamined\":" << analysis.candidates_examined
      << ",\"individuallyRationalCount\":" << analysis.individually_rational_count
      << ",\"paretoFrontierSize\":" << analysis.pareto_frontier_size
      << ",\"bargainingGridLevels\":" << analysis.bargaining_grid_levels
      << ",\"batna\":{\"canada\":" << analysis.canada_batna
      << ",\"us\":" << analysis.us_batna
      << ",\"canadaStrategy\":\"" << escape_json(analysis.canada_batna_strategy)
      << "\",\"usStrategy\":\"" << escape_json(analysis.us_batna_strategy) << "\"}"
      << ",\"reservation\":{\"canada\":" << analysis.canada_reservation
      << ",\"us\":" << analysis.us_reservation << "}"
      << ",\"trust\":{\"independentUsTradeChannel\":"
      << (analysis.independent_us_trade_channel ? "true" : "false")
      << ",\"tradeBalanceIsObjective\":" << (analysis.trade_balance_is_objective ? "true" : "false")
      << ",\"mandateWeightsFixed\":" << (analysis.mandate_weights_fixed ? "true" : "false")
      << ",\"sectorScheduleVerified\":" << (analysis.sector_schedule_verified ? "true" : "false")
      << ",\"verificationMonteCarloDraws\":" << analysis.sector_verification_draws
      << ",\"dataIntegrityPass\":" << (analysis.data_integrity_pass ? "true" : "false") << "}"
      << ",\"recommendedPackage\":";
  package_json(out, analysis.recommended);
  out << ",\"frontier\":[";
  for (std::size_t i = 0; i < analysis.frontier.size(); ++i) {
    if (i) out << ',';
    package_json(out, analysis.frontier[i]);
  }
  out << "]}";
  return out.str();
}

inline std::string attach_negotiation_json(std::string policy_json, const NegotiationAnalysis& analysis) {
  if (policy_json.empty() || policy_json.back() != '}') return policy_json;
  policy_json.pop_back();
  policy_json += ",\"negotiation\":" + negotiation_to_json(analysis) + "}";
  return policy_json;
}

}  // namespace cad
