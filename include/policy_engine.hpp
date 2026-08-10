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
};

struct Scenario {
  std::string id, name, description;
  double first_move_bp = 0.0, fiscal_impulse = 0.0, productive_share = 0.5;
  double score = 0.0, boc_score = 0.0, federal_score = 0.0;
  double inflation = 0.0, growth = 0.0, unemployment = 0.0;
  double debt_gdp = 0.0, housing_gap = 0.0, recession_risk = 0.0;
  std::array<double, 12> rates{}, inflation_path{}, growth_path{}, debt_path{};
};

struct Result {
  std::string regime, signal, rationale;
  double data_confidence = 0.0, neutral_rate = 0.0, policy_gap = 0.0;
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
