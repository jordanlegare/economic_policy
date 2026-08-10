#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cad {

struct Economy {
  double policy_rate = 2.75, inflation = 2.4, core_inflation = 2.6;
  double gdp_growth = 1.6, output_gap = -0.4, unemployment = 6.4;
  double wage_growth = 3.5, productivity_growth = 0.8, population_growth = 2.1;
  double usdcad = 1.38, oil_price = 74.0, credit_spread = 1.35;
  double housing_gap = 7.0, household_debt_income = 178.0;
  double fiscal_balance_gdp = -1.2, federal_debt_gdp = 42.0;
  double program_growth = 2.0, tax_impulse = 0.0, infrastructure_impulse = 0.3;
  double global_growth = 2.8, inflation_expectations = 2.2;
  // Bilateral trade assumptions. Tariffs are effective, trade-weighted rates.
  double us_growth = 2.0, us_inflation = 2.7;
  // The opening briefing is deliberately a 50% stress baseline. The browser
  // and direct engine users therefore begin from the same tariff assumption.
  double us_tariff_canada = 50.0, canada_retaliatory_tariff = 5.0;
  double exports_to_us_share = 75.0, imports_from_us_share = 49.0;
  double exports_gdp = 25.0, import_content_consumption = 22.0;
  double trade_elasticity = 0.65, border_friction = 2.0;
  // Annual bilateral goods baseline, CAD billions. Explicit values make the
  // resulting fiscal receipts auditable rather than inferred from a score.
  double canada_exports_to_us_cad = 596.9, canada_imports_from_us_cad = 373.7;
  double tariff_relief = 0.0, trade_diversification = 0.0;
  // User decision preferences. These change ranking, not the economic baseline.
  // Neutral bootstrap values are replaced by the opening allocation search.
  // The linked shares always total 100.
  double canada_priority = 50.0, us_priority = 50.0;
  double risk_aversion = 50.0, cooperation_ceiling = 50.0;
  // Sector-specific negotiating positions, expressed as a percentage of the
  // headline tariff applied to each two-digit NAICS sector (0 = exempt).
  std::array<double, 20> us_sector_coverage{}, canada_sector_coverage{};

  Economy() { us_sector_coverage.fill(100.0); canada_sector_coverage.fill(100.0); }
};

// The complete two-digit NAICS economy, grouped into its 20 standard sectors.
// Changes are percentage differences from a no-tariff baseline at quarter 12.
struct SectorImpact {
  std::string code, name;
  double canada_output = 0.0, canada_jobs = 0.0, canada_prices = 0.0;
  double us_output = 0.0, us_jobs = 0.0, us_prices = 0.0;
  double exposure = 0.0;
};

struct Scenario {
  std::string id, name, description;
  double first_move_bp = 0.0, fiscal_impulse = 0.0, productive_share = 0.5;
  double negotiated_relief = 0.0;
  double score = 0.0, boc_score = 0.0, federal_score = 0.0, us_score = 0.0;
  double inflation = 0.0, growth = 0.0, unemployment = 0.0;
  double us_growth = 0.0, bilateral_growth_floor = 0.0;
  bool sustained_bilateral_growth = false;
  double debt_gdp = 0.0, housing_gap = 0.0, recession_risk = 0.0;
  double cost_of_living = 0.0, real_income_growth = 0.0, export_change = 0.0;
  double us_tariff_revenue_usd = 0.0, us_tariff_revenue_cad = 0.0;
  double canada_tariff_revenue_cad = 0.0, canada_tariff_revenue_usd = 0.0;
  double canada_trade_balance_cad = 0.0, us_trade_balance_usd = 0.0;
  double trade_balance_gap_usd = 0.0, trade_balance_progress = 0.0;
  double us_export_expansion_usd = 0.0, canada_export_redirection_cad = 0.0;
  bool zero_trade_deficit = false;
  double debt_stress_p90 = 0.0, inflation_stress_p90 = 0.0;
  std::array<double, 12> rates{}, inflation_path{}, growth_path{}, us_growth_path{}, debt_path{}, cost_path{}, export_path{};
  std::vector<SectorImpact> sectors;
};

struct WinWinRecommendation {
  double canada_priority = 50.0, us_priority = 50.0;
  double risk_aversion = 50.0, cooperation_ceiling = 50.0;
  std::string strategy_id, explanation;
  std::array<double, 20> us_sector_coverage{}, canada_sector_coverage{};
  std::array<double, 20> us_sector_output{}, canada_sector_value{};
};

struct Result {
  std::string regime, signal, rationale;
  double data_confidence = 0.0, neutral_rate = 0.0, policy_gap = 0.0;
  int candidates_examined = 0, allocations_examined = 0;
  WinWinRecommendation recommendation;
  std::vector<Scenario> scenarios;
};

class PolicyEngine {
 public:
  explicit PolicyEngine(std::uint64_t seed = 20260810) : seed_(seed) {}
  Result evaluate(const Economy& economy) const;
 private:
  std::uint64_t seed_;
};

std::string to_json(const Result& result);

}  // namespace cad
