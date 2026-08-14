#include "policy_engine.hpp"
#include "compute_executor.hpp"
#include "monte_carlo_backend.hpp"
#include "policy_dynamics.hpp"
#include "trade_network.hpp"
#include "bilateral_trade.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace cad {
namespace {

double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
double sq(double x) { return x * x; }

constexpr int kBaseDraws = 700;
constexpr int kVerificationDraws = 2800;
constexpr double kSectorGridStep = 25.0;
constexpr std::size_t kSectorStagedFinalists = 8;
constexpr std::size_t kSectorExhaustiveVerificationCap = 128;

const auto& sector_profiles = trade_sector_profiles();

struct SectorUtility {
  double canada = 0.0;
  double us = 0.0;
  SectorImpact impact;
};

std::vector<double> coverage_levels(double current, double cooperation_ceiling,
                                    double negotiated_relief) {
  (void)cooperation_ceiling;
  (void)negotiated_relief;
  // Delegation sector coverage is an authoritative scenario input. The policy
  // engine may evaluate policy and bargaining packages around that scenario,
  // but it must never generate a substitute coverage level for either party.
  return {clamp(current, 0.0, 100.0)};
}

TradeNetworkInput make_trade_network_input(const Economy& e, const Scenario& policy) {
  TradeNetworkInput input;
  input.us_headline_tariff = e.us_tariff_canada;
  input.canada_headline_tariff = e.canada_retaliatory_tariff;
  input.negotiated_relief = policy.negotiated_relief;
  input.diversification = clamp(policy.diversification + e.trade_diversification, 0.0, .75);
  input.trade_elasticity = e.trade_elasticity;
  input.price_pass_through = e.tariff_price_pass_through;
  input.tuning = e.trade_network_tuning;
  input.us_coverage = e.us_sector_coverage;
  input.canada_coverage = e.canada_sector_coverage;
  input.us_trade_elasticity = e.us_sector_trade_elasticity;
  input.canada_trade_elasticity = e.canada_sector_trade_elasticity;
  input.us_price_pass_through = e.us_sector_price_pass_through;
  input.canada_price_pass_through = e.canada_sector_price_pass_through;
  return input;
}

SectorImpact direct_sector_impact(const Economy& e, const Scenario& policy,
                                  std::size_t sector, double us_coverage,
                                  const TariffIncidence& us_incidence,
                                  const TariffIncidence& canada_incidence) {
  const auto& p = sector_profiles[sector];
  const double deescalation = clamp(policy.negotiated_relief / 100.0, 0.0, 1.0);
  const double diversification = clamp(policy.diversification + e.trade_diversification, 0.0, 0.75);
  const double uc = clamp(us_coverage / 100.0, 0.0, 1.0);
  const double canada_quantity_loss = clamp(us_incidence.quantity_loss, 0.0, 1.0);
  const double us_quantity_loss = clamp(canada_incidence.quantity_loss, 0.0, 1.0);
  const double supply = policy.productive_share * policy.fiscal_impulse * (.16 + .12 * p.cyclical);
  const double ca_shock = canada_quantity_loss * p.trade * (.72 - .28 * diversification)
      + e.border_friction / 100.0 * p.trade * .18;
  const double us_shock = us_quantity_loss * p.import * .46
      + canada_quantity_loss * p.import * .12;
  const double us_protection = canada_quantity_loss * p.trade * .24 * (1.0 - .5 * uc);

  SectorImpact impact;
  impact.code = p.code;
  impact.name = p.name;
  impact.exposure = 100.0 * p.trade;
  impact.canada_output = 100.0 * (-ca_shock + supply + policy.targeted_relief * .10 * p.jobs);
  impact.us_output = 100.0 * (-us_shock + us_protection + deescalation * .012 * p.trade);
  impact.canada_jobs = impact.canada_output * (.30 + .42 * p.jobs);
  impact.us_jobs = impact.us_output * (.28 + .38 * p.jobs);
  impact.canada_prices = canada_incidence.buyer_pass_through * p.import
      + us_incidence.buyer_pass_through * p.import * .05 - 100.0 * supply * .10;
  impact.us_prices = us_incidence.buyer_pass_through * p.import
      + canada_incidence.buyer_pass_through * p.import * .10;
  impact.us_applied_tariff = us_incidence.applied_tariff;
  impact.canada_applied_tariff = canada_incidence.applied_tariff;
  impact.us_buyer_pass_through = us_incidence.buyer_pass_through;
  impact.canada_buyer_pass_through = canada_incidence.buyer_pass_through;
  impact.canada_exporter_absorption = us_incidence.exporter_absorption;
  impact.us_exporter_absorption = canada_incidence.exporter_absorption;
  impact.us_importer_absorption = us_incidence.importer_absorption;
  impact.canada_importer_absorption = canada_incidence.importer_absorption;
  return impact;
}

SectorUtility sector_utility(const Economy& e, const Scenario& policy, std::size_t sector,
                             double us_coverage, double canada_coverage) {
  const auto input = make_trade_network_input(e, policy);
  const auto network = evaluate_trade_source(input, sector, us_coverage, canada_coverage);
  const auto direct = direct_sector_impact(e, policy, sector, us_coverage,
      network.us_tariff, network.canada_tariff);

  SectorUtility out;
  out.impact = direct;
  out.impact.canada_output += network.canada_output[sector];
  out.impact.canada_jobs += network.canada_jobs[sector];
  out.impact.canada_prices += network.canada_prices[sector];
  out.impact.us_output += network.us_output[sector];
  out.impact.us_jobs += network.us_jobs[sector];
  out.impact.us_prices += network.us_prices[sector];
  out.impact.canada_upstream_cost = network.canada_upstream_cost[sector];
  out.impact.us_upstream_cost = network.us_upstream_cost[sector];

  const double price_weight = .65 + .70 * clamp(e.risk_aversion / 100.0, 0.0, 1.0)
      + .18 * std::max(0.0, (e.inflation + e.us_inflation) / 2.0 - 2.0);
  for (std::size_t downstream = 0; downstream < std::size(sector_profiles); ++downstream) {
    const auto& p = sector_profiles[downstream];
    double canada_output = network.canada_output[downstream];
    double canada_jobs = network.canada_jobs[downstream];
    double canada_prices = network.canada_prices[downstream];
    double us_output = network.us_output[downstream];
    double us_jobs = network.us_jobs[downstream];
    double us_prices = network.us_prices[downstream];
    if (downstream == sector) {
      canada_output += direct.canada_output;
      canada_jobs += direct.canada_jobs;
      canada_prices += direct.canada_prices;
      us_output += direct.us_output;
      us_jobs += direct.us_jobs;
      us_prices += direct.us_prices;
    }
    const double ca_jobs_weight = .45 + .08 * std::max(0.0, e.unemployment - 5.0);
    const double us_jobs_weight = .45 + .08 * std::max(0.0, 4.5 - e.us_growth);
    out.canada += p.trade * (canada_output + ca_jobs_weight * canada_jobs
        - price_weight * canada_prices);
    out.us += p.import * (us_output + us_jobs_weight * us_jobs
        - price_weight * us_prices);
  }

  const auto& source_profile = sector_profiles[sector];
  const double cc = clamp(canada_coverage / 100.0, 0.0, 1.0);
  const double leverage = 100.0 * network.canada_tariff.quantity_loss
      * source_profile.trade * .16 * (1.0 - .65 * cc);
  out.canada += source_profile.trade * leverage;
  return out;
}

void add_sector_impacts(Scenario& s, const Economy& e) {
  const auto network = evaluate_trade_network(make_trade_network_input(e, s));
  s.sectors.clear();
  s.sectors.reserve(std::size(sector_profiles));
  for (std::size_t sector = 0; sector < std::size(sector_profiles); ++sector) {
    const auto& n = network.sectors[sector];
    auto impact = direct_sector_impact(e, s, sector, e.us_sector_coverage[sector],
        n.us_tariff, n.canada_tariff);
    impact.canada_output += n.canada_indirect_output;
    impact.canada_jobs += n.canada_indirect_jobs;
    impact.canada_prices += n.canada_indirect_prices;
    impact.us_output += n.us_indirect_output;
    impact.us_jobs += n.us_indirect_jobs;
    impact.us_prices += n.us_indirect_prices;
    impact.canada_upstream_cost = n.canada_upstream_cost;
    impact.us_upstream_cost = n.us_upstream_cost;
    s.sectors.push_back(std::move(impact));
  }
}

struct CoverageCandidate {
  double canada_raw = 0.0;
  double us_raw = 0.0;
  double canada_score = 0.0;
  double us_score = 0.0;
  double objective = -1e100;
  std::array<double, 20> us_coverage{};
  std::array<double, 20> canada_coverage{};
};

struct SectorSearch {
  std::vector<CoverageCandidate> finalists;
  int candidates_examined = 0;
  int pareto_frontier_size = 0;
  bool verification_exhaustive = true;
  double canada_min = 0.0, canada_max = 0.0;
  double us_min = 0.0, us_max = 0.0;
  double baseline_canada_score = 0.0, baseline_us_score = 0.0;
};

double normalize_utility(double value, double lo, double hi) {
  return 100.0 * (value - lo) / std::max(1e-9, hi - lo);
}

SectorSearch search_sector_frontier(const Economy& e, const Scenario& policy) {
  SectorSearch search;
  std::vector<CoverageCandidate> frontier(1);
  double ca_global_min = 0.0, ca_global_max = 0.0;
  double us_global_min = 0.0, us_global_max = 0.0;
  double baseline_ca_raw = 0.0, baseline_us_raw = 0.0;

  for (std::size_t sector = 0; sector < std::size(sector_profiles); ++sector) {
    const auto us_levels = coverage_levels(e.us_sector_coverage[sector], e.cooperation_ceiling, policy.negotiated_relief);
    const auto ca_levels = coverage_levels(e.canada_sector_coverage[sector], e.cooperation_ceiling, policy.negotiated_relief);
    struct Option { double uc, cc, ca, us; };
    std::vector<Option> options;
    options.reserve(us_levels.size() * ca_levels.size());
    double sector_ca_min = std::numeric_limits<double>::infinity();
    double sector_ca_max = -std::numeric_limits<double>::infinity();
    double sector_us_min = std::numeric_limits<double>::infinity();
    double sector_us_max = -std::numeric_limits<double>::infinity();

    for (double uc : us_levels) for (double cc : ca_levels) {
      const auto value = sector_utility(e, policy, sector, uc, cc);
      options.push_back({uc, cc, value.canada, value.us});
      sector_ca_min = std::min(sector_ca_min, value.canada);
      sector_ca_max = std::max(sector_ca_max, value.canada);
      sector_us_min = std::min(sector_us_min, value.us);
      sector_us_max = std::max(sector_us_max, value.us);
    }
    ca_global_min += sector_ca_min;
    ca_global_max += sector_ca_max;
    us_global_min += sector_us_min;
    us_global_max += sector_us_max;
    const auto baseline = sector_utility(
        e, policy, sector, e.us_sector_coverage[sector], e.canada_sector_coverage[sector]);
    baseline_ca_raw += baseline.canada;
    baseline_us_raw += baseline.us;

    std::vector<CoverageCandidate> expanded;
    expanded.reserve(frontier.size() * options.size());
    for (const auto& partial : frontier) {
      for (const auto& option : options) {
        CoverageCandidate candidate = partial;
        candidate.canada_raw += option.ca;
        candidate.us_raw += option.us;
        candidate.us_coverage[sector] = option.uc;
        candidate.canada_coverage[sector] = option.cc;
        expanded.push_back(std::move(candidate));
      }
    }
    search.candidates_examined += static_cast<int>(expanded.size());
    std::sort(expanded.begin(), expanded.end(), [](const CoverageCandidate& a, const CoverageCandidate& b) {
      if (std::abs(a.canada_raw - b.canada_raw) > 1e-9) return a.canada_raw > b.canada_raw;
      return a.us_raw > b.us_raw;
    });
    frontier.clear();
    double best_us = -std::numeric_limits<double>::infinity();
    for (auto& candidate : expanded) {
      if (candidate.us_raw > best_us + 1e-9) {
        best_us = candidate.us_raw;
        frontier.push_back(std::move(candidate));
      }
    }
  }

  search.canada_min = ca_global_min;
  search.canada_max = ca_global_max;
  search.us_min = us_global_min;
  search.us_max = us_global_max;
  search.baseline_canada_score = normalize_utility(baseline_ca_raw, ca_global_min, ca_global_max);
  search.baseline_us_score = normalize_utility(baseline_us_raw, us_global_min, us_global_max);
  search.pareto_frontier_size = static_cast<int>(frontier.size());
  const double ca_weight = clamp(e.canada_priority, 1.0, 100.0);
  const double us_weight = clamp(e.us_priority, 1.0, 100.0);
  const double weight_total = ca_weight + us_weight;

  std::vector<CoverageCandidate> win_win;
  for (auto& candidate : frontier) {
    candidate.canada_score = normalize_utility(candidate.canada_raw, ca_global_min, ca_global_max);
    candidate.us_score = normalize_utility(candidate.us_raw, us_global_min, us_global_max);
    if (candidate.canada_score + 1e-9 < search.baseline_canada_score
        || candidate.us_score + 1e-9 < search.baseline_us_score) continue;
    const double nash = std::exp((ca_weight * std::log(std::max(.01, candidate.canada_score))
        + us_weight * std::log(std::max(.01, candidate.us_score))) / weight_total);
    const double fairness = std::min(candidate.canada_score, candidate.us_score);
    double coverage_sum = 0.0;
    for (std::size_t i = 0; i < candidate.us_coverage.size(); ++i)
      coverage_sum += candidate.us_coverage[i] + candidate.canada_coverage[i];
    candidate.objective = .72 * nash + .28 * fairness - .00002 * coverage_sum;
    win_win.push_back(candidate);
  }

  if (win_win.empty()) {
    CoverageCandidate baseline;
    baseline.canada_raw = baseline_ca_raw;
    baseline.us_raw = baseline_us_raw;
    baseline.canada_score = search.baseline_canada_score;
    baseline.us_score = search.baseline_us_score;
    baseline.us_coverage = e.us_sector_coverage;
    baseline.canada_coverage = e.canada_sector_coverage;
    baseline.objective = std::min(baseline.canada_score, baseline.us_score);
    win_win.push_back(baseline);
  }

  std::sort(win_win.begin(), win_win.end(), [](const CoverageCandidate& a, const CoverageCandidate& b) {
    return a.objective > b.objective;
  });
  const std::size_t cap = e.exhaustive_policy_search
      ? kSectorExhaustiveVerificationCap : kSectorStagedFinalists;
  search.verification_exhaustive = win_win.size() <= cap;
  const std::size_t keep = std::min<std::size_t>(cap, win_win.size());
  search.finalists.assign(win_win.begin(), win_win.begin() + keep);
  return search;
}

Scenario simulate(const Economy& e, const StructuralParameters& p, std::string id, std::string name, std::string description,
                  double move, double fiscal, double productive, double deescalation,
                  double targeted_relief, double diversification, std::uint64_t seed,
                  int draws = kBaseDraws) {
  deescalation = std::min(deescalation,
      clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0));
  Scenario s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.description = std::move(description);
  s.first_move_bp = move;
  s.fiscal_impulse = fiscal;
  s.productive_share = productive;
  s.negotiated_relief = 100.0 * deescalation;
  s.targeted_relief = targeted_relief;
  s.diversification = diversification;
  s.applied_us_sector_coverage = e.us_sector_coverage;
  s.applied_canada_sector_coverage = e.canada_sector_coverage;

