#pragma once

#include "finalist_resimulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace cad {

namespace border_finalist_resimulation_detail {

inline double clamp_fraction(double value) {
  return std::max(0.0, std::min(1.0, value));
}

inline void mark_border_mapping(std::vector<BargainingTermMapping>& mappings) {
  for (auto& mapping : mappings) {
    if (mapping.term_id != "border-facilitation") continue;
    mapping.production_mapped = true;
    mapping.production_target = "Economy.border_friction";
    mapping.unit_contract =
        "fractional reduction of the submitted border-friction index; index floor 0";
    mapping.transformation =
        "border_friction' = max(0, border_friction * (1 - facilitation_fraction))";
    mapping.provenance_class =
        "transparent structural-normalization assumption; not empirically calibrated";
    mapping.uncertainty_treatment =
        "inherits the submitted/model-default border-friction index; no additional coefficient or distribution introduced";
    return;
  }
}

inline NegotiationPackage consume_border_term(const NegotiationPackage& package) {
  NegotiationPackage mapped = package;
  for (auto& issue : mapped.issues) {
    if (issue.id != "border-facilitation") continue;
    issue.canada_move = 0.0;
    issue.us_move = 0.0;
  }
  return mapped;
}

inline RobustRecommendationAnalysis isolate_robustness(
    const RobustRecommendationAnalysis& robustness, const std::string& package_id) {
  RobustRecommendationAnalysis isolated;
  isolated.recommended_package_id = package_id;
  isolated.required_joint_clear_probability = robustness.required_joint_clear_probability;
  isolated.second_stage_monte_carlo_draws = robustness.second_stage_monte_carlo_draws;
  isolated.empirically_calibrated = robustness.empirically_calibrated;
  isolated.complete_frontier_evaluated = robustness.complete_frontier_evaluated;
  isolated.parameter_distributions = robustness.parameter_distributions;
  for (const auto& metrics : robustness.packages) {
    if (metrics.package_id == package_id) {
      isolated.packages.push_back(metrics);
      break;
    }
  }
  return isolated;
}

}  // namespace border_finalist_resimulation_detail

template<class Engine>
FinalistResimulationAnalysis verify_bargaining_finalists_with_border_mapping(
    Engine& engine, const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness,
    std::size_t finalist_limit = 3) {
  using namespace finalist_resimulation_detail;
  using namespace border_finalist_resimulation_detail;

  FinalistResimulationAnalysis analysis;
  analysis.finalist_limit = std::max<std::size_t>(1, finalist_limit);
  analysis.target_verification_draws = std::max(
      1, result.recommendation.verification_monte_carlo_draws);
  analysis.mappings = mapping_contract();
  mark_border_mapping(analysis.mappings);

  const auto selected = select_finalists(robustness, analysis.finalist_limit);
  analysis.finalists.reserve(selected.size());
  for (const auto& candidate : selected) {
    const NegotiationPackage* package = find_package(negotiation, candidate.id);
    if (!package) {
      FinalistResimulationRecord record;
      record.role = candidate.role;
      record.package_id = candidate.id;
      update_package_verification(negotiation, record);
      analysis.finalists.push_back(std::move(record));
      continue;
    }

    const auto terms = robust_detail::package_terms(*package);
    const double facilitation = clamp_fraction(terms.border_facilitation);
    Economy mapped_economy = economy;
    mapped_economy.border_friction = std::max(
        0.0, economy.border_friction * (1.0 - facilitation));

    NegotiationAnalysis isolated_negotiation;
    isolated_negotiation.recommended = consume_border_term(*package);
    isolated_negotiation.frontier.push_back(isolated_negotiation.recommended);
    isolated_negotiation.pareto_frontier_size = 1;
    auto isolated_robustness = isolate_robustness(robustness, candidate.id);

    auto isolated_analysis = verify_bargaining_finalists(
        engine, mapped_economy, result, isolated_negotiation,
        isolated_robustness, 1);

    FinalistResimulationRecord record;
    if (!isolated_analysis.finalists.empty())
      record = std::move(isolated_analysis.finalists.front());
    record.role = candidate.role;
    record.package_id = candidate.id;
    record.strategy_id = package->strategy_id;
    update_package_verification(negotiation, record);
    analysis.finalists.push_back(std::move(record));
  }
  return analysis;
}

}  // namespace cad
