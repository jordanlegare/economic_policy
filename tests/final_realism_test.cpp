#include "border_finalist_resimulation.hpp"
#include "policy_dynamics.hpp"
#include "policy_engine.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

const cad::Scenario& scenario(const cad::Result& result, const char* id) {
  for (const auto& s : result.scenarios) if (s.id == id) return s;
  assert(false && "scenario not found");
  return result.scenarios.front();
}

struct BorderTrackingEngine {
  int calls = 0;
  double last_border_friction = -1.0;

  cad::Result evaluate(cad::Economy& economy) {
    ++calls;
    last_border_friction = economy.border_friction;
    cad::Result result;
    result.recommendation.verification_monte_carlo_draws = 2800;
    cad::Scenario s;
    s.id = "border-strategy";
    s.canada_score = 70.0;
    s.us_score = 69.0;
    s.bilateral_growth_floor = 0.5;
    s.sector_verified = true;
    result.scenarios.push_back(s);
    return result;
  }
};

cad::NegotiationPackage border_package() {
  cad::NegotiationPackage package;
  package.id = "pkg-border";
  package.strategy_id = "border-strategy";
  package.strategy_name = "Border finalist";
  package.macro_base_verified = true;
  package.sector_posture_verified = true;
  package.bargaining_terms_screened = true;
  package.us_sector_coverage.fill(100.0);
  package.canada_sector_coverage.fill(100.0);
  package.issues = {
      {"us-tariff-relief", "U.S. tariff relief", 0.0, 0.0},
      {"canada-tariff-relief", "Canadian tariff relief", 0.0, 0.0},
      {"border-facilitation", "Border facilitation", 50.0, 50.0},
      {"procurement", "Procurement", 0.0, 0.0},
      {"supply-chain", "Supply chain", 0.0, 0.0}
  };
  return package;
}

}  // namespace

