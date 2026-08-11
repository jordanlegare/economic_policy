#pragma once

#include "negotiation_support.hpp"

#include <algorithm>
#include <string>

namespace cad {

// The bargaining layer scores concessions as incremental gains over a macro
// scenario. The macro engine now owns the authoritative country-specific export
// baselines. This adapter preserves the bargaining utilities while rebasing the
// displayed U.S. export outcome onto Scenario::us_export_change rather than
// reconstructing a second, potentially inconsistent baseline.
inline void align_negotiation_trade_channels(const Economy& economy, const Result& result,
                                             NegotiationAnalysis& analysis) {
  const double us_tariff_exposure = economy.trade_elasticity
      * economy.canada_retaliatory_tariff
      * std::max(0.0, std::min(1.0, economy.imports_from_us_share / 100.0));

  const auto scenario_for = [&](const std::string& strategy_id) -> const Scenario* {
    for (const auto& scenario : result.scenarios)
      if (scenario.id == strategy_id) return &scenario;
    return nullptr;
  };
  const auto issue_move = [](const NegotiationPackage& package, const char* id,
                             bool canada) {
    for (const auto& issue : package.issues) {
      if (issue.id == id) return canada ? issue.canada_move : issue.us_move;
    }
    return 0.0;
  };

  const auto align_package = [&](NegotiationPackage& package) {
    const auto* scenario = scenario_for(package.strategy_id);
    if (!scenario) return;
    const double canada_relief = issue_move(package, "canada-tariff-relief", true) / 100.0;
    const double border = issue_move(package, "border-facilitation", true) / 100.0;
    const double procurement = issue_move(package, "procurement", true) / 100.0;
    const double supply = issue_move(package, "supply-chain", true) / 100.0;
    package.us_export_change = scenario->us_export_change
        + 0.55 * us_tariff_exposure * canada_relief
        + 0.95 * border
        + 0.95 * procurement
        + 0.35 * supply;
  };

  for (auto& package : analysis.frontier) align_package(package);
  align_package(analysis.recommended);
}

}  // namespace cad
