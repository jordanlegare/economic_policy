#pragma once

#include "finalist_resimulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace cad {

namespace linked_finalist_resimulation_detail {

inline double clamp_fraction(double value) {
  return std::max(0.0, std::min(1.0, value));
}

inline void mark_linked_mappings(std::vector<BargainingTermMapping>& mappings) {
  for (auto& mapping : mappings) {
    if (mapping.term_id == "border-facilitation") {
      mapping.production_mapped = true;
      mapping.production_target = "Economy.border_friction";
      mapping.unit_contract =
          "bargaining fraction [0,1] removes the same fraction of the submitted nonnegative border-friction index";
      mapping.transformation =
          "border_friction' = max(0, border_friction * (1 - facilitation_fraction)); sign: weakly lowers trade friction";
      mapping.provenance_class =
          "transparent structural-normalization assumption; source=model_default; vintage=unsourced-model-default; not empirically calibrated";
      mapping.uncertainty_treatment =
          "fixed bargaining normalization; inherits uncertainty in the submitted/model-default border-friction index; no new coefficient";
    } else if (mapping.term_id == "procurement") {
      mapping.production_mapped = true;
      mapping.production_target =
          "Economy.trade_network_tuning.procurement_quantity_uplift_pp";
      mapping.unit_contract =
          "bargaining fraction [0,1] maps to [0,1] percentage point additive two-way bilateral quantity access; resulting quantity ratios remain bounded [0,1.5]";
      mapping.transformation =
          "procurement_uplift_pp = reciprocity_fraction; each directional sector quantity ratio' = clamp(base_ratio + procurement_uplift_pp/100, 0, 1.5); sign: weakly raises bilateral quantity access";
      mapping.provenance_class =
          "fixed structural-normalization assumption; source=internal_model_design_v4; vintage=2026-08-14; not empirically calibrated";
      mapping.uncertainty_treatment =
          "normalization is fixed; tariff, elasticity and macro uncertainty remain active; no fiscal impulse, infrastructure impulse, targeted relief or tariff rate is changed";
    } else if (mapping.term_id == "supply-chain") {
      mapping.production_mapped = true;
      mapping.production_target =
          "Economy.trade_network_tuning.supply_chain_mitigation";
      mapping.unit_contract =
          "bargaining fraction [0,1] is the share [0,100%] of indirect supplier-demand and input-cost network propagation mitigated";
      mapping.transformation =
          "every indirect IO-network supplier-demand/input-cost contribution is multiplied by (1 - commitment_fraction); sign: weakly reduces indirect network drag/cost propagation; direct tariff incidence and bilateral quantity response are unchanged";
      mapping.provenance_class =
          "fixed structural-normalization assumption; source=internal_model_design_v4; vintage=2026-08-14; not empirically calibrated";
      mapping.uncertainty_treatment =
          "normalization is fixed while the existing structural-registry network coefficients retain their declared uncertainty; no productive-investment, productivity or diversification control is changed";
    }
  }
}

inline NegotiationPackage consume_linked_terms(const NegotiationPackage& package) {
  NegotiationPackage mapped = package;
  for (auto& issue : mapped.issues) {
    if (issue.id != "border-facilitation"
        && issue.id != "procurement"
        && issue.id != "supply-chain")
      continue;
    issue.canada_move = 0.0;
    issue.us_move = 0.0;
  }
  return mapped;
}

inline RobustRecommendationAnalysis isolate_robustness(
    const RobustRecommendationAnalysis& robustness, const std::string& package_id) {
  RobustRecommendationAnalysis isolated;
  isolated.second_stage_monte_carlo_draws = robustness.second_stage_monte_carlo_draws;
  isolated.seed = robustness.seed;
  isolated.cvar_tail_probability = robustness.cvar_tail_probability;
  isolated.required_joint_clear_probability = robustness.required_joint_clear_probability;
  isolated.common_random_numbers = robustness.common_random_numbers;
  isolated.parameter_uncertainty_included = robustness.parameter_uncertainty_included;
  isolated.political_acceptance_probability_estimated =
      robustness.political_acceptance_probability_estimated;
  isolated.empirically_calibrated = robustness.empirically_calibrated;
  isolated.bounded_memory_two_pass = robustness.bounded_memory_two_pass;
  isolated.candidate_set_complete = robustness.candidate_set_complete;
  isolated.uncertainty_grade = robustness.uncertainty_grade;
  isolated.selection_rule = robustness.selection_rule;
  isolated.distributions = robustness.distributions;
  isolated.recommended_package_id = package_id;
  for (const auto& metrics : robustness.packages) {
    if (metrics.package_id == package_id) {
      isolated.packages.push_back(metrics);
      break;
    }
  }
  return isolated;
}

}  // namespace linked_finalist_resimulation_detail

template<class Engine>
FinalistResimulationAnalysis verify_bargaining_finalists_with_linked_mappings(
    Engine& engine, const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness,
    std::size_t finalist_limit = 3) {
  using namespace finalist_resimulation_detail;
  using namespace linked_finalist_resimulation_detail;

  FinalistResimulationAnalysis analysis;
  analysis.finalist_limit = std::max<std::size_t>(1, finalist_limit);
  analysis.target_verification_draws = std::max(
      1, result.recommendation.verification_monte_carlo_draws);
  analysis.mappings = mapping_contract();
  mark_linked_mappings(analysis.mappings);

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
    Economy mapped_economy = economy;
    mapped_economy.border_friction = std::max(
        0.0, economy.border_friction
            * (1.0 - clamp_fraction(terms.border_facilitation)));
    mapped_economy.trade_network_tuning.procurement_quantity_uplift_pp =
        clamp_fraction(terms.procurement_reciprocity);
    mapped_economy.trade_network_tuning.supply_chain_mitigation =
        clamp_fraction(terms.supply_chain_commitment);

    NegotiationAnalysis isolated_negotiation;
    isolated_negotiation.recommended = consume_linked_terms(*package);
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
