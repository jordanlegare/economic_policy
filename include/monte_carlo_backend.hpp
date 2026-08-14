#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cad::monte_carlo {

inline constexpr std::size_t kQuarterCount = 12;
inline constexpr std::size_t kInnovationsPerQuarter = 8;
inline constexpr double kGpuEquivalenceTolerance = 1e-10;

struct StructuralInputs {
  double neutral_rate = 2.5;
  double inflation_target = 2.0;
  double rate_inflation_response = 0.75;
  double rate_output_response = 0.25;
  double max_quarterly_rate_step = 0.25;
  double output_persistence = 0.72;
  double fiscal_demand_multiplier = 0.36;
  double real_rate_demand_sensitivity = 0.18;
  double global_growth_sensitivity = 0.08;
  double inflation_persistence = 0.68;
  double inflation_expectations_weight = 0.32;
  double phillips_curve_slope = 0.12;
  double import_price_pass_through = 0.022;
  double oil_inflation_sensitivity = 0.018;
  double output_shock_sd = 0.16;
  double inflation_shock_sd = 0.11;
  double growth_shock_sd = 0.25;
  double us_growth_shock_sd = 0.18;
  double export_shock_sd = 0.35;
  double us_export_shock_sd = 0.30;
};

struct Input {
  int draws = 0;
  double move_bp = 0.0;
  double productive_share = 0.0;

  double policy_rate = 0.0;
  double core_inflation = 0.0;
  double output_gap = 0.0;
  double unemployment = 0.0;
  double federal_debt_gdp = 0.0;
  double housing_gap = 0.0;
  double us_growth = 0.0;
  double gdp_growth = 0.0;
  double population_growth = 0.0;
  double credit_spread = 0.0;
  double fiscal_balance_gdp = 0.0;
  double wage_growth = 0.0;
  double headline_inflation = 0.0;
  double global_growth = 0.0;
  double inflation_expectations = 0.0;
  double oil_price = 0.0;
  double border_friction = 0.0;
  double fx_pressure = 0.0;

  StructuralInputs parameters{};

  std::array<double, kQuarterCount> fiscal{};
  std::array<double, kQuarterCount> productive_investment{};
  std::array<double, kQuarterCount> targeted_relief{};
  std::array<double, kQuarterCount> diversification{};
  std::array<double, kQuarterCount> deescalation{};
  std::array<double, kQuarterCount> us_tariff{};
  std::array<double, kQuarterCount> canada_tariff{};
  std::array<double, kQuarterCount> trade_drag{};
  std::array<double, kQuarterCount> us_supply_chain_drag{};
  std::array<double, kQuarterCount> import_price{};
  std::array<double, kQuarterCount> supply{};
  std::array<double, kQuarterCount> relief_cost{};
  std::array<double, kQuarterCount> canada_export_quantity_ratio{};
  std::array<double, kQuarterCount> us_export_quantity_ratio{};
};

struct Innovation {
  double export_z = 0.0;
  double us_export_z = 0.0;
  double output_z = 0.0;
  double inflation_z = 0.0;
  double growth_z = 0.0;
  double us_growth_z = 0.0;
  double unemployment_z = 0.0;
  double housing_z = 0.0;
};

struct DrawResult {
  std::array<double, kQuarterCount> rates{};
  std::array<double, kQuarterCount> inflation{};
  std::array<double, kQuarterCount> growth{};
  std::array<double, kQuarterCount> us_growth{};
  std::array<double, kQuarterCount> debt{};
  std::array<double, kQuarterCount> cost{};
  std::array<double, kQuarterCount> exports{};
  std::array<double, kQuarterCount> us_exports{};

  double terminal_inflation = 0.0;
  double terminal_growth = 0.0;
  double terminal_us_growth = 0.0;
  double terminal_unemployment = 0.0;
  double terminal_debt = 0.0;
  double terminal_housing = 0.0;
  double terminal_cost = 0.0;
  double terminal_income = 0.0;
  double terminal_exports = 0.0;
  double terminal_us_exports = 0.0;
  bool recession = false;
};

struct BatchResult {
  std::vector<DrawResult> draws;
  std::string backend = "cpu";
};

struct BackendStatus {
  bool opencl_library_present = false;
  bool device_present = false;
  bool fp64_supported = false;
  bool equivalence_checked = false;
  bool equivalence_passed = false;
  bool active = false;
  std::string active_backend = "cpu-multicore";
  std::string device_name;
  std::string detail;
  double max_equivalence_error = 0.0;
  std::uint64_t gpu_runs = 0;
  std::uint64_t cpu_fallback_runs = 0;
};

std::vector<Innovation> generate_innovations(
    std::uint64_t seed, int draws, double output_inflation_correlation,
    double tail_threshold, double tail_scale, double stress_scale,
    bool stress_regime);

BatchResult run_cpu(const Input& input, const std::vector<Innovation>& innovations);
BatchResult run(const Input& input, const std::vector<Innovation>& innovations);
BackendStatus status();

// Test/diagnostic entrypoint. Production selection still goes through run(),
// which qualifies a device against run_cpu() before promotion.
bool run_opencl_for_equivalence_test(
    const Input& input, const std::vector<Innovation>& innovations,
    BatchResult& output, std::string& error);

double maximum_difference(const BatchResult& reference, const BatchResult& candidate);

}  // namespace cad::monte_carlo
