#pragma once

#include "negotiation_support.hpp"
#include "robust_recommendation.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace cad::interactive_frontier {

inline constexpr std::size_t kInitialPackageLimit = 100;

inline std::string negotiation_to_json(
    const NegotiationAnalysis& analysis,
    std::size_t frontier_limit = kInitialPackageLimit) {
  using namespace negotiation_detail;
  const std::size_t returned = std::min(frontier_limit, analysis.frontier.size());
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << "{\"candidatesExamined\":" << analysis.candidates_examined
      << ",\"individuallyRationalCount\":" << analysis.individually_rational_count
      << ",\"paretoFrontierSize\":" << analysis.pareto_frontier_size
      << ",\"paretoDefinition\":\"epsilon\""
      << ",\"paretoUtilityTolerance\":" << analysis.pareto_utility_tolerance
      << ",\"frontierComplete\":" << (analysis.frontier_complete ? "true" : "false")
      << ",\"frontierTotal\":" << analysis.frontier.size()
      << ",\"frontierReturned\":" << returned
      << ",\"frontierTruncated\":"
      << (returned < analysis.frontier.size() ? "true" : "false")
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
      << ",\"frontierComplete\":" << (analysis.frontier_complete ? "true" : "false")
      << ",\"verificationMonteCarloDraws\":" << analysis.sector_verification_draws
      << ",\"dataIntegrityPass\":" << (analysis.data_integrity_pass ? "true" : "false") << "}"
      << ",\"recommendedPackage\":";
  package_json(out, analysis.recommended);
  out << ",\"frontier\":[";
  for (std::size_t i = 0; i < returned; ++i) {
    if (i) out << ',';
    package_json(out, analysis.frontier[i]);
  }
  out << "]}";
  return out.str();
}

inline void robust_package_json(std::ostringstream& out,
                                const RobustPackageMetrics& p) {
  using robust_detail::esc;
  out << "{\"packageId\":\"" << esc(p.package_id)
      << "\",\"strategyId\":\"" << esc(p.strategy_id)
      << "\",\"samples\":" << p.samples
      << ",\"canadaMeanSurplus\":" << p.canada_mean_surplus
      << ",\"usMeanSurplus\":" << p.us_mean_surplus
      << ",\"canadaMedianSurplus\":" << p.canada_median_surplus
      << ",\"usMedianSurplus\":" << p.us_median_surplus
      << ",\"canadaCi95\":[" << p.canada_ci95_low << ',' << p.canada_ci95_high << ']'
      << ",\"usCi95\":[" << p.us_ci95_low << ',' << p.us_ci95_high << ']'
      << ",\"canadaCvar10Surplus\":" << p.canada_cvar10_surplus
      << ",\"usCvar10Surplus\":" << p.us_cvar10_surplus
      << ",\"canadaClearProbability\":" << p.canada_clear_probability
      << ",\"usClearProbability\":" << p.us_clear_probability
      << ",\"jointClearProbability\":" << p.joint_clear_probability
      << ",\"rankWinProbability\":" << p.rank_win_probability
      << ",\"meanRegret\":" << p.mean_regret
      << ",\"p95Regret\":" << p.p95_regret
      << ",\"maxRegret\":" << p.max_regret
      << ",\"robustFloor\":" << p.robust_floor
      << ",\"clearsProbabilityGate\":"
      << (p.clears_probability_gate ? "true" : "false") << '}';
}

