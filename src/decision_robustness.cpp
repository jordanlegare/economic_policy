#include "policy_engine.hpp"
#include "robustness.hpp"
#include "structural_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace cad {
namespace {

bool generated_strategy(const std::string& id) {
  return id == "custom" || id.rfind("custom-", 0) == 0;
}

std::string strategy_family(const std::string& id) {
  return generated_strategy(id) ? "custom" : id;
}

bool same_policy_controls(const Scenario& a, const Scenario& b) {
  return std::abs(a.first_move_bp - b.first_move_bp) < 1e-9
      && std::abs(a.fiscal_impulse - b.fiscal_impulse) < 1e-9
      && std::abs(a.productive_share - b.productive_share) < 1e-9
      && std::abs(a.negotiated_relief - b.negotiated_relief) < 1e-9
      && std::abs(a.targeted_relief - b.targeted_relief) < 1e-9
      && std::abs(a.diversification - b.diversification) < 1e-9;
}

bool same_package(const Scenario& a, const Scenario& b) {
  for (std::size_t i = 0; i < a.applied_us_sector_coverage.size(); ++i) {
    if (std::abs(a.applied_us_sector_coverage[i]
        - b.applied_us_sector_coverage[i]) > 1e-9) return false;
    if (std::abs(a.applied_canada_sector_coverage[i]
        - b.applied_canada_sector_coverage[i]) > 1e-9) return false;
  }
  return true;
}

const Scenario* selected_scenario(const Result& result) {
  for (const auto& scenario : result.scenarios)
    if (scenario.id == result.recommendation.strategy_id) return &scenario;
  return result.scenarios.empty() ? nullptr : &result.scenarios.front();
}

const Scenario* best_generated(const Result& result) {
  for (const auto& scenario : result.scenarios)
    if (generated_strategy(scenario.id)) return &scenario;
  return nullptr;
}

const Scenario* matching_reference(const Result& result, const Scenario& reference) {
  const std::string family = strategy_family(reference.id);
  for (const auto& scenario : result.scenarios) {
    if (strategy_family(scenario.id) == family
        && same_policy_controls(scenario, reference)) return &scenario;
  }
  return nullptr;
}

const Scenario* matching_baseline_scenario(const Result& baseline,
                                           const Scenario& candidate) {
  const std::string family = strategy_family(candidate.id);
  for (const auto& scenario : baseline.scenarios) {
    if (strategy_family(scenario.id) != family) continue;
    if (!generated_strategy(candidate.id) || same_policy_controls(scenario, candidate))
      return &scenario;
  }
  return nullptr;
}

int sector_optimized_scenario_count(const Result& result) {
  int count = 0;
  for (const auto& scenario : result.scenarios)
    if (scenario.id != "baseline") ++count;
  return count;
}

std::string json_escape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

}  // namespace

Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {
  return evaluate_robust(economy, parameter_draws,
      EvaluationOptions{economy.exhaustive_policy_search});
}

Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws,
                                     EvaluationOptions options) const {
  // Deliberately obtain the reference recommendation from the same production
  // evaluator used by /api/evaluate. Structural robustness must never maintain
  // a second copy of the macro, tariff-incidence, input-output, sector-search or
  // welfare equations: every structural draw below constructs a PolicyEngine
  // with the drawn parameters and calls evaluate() again.
  Result baseline = evaluate(economy, options);
  auto& summary = baseline.recommendation.robustness;
  summary.parameter_draws = std::max(0, parameter_draws);
  summary.calibration_id = parameters_.calibration_id;
  summary.calibration_vintage = parameters_.calibration_vintage;
  summary.parameter_registry_id = parameters_.uncertainty_registry.loaded
      ? parameters_.uncertainty_registry.registry_id : "none";
  summary.sampled_parameter_count = sampled_structural_parameter_count(
      parameters_.uncertainty_registry);
  summary.parameter_bounds_active = summary.parameter_draws > 0
      && parameters_.uncertainty_registry.loaded
      && summary.sampled_parameter_count > 0;
  summary.parameter_provenance_complete = structural_parameter_registry_complete(
      parameters_.uncertainty_registry);
  summary.methodology =
      "outer-structural-ensemble/production-policy-engine-rerun/"
      "full-288-policy-control-search/production-sector-pareto-search/"
      "production-trade-network/common-random-numbers";
  summary.structural_parameters_active = summary.parameter_draws > 0;
  summary.common_random_numbers = summary.parameter_draws > 0;
  summary.sector_packages_reoptimized = summary.parameter_draws > 0;
  summary.policy_controls_reoptimized = summary.parameter_draws > 0;
  summary.policy_control_candidates_per_draw = 288;

  if (summary.parameter_draws == 0 || baseline.scenarios.empty()) {
    summary.classification = "not-evaluated";
    summary.policy_control_candidates_per_draw = 0;
    return baseline;
  }

  const Scenario* reference_decision_ptr = selected_scenario(baseline);
  const Scenario* reference_generated_ptr = best_generated(baseline);
  if (!reference_decision_ptr || !reference_generated_ptr) {
    summary.classification = "not-evaluated";
    return baseline;
  }
  const Scenario reference_decision = *reference_decision_ptr;
  const Scenario reference_generated = *reference_generated_ptr;
  const std::string reference_family = strategy_family(reference_decision.id);

  const auto ensemble = draw_structural_parameters(
      parameters_, summary.parameter_draws,
      static_cast<std::uint64_t>(seed_) ^ 0x9e3779b97f4a7c15ULL);

  std::vector<double> reference_scores;
  reference_scores.reserve(ensemble.size());
  int reference_package_retained = 0;
  int reference_controls_retained = 0;

  for (const auto& parameters : ensemble) {
    // This is the critical parity boundary. The inner evaluation inherits the
    // same search mode as the submitted economy. In ordinary V2 use that means
    // the 288 generated controls are searched first and the selected generated
    // policy plus expert strategies receive the production sector optimizer.
    // If exhaustive startup mode is explicitly requested, the exact exhaustive
    // production search is rerun as well rather than approximated here.
    PolicyEngine draw_engine(seed_, parameters, parameters_.uncertainty_registry);
    Result draw = draw_engine.evaluate(economy, options);
    const Scenario* draw_selected = selected_scenario(draw);
    const Scenario* draw_generated = best_generated(draw);
    if (!draw_selected || !draw_generated) continue;

    summary.policy_control_candidates_examined +=
        static_cast<std::uint64_t>(std::max(0, draw.candidates_examined));
    if (same_policy_controls(*draw_generated, reference_generated)) {
      ++reference_controls_retained;
    } else {
      ++summary.policy_control_changes;
    }

    const int optimized = sector_optimized_scenario_count(draw);
    summary.sector_frontiers_built += optimized;
    summary.nested_sector_optimizations += static_cast<std::uint64_t>(optimized);
    // Production currently exposes detailed frontier counts for the selected
    // recommendation. These counters therefore accumulate the exact exposed
    // production counts rather than fabricating totals for hidden strategies.
    summary.nested_sector_candidates_examined += static_cast<std::uint64_t>(
        std::max(0, draw.recommendation.sector_candidates_examined));
    summary.nested_sector_finalists_resimulated += static_cast<std::uint64_t>(
        std::max(0, draw.recommendation.sector_finalists_resimulated));

    for (const auto& scenario : draw.scenarios) {
      if (scenario.id == "baseline") continue;
      const Scenario* prior = matching_baseline_scenario(baseline, scenario);
      if (prior && !same_package(*prior, scenario)) ++summary.sector_package_changes;
    }

    const std::string draw_family = strategy_family(draw_selected->id);
    if (draw_family == reference_family) ++summary.strategy_family_wins;
    const bool same_reference_controls = draw_family == reference_family
        && same_policy_controls(*draw_selected, reference_decision);
    if (same_reference_controls) ++summary.recommendation_wins;

    const Scenario* reference_under_draw = matching_reference(draw, reference_decision);
    if (reference_under_draw) {
      reference_scores.push_back(reference_under_draw->score);
      if (same_package(*reference_under_draw, reference_decision))
        ++reference_package_retained;
    }
  }

  const double draw_count = static_cast<double>(ensemble.size());
  summary.recommendation_win_rate = ensemble.empty() ? 0.0
      : static_cast<double>(summary.recommendation_wins) / draw_count;
  summary.strategy_family_win_rate = ensemble.empty() ? 0.0
      : static_cast<double>(summary.strategy_family_wins) / draw_count;
  summary.reference_policy_control_retention_rate = ensemble.empty() ? 0.0
      : static_cast<double>(reference_controls_retained) / draw_count;
  summary.reference_package_retention_rate = ensemble.empty() ? 0.0
      : static_cast<double>(reference_package_retained) / draw_count;

  if (!reference_scores.empty()) {
    summary.score_mean = std::accumulate(reference_scores.begin(), reference_scores.end(), 0.0)
        / static_cast<double>(reference_scores.size());
    summary.score_p10 = robustness_quantile(reference_scores, 0.10);
    summary.score_p90 = robustness_quantile(reference_scores, 0.90);
  }
  summary.classification = classify_robustness(summary.recommendation_win_rate);

  std::ostringstream note;
  note << " V2 full decision robustness now delegates every structural calibration to the "
       << "production PolicyEngine, so the empirical trade network, tariff incidence, macro "
       << "simulation, policy search and sector Pareto search cannot drift into a parallel model. "
       << summary.recommendation_wins << "/" << summary.parameter_draws
       << " structural calibrations retain the reference control decision ("
       << std::fixed << std::setprecision(1)
       << 100.0 * summary.recommendation_win_rate << "%, " << summary.classification
       << "). The broader strategy family wins in "
       << 100.0 * summary.strategy_family_win_rate
       << "% of calibrations; the reference generated control package is retained in "
       << 100.0 * summary.reference_policy_control_retention_rate << "%.";
  baseline.recommendation.explanation += note.str();
  return baseline;
}