  double inf_sum = 0.0, growth_sum = 0.0, us_growth_sum = 0.0, u_sum = 0.0;
  double debt_sum = 0.0, house_sum = 0.0, cost_sum = 0.0, income_sum = 0.0;
  double export_sum = 0.0, us_export_sum = 0.0, recessions = 0.0;
  std::array<double,12> rp{}, ip{}, gp{}, ugp{}, dp{}, cp{}, xp{}, uxp{};
  std::vector<double> terminal_debt, terminal_inflation;
  terminal_debt.reserve(draws);
  terminal_inflation.reserve(draws);

  double us_barrier_coverage = 0.0, ca_barrier_coverage = 0.0;
  double ca_export_weight = 0.0, us_export_weight = 0.0;
  for (std::size_t i = 0; i < std::size(sector_profiles); ++i) {
    ca_export_weight += sector_profiles[i].trade;
    us_export_weight += sector_profiles[i].import;
    us_barrier_coverage += sector_profiles[i].trade
        * clamp(e.us_sector_coverage[i] / 100.0, 0.0, 1.0);
    ca_barrier_coverage += sector_profiles[i].import
        * clamp(e.canada_sector_coverage[i] / 100.0, 0.0, 1.0);
  }
  us_barrier_coverage /= std::max(1e-9, ca_export_weight);
  ca_barrier_coverage /= std::max(1e-9, us_export_weight);

