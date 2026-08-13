#pragma once

#include "trade_network.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cad {

struct StructuralParameterProvenance {
  std::string name;
  double baseline = 0.0;
  std::string unit;
  std::string kind;
  std::string source_id;
  std::string vintage;
  double lower_bound = 0.0;
  double upper_bound = 0.0;
  std::string distribution = "fixed";
  double relative_sigma = 0.0;
  bool sampled = false;
  std::string notes;
};

struct StructuralParameterCorrelation {
  std::string left;
  std::string right;
  double correlation = 0.0;
  std::string kind;
  std::string source_id;
  std::string vintage;
  std::string notes;
};

struct StructuralParameterRegistry {
  std::string registry_id = "none";
  std::string as_of;
  bool loaded = false;
  std::vector<StructuralParameterProvenance> entries;
  std::vector<StructuralParameterCorrelation> correlations;

  const StructuralParameterProvenance* find(const std::string& name) const {
    for (const auto& entry : entries) if (entry.name == name) return &entry;
    return nullptr;
  }
};

struct StructuralParameters {
  std::string calibration_id = "baseline-v1";
  std::string calibration_vintage = "illustrative";
  StructuralParameterRegistry uncertainty_registry;

  double neutral_rate = 2.5;
  double inflation_target = 2.0;
  double rate_inflation_response = 0.75;
  double rate_output_response = 0.25;
  double max_quarterly_rate_step = 0.25;

  double output_persistence = 0.72;
  double fiscal_demand_multiplier = 0.36;
  double real_rate_demand_sensitivity = 0.18;
  double productive_supply_multiplier = 0.22;
  double global_growth_sensitivity = 0.08;

  double inflation_persistence = 0.68;
  double inflation_expectations_weight = 0.32;
  double phillips_curve_slope = 0.12;
  double fx_pass_through = 0.35;
  double import_price_pass_through = 0.022;
  double oil_inflation_sensitivity = 0.018;

  double canada_trade_drag_scale = 1.0;
  double us_retaliation_drag_scale = 1.0;
  double tariff_revenue_elasticity_scale = 1.0;

  // Production-network transmission coefficients. These were historically
  // embedded in trade_network.cpp; keeping them here makes their provenance,
  // bounds and structural uncertainty executable and auditable.
  double network_supplier_demand_transmission = 0.30;
  double network_input_cost_incidence = 0.85;
  double network_downstream_cost_transmission = 0.85;
  double network_price_cost_pass_through = 0.70;
  double network_output_cost_base = 0.12;
  double network_output_cost_cyclical = 0.18;
  double network_jobs_output_base = 0.20;
  double network_jobs_output_exposure = 0.35;

  double output_shock_sd = 0.16;
  double inflation_shock_sd = 0.11;
  // Frozen empirical residual-correlation estimate from the same 75-quarter
  // output-gap / inflation estimation panel. This captures only the identified
  // two-equation contemporaneous covariance; it is not a claim that the full
  // macro innovation vector has been empirically identified.
  double output_inflation_shock_correlation = -0.006249264169;
  double growth_shock_sd = 0.25;
  double us_growth_shock_sd = 0.18;
  double export_shock_sd = 0.35;
  double us_export_shock_sd = 0.30;

  // Tail/regime mechanics are explicit structural assumptions. They transform
  // the same seeded Gaussian innovations, so common-random-number comparisons
  // remain deterministic without pretending the tail shape is empirically
  // identified from the short calibration sample.
  double shock_tail_threshold = 2.0;
  double shock_tail_scale = 1.75;
  double stress_regime_shock_scale = 1.35;

  // Global multiplier for the declared per-parameter uncertainty widths.
  // 0 disables structural uncertainty; 0.10 is the reference V2 scale.
  double uncertainty_scale = 0.10;
};

