#include "policy_engine.hpp"
#include "robustness.hpp"
#include "structural_calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace cad {
namespace {

double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
double sq(double x) { return x * x; }

constexpr int kRobustBaseDraws = 700;
constexpr int kRobustVerificationDraws = 2800;
constexpr std::size_t kRobustSectorFinalists = 8;

struct SectorProfile {
  const char* code;
  const char* name;
  double trade;
  double import;
  double jobs;
  double cyclical;
};

// This is the same production sector screen used by PolicyEngine::evaluate().
// Structural macro parameters do not enter this deterministic Pareto screen;
// they enter the stochastic finalist ranking below. That makes the frontier
// safe to cache once per policy without changing the nested optimization.
constexpr std::array<SectorProfile, 20> kSectorProfiles{{
  {"11","Agriculture, forestry, fishing & hunting",.82,.42,.72,.65},
  {"21","Mining, quarrying, oil & gas",.88,.18,.32,.75},
  {"22","Utilities",.16,.10,.25,.25},
  {"23","Construction",.18,.28,.82,.88},
  {"31-33","Manufacturing",.94,.76,.68,.92},
  {"42","Wholesale trade",.68,.58,.64,.74},
  {"44-45","Retail trade",.30,.72,.88,.62},
  {"48-49","Transportation & warehousing",.72,.48,.70,.86},
  {"51","Information & cultural industries",.34,.30,.48,.44},
  {"52","Finance & insurance",.22,.20,.34,.55},
  {"53","Real estate, rental & leasing",.10,.12,.30,.78},
  {"54","Professional, scientific & technical services",.38,.26,.58,.48},
  {"55","Management of companies & enterprises",.20,.18,.24,.40},
  {"56","Administrative, support & waste services",.28,.24,.86,.72},
  {"61","Educational services",.08,.10,.82,.18},
  {"62","Health care & social assistance",.06,.14,.94,.16},
  {"71","Arts, entertainment & recreation",.14,.16,.88,.68},
  {"72","Accommodation & food services",.18,.52,.96,.82},
  {"81","Other services (except public administration)",.16,.30,.90,.58},
  {"91","Public administration",.04,.08,.62,.12}
}};

struct SectorUtility {
  double canada = 0.0;
  double us = 0.0;
  SectorImpact impact;
};

std::vector<double> coverage_levels(double current, double cooperation_ceiling,
                                    double negotiated_relief) {
  const double start = clamp(current, 0.0, 100.0);
  const double cap = clamp(cooperation_ceiling / 100.0, 0.0, 1.0);
  const double rate_relief = clamp(negotiated_relief / 100.0, 0.0, cap);
  const double minimum_coverage_ratio = (1.0 - rate_relief) > 1e-12
      ? clamp((1.0 - cap) / (1.0 - rate_relief), 0.0, 1.0)
      : 1.0;
  const double max_coverage_relief = 1.0 - minimum_coverage_ratio;
  std::vector<double> levels;
  for (double fraction : {0.0, .25, .50, .75, 1.0})
    levels.push_back(start * (1.0 - max_coverage_relief * fraction));
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end(),
      [](double a, double b) { return std::abs(a - b) < 1e-9; }), levels.end());
  return levels;
}

