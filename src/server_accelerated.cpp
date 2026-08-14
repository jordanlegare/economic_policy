#include "calibration.hpp"
#include "evaluation_profile.hpp"
#include "interactive_frontier.hpp"
#include "negotiation_support.hpp"
#include "robust_recommendation_hot.hpp"
#include "robust_trade_diplomacy.hpp"
#include "trade_diplomacy_platform.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace cad {

NegotiationAnalysis profiled_analyze_negotiation(
    const Economy& economy, const Result& result) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::negotiation);
  return ::cad::analyze_negotiation(economy, result);
}

RobustRecommendationAnalysis profiled_analyze_robust_recommendations(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation,
    const CalibrationSnapshot& calibration,
    int draws = 5000, std::uint64_t seed = 20260811) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::robustness);
  return ::cad::analyze_robust_recommendations_hot(
      economy, result, negotiation, calibration, draws, seed);
}

PublishedTradeDiplomacyPlatform profiled_build_trade_diplomacy_platform(
    const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::platform);
  ::cad::ensure_robust_package_in_interactive_preview(
      negotiation, robustness.recommended_package_id);
  return ::cad::build_trade_diplomacy_publication(
      economy, result, negotiation, robustness);
}

template<class... Args>
auto profiled_attach_calibration_json(Args&&... args)
    -> decltype(::cad::attach_calibration_json(std::forward<Args>(args)...)) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return ::cad::attach_calibration_json(std::forward<Args>(args)...);
}

std::string profiled_attach_negotiation_json(
    std::string base_json, const NegotiationAnalysis& analysis) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return interactive_frontier::attach_negotiation_json(
      std::move(base_json), analysis);
}

std::string profiled_attach_robustness_json(
    std::string base_json, const RobustRecommendationAnalysis& analysis) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return interactive_frontier::attach_robustness_json(
      std::move(base_json), analysis);
}

std::string profiled_attach_trade_diplomacy_json(
    std::string base_json, const PublishedTradeDiplomacyPlatform& publication) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return ::cad::attach_published_trade_diplomacy_json(
      std::move(base_json), publication);
}

}  // namespace cad

// Keep the server routing/session implementation unchanged. Interpose only the
// synchronous phases that execute after PolicyEngine::evaluate() so the existing
// live profiler remains useful until the HTTP response is actually built. The
// server computes `robustness` immediately before constructing the operations
// platform; bind that in-scope result explicitly so operations cannot choose a
// second primary package.
#define analyze_negotiation profiled_analyze_negotiation
#define analyze_robust_recommendations profiled_analyze_robust_recommendations
#define build_trade_diplomacy_platform(economy, result, negotiation) \
  profiled_build_trade_diplomacy_platform((economy), (result), (negotiation), robustness)
#define attach_calibration_json profiled_attach_calibration_json
#define attach_negotiation_json profiled_attach_negotiation_json
#define attach_robustness_json profiled_attach_robustness_json
#define attach_trade_diplomacy_json profiled_attach_trade_diplomacy_json
#include "server.cpp"
#undef attach_trade_diplomacy_json
#undef attach_robustness_json
#undef attach_negotiation_json
#undef attach_calibration_json
#undef build_trade_diplomacy_platform
#undef analyze_robust_recommendations
#undef analyze_negotiation