// Internal loss-function coefficients are typed separately from economic
// structural parameters and delegation priorities. Normal optimization keeps
// them fixed. V2 welfare sensitivity may vary explicitly declared components
// without disguising normative/model-design choices as estimated economics.
struct DecisionLossWeights {
  double boc_inflation = 3.8;
  double boc_unemployment = 1.2;
  double boc_contraction = 0.7;
  double boc_recession = 0.018;

  double federal_debt = 0.32;
  double federal_contraction = 0.7;
  double federal_unemployment = 0.8;
  double federal_housing = 0.012;

  double us_exports = 0.55;
  double us_inflation = 0.8;
  double us_growth = 0.55;
  double us_retaliation = 0.25;
};

struct Economy {
  double policy_rate = 2.75, inflation = 2.4, core_inflation = 2.6;
  double gdp_growth = 1.6, output_gap = -0.4, unemployment = 6.4;
  double wage_growth = 3.5, productivity_growth = 0.8, population_growth = 2.1;
  double usdcad = 1.38, oil_price = 74.0, credit_spread = 1.35;
  double housing_gap = 7.0, household_debt_income = 178.0;
  double fiscal_balance_gdp = -1.2, federal_debt_gdp = 42.0;
  double program_growth = 2.0, tax_impulse = 0.0, infrastructure_impulse = 0.3;
  double global_growth = 2.8, inflation_expectations = 2.2;
  double us_growth = 2.0, us_inflation = 2.7;
  double us_tariff_canada = 50.0, canada_retaliatory_tariff = 5.0;
  double exports_to_us_share = 75.0, imports_from_us_share = 49.0;
  double exports_gdp = 25.0, import_content_consumption = 22.0;
  double trade_elasticity = 0.65, border_friction = 2.0;
  double tariff_price_pass_through = 0.24;
  double canada_exports_to_us_cad = 596.9, canada_imports_from_us_cad = 373.7;
  double tariff_relief = 0.0, trade_diversification = 0.0;
  double canada_priority = 50.0, us_priority = 50.0;
  double risk_aversion = 50.0, cooperation_ceiling = 50.0;
  double minimum_bilateral_growth = 0.0;
  DecisionLossWeights loss_weights{};
  // Internal structural tuning copied from the PolicyEngine before evaluation.
  // It is intentionally absent from the public request contract.
  TradeNetworkTuning trade_network_tuning{};
  // The web application's primary evaluation enables this mode after seeding
  // controls from the certified baseline. Low-level and robustness tests can
  // keep the staged mode when they are not claiming startup global optimality.
  bool exhaustive_policy_search = false;
  std::array<double, 20> us_sector_coverage{}, canada_sector_coverage{};

  // Production-compatible directional sector evidence can be supplied here.
  // Zero means "no direct sector estimate": the trade network then falls back
  // to the audited aggregate elasticity/pass-through anchors. Reference-only
  // literature must not be copied into these arrays merely because it exists.
  std::array<double, 20> us_sector_trade_elasticity{};
  std::array<double, 20> canada_sector_trade_elasticity{};
  std::array<double, 20> us_sector_price_pass_through{};
  std::array<double, 20> canada_sector_price_pass_through{};

  Economy() {
    us_sector_coverage.fill(100.0);
    canada_sector_coverage.fill(100.0);
    us_sector_trade_elasticity.fill(0.0);
    canada_sector_trade_elasticity.fill(0.0);
    us_sector_price_pass_through.fill(0.0);
    canada_sector_price_pass_through.fill(0.0);
  }
};

struct SectorImpact {
  std::string code, name;
  double canada_output = 0.0, canada_jobs = 0.0, canada_prices = 0.0;
  double us_output = 0.0, us_jobs = 0.0, us_prices = 0.0;
  double exposure = 0.0;
  // Negotiator-facing tariff-incidence and production-network diagnostics.
  // Tariff values are percentage points; upstream costs are sector marginal-
  // cost pressure propagated through the country-specific 20x20 networks.
  double us_applied_tariff = 0.0, canada_applied_tariff = 0.0;
  double us_buyer_pass_through = 0.0, canada_buyer_pass_through = 0.0;
  double canada_exporter_absorption = 0.0, us_exporter_absorption = 0.0;
  double us_importer_absorption = 0.0, canada_importer_absorption = 0.0;
  double canada_upstream_cost = 0.0, us_upstream_cost = 0.0;
};