  const auto implementation = build_policy_implementation_paths(
      fiscal, productive, 100.0 * deescalation, targeted_relief, diversification);
  s.fiscal_path = implementation.fiscal;
  s.productive_investment_path = implementation.productive_investment;
  s.negotiated_relief_path = implementation.negotiated_relief;
  s.targeted_relief_path = implementation.targeted_relief;
  s.diversification_path = implementation.diversification;

  std::array<TradeNetworkResult, 12> networks{};
  std::array<BilateralTradeState, 12> trade_states{};
  std::array<double, 12> deescalation_path{}, us_tariff_path{}, ca_tariff_path{};
  std::array<double, 12> trade_drag_path{}, us_trade_drag_path{}, import_price_path{};
  std::array<double, 12> supply_path{}, relief_cost_path{};
  for (int q = 0; q < 12; ++q) {
    const auto qi = static_cast<std::size_t>(q);
    deescalation_path[qi] = clamp(implementation.negotiated_relief[qi] / 100.0, 0.0,
        clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0));

    Scenario quarter_policy = s;
    quarter_policy.negotiated_relief = implementation.negotiated_relief[qi];
    quarter_policy.diversification = implementation.diversification[qi];
    networks[qi] = evaluate_trade_network(make_trade_network_input(e, quarter_policy));
    trade_states[qi] = build_bilateral_trade_state(e, quarter_policy, p, networks[qi]);
    us_tariff_path[qi] = trade_states[qi].effective_us_tariff;
    ca_tariff_path[qi] = trade_states[qi].effective_canada_tariff;

