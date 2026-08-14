#pragma once

#include "robust_recommendation.hpp"
#include "trade_diplomacy_platform.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cad {

struct PublishedTradeDiplomacyPlatform {
  TradeDiplomacyPlatform platform;
  NegotiationPackage robust_recommended_package;
  bool has_robust_recommended_package = false;
  std::size_t total_robust_package_count = 0;
  std::string decision_authority = "second-stage-robustness";
  bool stress_cases_are_diagnostics = true;
};

namespace robust_trade_diplomacy_detail {

inline const NegotiationPackage* find_package(const NegotiationAnalysis& negotiation,
                                               const std::string& package_id) {
  if (package_id.empty()) return nullptr;
  for (const auto& package : negotiation.frontier)
    if (package.id == package_id) return &package;
  if (negotiation.recommended.id == package_id) return &negotiation.recommended;
  return nullptr;
}

inline const DiplomacyRobustPackage* find_operational_package(
    const TradeDiplomacyPlatform& platform, const std::string& package_id) {
  for (const auto& package : platform.robust_packages)
    if (package.package_id == package_id) return &package;
  return nullptr;
}

inline void append_unique_operational_package(
    std::vector<DiplomacyRobustPackage>& output,
    std::unordered_set<std::string>& seen,
    const TradeDiplomacyPlatform& source,
    const std::string& package_id,
    std::size_t limit) {
  if (output.size() >= limit || package_id.empty() || seen.count(package_id)) return;
  const auto* package = find_operational_package(source, package_id);
  if (!package) return;
  seen.insert(package_id);
  output.push_back(*package);
}

inline std::string bridge_candidate(const TradeDiplomacyPlatform& platform,
                                    const NegotiationAnalysis& negotiation,
                                    const std::string& robust_primary,
                                    const std::string& former_primary) {
  if (!former_primary.empty() && former_primary != robust_primary
      && find_package(negotiation, former_primary))
    return former_primary;
  if (!platform.bridge_package_id.empty() && platform.bridge_package_id != robust_primary
      && find_package(negotiation, platform.bridge_package_id))
    return platform.bridge_package_id;
  for (const auto& package : negotiation.frontier)
    if (package.id != robust_primary) return package.id;
  return {};
}

}  // namespace robust_trade_diplomacy_detail

inline PublishedTradeDiplomacyPlatform build_trade_diplomacy_publication(
    const Economy& economy, const Result& result,
    const NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness,
    std::size_t operational_package_limit = 10) {
  using namespace robust_trade_diplomacy_detail;
  PublishedTradeDiplomacyPlatform publication;
  publication.platform = build_trade_diplomacy_platform(economy, result, negotiation);
  publication.total_robust_package_count = publication.platform.robust_packages.size();
  operational_package_limit = std::max<std::size_t>(1, operational_package_limit);

  const std::string former_primary = publication.platform.recommended_robust_package_id;
  const std::string robust_primary = robustness.recommended_package_id;
  if (const auto* full = find_package(negotiation, robust_primary)) {
    publication.robust_recommended_package = *full;
    publication.has_robust_recommended_package = true;
    publication.platform.recommended_robust_package_id = robust_primary;
    publication.platform.bridge_package_id = bridge_candidate(
        publication.platform, negotiation, robust_primary, former_primary);

    if (const auto* operational = find_operational_package(publication.platform, robust_primary))
      publication.platform.recommended_worst_case_surplus = operational->worst_case_surplus;

    // The round plan may sequence implementation diagnostics, but every actual
    // package reference must point at the same 5,000-draw decision authority.
    for (auto& step : publication.platform.round_plan)
      if (!step.package_id.empty()) step.package_id = robust_primary;
  }

  // The six hand-authored stress cases remain useful diagnostics. They no longer
  // choose the published primary, and their transport is bounded independently
  // of the size of the bargaining frontier.
  const TradeDiplomacyPlatform complete = publication.platform;
  std::vector<DiplomacyRobustPackage> bounded;
  bounded.reserve(std::min<std::size_t>(operational_package_limit,
                                       complete.robust_packages.size()));
  std::unordered_set<std::string> seen;
  append_unique_operational_package(bounded, seen, complete,
      publication.platform.recommended_robust_package_id, operational_package_limit);
  append_unique_operational_package(bounded, seen, complete,
      publication.platform.bridge_package_id, operational_package_limit);
  for (const auto& stress : complete.robustness_cases)
    append_unique_operational_package(bounded, seen, complete,
        stress.winner_package_id, operational_package_limit);
  for (const auto& package : complete.robust_packages) {
    if (bounded.size() >= operational_package_limit) break;
    if (seen.insert(package.package_id).second) bounded.push_back(package);
  }
  publication.platform.robust_packages = std::move(bounded);
  return publication;
}

inline std::string published_trade_diplomacy_json(
    const PublishedTradeDiplomacyPlatform& publication) {
  std::string json = trade_diplomacy_json(publication.platform);
  if (json.empty() || json.back() != '}') return json;
  json.pop_back();
  json += ",\"totalPackageCount\":"
      + std::to_string(publication.total_robust_package_count)
      + ",\"decisionAuthority\":\""
      + diplomacy_detail::escape_platform_json(publication.decision_authority)
      + "\",\"stressCasesAreDiagnostics\":"
      + std::string(publication.stress_cases_are_diagnostics ? "true" : "false")
      + "}";
  return json;
}

inline std::string attach_published_trade_diplomacy_json(
    std::string base_json, const PublishedTradeDiplomacyPlatform& publication) {
  if (base_json.empty() || base_json.back() != '}') return base_json;
  base_json.pop_back();
  base_json += ",\"robustRecommendedPackage\":";
  if (publication.has_robust_recommended_package) {
    std::ostringstream package_json;
    package_json << std::fixed << std::setprecision(3);
    negotiation_detail::package_json(package_json,
        publication.robust_recommended_package);
    base_json += package_json.str();
  } else {
    base_json += "null";
  }
  base_json += ",\"tradeDiplomacy\":"
      + published_trade_diplomacy_json(publication) + "}";
  return base_json;
}

}  // namespace cad
