#pragma once

#include "linked_finalist_resimulation.hpp"

namespace cad {

// Backward-compatible entry point retained for callers introduced by #122.
// The production verifier now owns all reviewed linked bargaining mappings, so
// this alias intentionally delegates to the complete linked-mapping contract.
template<class Engine>
FinalistResimulationAnalysis verify_bargaining_finalists_with_border_mapping(
    Engine& engine, const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness,
    std::size_t finalist_limit = 3) {
  return verify_bargaining_finalists_with_linked_mappings(
      engine, economy, result, negotiation, robustness, finalist_limit);
}

}  // namespace cad