    trade_drag_path[qi] = trade_states[qi].canada_macro_trade_drag
        + networks[qi].canada_supply_chain_drag;
    us_trade_drag_path[qi] = trade_states[qi].us_macro_trade_drag
        + networks[qi].us_supply_chain_drag;
    import_price_path[qi] = e.imports_from_us_share / 100.0
        * e.import_content_consumption / 100.0 * ca_tariff_path[qi]
        + networks[qi].canada_input_cost_pressure;
    supply_path[qi] = implementation.productive_investment[qi]
        * p.productive_supply_multiplier + e.productivity_growth * .035;
    relief_cost_path[qi] = implementation.targeted_relief[qi] + e.tariff_relief;
  }
  const bool stress_regime = macro_stress_regime(e);
  const double fx = (e.usdcad - 1.34) * p.fx_pass_through;

  monte_carlo::Input monte;
  monte.draws = draws;
  monte.move_bp = move;
  monte.productive_share = productive;
  monte.policy_rate = e.policy_rate;
  monte.core_inflation = e.core_inflation;
  monte.output_gap = e.output_gap;
  monte.unemployment = e.unemployment;
  monte.federal_debt_gdp = e.federal_debt_gdp;
  monte.housing_gap = e.housing_gap;
  monte.us_growth = e.us_growth;
  monte.gdp_growth = e.gdp_growth;
  monte.population_growth = e.population_growth;
  monte.credit_spread = e.credit_spread;
  monte.fiscal_balance_gdp = e.fiscal_balance_gdp;
  monte.wage_growth = e.wage_growth;
  monte.headline_inflation = e.inflation;
  monte.global_growth = e.global_growth;
  monte.inflation_expectations = e.inflation_expectations;
  monte.oil_price = e.oil_price;
  monte.border_friction = e.border_friction;
  monte.fx_pressure = fx;
  monte.parameters.neutral_rate = p.neutral_rate;
  monte.parameters.inflation_target = p.inflation_target;
  monte.parameters.rate_inflation_response = p.rate_inflation_response;
  monte.parameters.rate_output_response = p.rate_output_response;
  monte.parameters.max_quarterly_rate_step = p.max_quarterly_rate_step;
  monte.parameters.output_persistence = p.output_persistence;
  monte.parameters.fiscal_demand_multiplier = p.fiscal_demand_multiplier;
  monte.parameters.real_rate_demand_sensitivity = p.real_rate_demand_sensitivity;
  monte.parameters.global_growth_sensitivity = p.global_growth_sensitivity;
  monte.parameters.inflation_persistence = p.inflation_persistence;
  monte.parameters.inflation_expectations_weight = p.inflation_expectations_weight;
  monte.parameters.phillips_curve_slope = p.phillips_curve_slope;
  monte.parameters.import_price_pass_through = p.import_price_pass_through;
  monte.parameters.oil_inflation_sensitivity = p.oil_inflation_sensitivity;
  monte.parameters.output_shock_sd = p.output_shock_sd;
  monte.parameters.inflation_shock_sd = p.inflation_shock_sd;
  monte.parameters.growth_shock_sd = p.growth_shock_sd;
  monte.parameters.us_growth_shock_sd = p.us_growth_shock_sd;
  monte.parameters.export_shock_sd = p.export_shock_sd;
  monte.parameters.us_export_shock_sd = p.us_export_shock_sd;
  monte.fiscal = implementation.fiscal;
  monte.productive_investment = implementation.productive_investment;
  monte.targeted_relief = implementation.targeted_relief;
  monte.diversification = implementation.diversification;
  monte.deescalation = deescalation_path;
  monte.us_tariff = us_tariff_path;
  monte.canada_tariff = ca_tariff_path;
  monte.trade_drag = trade_drag_path;
  monte.import_price = import_price_path;
  monte.supply = supply_path;
  monte.relief_cost = relief_cost_path;
  for (std::size_t q = 0; q < monte_carlo::kQuarterCount; ++q) {
    monte.us_supply_chain_drag[q] = networks[q].us_supply_chain_drag;
    monte.canada_export_quantity_ratio[q] = trade_states[q].canada_total_export_quantity_ratio;
    monte.us_export_quantity_ratio[q] = trade_states[q].us_bilateral_quantity_ratio;
  }

  const auto innovations = monte_carlo::generate_innovations(
      seed, draws, p.output_inflation_shock_correlation,
      p.shock_tail_threshold, p.shock_tail_scale,
      p.stress_regime_shock_scale, stress_regime);
  const auto monte_result = monte_carlo::run(monte, innovations);

  for (const auto& draw : monte_result.draws) {
    for (std::size_t q = 0; q < monte_carlo::kQuarterCount; ++q) {
      rp[q] += draw.rates[q];
      ip[q] += draw.inflation[q];
      gp[q] += draw.growth[q];
      ugp[q] += draw.us_growth[q];
      dp[q] += draw.debt[q];
      cp[q] += draw.cost[q];
      xp[q] += draw.exports[q];
      uxp[q] += draw.us_exports[q];
    }
    inf_sum += draw.terminal_inflation;
    growth_sum += draw.terminal_growth;
    us_growth_sum += draw.terminal_us_growth;
    u_sum += draw.terminal_unemployment;
    debt_sum += draw.terminal_debt;
    house_sum += draw.terminal_housing;
    cost_sum += draw.terminal_cost;
    income_sum += draw.terminal_income;
    export_sum += draw.terminal_exports;
    us_export_sum += draw.terminal_us_exports;
    terminal_debt.push_back(draw.terminal_debt);
    terminal_inflation.push_back(draw.terminal_inflation);
    if (draw.recession) recessions += 1.0;
  }

  for (int q = 0; q < 12; ++q) {
    s.rates[q] = rp[q] / draws;
    s.inflation_path[q] = ip[q] / draws;
    s.growth_path[q] = gp[q] / draws;
    s.us_growth_path[q] = ugp[q] / draws;
    s.debt_path[q] = dp[q] / draws;
    s.cost_path[q] = cp[q] / draws;
    s.export_path[q] = xp[q] / draws;
    s.us_export_path[q] = uxp[q] / draws;
  }

  s.inflation = inf_sum / draws;
  s.growth = growth_sum / draws;
  s.us_growth = us_growth_sum / draws;
  s.unemployment = u_sum / draws;
  s.bilateral_growth_floor = std::min(
      *std::min_element(s.growth_path.begin(), s.growth_path.end()),
      *std::min_element(s.us_growth_path.begin(), s.us_growth_path.end()));
  s.sustained_bilateral_growth = s.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  s.debt_gdp = debt_sum / draws;
  s.housing_gap = house_sum / draws;
  s.recession_risk = 100.0 * recessions / draws;
  s.cost_of_living = cost_sum / draws;
  s.real_income_growth = income_sum / draws;
  s.export_change = export_sum / draws;
  s.us_export_change = us_export_sum / draws;

  const auto& terminal_trade = trade_states.back();
  s.us_export_expansion_usd = 0.0;
  s.canada_export_redirection_cad = terminal_trade.canada_export_redirection_cad;
  s.us_tariff_revenue_cad = terminal_trade.us_tariff_revenue_cad;
  s.us_tariff_revenue_usd = s.us_tariff_revenue_cad / e.usdcad;
  s.canada_tariff_revenue_cad = terminal_trade.canada_tariff_revenue_cad;
  s.canada_tariff_revenue_usd = s.canada_tariff_revenue_cad / e.usdcad;
  s.canada_trade_balance_cad = terminal_trade.canada_trade_balance_cad;
  s.us_trade_balance_usd = -s.canada_trade_balance_cad / e.usdcad;
  s.trade_balance_gap_usd = std::abs(s.us_trade_balance_usd);
  const double initial_gap = std::abs(e.canada_exports_to_us_cad - e.canada_imports_from_us_cad)
      / e.usdcad;
  s.trade_balance_progress = 100.0
      * (1.0 - s.trade_balance_gap_usd / std::max(.001, initial_gap));
  s.zero_trade_deficit = s.trade_balance_gap_usd < .05;

  const auto p90_index = static_cast<std::size_t>(draws * 9 / 10);
  auto debt_p90 = terminal_debt.begin() + static_cast<std::ptrdiff_t>(p90_index);
  auto inflation_p90 = terminal_inflation.begin() + static_cast<std::ptrdiff_t>(p90_index);
  std::nth_element(terminal_debt.begin(), debt_p90, terminal_debt.end());
  std::nth_element(terminal_inflation.begin(), inflation_p90, terminal_inflation.end());
  s.debt_stress_p90 = *debt_p90;
  s.inflation_stress_p90 = *inflation_p90;

  const auto& w = e.loss_weights;
  const double mandate_loss = w.boc_inflation * sq(s.inflation - p.inflation_target)
      + w.boc_unemployment * sq(std::max(0.0, s.unemployment - 5.8))
      + w.boc_contraction * sq(std::min(0.0, s.growth))
      + w.boc_recession * s.recession_risk;
  const double federal_loss = w.federal_debt * sq(std::max(0.0, s.debt_gdp - e.federal_debt_gdp))
      + w.federal_contraction * sq(std::min(0.0, s.growth))
      + w.federal_unemployment * sq(std::max(0.0, s.unemployment - 6.0))
      + w.federal_housing * sq(s.housing_gap);
  s.boc_score = 100.0 / (1.0 + mandate_loss);
  s.federal_score = 100.0 / (1.0 + federal_loss);
  s.canada_score = std::sqrt(std::max(.01, s.boc_score)
      * std::max(.01, s.federal_score));

  const double us_inflation_pressure = std::max(
      0.0, e.us_inflation - 2.0 + terminal_trade.effective_us_tariff * .025
      + .10 * networks.back().us_input_cost_pressure);
  const double us_loss = w.us_exports * sq(std::max(0.0, -s.us_export_change))
      + w.us_inflation * sq(us_inflation_pressure)
      + w.us_growth * sq(std::max(0.0, 1.8 - s.us_growth))
      + w.us_retaliation * sq(terminal_trade.effective_canada_tariff);
  s.us_score = 100.0 / (1.0 + us_loss);

  const double canada = s.canada_score;
  const double floor = std::min(canada, s.us_score);
  const double ca_weight = clamp(e.canada_priority, 1.0, 100.0);
  const double us_weight = clamp(e.us_priority, 1.0, 100.0);
  const double total = ca_weight + us_weight;
  const double nash = std::exp((ca_weight * std::log(std::max(.01, canada))
      + us_weight * std::log(std::max(.01, s.us_score))) / total);
  const double safety = clamp(e.risk_aversion / 100.0, 0.0, 1.0);
  const double tail_penalty = safety * (.10 * s.recession_risk
      + .35 * std::max(0.0, s.inflation_stress_p90 - 3.0)
      + .08 * std::max(0.0, s.debt_stress_p90 - e.federal_debt_gdp - 5.0));
  s.score = (.78 - .18 * safety) * nash + (.22 + .18 * safety) * floor - tail_penalty;

  add_sector_impacts(s, e);
  return s;
}

double deal_score(const Scenario& s, const Economy& e, bool enforce_growth = true) {
  if (enforce_growth && s.bilateral_growth_floor + 1e-9 < e.minimum_bilateral_growth)
    return -1e100;
  const double canada = s.canada_score;
  const double ca_weight = clamp(e.canada_priority, 1.0, 100.0);
  const double us_weight = clamp(e.us_priority, 1.0, 100.0);
  const double total = ca_weight + us_weight;
  const double nash = std::exp((ca_weight * std::log(std::max(.01, canada))
      + us_weight * std::log(std::max(.01, s.us_score))) / total);
  const double floor = std::min(canada, s.us_score);
  const double safety = clamp(e.risk_aversion / 100.0, 0.0, 1.0);
  const double tail_penalty = safety * (.10 * s.recession_risk
      + .35 * std::max(0.0, s.inflation_stress_p90 - 3.0)
      + .08 * std::max(0.0, s.debt_stress_p90 - e.federal_debt_gdp - 5.0));
  double sector_floor = std::numeric_limits<double>::infinity();
  double sector_sum = 0.0;
  std::size_t count = 0;
  for (const auto& x : s.sectors) {
    const std::array<double,6> metrics{
      x.canada_output, x.canada_jobs, -x.canada_prices,
      x.us_output, x.us_jobs, -x.us_prices
    };
    for (double metric : metrics) {
      sector_floor = std::min(sector_floor, metric);
      sector_sum += metric;
      ++count;
    }
  }
  const double sector_average = count ? sector_sum / static_cast<double>(count) : 0.0;
  return .66 * nash + .26 * floor - .05 * tail_penalty
      + .055 * sector_floor + .018 * sector_average;
}