struct Scenario {
  std::string id, name, description;
  double first_move_bp = 0.0, fiscal_impulse = 0.0, productive_share = 0.5;
  double negotiated_relief = 0.0, targeted_relief = 0.0, diversification = 0.0;
  double score = 0.0, boc_score = 0.0, federal_score = 0.0;
  double canada_score = 0.0, us_score = 0.0;
  // Average directional effective-tariff/coverage deviation from the submitted
  // UI posture, expressed on a 0-100 headline-equivalent distance scale.
  double trade_posture_distance = 0.0;
  // True when this verified scenario leaves both national welfare scores no
  // worse than the submitted posture and satisfies the bilateral growth floor.
  bool anchor_win_win = false;
  double inflation = 0.0, growth = 0.0, unemployment = 0.0;
  double us_growth = 0.0, bilateral_growth_floor = 0.0;
  bool sustained_bilateral_growth = false;
  double debt_gdp = 0.0, housing_gap = 0.0, recession_risk = 0.0;
  double cost_of_living = 0.0, real_income_growth = 0.0;
  double export_change = 0.0, us_export_change = 0.0;
  double us_tariff_revenue_usd = 0.0, us_tariff_revenue_cad = 0.0;
  double canada_tariff_revenue_cad = 0.0, canada_tariff_revenue_usd = 0.0;
  double canada_trade_balance_cad = 0.0, us_trade_balance_usd = 0.0;
  double trade_balance_gap_usd = 0.0, trade_balance_progress = 0.0;
  double us_export_expansion_usd = 0.0, canada_export_redirection_cad = 0.0;
  bool zero_trade_deficit = false;
  double debt_stress_p90 = 0.0, inflation_stress_p90 = 0.0;
  bool sector_verified = false;
  std::array<double, 20> applied_us_sector_coverage{}, applied_canada_sector_coverage{};
  std::array<double, 12> rates{}, inflation_path{}, growth_path{}, us_growth_path{}, debt_path{}, cost_path{}, export_path{}, us_export_path{};
  // Auditable implementation profiles derived from the chosen policy amplitudes.
  // These are deterministic policy rules, not additional optimizer dimensions.
  std::array<double, 12> fiscal_path{}, productive_investment_path{}, negotiated_relief_path{}, targeted_relief_path{}, diversification_path{};
  std::vector<SectorImpact> sectors;
};

struct RobustnessSummary {
  int parameter_draws = 0;
  int recommendation_wins = 0;
  int strategy_family_wins = 0;
  double recommendation_win_rate = 0.0;
  double strategy_family_win_rate = 0.0;
  double score_mean = 0.0;
  double score_p10 = 0.0;
  double score_p90 = 0.0;
  std::string classification = "not-evaluated";
  std::string calibration_id;
  std::string calibration_vintage;
  std::string parameter_registry_id = "none";
  std::string methodology = "not-evaluated";
  std::string structural_sampling_dependence = "not-evaluated";
  int sampled_parameter_count = 0;
  int correlation_pair_count = 0;
  bool correlation_matrix_valid = false;
  bool structural_parameters_active = false;
  bool common_random_numbers = false;
  bool sector_packages_reoptimized = false;
  bool policy_controls_reoptimized = false;
  bool parameter_bounds_active = false;
  bool parameter_provenance_complete = false;

  int policy_control_candidates_per_draw = 0;
  std::uint64_t policy_control_candidates_examined = 0;
  std::uint64_t policy_control_changes = 0;
  double reference_policy_control_retention_rate = 0.0;

  int sector_frontiers_built = 0;
  std::uint64_t nested_sector_optimizations = 0;
  std::uint64_t nested_sector_candidates_examined = 0;
  std::uint64_t nested_sector_finalists_resimulated = 0;
  std::uint64_t sector_package_changes = 0;
  double reference_package_retention_rate = 0.0;
};