inline std::string robustness_to_json(
    const RobustRecommendationAnalysis& analysis,
    std::size_t package_limit = kInitialPackageLimit) {
  using robust_detail::esc;
  const std::size_t returned = std::min(package_limit, analysis.packages.size());
  auto recommended = std::find_if(
      analysis.packages.begin(), analysis.packages.end(),
      [&](const RobustPackageMetrics& p) {
        return p.package_id == analysis.recommended_package_id;
      });
  const std::size_t recommended_index = recommended == analysis.packages.end()
      ? std::numeric_limits<std::size_t>::max()
      : static_cast<std::size_t>(recommended - analysis.packages.begin());

  std::ostringstream out;
  out << std::fixed << std::setprecision(4);
  out << "{\"secondStageMonteCarloDraws\":" << analysis.second_stage_monte_carlo_draws
      << ",\"seed\":" << analysis.seed
      << ",\"cvarTailProbability\":" << analysis.cvar_tail_probability
      << ",\"requiredJointClearProbability\":" << analysis.required_joint_clear_probability
      << ",\"commonRandomNumbers\":" << (analysis.common_random_numbers ? "true" : "false")
      << ",\"parameterUncertaintyIncluded\":" << (analysis.parameter_uncertainty_included ? "true" : "false")
      << ",\"politicalAcceptanceProbabilityEstimated\":" << (analysis.political_acceptance_probability_estimated ? "true" : "false")
      << ",\"empiricallyCalibrated\":" << (analysis.empirically_calibrated ? "true" : "false")
      << ",\"boundedMemoryTwoPass\":" << (analysis.bounded_memory_two_pass ? "true" : "false")
      << ",\"candidateSetComplete\":" << (analysis.candidate_set_complete ? "true" : "false")
      << ",\"uncertaintyGrade\":\"" << esc(analysis.uncertainty_grade)
      << "\",\"recommendedPackageId\":\"" << esc(analysis.recommended_package_id)
      << "\",\"packageTotal\":" << analysis.packages.size()
      << ",\"packageReturned\":" << returned
      << ",\"packageTruncated\":"
      << (returned < analysis.packages.size() ? "true" : "false")
      << ",\"selectionRule\":\"" << esc(analysis.selection_rule)
      << "\",\"parameterDistributions\":[";
  for (std::size_t i = 0; i < analysis.distributions.size(); ++i) {
    if (i) out << ',';
    const auto& d = analysis.distributions[i];
    out << "{\"name\":\"" << esc(d.name)
        << "\",\"mean\":" << d.mean
        << ",\"standardDeviation\":" << d.standard_deviation
        << ",\"lowerBound\":" << d.lower_bound
        << ",\"upperBound\":" << d.upper_bound
        << ",\"evidenceClass\":\"" << esc(d.evidence_class)
        << "\",\"source\":\"" << esc(d.source) << "\"}";
  }
  out << "],\"recommendedPackage\":";
  if (recommended == analysis.packages.end()) out << "null";
  else robust_package_json(out, *recommended);

  out << ",\"packages\":[";
  for (std::size_t slot = 0; slot < returned; ++slot) {
    std::size_t index = slot;
    if (returned > 0 && slot + 1 == returned
        && recommended_index >= returned
        && recommended_index < analysis.packages.size()) {
      index = recommended_index;
    }
    if (slot) out << ',';
    robust_package_json(out, analysis.packages[index]);
  }
  out << "],\"interpretation\":\"Probabilities describe model outcomes conditional on declared uncertainty distributions; they are not estimates of political acceptance. candidateSetComplete must be true before the robust package is described as the best package on the declared startup search grid. The complete epsilon-Pareto set is evaluated server-side; the interactive response is a bounded preview when packageTruncated is true.\"}";
  return out.str();
}

inline std::string attach_negotiation_json(
    std::string policy_json, const NegotiationAnalysis& analysis,
    std::size_t frontier_limit = kInitialPackageLimit) {
  if (policy_json.empty() || policy_json.back() != '}') return policy_json;
  policy_json.pop_back();
  policy_json += ",\"negotiation\":"
      + negotiation_to_json(analysis, frontier_limit) + "}";
  return policy_json;
}

inline std::string attach_robustness_json(
    std::string base_json, const RobustRecommendationAnalysis& analysis,
    std::size_t package_limit = kInitialPackageLimit) {
  if (base_json.empty() || base_json.back() != '}') return base_json;
  base_json.pop_back();
  base_json += ",\"robustness\":"
      + robustness_to_json(analysis, package_limit) + "}";
  return base_json;
}

}  // namespace cad::interactive_frontier