double min_sector_metric(const Scenario& s) {
  double out = std::numeric_limits<double>::infinity();
  for (const auto& x : s.sectors) {
    out = std::min(out, x.canada_output);
    out = std::min(out, x.canada_jobs);
    out = std::min(out, -x.canada_prices);
    out = std::min(out, x.us_output);
    out = std::min(out, x.us_jobs);
    out = std::min(out, -x.us_prices);
  }
  return std::isfinite(out) ? out : 0.0;
}

struct ScenarioSpec {
  std::string id;
  std::string name;
  std::string description;
  double move = 0.0;
  double fiscal = 0.0;
  double productive = 0.0;
  double deescalation = 0.0;
  double relief = 0.0;
  double diversification = 0.0;
};

Scenario simulate_spec(const Economy& e, const StructuralParameters& parameters,
                       const ScenarioSpec& spec, std::uint64_t seed, int draws) {
  return simulate(e, parameters, spec.id, spec.name, spec.description,
      spec.move, spec.fiscal, spec.productive, spec.deescalation,
      spec.relief, spec.diversification, seed, draws);
}

struct ScenarioMeta {
  std::string id;
  int candidates_examined = 0;
  int pareto_size = 0;
  int finalists = 0;
  bool exhaustive = true;
  double sector_ca_score = 0.0;
  double sector_us_score = 0.0;
  double baseline_ca_score = 0.0;
  double baseline_us_score = 0.0;
};

const ScenarioMeta* find_meta(const std::vector<ScenarioMeta>& meta, const std::string& id) {
  for (const auto& item : meta) if (item.id == id) return &item;
  return nullptr;
}

void fill_sector_display_metrics(const Economy& e, const Scenario& policy,
                                 WinWinRecommendation& recommendation) {
  for (std::size_t i = 0; i < std::size(sector_profiles); ++i) {
    const auto us_levels = coverage_levels(e.us_sector_coverage[i], e.cooperation_ceiling, policy.negotiated_relief);
    const auto ca_levels = coverage_levels(e.canada_sector_coverage[i], e.cooperation_ceiling, policy.negotiated_relief);
    double ca_min = std::numeric_limits<double>::infinity();
    double ca_max = -std::numeric_limits<double>::infinity();
    double us_min = std::numeric_limits<double>::infinity();
    double us_max = -std::numeric_limits<double>::infinity();
    for (double uc : us_levels) for (double cc : ca_levels) {
      const auto value = sector_utility(e, policy, i, uc, cc);
      ca_min = std::min(ca_min, value.canada);
      ca_max = std::max(ca_max, value.canada);
      us_min = std::min(us_min, value.us);
      us_max = std::max(us_max, value.us);
    }
    const auto selected = sector_utility(e, policy, i,
        recommendation.us_sector_coverage[i], recommendation.canada_sector_coverage[i]);
    recommendation.canada_sector_value[i] = normalize_utility(selected.canada, ca_min, ca_max);
    recommendation.us_sector_output[i] = normalize_utility(selected.us, us_min, us_max);
  }
}

std::string esc(const std::string& x) {
  std::string out;
  for (char c : x) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

template<std::size_t N>
void array_json(std::ostringstream& out, const std::array<double,N>& values) {
  out << '[';
  for (std::size_t i = 0; i < N; ++i) {
    if (i) out << ',';
    out << values[i];
  }
  out << ']';
}

}  // namespace

Result PolicyEngine::evaluate(const Economy& economy) const {
  return evaluate(economy, EvaluationOptions{economy.exhaustive_policy_search});
}