struct WinWinRecommendation {
  double canada_priority = 50.0, us_priority = 50.0;
  double gdp_growth_floor = 0.0;
  double risk_aversion = 50.0, cooperation_ceiling = 50.0;
  std::string strategy_id, explanation;
  std::array<double, 20> us_sector_coverage{}, canada_sector_coverage{};
  std::array<double, 20> us_sector_output{}, canada_sector_value{};
  int sector_candidates_examined = 0;
  int sector_pareto_frontier_size = 0;
  int sector_finalists_resimulated = 0;
  int policy_candidates_verified = 0;
  int base_monte_carlo_draws = 700;
  int verification_monte_carlo_draws = 2800;
  double sector_grid_step = 25.0;
  double verified_canada_score = 0.0, verified_us_score = 0.0;
  double baseline_canada_score = 0.0, baseline_us_score = 0.0;
  double verified_min_sector_metric = 0.0;
  // User-steered selection: maximize verified welfare first, then within this
  // practical score band choose the package closest to the submitted trade
  // posture rather than jumping for an economically negligible score gain.
  double user_anchor_welfare_tolerance = 0.5;
  double best_verified_score = 0.0;
  double selected_trade_posture_distance = 0.0;
  bool user_anchor_selection_active = false;
  bool verified_win_win = false;
  bool global_search_complete = false;
  bool growth_constraint_met = false;
  bool independent_us_trade_channel = true;
  bool trade_balance_is_objective = false;
  bool mandate_weights_fixed = true;
  RobustnessSummary robustness;
  std::string sector_search_method = "Exact Pareto dynamic program at 25% increments of each side's permitted sector-relief envelope; the exact submitted posture is retained as an anchor candidate, and exhaustive startup mode verifies every retained frontier package unless the explicit safety cap binds";
  std::string trade_network_method = "not-evaluated";
};

struct Result {
  std::string regime, signal, rationale;
  double data_confidence = 0.0, neutral_rate = 0.0, policy_gap = 0.0;
  int candidates_examined = 0, allocations_examined = 0, gdp_floors_examined = 0;
  WinWinRecommendation recommendation;
  std::vector<Scenario> scenarios;
};

struct EvaluationOptions {
  bool exhaustive_policy_search = false;
};

inline EvaluationOptions production_evaluation_options() {
  return EvaluationOptions{true};
}

class PolicyEngine {
 public:
  explicit PolicyEngine(std::uint64_t seed = 20260810,
                        StructuralParameters parameters = {},
                        StructuralParameterRegistry parameter_registry = {})
      : seed_{seed}, parameters_{std::move(parameters)} {
    if (parameter_registry.loaded)
      parameters_.uncertainty_registry = std::move(parameter_registry);
  }
  Result evaluate(const Economy& economy) const;
  Result evaluate(const Economy& economy, EvaluationOptions options) const;
  // Full V2 decision robustness samples structural calibrations, re-runs the
  // complete generated policy-control search, then re-optimizes the 20-sector
  // negotiation package before stochastic verification inside every draw.
  Result evaluate_robust(const Economy& economy, int parameter_draws = 24) const;
  Result evaluate_robust(const Economy& economy, int parameter_draws,
                         EvaluationOptions options) const;
  const StructuralParameters& parameters() const { return parameters_; }
  const StructuralParameterRegistry& parameter_registry() const {
    return parameters_.uncertainty_registry;
  }

 private:
  struct CommonSeed {
    std::uint64_t value;
    operator std::uint64_t() const { return value; }
    friend CommonSeed operator+(CommonSeed seed, int) { return seed; }
    friend CommonSeed operator+(CommonSeed seed, std::size_t) { return seed; }
  };

  CommonSeed seed_;
  StructuralParameters parameters_;
};

std::string to_json(const Result& result);
std::string robustness_to_json(const Result& result);

}  // namespace cad
