#pragma once

#include "policy_engine.hpp"

#include <string>

namespace cad::policy_truth_surface {

inline bool selected_sector_posture_verified(const Result& result) {
  for (const auto& scenario : result.scenarios) {
    if (scenario.id == result.recommendation.strategy_id)
      return scenario.sector_verified;
  }
  return false;
}

inline Result truthful_result(Result result) {
  auto& recommendation = result.recommendation;
  const bool policy_grid_complete = recommendation.global_search_complete;

  // `coverage_levels()` deliberately returns only the submitted delegation
  // value. These legacy search-diagnostic fields therefore must not advertise a
  // 25-point sector grid or a Pareto optimization that did not run.
  recommendation.sector_grid_step = 0.0;
  recommendation.sector_candidates_examined = 0;
  recommendation.sector_pareto_frontier_size = 0;
  recommendation.sector_finalists_resimulated = 0;
  recommendation.sector_search_method =
      "authoritative-input: submitted 20-sector posture is evaluated and verified, not optimized";

  if (policy_grid_complete) {
    result.rationale = recommendation.verified_win_win
        ? "Best verified bilateral-welfare win-win on the complete declared policy-control grid under the submitted authoritative 20-sector posture; finite-grid verification is not a continuous global-optimum claim."
        : "Best bilateral-welfare package on the complete declared policy-control grid under the submitted authoritative 20-sector posture, subject to the model's no-harm, growth and robustness constraints.";
  } else {
    result.rationale =
        "Best retained policy-control package under the submitted authoritative 20-sector posture; the policy search was staged and is not certified as complete.";
  }

  recommendation.explanation =
      "The recommendation keeps the delegation's 20-sector coverage vectors as authoritative operator input. Sector coverage is not searched or reoptimized. Policy-control alternatives are compared with common random numbers, and retained policy candidates are re-simulated with the submitted sector posture at the declared verification draw count. Completeness therefore refers only to the policy-control dimensions actually searched; sectorPostureVerified reports whether the submitted posture was propagated through the verification simulation.";

  // Preserve the legacy field for compatibility, but its production JSON is
  // accompanied by an explicit semantic label and truthful replacement fields.
  recommendation.global_search_complete = policy_grid_complete;
  return result;
}

inline std::string to_json(const Result& result) {
  const bool policy_grid_complete = result.recommendation.global_search_complete;
  const bool sector_posture_verified = selected_sector_posture_verified(result);
  Result truthful = truthful_result(result);
  std::string json = ::cad::to_json(truthful);

  // The legacy serializer derives this flag from globalSearchComplete rather
  // than from an actual sector-search field. Force it false in the production
  // truth surface so legacy and replacement fields cannot contradict each other.
  const std::string legacy_exhaustive = "\"sectorSearchExhaustive\":true";
  if (const auto legacy_position = json.find(legacy_exhaustive);
      legacy_position != std::string::npos) {
    json.replace(legacy_position, legacy_exhaustive.size(),
        "\"sectorSearchExhaustive\":false");
  }

  const std::string marker = std::string("\"globalSearchComplete\":")
      + (truthful.recommendation.global_search_complete ? "true" : "false");
  const auto position = json.find(marker);
  if (position != std::string::npos) {
    const auto insert_at = position + marker.size();
    json.insert(insert_at,
        std::string(",\"globalSearchCompleteSemantics\":\"policy-grid-complete-with-fixed-sector-posture\"")
        + ",\"policyGridComplete\":" + (policy_grid_complete ? "true" : "false")
        + ",\"sectorPostureMode\":\"authoritative-input\""
        + ",\"sectorSearchPerformed\":false"
        + ",\"sectorPostureVerified\":" + (sector_posture_verified ? "true" : "false")
        + ",\"sectorPostureVerificationDraws\":"
        + std::to_string(truthful.recommendation.verification_monte_carlo_draws));
  }
  return json;
}

}  // namespace cad::policy_truth_surface
