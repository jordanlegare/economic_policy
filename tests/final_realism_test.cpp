#include "bilateral_trade.hpp"
#include "calibration.hpp"
#include "linked_finalist_resimulation.hpp"
#include "policy_dynamics.hpp"
#include "policy_engine.hpp"
#include "trade_network.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

const cad::Scenario& scenario(const cad::Result& result, const char* id) {
  for (const auto& s : result.scenarios) if (s.id == id) return s;
  assert(false && "scenario not found");
  return result.scenarios.front();
}

struct LinkedTrackingEngine {
  int calls = 0;
  double last_border_friction = -1.0;
  double last_procurement_uplift_pp = -1.0;
  double last_supply_chain_mitigation = -1.0;

  cad::Result evaluate(cad::Economy& economy) {
    ++calls;
    last_border_friction = economy.border_friction;
    last_procurement_uplift_pp =
        economy.trade_network_tuning.procurement_quantity_uplift_pp;
    last_supply_chain_mitigation =
        economy.trade_network_tuning.supply_chain_mitigation;
    cad::Result result;
    result.recommendation.verification_monte_carlo_draws = 2800;
    cad::Scenario s;
    s.id = "linked-strategy";
    s.canada_score = 70.0;
    s.us_score = 69.0;
    s.bilateral_growth_floor = 0.5;
    s.sector_verified = true;
    result.scenarios.push_back(s);
    return result;
  }
};