Result PolicyEngine::evaluate(const Economy& economy, EvaluationOptions options) const {
  Economy e = economy;
  e.exhaustive_policy_search = options.exhaustive_policy_search;
  e.trade_network_tuning.supplier_demand_transmission = parameters_.network_supplier_demand_transmission;
  e.trade_network_tuning.input_cost_incidence = parameters_.network_input_cost_incidence;
  e.trade_network_tuning.downstream_cost_transmission = parameters_.network_downstream_cost_transmission;
  e.trade_network_tuning.price_cost_pass_through = parameters_.network_price_cost_pass_through;
  e.trade_network_tuning.output_cost_base = parameters_.network_output_cost_base;
  e.trade_network_tuning.output_cost_cyclical = parameters_.network_output_cost_cyclical;
  e.trade_network_tuning.jobs_output_base = parameters_.network_jobs_output_base;
  e.trade_network_tuning.jobs_output_exposure = parameters_.network_jobs_output_exposure;
  Result r;
  if (e.core_inflation > 3.2) r.regime = "Inflation pressure";
  else if (e.credit_spread > 2.25) r.regime = "Financial stress";
  else if (e.gdp_growth < 0) r.regime = "Contraction";
  else if (e.output_gap < -.5) r.regime = "Below potential";
  else r.regime = "Balanced expansion";

  r.neutral_rate = clamp(parameters_.neutral_rate + .16 * (e.productivity_growth - 1.0)
      + .10 * (e.global_growth - 2.7), 1.75, 3.5);
  r.policy_gap = e.policy_rate - r.neutral_rate;
  r.data_confidence = clamp(92.0 - 4.0 * std::abs(e.inflation - e.core_inflation)
      - 2.0 * std::abs(e.output_gap), 70.0, 97.0);

  const std::vector<ScenarioSpec> expert_specs{
    {"statusquo","Tariff status quo","Current tariffs persist; BoC and fiscal settings hold.",0,0,.5,0,0,0},
    {"retaliate","Symmetric retaliation","Canada matches trade barriers and supports affected demand.",0,.35,.25,0,.25,0},
    {"relief","Worker transition bridge","A measured cut and temporary, targeted tariff adjustment support.",-25,.30,.65,0,.35,.15},
    {"compact","North American compact","Mutual tariff removal, border facilitation and productive Canadian investment.",0,.25,.9,.85,.10,.20},
    {"diversify","Market diversification","Trade infrastructure and export-market diversification with a BoC hold.",0,.35,.9,0,.10,.45},
    {"guardrail","Inflation guardrail","A 25 bp increase and limited retaliation constrain tariff pass-through.",25,-.10,.75,.20,0,.10},
    {"supply","Cost-of-living supply plan","Housing, logistics and productivity investment with targeted household relief.",0,.40,.95,.35,.20,.25},
    {"stabilizer","Automatic stabilizers","Income insurance absorbs the trade shock while monetary policy remains data dependent.",0,.22,.35,0,.30,.08},
    {"eastwest","East-west trade corridor","Ports, rail and interprovincial trade reform accelerate non-U.S. market access.",0,.48,.96,0,.08,.60},
    {"productivity","Productivity compact","Accelerated investment expensing, skills and competition policy lift supply capacity.",0,.32,1.0,.10,.05,.30},
    {"defence","Fiscal consolidation buffer","Spending restraint preserves debt capacity while the Bank cushions demand.",-25,-.22,.70,0,0,.12},
    {"sectoral","Sector-targeted response","Time-limited support protects tariff-exposed workers without broad retaliation.",0,.28,.62,.05,.48,.22},
    {"balance","Balanced market-access compact","Procurement, standards and export-finance measures expand two-way market access without forcing a bilateral accounting target.",0,.45,.95,.70,.08,.55}
  };
  r.scenarios.resize(expert_specs.size());
  compute::parallel_for(expert_specs.size(), [&](std::size_t index) {
    r.scenarios[index] = simulate_spec(
        e, parameters_, expert_specs[index], seed_, kBaseDraws);
  });

  std::vector<ScenarioSpec> custom_specs;
  custom_specs.reserve(288);
  int candidate = 0;
  const double max_deescalation = clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0);
  for (double move : {-25.0, 0.0, 25.0})
    for (double fiscal : {-0.15, 0.10, 0.35, 0.60})
      for (double productive : {0.35, 0.65, 0.90})
        for (double cooperation : {0.0, .33, .67, 1.0})
          for (double diversification_boost : {0.0, .15}) {
            const double deescalation = cooperation * max_deescalation;
            const double relief = clamp(.42 * (1.0 - productive)
                + .08 * (1.0 - deescalation), 0.0, .45);
            const double diversification = clamp(.08 + .48 * productive * (1.0 - deescalation)
                + diversification_boost, 0.0, .70);
            ++candidate;
            ScenarioSpec spec;
            spec.id = "custom";
            spec.name = "Custom win-win frontier";
            spec.description = "Autonomously generated from the policy search.";
            spec.move = move;
            spec.fiscal = fiscal;
            spec.productive = productive;
            spec.deescalation = deescalation;
            spec.relief = relief;
            spec.diversification = diversification;
            if (e.exhaustive_policy_search) {
              std::ostringstream generated_id;
              generated_id << "custom-" << std::setw(3) << std::setfill('0') << candidate;
              spec.id = generated_id.str();
              spec.name = "Generated win-win mix";
              std::ostringstream description;
              description << "Generated policy mix " << candidate << " of 288: "
                  << (move < 0 ? "ease 25 bp" : move > 0 ? "tighten 25 bp" : "hold rates")
                  << ", " << std::setprecision(2) << fiscal << "% fiscal impulse, "
                  << 100.0 * deescalation << "% negotiated rate relief, productive share "
                  << 100.0 * productive << "%, diversification " << 100.0 * diversification << "%.";
              spec.description = description.str();
            }
            custom_specs.push_back(std::move(spec));
          }

  std::vector<Scenario> custom_results(custom_specs.size());
  compute::parallel_for(custom_specs.size(), [&](std::size_t index) {
    custom_results[index] = simulate_spec(
        e, parameters_, custom_specs[index], seed_, kBaseDraws);
  });
  r.candidates_examined = static_cast<int>(custom_specs.size());
  if (e.exhaustive_policy_search) {
    r.scenarios.reserve(r.scenarios.size() + custom_results.size());
    for (auto& scenario : custom_results) r.scenarios.push_back(std::move(scenario));
  } else {
    Scenario custom;
    bool have_custom = false;
    double best_custom_score = -std::numeric_limits<double>::infinity();
    for (auto& scenario : custom_results) {
      const double candidate_score = deal_score(scenario, e, false);
      if (!have_custom || candidate_score > best_custom_score) {
        best_custom_score = candidate_score;
        custom = std::move(scenario);
        have_custom = true;
      }
    }
    std::ostringstream custom_description;
    custom_description << "Best of " << candidate << " generated policy mixes under fixed delegation priorities: "
        << (custom.first_move_bp < 0 ? "ease monetary policy"
            : custom.first_move_bp > 0 ? "tighten monetary policy" : "hold rates")
        << ", " << std::setprecision(2) << custom.fiscal_impulse
        << "% fiscal impulse, negotiated relief within the cooperation limit, and market diversification.";
    custom.description = custom_description.str();
    r.scenarios.push_back(std::move(custom));
  }

  const double priority_total = std::max(1e-9, std::max(0.0, e.canada_priority)
      + std::max(0.0, e.us_priority));
  r.recommendation.canada_priority = 100.0 * std::max(0.0, e.canada_priority) / priority_total;
  r.recommendation.us_priority = 100.0 * std::max(0.0, e.us_priority) / priority_total;
  r.recommendation.gdp_growth_floor = e.minimum_bilateral_growth;
  r.recommendation.risk_aversion = e.risk_aversion;
  r.recommendation.cooperation_ceiling = e.cooperation_ceiling;
  r.recommendation.base_monte_carlo_draws = kBaseDraws;
  r.recommendation.verification_monte_carlo_draws = kVerificationDraws;
  r.recommendation.sector_grid_step = kSectorGridStep;
  r.recommendation.policy_candidates_verified = static_cast<int>(r.scenarios.size());
  r.recommendation.trade_network_method = trade_network_methodology();
  r.allocations_examined = 1;
  r.gdp_floors_examined = 1;

  Scenario starting_baseline;
  double baseline_canada = 0.0;
  double baseline_us = 0.0;
  if (e.exhaustive_policy_search) {
    starting_baseline = simulate(e, parameters_, "baseline", "Starting posture",
        "The submitted tariff, sector-coverage, monetary and fiscal posture with no negotiated policy change.",
        0.0, 0.0, 0.5, 0.0, 0.0, 0.0, seed_, kVerificationDraws);
    starting_baseline.sector_verified = true;
    baseline_canada = starting_baseline.canada_score;
    baseline_us = starting_baseline.us_score;
    r.recommendation.baseline_canada_score = baseline_canada;
    r.recommendation.baseline_us_score = baseline_us;
  }

  struct VerificationOutcome {
    Scenario scenario;
    ScenarioMeta meta;
    bool frontier_exhaustive = true;
  };
  std::vector<VerificationOutcome> outcomes(r.scenarios.size());
  compute::parallel_for(r.scenarios.size(), [&](std::size_t index) {
    const Scenario& base = r.scenarios[index];
    const SectorSearch search = search_sector_frontier(e, base);
    Scenario selected;
    CoverageCandidate selected_coverage;
    bool have_selected = false;
    double selected_rank = -1e100;
    int finalists_run = 0;

    for (const auto& coverage : search.finalists) {
      Economy candidate_e = e;
      candidate_e.us_sector_coverage = coverage.us_coverage;
      candidate_e.canada_sector_coverage = coverage.canada_coverage;
      const int candidate_draws = e.exhaustive_policy_search ? kVerificationDraws : kBaseDraws;
      auto verified = simulate(candidate_e, parameters_, base.id, base.name, base.description,
          base.first_move_bp, base.fiscal_impulse, base.productive_share,
          base.negotiated_relief / 100.0, base.targeted_relief, base.diversification,
          seed_, candidate_draws);
      verified.sector_verified = true;
      const double raw_score = deal_score(verified, e, false);
      const bool growth_ok = verified.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
      bool national_win_win = true;
      if (e.exhaustive_policy_search) {
        const double canada_score = verified.canada_score;
        national_win_win = canada_score + 1e-9 >= baseline_canada
            && verified.us_score + 1e-9 >= baseline_us;
      }
      const double rank = (growth_ok && national_win_win ? 0.0 : -1e6) + raw_score;
      ++finalists_run;
      if (!have_selected || rank > selected_rank) {
        selected_rank = rank;
        selected = std::move(verified);
        selected_coverage = coverage;
        have_selected = true;
      }
    }

    if (!have_selected) {
      selected = base;
      selected_coverage.us_coverage = e.us_sector_coverage;
      selected_coverage.canada_coverage = e.canada_sector_coverage;
      selected_coverage.canada_score = search.baseline_canada_score;
      selected_coverage.us_score = search.baseline_us_score;
    }
    selected.score = selected_rank;

    ScenarioMeta meta;
    meta.id = selected.id;
    meta.candidates_examined = search.candidates_examined;
    meta.pareto_size = search.pareto_frontier_size;
    meta.finalists = finalists_run;
    meta.exhaustive = search.verification_exhaustive;
    meta.sector_ca_score = selected_coverage.canada_score;
    meta.sector_us_score = selected_coverage.us_score;
    meta.baseline_ca_score = search.baseline_canada_score;
    meta.baseline_us_score = search.baseline_us_score;

    outcomes[index].scenario = std::move(selected);
    outcomes[index].meta = std::move(meta);
    outcomes[index].frontier_exhaustive = search.verification_exhaustive;
  });

  std::vector<ScenarioMeta> meta;
  meta.reserve(r.scenarios.size());
  bool all_sector_frontiers_verified = true;
  for (std::size_t i = 0; i < outcomes.size(); ++i) {
    if (e.exhaustive_policy_search && !outcomes[i].frontier_exhaustive)
      all_sector_frontiers_verified = false;
    r.scenarios[i] = std::move(outcomes[i].scenario);
    meta.push_back(std::move(outcomes[i].meta));
  }

  if (e.exhaustive_policy_search) {
    const bool baseline_growth_ok = starting_baseline.bilateral_growth_floor + 1e-9
        >= e.minimum_bilateral_growth;
    starting_baseline.score = (baseline_growth_ok ? 0.0 : -1e6)
        + deal_score(starting_baseline, e, false);
    r.scenarios.push_back(std::move(starting_baseline));
  } else {
    std::vector<Scenario> verified_scenarios(r.scenarios.size());
    compute::parallel_for(r.scenarios.size(), [&](std::size_t index) {
      const Scenario& scenario = r.scenarios[index];
      Economy verified_e = e;
      verified_e.us_sector_coverage = scenario.applied_us_sector_coverage;
      verified_e.canada_sector_coverage = scenario.applied_canada_sector_coverage;
      auto verified = simulate(verified_e, parameters_, scenario.id, scenario.name, scenario.description,
          scenario.first_move_bp, scenario.fiscal_impulse, scenario.productive_share,
          scenario.negotiated_relief / 100.0, scenario.targeted_relief, scenario.diversification,
          seed_, kVerificationDraws);
      verified.sector_verified = true;
      const double raw_score = deal_score(verified, e, false);
      const bool growth_ok = verified.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
      verified.score = (growth_ok ? 0.0 : -1e6) + raw_score;
      verified_scenarios[index] = std::move(verified);
    });
    r.scenarios = std::move(verified_scenarios);
  }

  std::sort(r.scenarios.begin(), r.scenarios.end(),
      [](const Scenario& a, const Scenario& b) { return a.score > b.score; });
  const auto& best = r.scenarios.front();
  const auto* best_meta = find_meta(meta, best.id);

  r.recommendation.strategy_id = best.id;
  r.recommendation.us_sector_coverage = best.applied_us_sector_coverage;
  r.recommendation.canada_sector_coverage = best.applied_canada_sector_coverage;
  r.recommendation.verified_canada_score = best.canada_score;
  r.recommendation.verified_us_score = best.us_score;
  r.recommendation.verified_min_sector_metric = min_sector_metric(best);
  r.recommendation.growth_constraint_met =
      best.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  const bool sector_win_win = !best_meta
      || (best_meta->sector_ca_score + 1e-9 >= best_meta->baseline_ca_score
          && best_meta->sector_us_score + 1e-9 >= best_meta->baseline_us_score);
  const bool national_win_win = !e.exhaustive_policy_search
      || (r.recommendation.verified_canada_score + 1e-9 >= baseline_canada
          && r.recommendation.verified_us_score + 1e-9 >= baseline_us);
  r.recommendation.verified_win_win = sector_win_win
      && national_win_win && r.recommendation.growth_constraint_met;
  r.recommendation.global_search_complete = e.exhaustive_policy_search
      && all_sector_frontiers_verified;
  if (best_meta) {
    r.recommendation.sector_candidates_examined = best_meta->candidates_examined;
    r.recommendation.sector_pareto_frontier_size = best_meta->pareto_size;
    r.recommendation.sector_finalists_resimulated = best_meta->finalists;
  }
  fill_sector_display_metrics(e, best, r.recommendation);

  std::ostringstream recommendation;
  recommendation << "The recommendation keeps the delegation priorities fixed at "
      << std::setprecision(3) << r.recommendation.canada_priority << "% Canada / "
      << r.recommendation.us_priority << "% United States. ";
  if (e.exhaustive_policy_search) {
    recommendation << "The startup optimizer carries all 288 generated policy-control mixes plus 13 expert strategies into the 20-sector Pareto search. "
        << "Each retained sector win-win schedule is re-simulated with " << kVerificationDraws
        << " common-random-number draws before selection; both national welfare scores must also be no worse than the submitted starting posture. ";
    if (r.recommendation.global_search_complete)
      recommendation << "The sector verification cap did not bind, so the declared startup grid was exhaustively verified. ";
    else
      recommendation << "The sector verification safety cap bound for at least one strategy, so globalSearchComplete is false and the result must be described as the best verified retained package rather than a global optimum. ";
  } else {
    recommendation << "The staged optimizer builds the non-dominated global frontier of bilateral sector-coverage schedules at "
        << kSectorGridStep << "% increments, re-simulates up to " << kSectorStagedFinalists
        << " frontier schedules per strategy, then rechecks each selected strategy with "
        << kVerificationDraws << " common-random-number draws. ";
  }
  recommendation << "The macro trade block, tariff ledger, direct sector welfare and production-network first-round shocks now consume the same sector-weighted constant-elasticity bilateral quantity response. Large tariff stress cases therefore remain positive without an arbitrary trade floor or a parallel linear sector equation. Production-network supplier-demand, input-cost, price, output and jobs propagation uses explicit structural-registry coefficients rather than hidden constants. Canadian and U.S. export channels remain independent, and bilateral trade balance remains report-only rather than a welfare objective.";
  r.recommendation.explanation = recommendation.str();

  for (auto& scenario : r.scenarios)
    scenario.sustained_bilateral_growth =
        scenario.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;

  r.signal = best.first_move_bp > 0 ? "Raise 25 bp"
      : best.first_move_bp < 0 ? "Cut 25 bp" : "Hold & coordinate";
  if (r.recommendation.global_search_complete && r.recommendation.verified_win_win) {
    r.rationale = "The " + best.name
        + " is the highest verified bilateral-welfare win-win on the complete declared startup grid under fixed mandates, the national no-worse baseline test, the growth constraint, production-network spillovers and full sector-package stochastic verification.";
  } else {
    r.rationale = "The " + best.name
        + " is the highest verified bilateral-welfare package among the retained search candidates under the fixed mandate, sector Pareto screen, production-network spillovers, growth constraint and stochastic verification.";
  }
  return r;
}

