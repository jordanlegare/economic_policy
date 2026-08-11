#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <random>
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
constexpr std::size_t kSectorFinalists = 8;

struct SectorProfile {
  const char* code;
  const char* name;
  double trade;
  double import;
  double jobs;
  double cyclical;
};

constexpr SectorProfile sector_profiles[] = {
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
};

struct SectorUtility {
  double canada = 0.0;
  double us = 0.0;
  SectorImpact impact;
};

std::vector<double> coverage_levels(double current) {
  std::vector<double> levels{0.0, 25.0, 50.0, 75.0, 100.0, clamp(current, 0.0, 100.0)};
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end(),
      [](double a, double b) { return std::abs(a - b) < 1e-9; }), levels.end());
  return levels;
}

SectorUtility sector_utility(const Economy& e, const Scenario& policy, std::size_t sector,
                             double us_coverage, double canada_coverage) {
  const auto& p = sector_profiles[sector];
  const double deescalation = clamp(policy.negotiated_relief / 100.0, 0.0, 1.0);
  const double diversification = clamp(policy.diversification + e.trade_diversification, 0.0, 0.75);
  const double uc = clamp(us_coverage / 100.0, 0.0, 1.0);
  const double cc = clamp(canada_coverage / 100.0, 0.0, 1.0);
  const double us_tariff = e.us_tariff_canada * (1.0 - deescalation) / 100.0 * uc;
  const double ca_tariff = e.canada_retaliatory_tariff * (1.0 - deescalation) / 100.0 * cc;
  const double supply = policy.productive_share * policy.fiscal_impulse * (.16 + .12 * p.cyclical);
  const double ca_shock = us_tariff * p.trade * (.72 - .28 * diversification)
      + e.border_friction / 100.0 * p.trade * .18;
  const double us_shock = ca_tariff * p.import * .46 + us_tariff * p.import * .12;
  const double us_protection = us_tariff * p.trade * .24 * (1.0 - .5 * uc);

  SectorUtility out;
  out.impact.code = p.code;
  out.impact.name = p.name;
  out.impact.exposure = 100.0 * p.trade;
  out.impact.canada_output = 100.0 * (-ca_shock + supply + policy.targeted_relief * .10 * p.jobs);
  out.impact.us_output = 100.0 * (-us_shock + us_protection + deescalation * .012 * p.trade);
  out.impact.canada_jobs = out.impact.canada_output * (.30 + .42 * p.jobs);
  out.impact.us_jobs = out.impact.us_output * (.28 + .38 * p.jobs);
  out.impact.canada_prices = 100.0 * (ca_tariff * p.import * .30 + us_tariff * p.import * .05 - supply * .10);
  out.impact.us_prices = 100.0 * (us_tariff * p.import * .24 + ca_tariff * p.import * .10);

  const double price_weight = .65 + .70 * clamp(e.risk_aversion / 100.0, 0.0, 1.0)
      + .18 * std::max(0.0, (e.inflation + e.us_inflation) / 2.0 - 2.0);
  const double ca_jobs_weight = .45 + .08 * std::max(0.0, e.unemployment - 5.0);
  const double us_jobs_weight = .45 + .08 * std::max(0.0, 4.5 - e.us_growth);
  const double leverage = 100.0 * ca_tariff * p.trade * .16 * (1.0 - .65 * cc);

  out.canada = p.trade * (out.impact.canada_output + ca_jobs_weight * out.impact.canada_jobs
      - price_weight * out.impact.canada_prices + leverage);
  out.us = p.import * (out.impact.us_output + us_jobs_weight * out.impact.us_jobs
      - price_weight * out.impact.us_prices);
  return out;
}