cad::NegotiationPackage linked_package() {
  cad::NegotiationPackage package;
  package.id = "pkg-linked";
  package.strategy_id = "linked-strategy";
  package.strategy_name = "Fully linked finalist";
  package.macro_base_verified = true;
  package.sector_posture_verified = true;
  package.bargaining_terms_screened = true;
  package.us_sector_coverage.fill(100.0);
  package.canada_sector_coverage.fill(100.0);
  package.issues = {
      {"us-tariff-relief", "U.S. tariff relief", 0.0, 25.0},
      {"canada-tariff-relief", "Canadian tariff relief", 25.0, 0.0},
      {"border-facilitation", "Border facilitation", 50.0, 50.0},
      {"procurement", "Procurement", 100.0, 100.0},
      {"supply-chain", "Supply chain", 75.0, 75.0}
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

  // Zero linked-state defaults are a strict no-op under a fixed seed.
  cad::Economy explicit_zero = economy;
  explicit_zero.trade_network_tuning.procurement_quantity_uplift_pp = 0.0;
  explicit_zero.trade_network_tuning.supply_chain_mitigation = 0.0;
  cad::PolicyEngine zero_a(20260814);
  cad::PolicyEngine zero_b(20260814);
  assert(cad::to_json(zero_a.evaluate(economy))
      == cad::to_json(zero_b.evaluate(explicit_zero)));

  // Supply-chain commitment owns only indirect IO-network propagation. Full
  // mitigation removes supplier-demand/input-cost spillovers while leaving the
  // direct tariff incidence and direct bilateral trade drag unchanged.
  cad::TradeNetworkInput network_input;
  network_input.us_headline_tariff = 25.0;
  network_input.canada_headline_tariff = 10.0;
  network_input.trade_elasticity = 0.65;
  network_input.price_pass_through = 0.24;
  network_input.us_coverage.fill(100.0);
  network_input.canada_coverage.fill(100.0);
  const auto network_base = cad::evaluate_trade_network(network_input);
  cad::TradeNetworkInput network_full_mitigation = network_input;
  network_full_mitigation.tuning.supply_chain_mitigation = 1.0;
  const auto network_mitigated = cad::evaluate_trade_network(network_full_mitigation);
  assert(network_base.canada_supply_chain_drag > 0.0);
  assert(network_base.us_supply_chain_drag > 0.0);
  assert(std::abs(network_mitigated.canada_supply_chain_drag) < 1e-12);
  assert(std::abs(network_mitigated.us_supply_chain_drag) < 1e-12);
  assert(std::abs(network_mitigated.canada_input_cost_pressure) < 1e-12);
  assert(std::abs(network_mitigated.us_input_cost_pressure) < 1e-12);
  assert(std::abs(network_base.sectors[4].us_tariff.applied_tariff
      - network_mitigated.sectors[4].us_tariff.applied_tariff) < 1e-12);
  assert(std::abs(network_base.sectors[4].canada_direct_trade_drag
      - network_mitigated.sectors[4].canada_direct_trade_drag) < 1e-12);

  // Procurement reciprocity is a dedicated two-way bilateral quantity-access
  // channel, not fiscal impulse. One full bargaining move means +1 percentage
  // point to each sector quantity ratio and therefore changes the same export,
  // tariff-revenue and signed trade-demand ledger used by the macro engine.
  cad::Economy procurement_economy;
  procurement_economy.us_tariff_canada = 5.0;
  procurement_economy.canada_retaliatory_tariff = 1.5;
  procurement_economy.border_friction = 0.0;
  procurement_economy.us_sector_coverage.fill(100.0);
  procurement_economy.canada_sector_coverage.fill(100.0);
  cad::Scenario procurement_policy;
  cad::TradeNetworkInput procurement_network_input;
  procurement_network_input.us_headline_tariff = procurement_economy.us_tariff_canada;
  procurement_network_input.canada_headline_tariff = procurement_economy.canada_retaliatory_tariff;
  procurement_network_input.trade_elasticity = procurement_economy.trade_elasticity;
  procurement_network_input.price_pass_through = procurement_economy.tariff_price_pass_through;
  procurement_network_input.us_coverage.fill(100.0);
  procurement_network_input.canada_coverage.fill(100.0);
  const auto procurement_network = cad::evaluate_trade_network(procurement_network_input);
  cad::StructuralParameters procurement_parameters;
  const auto procurement_base = cad::build_bilateral_trade_state(
      procurement_economy, procurement_policy, procurement_parameters,
      procurement_network);
  cad::Economy procurement_open = procurement_economy;
  procurement_open.trade_network_tuning.procurement_quantity_uplift_pp = 1.0;
  const auto procurement_mapped = cad::build_bilateral_trade_state(
      procurement_open, procurement_policy, procurement_parameters,
      procurement_network);
  assert(std::abs(procurement_mapped.canada_bilateral_quantity_ratio
      - procurement_base.canada_bilateral_quantity_ratio - 0.01) < 1e-12);
  assert(std::abs(procurement_mapped.us_bilateral_quantity_ratio
      - procurement_base.us_bilateral_quantity_ratio - 0.01) < 1e-12);
  assert(procurement_mapped.canada_exports_to_us_cad
      > procurement_base.canada_exports_to_us_cad);
  assert(procurement_mapped.canada_imports_from_us_cad
      > procurement_base.canada_imports_from_us_cad);
  assert(procurement_mapped.us_tariff_revenue_cad
      > procurement_base.us_tariff_revenue_cad);
  assert(procurement_mapped.canada_tariff_revenue_cad
      > procurement_base.canada_tariff_revenue_cad);
  assert(procurement_mapped.canada_macro_trade_drag
      < procurement_base.canada_macro_trade_drag);
  assert(procurement_mapped.us_macro_trade_drag
      < procurement_base.us_macro_trade_drag);
  assert(procurement_open.fiscal_balance_gdp == procurement_economy.fiscal_balance_gdp);
  assert(procurement_open.infrastructure_impulse == procurement_economy.infrastructure_impulse);

  // Finalist-only channels must also separate calibrated evaluation-cache keys.
  cad::Economy cache_a;
  cad::Economy cache_b = cache_a;
  cache_b.trade_network_tuning.procurement_quantity_uplift_pp = 0.5;
  assert(cad::linked_bargaining_cache_namespace("snapshot", cache_a)
      != cad::linked_bargaining_cache_namespace("snapshot", cache_b));
  cache_b = cache_a;
  cache_b.trade_network_tuning.supply_chain_mitigation = 0.5;
  assert(cad::linked_bargaining_cache_namespace("snapshot", cache_a)
      != cad::linked_bargaining_cache_namespace("snapshot", cache_b));

  // All five linked bargaining terms are now nonzero and production-owned. The
  // wrapper materializes border, procurement and supply-chain state, consumes
  // those reduced-form issue amplitudes, then lets the generic verifier
  // materialize both tariff-relief directions and run the immutable source
  // strategy through the final gate.
  const auto package_terms = cad::robust_detail::package_terms(linked_package());
  assert(package_terms.us_tariff_relief > 0.0);
  assert(package_terms.canada_tariff_relief > 0.0);
  assert(package_terms.border_facilitation > 0.0);
  assert(package_terms.procurement_reciprocity > 0.0);
  assert(package_terms.supply_chain_commitment > 0.0);

  cad::Economy linked_economy;
  linked_economy.border_friction = 2.0;
  linked_economy.minimum_bilateral_growth = 0.0;
  cad::Result linked_source;
  linked_source.recommendation.verification_monte_carlo_draws = 2800;
  linked_source.recommendation.baseline_canada_score = 60.0;
  linked_source.recommendation.baseline_us_score = 59.0;
  linked_source.recommendation.independent_us_trade_channel = true;
  linked_source.recommendation.trade_balance_is_objective = false;
  linked_source.recommendation.mandate_weights_fixed = true;
  cad::Scenario source_scenario;
  source_scenario.id = "linked-strategy";
  source_scenario.sector_verified = true;
  linked_source.scenarios.push_back(source_scenario);

  cad::NegotiationAnalysis linked_negotiation;
  linked_negotiation.recommended = linked_package();
  linked_negotiation.frontier.push_back(linked_negotiation.recommended);
  linked_negotiation.pareto_frontier_size = 1;
  cad::RobustRecommendationAnalysis linked_robustness;
  linked_robustness.recommended_package_id = "pkg-linked";
  cad::RobustPackageMetrics linked_metrics;
  linked_metrics.package_id = "pkg-linked";
  linked_metrics.strategy_id = "linked-strategy";
  linked_metrics.samples = 5000;
  linked_metrics.clears_probability_gate = true;
  linked_robustness.packages.push_back(linked_metrics);

  LinkedTrackingEngine linked_engine;
  const auto linked_analysis = cad::verify_bargaining_finalists_with_linked_mappings(
      linked_engine, linked_economy, linked_source,
      linked_negotiation, linked_robustness, 1);
  assert(linked_engine.calls == 1);
  assert(std::abs(linked_engine.last_border_friction - 1.0) < 1e-12);
  assert(std::abs(linked_engine.last_procurement_uplift_pp - 1.0) < 1e-12);
  assert(std::abs(linked_engine.last_supply_chain_mitigation - 0.75) < 1e-12);
  assert(linked_analysis.finalists.size() == 1);
  assert(linked_analysis.finalists.front().mapping_complete);
  assert(linked_analysis.finalists.front().full_package_resimulated);
  assert(linked_analysis.finalists.front().verified_win_win);
  assert(linked_negotiation.recommended.full_package_resimulated);
  assert(linked_negotiation.recommended.verified_win_win);

  bool border_mapping_found = false;
  bool procurement_mapping_found = false;
  bool supply_mapping_found = false;
  for (const auto& mapping : linked_analysis.mappings) {
    if (mapping.term_id == "border-facilitation") {
      border_mapping_found = true;
      assert(mapping.production_mapped);
      assert(mapping.production_target == "Economy.border_friction");
      assert(mapping.provenance_class.find("not empirically calibrated")
          != std::string::npos);
    } else if (mapping.term_id == "procurement") {
      procurement_mapping_found = true;
      assert(mapping.production_mapped);
      assert(mapping.production_target.find("procurement_quantity_uplift_pp")
          != std::string::npos);
      assert(mapping.unit_contract.find("percentage point") != std::string::npos);
      assert(mapping.provenance_class.find("internal_model_design_v4")
          != std::string::npos);
    } else if (mapping.term_id == "supply-chain") {
      supply_mapping_found = true;
      assert(mapping.production_mapped);
      assert(mapping.production_target.find("supply_chain_mitigation")
          != std::string::npos);
      assert(mapping.transformation.find("direct tariff incidence")
          != std::string::npos);
      assert(mapping.provenance_class.find("internal_model_design_v4")
          != std::string::npos);
    }
  }
  assert(border_mapping_found);
  assert(procurement_mapping_found);
  assert(supply_mapping_found);
  return 0;
}
