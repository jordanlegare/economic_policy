#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace cad::monte_carlo {

inline constexpr std::size_t kQuarterCount = 12;
inline constexpr std::size_t kInnovationsPerQuarter = 8;
inline constexpr double kGpuEquivalenceTolerance = 1e-10;
inline constexpr int kAutoGpuMinimumDraws = 2048;
inline constexpr double kAutoGpuMinimumConcurrentSpeedup = 1.10;
inline constexpr int kSharedInnovationBankMinimumDraws = 700;
inline constexpr int kSharedInnovationBankDraws = 2800;

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
  std::size_t expected_cpu_parallelism = [] {
    const unsigned detected = std::thread::hardware_concurrency();
    return detected == 0 ? std::size_t{4} : static_cast<std::size_t>(detected);
  }();
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

// Immutable shared common-random-number bank. A requested 700-draw view can
// point at the same 2,800-draw storage later used for verification, so policy
// scenarios neither regenerate nor copy identical stochastic innovations.
class InnovationBank {
 public:
  using const_iterator = std::vector<Innovation>::const_iterator;

  InnovationBank() = default;
  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const Innovation& operator[](std::size_t index) const { return (*storage_)[index]; }
  const Innovation* data() const { return storage_ ? storage_->data() : nullptr; }
  const_iterator begin() const { return storage_ ? storage_->begin() : empty_storage().begin(); }
  const_iterator end() const {
    return storage_ ? storage_->begin() + static_cast<std::ptrdiff_t>(count_)
                    : empty_storage().end();
  }

  std::uint64_t identity() const { return identity_; }
  std::size_t storage_size() const { return storage_ ? storage_->size() : 0; }
  const Innovation* storage_data() const { return storage_ ? storage_->data() : nullptr; }
  InnovationBank prefix(int draws) const {
    if (draws <= 0) return {};
    const std::size_t requested = static_cast<std::size_t>(draws) * kQuarterCount;
    if (!storage_ || requested > storage_->size()) return {};
    return InnovationBank(storage_, requested, identity_);
  }

 private:
  static const std::vector<Innovation>& empty_storage() {
    static const std::vector<Innovation> empty;
    return empty;
  }
  InnovationBank(std::shared_ptr<const std::vector<Innovation>> storage,
                 std::size_t count, std::uint64_t identity)
      : storage_(std::move(storage)), count_(count), identity_(identity) {}

  std::shared_ptr<const std::vector<Innovation>> storage_;
  std::size_t count_ = 0;
  std::uint64_t identity_ = 0;

  friend InnovationBank generate_innovations(
      std::uint64_t, int, double, double, double, double, bool);
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
  bool performance_checked = false;
  bool performance_passed = false;
  bool active = false;
  std::string active_backend = "cpu-multicore";
  std::string device_name;
  std::string detail;
  double max_equivalence_error = 0.0;
  double measured_speedup = 0.0;
  double required_speedup = 0.0;
  std::size_t performance_lanes = 0;
  int performance_draws = 0;
  std::size_t max_concurrent_gpu_runs = 0;
  std::size_t pooled_opencl_lanes = 0;
  std::size_t resident_innovation_banks = 0;
  std::uint64_t gpu_runs = 0;
  std::uint64_t cpu_fallback_runs = 0;
  std::uint64_t innovation_bank_hits = 0;
  std::uint64_t innovation_bank_generations = 0;
  std::uint64_t gpu_innovation_uploads = 0;
  std::uint64_t opencl_lane_reuses = 0;
};

InnovationBank generate_innovations(
    std::uint64_t seed, int draws, double output_inflation_correlation,
    double tail_threshold, double tail_scale, double stress_scale,
    bool stress_regime);

BatchResult run_cpu(const Input& input, const InnovationBank& innovations);
BatchResult run(const Input& input, const InnovationBank& innovations);
BackendStatus status();

// Test/diagnostic entrypoint. Production selection still goes through run(),
// which qualifies a device against run_cpu() before promotion.
bool run_opencl_for_equivalence_test(
    const Input& input, const InnovationBank& innovations,
    BatchResult& output, std::string& error);

double maximum_difference(const BatchResult& reference, const BatchResult& candidate);

}  // namespace cad::monte_carlo