void add_sector_impacts(Scenario& s, const Economy& e) {
  s.sectors.clear();
  s.sectors.reserve(std::size(sector_profiles));
  for (std::size_t sector = 0; sector < std::size(sector_profiles); ++sector) {
    s.sectors.push_back(sector_utility(
        e, s, sector, e.us_sector_coverage[sector], e.canada_sector_coverage[sector]).impact);
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
    const auto us_levels = coverage_levels(e.us_sector_coverage[sector]);
    const auto ca_levels = coverage_levels(e.canada_sector_coverage[sector]);

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
  const std::size_t keep = std::min<std::size_t>(kSectorFinalists, win_win.size());
  search.finalists.assign(win_win.begin(), win_win.begin() + keep);
  return search;
}

Scenario simulate(const Economy& e, std::string id, std::string name, std::string description,
                  double move, double fiscal, double productive, double deescalation,
                  double targeted_relief, double diversification, std::uint64_t seed,
                  int draws = kBaseDraws) {
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

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
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

  for (int d = 0; d < draws; ++d) {
    double rate = e.policy_rate, inf = e.core_inflation, gap = e.output_gap, u = e.unemployment;
    double debt = e.federal_debt_gdp, housing = e.housing_gap;
    double export_change = 0.0, us_export_change = 0.0, cost = e.inflation;
    bool recession = false;

    for (int q = 0; q < 12; ++q) {
      const double coordinated = productive * fiscal;
      const double us_tariff = std::max(
          0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation));
      const double ca_tariff = std::max(
          0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
      const double exposed_exports = e.exports_to_us_share / 100.0
          * (1.0 - clamp(diversification + e.trade_diversification, 0.0, 0.75));
      const double trade_drag = exposed_exports * e.exports_gdp / 100.0
          * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0;
      const double us_trade_drag = e.imports_from_us_share / 100.0
          * e.trade_elasticity * (ca_tariff + .45 * e.border_friction) / 100.0;
      const double import_price = e.imports_from_us_share / 100.0
          * e.import_content_consumption / 100.0 * ca_tariff;
      const double rate_target = clamp(2.5 + .75 * (inf - 2.0) + .25 * gap, .25, 7.0);
      if (q == 0) rate = clamp(rate + move / 100.0, 0.0, 8.0);
      else rate = clamp(rate + clamp(rate_target - rate, -.25, .25), 0.0, 8.0);
      const double demand = fiscal * (1.0 - productive) * .36 - (rate - 2.5) * .18;
      const double supply = coordinated * .22 + e.productivity_growth * .035;

      export_change = -100.0 * trade_drag + .35 * (e.us_growth - 2.0)
          + 2.0 * diversification + shock(rng) * .35;
      us_export_change = -100.0 * us_trade_drag + .30 * (e.gdp_growth - 1.5)
          + 1.5 * deescalation + shock(rng) * .30;

      gap = .72 * gap + demand - trade_drag + .08 * (e.global_growth - 2.7)
          + shock(rng) * .16;
      const double fx = (e.usdcad - 1.34) * .35;
      inf = .68 * inf + .32 * e.inflation_expectations + .12 * gap + fx
          - supply + .022 * import_price - .018 * (e.oil_price - 75.0) + shock(rng) * .11;
      const double growth = clamp(1.75 + gap - .18 * e.credit_spread
          + coordinated * .24 + shock(rng) * .25, -3.0, 5.5);
      const double us_growth = clamp(e.us_growth + .16 * coordinated + .28 * deescalation
          - .010 * us_tariff - .014 * ca_tariff - .04 * e.border_friction
          + shock(rng) * .18, -3.0, 5.5);
      u = clamp(u - .10 * (growth - 1.7) + shock(rng) * .035, 3.5, 11.0);
      housing = clamp(.78 * housing - 1.15 * (rate - 2.5)
          + .08 * (e.population_growth - 1.2) + shock(rng) * .5, -15.0, 30.0);
      const double relief_cost = targeted_relief + e.tariff_relief;
      debt += (-e.fiscal_balance_gdp + fiscal * .8 + relief_cost * .55
          + .045 * (rate - 2.5) * debt - .18 * growth) / 4.0;
      cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
          + .14 * std::max(0.0, e.wage_growth - growth) + .08 * import_price;
      recession = recession || growth < 0.0;

      rp[q] += rate; ip[q] += inf; gp[q] += growth; ugp[q] += us_growth;
      dp[q] += debt; cp[q] += cost; xp[q] += export_change; uxp[q] += us_export_change;
      if (q == 11) {
        inf_sum += inf; growth_sum += growth; us_growth_sum += us_growth; u_sum += u;
        debt_sum += debt; house_sum += housing; cost_sum += cost;
        income_sum += growth - cost + targeted_relief * .15;
        export_sum += export_change; us_export_sum += us_export_change;
        terminal_debt.push_back(debt); terminal_inflation.push_back(inf);
      }
    }
    if (recession) recessions += 1.0;
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

  const double effective_us_rate = std::max(
      0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double effective_ca_rate = std::max(
      0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation)) / 100.0;
  const double ca_exports = e.canada_exports_to_us_cad
      * std::max(.05, 1.0 - e.trade_elasticity * effective_us_rate);
  const double ca_imports = e.canada_imports_from_us_cad
      * std::max(.05, 1.0 - e.trade_elasticity * effective_ca_rate);

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
  s.debt_stress_p90 = terminal_debt[static_cast<std::size_t>(draws * 9 / 10)];
  s.inflation_stress_p90 = terminal_inflation[static_cast<std::size_t>(draws * 9 / 10)];

  const double mandate_loss = 3.8 * sq(s.inflation - 2.0)
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

  const double canada = std::sqrt(std::max(.01, s.boc_score) * std::max(.01, s.federal_score));
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

  const double canada = std::sqrt(std::max(.01, s.boc_score) * std::max(.01, s.federal_score));
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

struct ScenarioMeta {
  std::string id;
  int candidates_examined = 0;
  int pareto_size = 0;
  int finalists = 0;
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
    const auto us_levels = coverage_levels(e.us_sector_coverage[i]);
    const auto ca_levels = coverage_levels(e.canada_sector_coverage[i]);
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
    recommendation.canada_sector_value[i] =
        normalize_utility(selected.canada, ca_min, ca_max);
    recommendation.us_sector_output[i] =
        normalize_utility(selected.us, us_min, us_max);
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

Result PolicyEngine::evaluate(const Economy& e) const {
  Result r;
  if (e.core_inflation > 3.2) r.regime = "Inflation pressure";
  else if (e.credit_spread > 2.25) r.regime = "Financial stress";
  else if (e.gdp_growth < 0) r.regime = "Contraction";
  else if (e.output_gap < -.5) r.regime = "Below potential";
  else r.regime = "Balanced expansion";

  r.neutral_rate = clamp(2.35 + .16 * (e.productivity_growth - 1.0)
      + .10 * (e.global_growth - 2.7), 1.75, 3.5);
  r.policy_gap = e.policy_rate - r.neutral_rate;
  r.data_confidence = clamp(92.0 - 4.0 * std::abs(e.inflation - e.core_inflation)
      - 2.0 * std::abs(e.output_gap), 70.0, 97.0);

  auto add = [&](std::string id, std::string name, std::string description,
                 double move, double fiscal, double productive, double deescalation,
                 double relief, double diversification) {
    r.scenarios.push_back(simulate(e, std::move(id), std::move(name), std::move(description),
        move, fiscal, productive, deescalation, relief, diversification, seed_, kBaseDraws));
  };

  add("statusquo","Tariff status quo","Current tariffs persist; BoC and fiscal settings hold.",0,0,.5,0,0,0);
  add("retaliate","Symmetric retaliation","Canada matches trade barriers and supports affected demand.",0,.35,.25,0,.25,0);
  add("relief","Worker transition bridge","A measured cut and temporary, targeted tariff adjustment support.",-25,.30,.65,0,.35,.15);
  add("compact","North American compact","Mutual tariff removal, border facilitation and productive Canadian investment.",0,.25,.9,.85,.10,.20);
  add("diversify","Market diversification","Trade infrastructure and export-market diversification with a BoC hold.",0,.35,.9,0,.10,.45);
  add("guardrail","Inflation guardrail","A 25 bp increase and limited retaliation constrain tariff pass-through.",25,-.10,.75,.20,0,.10);
  add("supply","Cost-of-living supply plan","Housing, logistics and productivity investment with targeted household relief.",0,.40,.95,.35,.20,.25);
  add("stabilizer","Automatic stabilizers","Income insurance absorbs the trade shock while monetary policy remains data dependent.",0,.22,.35,0,.30,.08);
  add("eastwest","East-west trade corridor","Ports, rail and interprovincial trade reform accelerate non-U.S. market access.",0,.48,.96,0,.08,.60);
  add("productivity","Productivity compact","Accelerated investment expensing, skills and competition policy lift supply capacity.",0,.32,1.0,.10,.05,.30);
  add("defence","Fiscal consolidation buffer","Spending restraint preserves debt capacity while the Bank cushions demand.",-25,-.22,.70,0,0,.12);
  add("sectoral","Sector-targeted response","Time-limited support protects tariff-exposed workers without broad retaliation.",0,.28,.62,.05,.48,.22);
  add("balance","Balanced market-access compact","Procurement, standards and export-finance measures expand two-way market access without forcing a bilateral accounting target.",0,.45,.95,.70,.08,.55);

  Scenario custom;
  bool have_custom = false;
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
            auto s = simulate(e, "custom", "Custom win-win frontier",
                "Autonomously generated from the policy search.",
                move, fiscal, productive, deescalation, relief, diversification,
                seed_, kBaseDraws);
            ++candidate;
            if (!have_custom || deal_score(s, e, false) > deal_score(custom, e, false)) {
              custom = std::move(s);
              have_custom = true;
            }
          }
  r.candidates_examined = candidate;
  std::ostringstream custom_description;
  custom_description << "Best of " << candidate << " generated policy mixes under fixed delegation priorities: "
      << (custom.first_move_bp < 0 ? "ease monetary policy"
          : custom.first_move_bp > 0 ? "tighten monetary policy" : "hold rates")
      << ", " << std::setprecision(2) << custom.fiscal_impulse
      << "% fiscal impulse, negotiated relief within the cooperation limit, and market diversification.";
  custom.description = custom_description.str();
  r.scenarios.push_back(std::move(custom));

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
  r.allocations_examined = 1;
  r.gdp_floors_examined = 1;

  std::vector<ScenarioMeta> meta;
  meta.reserve(r.scenarios.size());
  for (auto& base : r.scenarios) {
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
      auto verified = simulate(candidate_e, base.id, base.name, base.description,
          base.first_move_bp, base.fiscal_impulse, base.productive_share,
          base.negotiated_relief / 100.0, base.targeted_relief, base.diversification,
          seed_, kBaseDraws);
      verified.sector_verified = true;
      const double raw_score = deal_score(verified, e, false);
      const bool growth_ok = verified.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
      const double rank = (growth_ok ? 0.0 : -1e6) + raw_score;
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
    base = std::move(selected);

    ScenarioMeta m;
    m.id = base.id;
    m.candidates_examined = search.candidates_examined;
    m.pareto_size = search.pareto_frontier_size;
    m.finalists = finalists_run;
    m.sector_ca_score = selected_coverage.canada_score;
    m.sector_us_score = selected_coverage.us_score;
    m.baseline_ca_score = search.baseline_canada_score;
    m.baseline_us_score = search.baseline_us_score;
    meta.push_back(std::move(m));
  }

  for (auto& scenario : r.scenarios) {
    Economy verified_e = e;
    verified_e.us_sector_coverage = scenario.applied_us_sector_coverage;
    verified_e.canada_sector_coverage = scenario.applied_canada_sector_coverage;
    auto verified = simulate(verified_e, scenario.id, scenario.name, scenario.description,
        scenario.first_move_bp, scenario.fiscal_impulse, scenario.productive_share,
        scenario.negotiated_relief / 100.0, scenario.targeted_relief, scenario.diversification,
        seed_, kVerificationDraws);
    verified.sector_verified = true;
    const double raw_score = deal_score(verified, e, false);
    const bool growth_ok = verified.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
    verified.score = (growth_ok ? 0.0 : -1e6) + raw_score;
    scenario = std::move(verified);
  }

  std::sort(r.scenarios.begin(), r.scenarios.end(),
      [](const Scenario& a, const Scenario& b) { return a.score > b.score; });
  const auto& best = r.scenarios.front();
  const auto* best_meta = find_meta(meta, best.id);

  r.recommendation.strategy_id = best.id;
  r.recommendation.us_sector_coverage = best.applied_us_sector_coverage;
  r.recommendation.canada_sector_coverage = best.applied_canada_sector_coverage;
  r.recommendation.verified_canada_score =
      std::sqrt(std::max(.01, best.boc_score) * std::max(.01, best.federal_score));
  r.recommendation.verified_us_score = best.us_score;
  r.recommendation.verified_min_sector_metric = min_sector_metric(best);
  r.recommendation.growth_constraint_met =
      best.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;
  if (best_meta) {
    r.recommendation.sector_candidates_examined = best_meta->candidates_examined;
    r.recommendation.sector_pareto_frontier_size = best_meta->pareto_size;
    r.recommendation.sector_finalists_resimulated = best_meta->finalists;
    r.recommendation.verified_win_win =
        best_meta->sector_ca_score + 1e-9 >= best_meta->baseline_ca_score
        && best_meta->sector_us_score + 1e-9 >= best_meta->baseline_us_score
        && r.recommendation.growth_constraint_met;
  }
  fill_sector_display_metrics(e, best, r.recommendation);

  std::ostringstream recommendation;
  recommendation << "The recommendation keeps the delegation priorities fixed at "
      << std::setprecision(3) << r.recommendation.canada_priority << "% Canada / "
      << r.recommendation.us_priority << "% United States. For each policy strategy, the engine "
      << "builds the non-dominated global frontier of bilateral sector-coverage schedules on a "
      << kSectorGridStep << "-point grid across all 20 sectors, rejects sector packages that make "
      << "either side worse than the starting sector posture, re-simulates up to "
      << kSectorFinalists << " frontier schedules through the macro model, then rechecks every "
      << "sector-optimized strategy with " << kVerificationDraws << " common-random-number draws. "
      << "Canadian and U.S. export channels are independent. Bilateral trade balance is reported "
      << "for diplomatic context but is not rewarded in the welfare objective.";
  r.recommendation.explanation = recommendation.str();

  for (auto& scenario : r.scenarios)
    scenario.sustained_bilateral_growth =
        scenario.bilateral_growth_floor + 1e-9 >= e.minimum_bilateral_growth;

  r.signal = best.first_move_bp > 0 ? "Raise 25 bp"
      : best.first_move_bp < 0 ? "Cut 25 bp" : "Hold & coordinate";
  r.rationale = "The " + best.name
      + " is the highest verified bilateral-welfare package under the fixed mandate, "
        "sector-level Pareto screen, growth constraint and second-stage stochastic verification.";
  return r;
}

std::string to_json(const Result& r) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(3);
  o << "{\"regime\":\"" << esc(r.regime)
    << "\",\"signal\":\"" << esc(r.signal)
    << "\",\"rationale\":\"" << esc(r.rationale)
    << "\",\"confidence\":" << r.data_confidence
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
    << ",\"sectorGridStep\":" << r.recommendation.sector_grid_step
    << ",\"baseMonteCarloDraws\":" << r.recommendation.base_monte_carlo_draws
    << ",\"verificationMonteCarloDraws\":" << r.recommendation.verification_monte_carlo_draws
    << ",\"verifiedCanadaScore\":" << r.recommendation.verified_canada_score
    << ",\"verifiedUsScore\":" << r.recommendation.verified_us_score
    << ",\"verifiedMinSectorMetric\":" << r.recommendation.verified_min_sector_metric
    << ",\"verifiedWinWin\":" << (r.recommendation.verified_win_win ? "true" : "false")
    << ",\"growthConstraintMet\":" << (r.recommendation.growth_constraint_met ? "true" : "false")
    << ",\"independentUsTradeChannel\":" << (r.recommendation.independent_us_trade_channel ? "true" : "false")
    << ",\"tradeBalanceIsObjective\":" << (r.recommendation.trade_balance_is_objective ? "true" : "false")
    << ",\"mandateWeightsFixed\":" << (r.recommendation.mandate_weights_fixed ? "true" : "false")
    << ",\"sectorSearchMethod\":\"" << esc(r.recommendation.sector_search_method)
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
        << ",\"prices\":" << x.us_prices << "}}";
    }
    o << "]}";
  }
  o << "]}";
  return o.str();
}

}  // namespace cad