std::string to_json(const Result& r) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "{\"regime\":\"" << esc(r.regime)
    << "\",\"signal\":\"" << esc(r.signal)
    << "\",\"rationale\":\"" << esc(r.rationale)
    << "\",\"stateConsistency\":" << r.data_confidence
    << ",\"confidence\":" << r.data_confidence
    << ",\"neutralRate\":" << r.neutral_rate
    << ",\"policyGap\":" << r.policy_gap
    << ",\"candidatesExamined\":" << r.candidates_examined
    << ",\"allocationsExamined\":" << r.allocations_examined
    << ",\"gdpFloorsExamined\":" << r.gdp_floors_examined
    << ",\"recommendation\":{\"canadaPriority\":" << r.recommendation.canada_priority
    << ",\"usPriority\":" << r.recommendation.us_priority
    << ",\"gdpGrowthFloor\":" << r.recommendation.gdp_growth_floor
    << ",\"riskAversion\":" << r.recommendation.risk_aversion
    << ",\"cooperationCeiling\":" << r.recommendation.cooperation_ceiling
    << ",\"strategyId\":\"" << esc(r.recommendation.strategy_id)
    << "\",\"sectorCandidatesExamined\":" << r.recommendation.sector_candidates_examined
    << ",\"sectorParetoFrontierSize\":" << r.recommendation.sector_pareto_frontier_size
    << ",\"sectorFinalistsResimulated\":" << r.recommendation.sector_finalists_resimulated
    << ",\"policyCandidatesVerified\":" << r.recommendation.policy_candidates_verified
    << ",\"sectorGridStep\":" << r.recommendation.sector_grid_step
    << ",\"baseMonteCarloDraws\":" << r.recommendation.base_monte_carlo_draws
    << ",\"verificationMonteCarloDraws\":" << r.recommendation.verification_monte_carlo_draws
    << ",\"verifiedCanadaScore\":" << r.recommendation.verified_canada_score
    << ",\"verifiedUsScore\":" << r.recommendation.verified_us_score
    << ",\"baselineCanadaScore\":" << r.recommendation.baseline_canada_score
    << ",\"baselineUsScore\":" << r.recommendation.baseline_us_score
    << ",\"verifiedMinSectorMetric\":" << r.recommendation.verified_min_sector_metric
    << ",\"verifiedWinWin\":" << (r.recommendation.verified_win_win ? "true" : "false")
    << ",\"globalSearchComplete\":" << (r.recommendation.global_search_complete ? "true" : "false")
    << ",\"growthConstraintMet\":" << (r.recommendation.growth_constraint_met ? "true" : "false")
    << ",\"independentUsTradeChannel\":" << (r.recommendation.independent_us_trade_channel ? "true" : "false")
    << ",\"tradeBalanceIsObjective\":" << (r.recommendation.trade_balance_is_objective ? "true" : "false")
    << ",\"mandateWeightsFixed\":" << (r.recommendation.mandate_weights_fixed ? "true" : "false")
    << ",\"sectorSearchMethod\":\"" << esc(r.recommendation.sector_search_method)
    << "\",\"tradeNetworkMethod\":\"" << esc(r.recommendation.trade_network_method)
    << "\",\"usSectorCoverage\":";
  array_json(o, r.recommendation.us_sector_coverage);
  o << ",\"canadaSectorCoverage\":";
  array_json(o, r.recommendation.canada_sector_coverage);
  o << ",\"usSectorOutput\":";
  array_json(o, r.recommendation.us_sector_output);
  o << ",\"canadaSectorValue\":";
  array_json(o, r.recommendation.canada_sector_value);
  o << ",\"explanation\":\"" << esc(r.recommendation.explanation) << "\"},\"scenarios\":[";

  for (std::size_t i = 0; i < r.scenarios.size(); ++i) {
    if (i) o << ',';
    const auto& s = r.scenarios[i];
    o << "{\"id\":\"" << esc(s.id)
      << "\",\"name\":\"" << esc(s.name)
      << "\",\"description\":\"" << esc(s.description)
      << "\",\"move\":" << s.first_move_bp
      << ",\"fiscal\":" << s.fiscal_impulse
      << ",\"score\":" << s.score
      << ",\"bocScore\":" << s.boc_score
      << ",\"federalScore\":" << s.federal_score
      << ",\"canadaScore\":" << s.canada_score
      << ",\"usScore\":" << s.us_score
      << ",\"inflation\":" << s.inflation
      << ",\"growth\":" << s.growth
      << ",\"usGrowth\":" << s.us_growth
      << ",\"bilateralGrowthFloor\":" << s.bilateral_growth_floor
      << ",\"sustainedBilateralGrowth\":" << (s.sustained_bilateral_growth ? "true" : "false")
      << ",\"unemployment\":" << s.unemployment
      << ",\"debt\":" << s.debt_gdp
      << ",\"housing\":" << s.housing_gap
      << ",\"recessionRisk\":" << s.recession_risk
      << ",\"rates\":";
    array_json(o, s.rates);
    o << ",\"costOfLiving\":" << s.cost_of_living
      << ",\"realIncome\":" << s.real_income_growth
      << ",\"exports\":" << s.export_change
      << ",\"usExports\":" << s.us_export_change
      << ",\"debtP90\":" << s.debt_stress_p90
      << ",\"inflationP90\":" << s.inflation_stress_p90
      << ",\"usTariffRevenueUsd\":" << s.us_tariff_revenue_usd
      << ",\"usTariffRevenueCad\":" << s.us_tariff_revenue_cad
      << ",\"canadaTariffRevenueCad\":" << s.canada_tariff_revenue_cad
      << ",\"canadaTariffRevenueUsd\":" << s.canada_tariff_revenue_usd
      << ",\"canadaTradeBalanceCad\":" << s.canada_trade_balance_cad
      << ",\"usTradeBalanceUsd\":" << s.us_trade_balance_usd
      << ",\"tradeBalanceGapUsd\":" << s.trade_balance_gap_usd
      << ",\"tradeBalanceProgress\":" << s.trade_balance_progress
      << ",\"usExportExpansionUsd\":" << s.us_export_expansion_usd
      << ",\"canadaExportRedirectionCad\":" << s.canada_export_redirection_cad
      << ",\"zeroTradeDeficit\":" << (s.zero_trade_deficit ? "true" : "false")
      << ",\"sectorVerified\":" << (s.sector_verified ? "true" : "false")
      << ",\"appliedUsSectorCoverage\":";
    array_json(o, s.applied_us_sector_coverage);
    o << ",\"appliedCanadaSectorCoverage\":";
    array_json(o, s.applied_canada_sector_coverage);
    o << ",\"inflationPath\":";
    array_json(o, s.inflation_path);
    o << ",\"growthPath\":";
    array_json(o, s.growth_path);
    o << ",\"usGrowthPath\":";
    array_json(o, s.us_growth_path);
    o << ",\"debtPath\":";
    array_json(o, s.debt_path);
    o << ",\"costPath\":";
    array_json(o, s.cost_path);
    o << ",\"exportPath\":";
    array_json(o, s.export_path);
    o << ",\"usExportPath\":";
    array_json(o, s.us_export_path);
    o << ",\"fiscalPath\":";
    array_json(o, s.fiscal_path);
    o << ",\"productiveInvestmentPath\":";
    array_json(o, s.productive_investment_path);
    o << ",\"negotiatedReliefPath\":";
    array_json(o, s.negotiated_relief_path);
    o << ",\"targetedReliefPath\":";
    array_json(o, s.targeted_relief_path);
    o << ",\"diversificationPath\":";
    array_json(o, s.diversification_path);
    o << ",\"sectors\":[";
    for (std::size_t j = 0; j < s.sectors.size(); ++j) {
      if (j) o << ',';
      const auto& x = s.sectors[j];
      o << "{\"code\":\"" << esc(x.code)
        << "\",\"name\":\"" << esc(x.name)
        << "\",\"exposure\":" << x.exposure
        << ",\"canada\":{\"output\":" << x.canada_output
        << ",\"jobs\":" << x.canada_jobs
        << ",\"prices\":" << x.canada_prices
        << "},\"us\":{\"output\":" << x.us_output
        << ",\"jobs\":" << x.us_jobs
        << ",\"prices\":" << x.us_prices
        << "},\"trade\":{\"usAppliedTariff\":" << x.us_applied_tariff
        << ",\"canadaAppliedTariff\":" << x.canada_applied_tariff
        << ",\"usBuyerPassThrough\":" << x.us_buyer_pass_through
        << ",\"canadaBuyerPassThrough\":" << x.canada_buyer_pass_through
        << ",\"canadaExporterAbsorption\":" << x.canada_exporter_absorption
        << ",\"usExporterAbsorption\":" << x.us_exporter_absorption
        << ",\"usImporterAbsorption\":" << x.us_importer_absorption
        << ",\"canadaImporterAbsorption\":" << x.canada_importer_absorption
        << ",\"canadaUpstreamCost\":" << x.canada_upstream_cost
        << ",\"usUpstreamCost\":" << x.us_upstream_cost << "}}";
    }
    o << "]}";
  }
  o << "]}";
  return o.str();
}

}  // namespace cad