std::string robustness_to_json(const Result& result) {
  const auto& r = result.recommendation.robustness;
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"parameterDraws\":" << r.parameter_draws
    << ",\"recommendationWins\":" << r.recommendation_wins
    << ",\"recommendationWinRate\":" << r.recommendation_win_rate
    << ",\"strategyFamilyWins\":" << r.strategy_family_wins
    << ",\"strategyFamilyWinRate\":" << r.strategy_family_win_rate
    << ",\"scoreMean\":" << r.score_mean
    << ",\"scoreP10\":" << r.score_p10
    << ",\"scoreP90\":" << r.score_p90
    << ",\"classification\":\"" << json_escape(r.classification)
    << "\",\"calibrationId\":\"" << json_escape(r.calibration_id)
    << "\",\"calibrationVintage\":\"" << json_escape(r.calibration_vintage)
    << "\",\"parameterRegistryId\":\"" << json_escape(r.parameter_registry_id)
    << "\",\"methodology\":\"" << json_escape(r.methodology)
    << "\",\"sampledParameterCount\":" << r.sampled_parameter_count
    << ",\"structuralParametersActive\":"
    << (r.structural_parameters_active ? "true" : "false")
    << ",\"commonRandomNumbers\":" << (r.common_random_numbers ? "true" : "false")
    << ",\"sectorPackagesReoptimized\":"
    << (r.sector_packages_reoptimized ? "true" : "false")
    << ",\"policyControlsReoptimized\":"
    << (r.policy_controls_reoptimized ? "true" : "false")
    << ",\"parameterBoundsActive\":" << (r.parameter_bounds_active ? "true" : "false")
    << ",\"parameterProvenanceComplete\":"
    << (r.parameter_provenance_complete ? "true" : "false")
    << ",\"policyControlCandidatesPerDraw\":" << r.policy_control_candidates_per_draw
    << ",\"policyControlCandidatesExamined\":" << r.policy_control_candidates_examined
    << ",\"policyControlChanges\":" << r.policy_control_changes
    << ",\"referencePolicyControlRetentionRate\":"
    << r.reference_policy_control_retention_rate
    << ",\"sectorFrontiersBuilt\":" << r.sector_frontiers_built
    << ",\"nestedSectorOptimizations\":" << r.nested_sector_optimizations
    << ",\"nestedSectorCandidatesExamined\":" << r.nested_sector_candidates_examined
    << ",\"nestedSectorFinalistsResimulated\":" << r.nested_sector_finalists_resimulated
    << ",\"sectorPackageChanges\":" << r.sector_package_changes
    << ",\"referencePackageRetentionRate\":"
    << r.reference_package_retention_rate << "}";
  return o.str();
}

}  // namespace cad
