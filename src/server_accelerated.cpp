#include "border_finalist_resimulation.hpp"
#include "calibration.hpp"
#include "evaluation_profile.hpp"
#include "finalist_resimulation.hpp"
#include "interactive_frontier.hpp"
#include "negotiation_support.hpp"
#include "policy_truth_surface.hpp"
#include "robust_recommendation_hot.hpp"
#include "robust_trade_diplomacy.hpp"
#include "server_session.hpp"
#include "trade_diplomacy_platform.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
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

struct PublishedTradeDiplomacyWithFinalists : PublishedTradeDiplomacyPlatform {
  FinalistResimulationAnalysis finalist_resimulation;
};

PublishedTradeDiplomacyWithFinalists profiled_build_trade_diplomacy_platform(
    CalibratedPolicyEngine& engine,
    const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::platform);
  auto finalist_resimulation = verify_bargaining_finalists_with_border_mapping(
      engine, economy, result, negotiation, robustness);
  ::cad::ensure_robust_package_in_interactive_preview(
      negotiation, robustness.recommended_package_id);
  auto publication = ::cad::build_trade_diplomacy_publication(
      economy, result, negotiation, robustness);

  PublishedTradeDiplomacyWithFinalists output;
  static_cast<PublishedTradeDiplomacyPlatform&>(output) = std::move(publication);
  output.finalist_resimulation = std::move(finalist_resimulation);
  return output;
}

std::string profiled_to_json(const Result& result) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return policy_truth_surface::to_json(result);
}

template<class T>
auto profiled_to_json(const T& value) -> decltype(::cad::to_json(value)) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  return ::cad::to_json(value);
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

std::string profiled_attach_trade_diplomacy_json(
    std::string base_json, const PublishedTradeDiplomacyWithFinalists& publication) {
  evaluation_profile::Scope scope(evaluation_profile::Phase::serialization);
  auto output = ::cad::attach_published_trade_diplomacy_json(
      std::move(base_json),
      static_cast<const PublishedTradeDiplomacyPlatform&>(publication));
  return ::cad::attach_finalist_resimulation_json(
      std::move(output), publication.finalist_resimulation);
}

}  // namespace cad

namespace cad::server {

bool parse_economy_with_negotiation_authority(
    const request_json::Object& object, Economy& economy, std::string& error,
    const std::shared_ptr<SessionState>& session, const std::string& path) {
  if (!::cad::server::parse_economy(object, economy, error)) return false;
  if (path != "/api/evaluate") return true;

  bool comparison_only = false;
  if (!request_json::boolean_value(
          object, "comparisonOnly", false, comparison_only, error))
    return false;
  if (comparison_only) return true;

  // The negotiation revision captured by the router immediately after parsing
  // now identifies the exact control authority used by the solve. Applying the
  // durable state here also means the evaluation provenance fingerprint is
  // computed from those authoritative controls rather than a parallel request
  // copy that merely happened to carry the same revision number.
  std::lock_guard<std::mutex> lock(session->mutex);
  session->negotiation.apply_to(economy);
  return true;
}

}  // namespace cad::server

// Keep the server routing/session implementation unchanged. Interpose only the
// synchronous phases that execute after PolicyEngine::evaluate(), the production
// truth serializer, the calibrated publication boundary, and the negotiation-
// owned input boundary for stateful evaluation. `comparisonOnly` deliberately
// bypasses negotiation-state authority for stateless comparisons.
#define parse_economy(object, economy, error) \
  ::cad::server::parse_economy_with_negotiation_authority( \
      (object), (economy), (error), session, request.path)
#define publish_evaluation(expected, economy, bargaining, robustness, fingerprint) \
  publish_evaluation_with_calibration( \
      (expected), (economy), (bargaining), (robustness), (fingerprint), \
      context.engine.snapshot().snapshot_id)
#define to_json profiled_to_json
#define analyze_negotiation profiled_analyze_negotiation
#define analyze_robust_recommendations profiled_analyze_robust_recommendations
#define build_trade_diplomacy_platform(economy, result, negotiation) \
  profiled_build_trade_diplomacy_platform( \
      context.engine, (economy), (result), (negotiation), robustness)
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
#undef to_json
#undef publish_evaluation
#undef parse_economy