SectorUtility sector_utility(const Economy& e, const Scenario& policy, std::size_t sector,
                             double us_coverage, double canada_coverage) {
  const auto& p = kSectorProfiles[sector];
  const double deescalation = clamp(policy.negotiated_relief / 100.0, 0.0, 1.0);
  const double diversification = clamp(
      policy.diversification + e.trade_diversification, 0.0, 0.75);
  const double uc = clamp(us_coverage / 100.0, 0.0, 1.0);
  const double cc = clamp(canada_coverage / 100.0, 0.0, 1.0);
  const double us_tariff = e.us_tariff_canada * (1.0 - deescalation) / 100.0 * uc;
  const double ca_tariff = e.canada_retaliatory_tariff * (1.0 - deescalation) / 100.0 * cc;
  const double supply = policy.productive_share * policy.fiscal_impulse
      * (.16 + .12 * p.cyclical);
  const double ca_shock = us_tariff * p.trade * (.72 - .28 * diversification)
      + e.border_friction / 100.0 * p.trade * .18;
  const double us_shock = ca_tariff * p.import * .46 + us_tariff * p.import * .12;
  const double us_protection = us_tariff * p.trade * .24 * (1.0 - .5 * uc);

  SectorUtility out;
  out.impact.code = p.code;
  out.impact.name = p.name;
  out.impact.exposure = 100.0 * p.trade;
  out.impact.canada_output = 100.0
      * (-ca_shock + supply + policy.targeted_relief * .10 * p.jobs);
  out.impact.us_output = 100.0
      * (-us_shock + us_protection + deescalation * .012 * p.trade);
  out.impact.canada_jobs = out.impact.canada_output * (.30 + .42 * p.jobs);
  out.impact.us_jobs = out.impact.us_output * (.28 + .38 * p.jobs);
  out.impact.canada_prices = 100.0
      * (ca_tariff * p.import * .30 + us_tariff * p.import * .05 - supply * .10);
  out.impact.us_prices = 100.0
      * (us_tariff * p.import * .24 + ca_tariff * p.import * .10);

  const double price_weight = .65 + .70 * clamp(e.risk_aversion / 100.0, 0.0, 1.0)
      + .18 * std::max(0.0, (e.inflation + e.us_inflation) / 2.0 - 2.0);
  const double ca_jobs_weight = .45 + .08 * std::max(0.0, e.unemployment - 5.0);
  const double us_jobs_weight = .45 + .08 * std::max(0.0, 4.5 - e.us_growth);
  const double leverage = 100.0 * ca_tariff * p.trade * .16 * (1.0 - .65 * cc);

  out.canada = p.trade * (out.impact.canada_output
      + ca_jobs_weight * out.impact.canada_jobs
      - price_weight * out.impact.canada_prices + leverage);
  out.us = p.import * (out.impact.us_output
      + us_jobs_weight * out.impact.us_jobs
      - price_weight * out.impact.us_prices);
  return out;
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
  double baseline_canada_score = 0.0;
  double baseline_us_score = 0.0;
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

  for (std::size_t sector = 0; sector < kSectorProfiles.size(); ++sector) {
    const auto us_levels = coverage_levels(
        e.us_sector_coverage[sector], e.cooperation_ceiling, policy.negotiated_relief);
    const auto ca_levels = coverage_levels(
        e.canada_sector_coverage[sector], e.cooperation_ceiling, policy.negotiated_relief);

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

    std::sort(expanded.begin(), expanded.end(),
        [](const CoverageCandidate& a, const CoverageCandidate& b) {
          if (std::abs(a.canada_raw - b.canada_raw) > 1e-9)
            return a.canada_raw > b.canada_raw;
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

  search.baseline_canada_score = normalize_utility(
      baseline_ca_raw, ca_global_min, ca_global_max);
  search.baseline_us_score = normalize_utility(
      baseline_us_raw, us_global_min, us_global_max);
  search.pareto_frontier_size = static_cast<int>(frontier.size());

  const double ca_weight = clamp(e.canada_priority, 1.0, 100.0);
  const double us_weight = clamp(e.us_priority, 1.0, 100.0);
  const double weight_total = ca_weight + us_weight;
  std::vector<CoverageCandidate> win_win;

  for (auto& candidate : frontier) {
    candidate.canada_score = normalize_utility(
        candidate.canada_raw, ca_global_min, ca_global_max);
    candidate.us_score = normalize_utility(
        candidate.us_raw, us_global_min, us_global_max);
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

  std::sort(win_win.begin(), win_win.end(),
      [](const CoverageCandidate& a, const CoverageCandidate& b) {
        return a.objective > b.objective;
      });
  const std::size_t keep = std::min<std::size_t>(kRobustSectorFinalists, win_win.size());
  search.finalists.assign(win_win.begin(), win_win.begin() + keep);
  return search;
}

void set_sector_package(Scenario& policy, const Economy& e,
                        const std::array<double, 20>& us_coverage,
                        const std::array<double, 20>& canada_coverage) {
  policy.applied_us_sector_coverage = us_coverage;
  policy.applied_canada_sector_coverage = canada_coverage;
  policy.sectors.clear();
  policy.sectors.reserve(kSectorProfiles.size());
  for (std::size_t sector = 0; sector < kSectorProfiles.size(); ++sector) {
    policy.sectors.push_back(sector_utility(
        e, policy, sector, us_coverage[sector], canada_coverage[sector]).impact);
  }
}

bool same_package(const Scenario& a, const Scenario& b) {
  for (std::size_t i = 0; i < a.applied_us_sector_coverage.size(); ++i) {
    if (std::abs(a.applied_us_sector_coverage[i]
        - b.applied_us_sector_coverage[i]) > 1e-9) return false;
    if (std::abs(a.applied_canada_sector_coverage[i]
        - b.applied_canada_sector_coverage[i]) > 1e-9) return false;
  }
  return true;
}

void directional_coverage(const Economy& e, double& us_barrier_coverage,
                          double& ca_barrier_coverage) {
  double ca_export_weight = 0.0;
  double us_export_weight = 0.0;
  us_barrier_coverage = 0.0;
  ca_barrier_coverage = 0.0;
  for (std::size_t i = 0; i < kSectorProfiles.size(); ++i) {
    ca_export_weight += kSectorProfiles[i].trade;
    us_export_weight += kSectorProfiles[i].import;
    us_barrier_coverage += kSectorProfiles[i].trade
        * clamp(e.us_sector_coverage[i] / 100.0, 0.0, 1.0);
    ca_barrier_coverage += kSectorProfiles[i].import
        * clamp(e.canada_sector_coverage[i] / 100.0, 0.0, 1.0);
  }
  us_barrier_coverage /= std::max(1e-9, ca_export_weight);
  ca_barrier_coverage /= std::max(1e-9, us_export_weight);
}

Scenario simulate_parameterized(const Economy& baseline_e, const Scenario& policy,
                                const StructuralParameters& p, std::uint64_t seed,
                                int draws) {
  Economy e = baseline_e;
  e.us_sector_coverage = policy.applied_us_sector_coverage;
  e.canada_sector_coverage = policy.applied_canada_sector_coverage;

  Scenario s = policy;
  s.rates.fill(0.0);
  s.inflation_path.fill(0.0);
  s.growth_path.fill(0.0);
  s.us_growth_path.fill(0.0);
  s.debt_path.fill(0.0);
  s.cost_path.fill(0.0);
  s.export_path.fill(0.0);
  s.us_export_path.fill(0.0);

  const double deescalation = std::min(policy.negotiated_relief / 100.0,
      clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0));
  double us_barrier_coverage = 0.0;
  double ca_barrier_coverage = 0.0;
  directional_coverage(e, us_barrier_coverage, ca_barrier_coverage);

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
  double inf_sum = 0.0, growth_sum = 0.0, us_growth_sum = 0.0, u_sum = 0.0;
  double debt_sum = 0.0, house_sum = 0.0, cost_sum = 0.0, income_sum = 0.0;
  double export_sum = 0.0, us_export_sum = 0.0, recessions = 0.0;
  std::array<double, 12> rp{}, ip{}, gp{}, ugp{}, dp{}, cp{}, xp{}, uxp{};
  std::vector<double> terminal_debt;
  std::vector<double> terminal_inflation;
  terminal_debt.reserve(static_cast<std::size_t>(draws));
  terminal_inflation.reserve(static_cast<std::size_t>(draws));

  for (int d = 0; d < draws; ++d) {
    double rate = e.policy_rate;
    double inf = e.core_inflation;
    double gap = e.output_gap;
    double u = e.unemployment;
    double debt = e.federal_debt_gdp;
    double housing = e.housing_gap;
    double export_change = 0.0;
    double us_export_change = 0.0;
    double cost = e.inflation;
    bool recession = false;

    for (int q = 0; q < 12; ++q) {
      const double coordinated = policy.productive_share * policy.fiscal_impulse;
      const double us_tariff = std::max(
          0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation));
      const double ca_tariff = std::max(
          0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
      const double exposed_exports = e.exports_to_us_share / 100.0
          * (1.0 - clamp(policy.diversification + e.trade_diversification, 0.0, 0.75));
      const double trade_drag = p.canada_trade_drag_scale
          * exposed_exports * e.exports_gdp / 100.0
          * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0;
      const double us_trade_drag = p.us_retaliation_drag_scale
          * e.imports_from_us_share / 100.0 * e.trade_elasticity
          * (ca_tariff + .45 * e.border_friction) / 100.0;
      const double import_price = e.imports_from_us_share / 100.0
          * e.import_content_consumption / 100.0 * ca_tariff;

      const double rate_target = clamp(p.neutral_rate
          + p.rate_inflation_response * (inf - p.inflation_target)
          + p.rate_output_response * gap, .25, 7.0);
      if (q == 0) {
        rate = clamp(rate + policy.first_move_bp / 100.0, 0.0, 8.0);
      } else {
        rate = clamp(rate + clamp(rate_target - rate,
            -p.max_quarterly_rate_step, p.max_quarterly_rate_step), 0.0, 8.0);
      }

      const double demand = policy.fiscal_impulse * (1.0 - policy.productive_share)
          * p.fiscal_demand_multiplier
          - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;
      const double supply = coordinated * p.productive_supply_multiplier
          + e.productivity_growth * .035;

      export_change = -100.0 * trade_drag + .35 * (e.us_growth - 2.0)
          + 2.0 * policy.diversification + shock(rng) * p.export_shock_sd;
      us_export_change = -100.0 * us_trade_drag + .30 * (e.gdp_growth - 1.5)
          + 1.5 * deescalation + shock(rng) * p.us_export_shock_sd;

      gap = p.output_persistence * gap + demand - trade_drag
          + p.global_growth_sensitivity * (e.global_growth - 2.7)
          + shock(rng) * p.output_shock_sd;
      const double fx = (e.usdcad - 1.34) * p.fx_pass_through;
      inf = p.inflation_persistence * inf
          + p.inflation_expectations_weight * e.inflation_expectations
          + p.phillips_curve_slope * gap + fx - supply
          + p.import_price_pass_through * import_price
          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)
          + shock(rng) * p.inflation_shock_sd;
      const double growth = clamp(1.75 + gap - .18 * e.credit_spread
          + coordinated * .24 + shock(rng) * p.growth_shock_sd, -3.0, 5.5);
      const double us_growth = clamp(e.us_growth + .16 * coordinated + .28 * deescalation
          - .010 * us_tariff - .014 * ca_tariff - .04 * e.border_friction
          + shock(rng) * p.us_growth_shock_sd, -3.0, 5.5);
      u = clamp(u - .10 * (growth - 1.7) + shock(rng) * .035, 3.5, 11.0);
      housing = clamp(.78 * housing - 1.15 * (rate - p.neutral_rate)
          + .08 * (e.population_growth - 1.2) + shock(rng) * .5, -15.0, 30.0);
      const double relief_cost = policy.targeted_relief + e.tariff_relief;
      debt += (-e.fiscal_balance_gdp + policy.fiscal_impulse * .8 + relief_cost * .55
          + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;
      cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
          + .14 * std::max(0.0, e.wage_growth - growth) + .08 * import_price;
      recession = recession || growth < 0.0;

      rp[q] += rate;
      ip[q] += inf;
      gp[q] += growth;
      ugp[q] += us_growth;
      dp[q] += debt;
      cp[q] += cost;
      xp[q] += export_change;
      uxp[q] += us_export_change;
      if (q == 11) {
        inf_sum += inf;
        growth_sum += growth;
        us_growth_sum += us_growth;
        u_sum += u;
        debt_sum += debt;
        house_sum += housing;
        cost_sum += cost;
        income_sum += growth - cost + policy.targeted_relief * .15;
        export_sum += export_change;
        us_export_sum += us_export_change;
        terminal_debt.push_back(debt);
        terminal_inflation.push_back(inf);
      }
    }
    if (recession) recessions += 1.0;
  }

  const double denom = static_cast<double>(draws);
  for (int q = 0; q < 12; ++q) {
    s.rates[q] = rp[q] / denom;
    s.inflation_path[q] = ip[q] / denom;
    s.growth_path[q] = gp[q] / denom;
    s.us_growth_path[q] = ugp[q] / denom;
    s.debt_path[q] = dp[q] / denom;
    s.cost_path[q] = cp[q] / denom;
    s.export_path[q] = xp[q] / denom;
    s.us_export_path[q] = uxp[q] / denom;
  }

  s.inflation = inf_sum / denom;
  s.growth = growth_sum / denom;
  s.us_growth = us_growth_sum / denom;
  s.unemployment = u_sum / denom;
  s.bilateral_growth_floor = std::min(
      *std::min_element(s.growth_path.begin(), s.growth_path.end()),
      *std::min_element(s.us_growth_path.begin(), s.us_growth_path.end()));
  s.sustained_bilateral_growth =
      s.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  s.debt_gdp = debt_sum / denom;
  s.housing_gap = house_sum / denom;
  s.recession_risk = 100.0 * recessions / denom;
  s.cost_of_living = cost_sum / denom;
  s.real_income_growth = income_sum / denom;
  s.export_change = export_sum / denom;
  s.us_export_change = us_export_sum / denom;

  const double effective_us_rate = std::max(
      0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double effective_ca_rate = std::max(
      0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double ledger_elasticity = e.trade_elasticity * p.tariff_revenue_elasticity_scale;
  const double ca_exports = e.canada_exports_to_us_cad
      * std::max(.05, 1.0 - ledger_elasticity * effective_us_rate);
  const double ca_imports = e.canada_imports_from_us_cad
      * std::max(.05, 1.0 - ledger_elasticity * effective_ca_rate);
  s.us_export_expansion_usd = 0.0;
  s.canada_export_redirection_cad = 0.0;
  s.us_tariff_revenue_cad = effective_us_rate * ca_exports;
  s.us_tariff_revenue_usd = s.us_tariff_revenue_cad / e.usdcad;
  s.canada_tariff_revenue_cad = effective_ca_rate * ca_imports;
  s.canada_tariff_revenue_usd = s.canada_tariff_revenue_cad / e.usdcad;
  s.canada_trade_balance_cad = ca_exports - ca_imports;
  s.us_trade_balance_usd = -s.canada_trade_balance_cad / e.usdcad;
  s.trade_balance_gap_usd = std::abs(s.us_trade_balance_usd);
  const double initial_gap = std::abs(e.canada_exports_to_us_cad - e.canada_imports_from_us_cad)
      / e.usdcad;
  s.trade_balance_progress = 100.0
      * (1.0 - s.trade_balance_gap_usd / std::max(.001, initial_gap));
  s.zero_trade_deficit = s.trade_balance_gap_usd < .05;

  std::sort(terminal_debt.begin(), terminal_debt.end());
  std::sort(terminal_inflation.begin(), terminal_inflation.end());
  const auto p90_index = static_cast<std::size_t>(draws * 9 / 10);
  s.debt_stress_p90 = terminal_debt[p90_index];
  s.inflation_stress_p90 = terminal_inflation[p90_index];

  const double mandate_loss = 3.8 * sq(s.inflation - p.inflation_target)
      + 1.2 * sq(std::max(0.0, s.unemployment - 5.8))
      + .7 * sq(std::min(0.0, s.growth)) + .018 * s.recession_risk;
  const double federal_loss = .32 * sq(std::max(0.0, s.debt_gdp - e.federal_debt_gdp))
      + .7 * sq(std::min(0.0, s.growth))
      + .8 * sq(std::max(0.0, s.unemployment - 6.0)) + .012 * sq(s.housing_gap);
  s.boc_score = 100.0 / (1.0 + mandate_loss);
  s.federal_score = 100.0 / (1.0 + federal_loss);

  const double us_inflation_pressure = std::max(
      0.0, e.us_inflation - 2.0 + e.us_tariff_canada * us_barrier_coverage * .025);
  const double us_loss = .55 * sq(std::max(0.0, -s.us_export_change))
      + .8 * sq(us_inflation_pressure)
      + .55 * sq(std::max(0.0, 1.8 - s.us_growth))
      + .25 * sq(e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
  s.us_score = 100.0 / (1.0 + us_loss);
  s.sector_verified = true;
  return s;
}

double conditional_deal_score(const Scenario& s, const Economy& e) {
  const double canada = std::sqrt(
      std::max(.01, s.boc_score) * std::max(.01, s.federal_score));
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
    const std::array<double, 6> metrics{
      x.canada_output, x.canada_jobs, -x.canada_prices,
      x.us_output, x.us_jobs, -x.us_prices
    };
    for (double metric : metrics) {
      sector_floor = std::min(sector_floor, metric);
      sector_sum += metric;
      ++count;
    }
  }
  if (!std::isfinite(sector_floor)) sector_floor = 0.0;
  const double sector_average = count ? sector_sum / static_cast<double>(count) : 0.0;
  return .66 * nash + .26 * floor - .05 * tail_penalty
      + .055 * sector_floor + .018 * sector_average;
}

void rank_parameterized_scenario(Scenario& s, const Economy& e) {
  const double raw_score = conditional_deal_score(s, e);
  const bool growth_ok = s.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  s.score = (growth_ok ? 0.0 : -1e6) + raw_score;
}

struct NestedSectorSelection {
  Scenario scenario;
  bool package_changed = false;
  int finalists_resimulated = 0;
};

NestedSectorSelection reoptimize_sector_package(
    const Economy& economy, const Scenario& reference_policy,
    const SectorSearch& search, const StructuralParameters& parameters,
    std::uint64_t common_macro_seed) {
  Scenario selected_policy;
  bool have_selected = false;
  double selected_rank = -std::numeric_limits<double>::infinity();
  int finalists_resimulated = 0;

  for (const auto& coverage : search.finalists) {
    Scenario candidate = reference_policy;
    set_sector_package(candidate, economy, coverage.us_coverage, coverage.canada_coverage);
    Scenario trial = simulate_parameterized(
        economy, candidate, parameters, common_macro_seed, kRobustBaseDraws);
    rank_parameterized_scenario(trial, economy);
    ++finalists_resimulated;
    if (!have_selected || trial.score > selected_rank) {
      selected_rank = trial.score;
      selected_policy = std::move(candidate);
      have_selected = true;
    }
  }

  if (!have_selected) {
    selected_policy = reference_policy;
    set_sector_package(selected_policy, economy,
        reference_policy.applied_us_sector_coverage,
        reference_policy.applied_canada_sector_coverage);
  }

  Scenario verified = simulate_parameterized(
      economy, selected_policy, parameters, common_macro_seed,
      kRobustVerificationDraws);
  rank_parameterized_scenario(verified, economy);

  NestedSectorSelection out;
  out.package_changed = !same_package(verified, reference_policy);
  out.finalists_resimulated = finalists_resimulated;
  out.scenario = std::move(verified);
  return out;
}

std::string json_escape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

}  // namespace

Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {
  Result baseline = evaluate(economy);
  auto& summary = baseline.recommendation.robustness;
  summary.parameter_draws = std::max(0, parameter_draws);
  summary.calibration_id = parameters_.calibration_id;
  summary.calibration_vintage = parameters_.calibration_vintage;
  summary.parameter_registry_id = parameters_.uncertainty_registry.loaded
      ? parameters_.uncertainty_registry.registry_id : "none";
  summary.sampled_parameter_count = sampled_structural_parameter_count(
      parameters_.uncertainty_registry);
  summary.parameter_bounds_active = summary.parameter_draws > 0
      && parameters_.uncertainty_registry.loaded
      && summary.sampled_parameter_count > 0;
  summary.parameter_provenance_complete = structural_parameter_registry_complete(
      parameters_.uncertainty_registry);
  summary.methodology =
      "outer-structural-ensemble/nested-sector-pareto-reoptimization/"
      "cached-parameter-invariant-frontiers/common-random-numbers";
  summary.structural_parameters_active = summary.parameter_draws > 0;
  summary.common_random_numbers = summary.parameter_draws > 0;
  summary.sector_packages_reoptimized = summary.parameter_draws > 0;

  if (summary.parameter_draws == 0 || baseline.scenarios.empty()) {
    summary.classification = "not-evaluated";
    return baseline;
  }

  // The exact sector Pareto screen is deterministic conditional on Economy and
  // policy controls. Build it once per policy, then re-rank all retained
  // finalists under every structural calibration. Rebuilding an identical
  // frontier inside every draw would add cost without changing the solution.
  std::vector<SectorSearch> sector_frontiers;
  sector_frontiers.reserve(baseline.scenarios.size());
  for (const auto& policy : baseline.scenarios)
    sector_frontiers.push_back(search_sector_frontier(economy, policy));
  summary.sector_frontiers_built = static_cast<int>(sector_frontiers.size());

  const std::string baseline_strategy = baseline.recommendation.strategy_id;
  const auto ensemble = draw_structural_parameters(
      parameters_, summary.parameter_draws,
      static_cast<std::uint64_t>(seed_) ^ 0x9e3779b97f4a7c15ULL);
  const std::uint64_t common_macro_seed = static_cast<std::uint64_t>(seed_);

  std::vector<double> selected_scores;
  selected_scores.reserve(ensemble.size());
  int reference_package_retained = 0;

  for (const auto& parameters : ensemble) {
    double best_score = -std::numeric_limits<double>::infinity();
    std::string best_strategy;

    for (std::size_t i = 0; i < baseline.scenarios.size(); ++i) {
      const auto& policy = baseline.scenarios[i];
      const auto& frontier = sector_frontiers[i];
      auto nested = reoptimize_sector_package(
          economy, policy, frontier, parameters, common_macro_seed);

      ++summary.nested_sector_optimizations;
      summary.nested_sector_candidates_examined +=
          static_cast<std::uint64_t>(std::max(0, frontier.candidates_examined));
      summary.nested_sector_finalists_resimulated +=
          static_cast<std::uint64_t>(std::max(0, nested.finalists_resimulated));
      if (nested.package_changed) ++summary.sector_package_changes;

      if (nested.scenario.id == baseline_strategy) {
        selected_scores.push_back(nested.scenario.score);
        if (!nested.package_changed) ++reference_package_retained;
      }
      if (nested.scenario.score > best_score) {
        best_score = nested.scenario.score;
        best_strategy = nested.scenario.id;
      }
    }
    if (best_strategy == baseline_strategy) ++summary.recommendation_wins;
  }

  summary.recommendation_win_rate = ensemble.empty() ? 0.0
      : static_cast<double>(summary.recommendation_wins)
          / static_cast<double>(ensemble.size());
  summary.reference_package_retention_rate = ensemble.empty() ? 0.0
      : static_cast<double>(reference_package_retained)
          / static_cast<double>(ensemble.size());
  if (!selected_scores.empty()) {
    summary.score_mean = std::accumulate(selected_scores.begin(), selected_scores.end(), 0.0)
        / static_cast<double>(selected_scores.size());
    summary.score_p10 = robustness_quantile(selected_scores, 0.10);
    summary.score_p90 = robustness_quantile(selected_scores, 0.90);
  }
  summary.classification = classify_robustness(summary.recommendation_win_rate);

  std::ostringstream note;
  note << " V2 nested structural robustness: " << summary.recommendation_wins << "/"
       << summary.parameter_draws << " parameter calibrations retain "
       << baseline_strategy << " (" << std::fixed << std::setprecision(1)
       << 100.0 * summary.recommendation_win_rate << "%, " << summary.classification
       << "). Each draw re-optimizes the verified 20-sector package across the production "
       << "Pareto finalists using common random numbers; the reference strategy keeps its "
       << "reference sector package in "
       << 100.0 * summary.reference_package_retention_rate << "% of calibrations.";
  baseline.recommendation.explanation += note.str();
  return baseline;
}

std::string robustness_to_json(const Result& result) {
  const auto& r = result.recommendation.robustness;
  std::ostringstream o;
  o << std::fixed << std::setprecision(6)
    << "{\"parameterDraws\":" << r.parameter_draws
    << ",\"recommendationWins\":" << r.recommendation_wins
    << ",\"recommendationWinRate\":" << r.recommendation_win_rate
    << ",\"scoreMean\":" << r.score_mean
    << ",\"scoreP10\":" << r.score_p10
    << ",\"scoreP90\":" << r.score_p90
    << ",\"classification\":\"" << json_escape(r.classification)
    << "\",\"calibrationId\":\"" << json_escape(r.calibration_id)
    << "\",\"calibrationVintage\":\"" << json_escape(r.calibration_vintage)
    << "\",\"parameterRegistryId\":\"" << json_escape(r.parameter_registry_id)
    << "\",\"methodology\":\"" << json_escape(r.methodology)
    << "\",\"sampledParameterCount\":" << r.sampled_parameter_count
    << ",\"structuralParametersActive\":"
    << (r.structural_parameters_active ? "true" : "false")
    << ",\"commonRandomNumbers\":" << (r.common_random_numbers ? "true" : "false")
    << ",\"sectorPackagesReoptimized\":"
    << (r.sector_packages_reoptimized ? "true" : "false")
    << ",\"parameterBoundsActive\":" << (r.parameter_bounds_active ? "true" : "false")
    << ",\"parameterProvenanceComplete\":"
    << (r.parameter_provenance_complete ? "true" : "false")
    << ",\"sectorFrontiersBuilt\":" << r.sector_frontiers_built
    << ",\"nestedSectorOptimizations\":" << r.nested_sector_optimizations
    << ",\"nestedSectorCandidatesExamined\":" << r.nested_sector_candidates_examined
    << ",\"nestedSectorFinalistsResimulated\":" << r.nested_sector_finalists_resimulated
    << ",\"sectorPackageChanges\":" << r.sector_package_changes
    << ",\"referencePackageRetentionRate\":"
    << r.reference_package_retention_rate << "}";
  return o.str();
}

}  // namespace cad