int main() {
  const auto path = cad::build_policy_implementation_paths(1.0, 0.6, 40.0, 0.5, 0.2);
  assert(std::abs(path.fiscal[0] - 0.65) < 1e-12);
  assert(std::abs(path.fiscal[2] - 1.0) < 1e-12);
  assert(path.fiscal[11] < path.fiscal[4]);
  assert(std::abs(path.productive_investment[0] - 0.12) < 1e-12);
  assert(std::abs(path.productive_investment[4] - 0.60) < 1e-12);
  assert(std::abs(path.negotiated_relief[0] - 14.0) < 1e-12);
  assert(std::abs(path.negotiated_relief[3] - 40.0) < 1e-12);
  assert(path.targeted_relief[11] < path.targeted_relief[1]);
  assert(path.diversification[0] < path.diversification[8]);
  assert(std::abs(path.diversification[8] - 0.2) < 1e-12);

  cad::StructuralParameters p;
  p.shock_tail_threshold = 2.0;
  p.shock_tail_scale = 1.75;
  p.stress_regime_shock_scale = 1.35;
  assert(std::abs(cad::regime_tail_innovation(1.5, p, false) - 1.5) < 1e-12);
  assert(std::abs(cad::regime_tail_innovation(2.1, p, false) - 2.1 * 1.75) < 1e-12);
  assert(std::abs(cad::regime_tail_innovation(-2.1, p, true)
      - (-2.1 * 1.75 * 1.35)) < 1e-12);

  cad::Economy normal;
  normal.us_tariff_canada = 5.0;
  normal.canada_retaliatory_tariff = 1.5;
  normal.credit_spread = 1.3;
  normal.gdp_growth = 1.0;
  assert(!cad::macro_stress_regime(normal));
  normal.us_tariff_canada = 50.0;
  assert(cad::macro_stress_regime(normal));

  // Production evaluation must expose the deterministic implementation paths.
  cad::Economy economy;
  economy.exhaustive_policy_search = false;
  cad::PolicyEngine engine(20260810);
  const auto base = engine.evaluate(economy);
  const auto& status = scenario(base, "statusquo");
  assert(std::abs(status.fiscal_path[0] - .65 * status.fiscal_impulse) < 1e-12);
  assert(std::abs(status.productive_investment_path[4]
      - status.fiscal_impulse * status.productive_share) < 1e-12);
  assert(std::abs(status.negotiated_relief_path[3] - status.negotiated_relief) < 1e-12);
  assert(std::abs(status.diversification_path[8] - status.diversification) < 1e-12);

  const auto json = cad::to_json(base);
  assert(json.find("\"fiscalPath\":[") != std::string::npos);
  assert(json.find("\"productiveInvestmentPath\":[") != std::string::npos);
  assert(json.find("\"negotiatedReliefPath\":[") != std::string::npos);
  assert(json.find("\"targetedReliefPath\":[") != std::string::npos);
  assert(json.find("\"diversificationPath\":[") != std::string::npos);

  // Internal decision coefficients are typed and sensitivity-visible, but the
  // institutional mandate flag remains fixed because the optimizer never tunes
  // them as policy controls.
  cad::Economy reweighted = economy;
  reweighted.loss_weights.boc_inflation *= 1.5;
  const auto weighted = engine.evaluate(reweighted);
  const auto& base_status = scenario(base, "statusquo");
  const auto& weighted_status = scenario(weighted, "statusquo");
  assert(std::abs(base_status.boc_score - weighted_status.boc_score) > 1e-8);
  assert(base.recommendation.mandate_weights_fixed);
  assert(weighted.recommendation.mandate_weights_fixed);

  // Border facilitation uses the existing production border-friction index with
  // no fitted conversion coefficient: a 50% facilitation move removes 50% of the
  // submitted index. The provenance remains an explicit structural assumption.
  cad::Economy border_economy;
  border_economy.border_friction = 2.0;
  border_economy.minimum_bilateral_growth = 0.0;
  cad::Result border_source;
  border_source.recommendation.verification_monte_carlo_draws = 2800;
  border_source.recommendation.baseline_canada_score = 60.0;
  border_source.recommendation.baseline_us_score = 59.0;
  border_source.recommendation.independent_us_trade_channel = true;
  border_source.recommendation.trade_balance_is_objective = false;
  border_source.recommendation.mandate_weights_fixed = true;
  cad::Scenario source_scenario;
  source_scenario.id = "border-strategy";
  source_scenario.sector_verified = true;
  border_source.scenarios.push_back(source_scenario);

  cad::NegotiationAnalysis border_negotiation;
  border_negotiation.recommended = border_package();
  border_negotiation.frontier.push_back(border_negotiation.recommended);
  border_negotiation.pareto_frontier_size = 1;
  cad::RobustRecommendationAnalysis border_robustness;
  border_robustness.recommended_package_id = "pkg-border";
  cad::RobustPackageMetrics border_metrics;
  border_metrics.package_id = "pkg-border";
  border_metrics.strategy_id = "border-strategy";
  border_metrics.samples = 5000;
  border_metrics.clears_probability_gate = true;
  border_robustness.packages.push_back(border_metrics);

  BorderTrackingEngine border_engine;
  const auto border_analysis = cad::verify_bargaining_finalists_with_border_mapping(
      border_engine, border_economy, border_source,
      border_negotiation, border_robustness, 1);
  assert(border_engine.calls == 1);
  assert(std::abs(border_engine.last_border_friction - 1.0) < 1e-12);
  assert(border_analysis.finalists.size() == 1);
  assert(border_analysis.finalists.front().mapping_complete);
  assert(border_analysis.finalists.front().full_package_resimulated);
  assert(border_analysis.finalists.front().verified_win_win);
  assert(border_negotiation.recommended.full_package_resimulated);
  assert(border_negotiation.recommended.verified_win_win);

  bool border_mapping_found = false;
  for (const auto& mapping : border_analysis.mappings) {
    if (mapping.term_id != "border-facilitation") continue;
    border_mapping_found = true;
    assert(mapping.production_mapped);
    assert(mapping.production_target == "Economy.border_friction");
    assert(mapping.provenance_class.find("structural-normalization") != std::string::npos);
    assert(mapping.provenance_class.find("not empirically calibrated") != std::string::npos);
  }
  assert(border_mapping_found);
  return 0;
}
