#pragma once

#include "durable_journal.hpp"
#include "policy_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <list>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cad::evaluation_cache {

#ifndef CAD_SOURCE_REVISION
#define CAD_SOURCE_REVISION "unknown"
#endif

inline constexpr std::size_t kDefaultMemoryEntries = 8;
inline constexpr std::uint64_t kDefaultDiskBudgetBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxCacheFileBytes = 128ull * 1024ull * 1024ull;

inline std::string source_revision() {
  return CAD_SOURCE_REVISION;
}

inline bool source_revision_known() {
  const std::string revision = source_revision();
  return !revision.empty() && revision != "unknown";
}

inline std::filesystem::path default_cache_root() {
#ifdef _WIN32
  if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
    if (*local_app_data)
      return std::filesystem::path(local_app_data) / "CanadaPolicyStudio" / "evaluation-cache";
  }
#endif
  return std::filesystem::path("runtime") / "evaluation-cache";
}

inline std::uint64_t fnv1a64_bytes(std::string_view bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : bytes) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::string hex64(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}

inline void append_array_identity(std::ostringstream& out,
                                  const std::array<double, 20>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out << ',';
    out << values[i];
  }
  out << ']';
}

inline std::string make_key(const Economy& e, const StructuralParameters& p,
                            const std::string& snapshot_id,
                            const std::string& registry_id,
                            bool exhaustive_policy_search) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  out << "cad-result-cache-v1"
      << "|source=" << std::quoted(source_revision())
      << "|snapshot=" << std::quoted(snapshot_id)
      << "|registry=" << std::quoted(registry_id)
      << "|structuralCalibration=" << std::quoted(p.calibration_id)
      << "|structuralVintage=" << std::quoted(p.calibration_vintage)
      << "|mode=" << (exhaustive_policy_search ? 1 : 0);

#define CAD_CACHE_FIELD(name) out << "|" #name "=" << e.name
  CAD_CACHE_FIELD(policy_rate); CAD_CACHE_FIELD(inflation); CAD_CACHE_FIELD(core_inflation);
  CAD_CACHE_FIELD(gdp_growth); CAD_CACHE_FIELD(output_gap); CAD_CACHE_FIELD(unemployment);
  CAD_CACHE_FIELD(wage_growth); CAD_CACHE_FIELD(productivity_growth); CAD_CACHE_FIELD(population_growth);
  CAD_CACHE_FIELD(usdcad); CAD_CACHE_FIELD(oil_price); CAD_CACHE_FIELD(credit_spread);
  CAD_CACHE_FIELD(housing_gap); CAD_CACHE_FIELD(household_debt_income);
  CAD_CACHE_FIELD(fiscal_balance_gdp); CAD_CACHE_FIELD(federal_debt_gdp);
  CAD_CACHE_FIELD(program_growth); CAD_CACHE_FIELD(tax_impulse); CAD_CACHE_FIELD(infrastructure_impulse);
  CAD_CACHE_FIELD(global_growth); CAD_CACHE_FIELD(inflation_expectations);
  CAD_CACHE_FIELD(us_growth); CAD_CACHE_FIELD(us_inflation);
  CAD_CACHE_FIELD(us_tariff_canada); CAD_CACHE_FIELD(canada_retaliatory_tariff);
  CAD_CACHE_FIELD(exports_to_us_share); CAD_CACHE_FIELD(imports_from_us_share);
  CAD_CACHE_FIELD(exports_gdp); CAD_CACHE_FIELD(import_content_consumption);
  CAD_CACHE_FIELD(trade_elasticity); CAD_CACHE_FIELD(border_friction);
  CAD_CACHE_FIELD(tariff_price_pass_through);
  CAD_CACHE_FIELD(canada_exports_to_us_cad); CAD_CACHE_FIELD(canada_imports_from_us_cad);
  CAD_CACHE_FIELD(tariff_relief); CAD_CACHE_FIELD(trade_diversification);
  CAD_CACHE_FIELD(canada_priority); CAD_CACHE_FIELD(us_priority);
  CAD_CACHE_FIELD(risk_aversion); CAD_CACHE_FIELD(cooperation_ceiling);
  CAD_CACHE_FIELD(minimum_bilateral_growth);
#undef CAD_CACHE_FIELD

#define CAD_CACHE_LOSS(name) out << "|loss." #name "=" << e.loss_weights.name
  CAD_CACHE_LOSS(boc_inflation); CAD_CACHE_LOSS(boc_unemployment);
  CAD_CACHE_LOSS(boc_contraction); CAD_CACHE_LOSS(boc_recession);
  CAD_CACHE_LOSS(federal_debt); CAD_CACHE_LOSS(federal_contraction);
  CAD_CACHE_LOSS(federal_unemployment); CAD_CACHE_LOSS(federal_housing);
  CAD_CACHE_LOSS(us_exports); CAD_CACHE_LOSS(us_inflation);
  CAD_CACHE_LOSS(us_growth); CAD_CACHE_LOSS(us_retaliation);
#undef CAD_CACHE_LOSS

#define CAD_CACHE_TUNING(name) out << "|tuning." #name "=" << e.trade_network_tuning.name
  CAD_CACHE_TUNING(supplier_demand_transmission);
  CAD_CACHE_TUNING(input_cost_incidence);
  CAD_CACHE_TUNING(downstream_cost_transmission);
  CAD_CACHE_TUNING(price_cost_pass_through);
  CAD_CACHE_TUNING(output_cost_base);
  CAD_CACHE_TUNING(output_cost_cyclical);
  CAD_CACHE_TUNING(jobs_output_base);
  CAD_CACHE_TUNING(jobs_output_exposure);
#undef CAD_CACHE_TUNING

  out << "|usSectorCoverage="; append_array_identity(out, e.us_sector_coverage);
  out << "|canadaSectorCoverage="; append_array_identity(out, e.canada_sector_coverage);
  out << "|usSectorTradeElasticity="; append_array_identity(out, e.us_sector_trade_elasticity);
  out << "|canadaSectorTradeElasticity="; append_array_identity(out, e.canada_sector_trade_elasticity);
  out << "|usSectorPricePassThrough="; append_array_identity(out, e.us_sector_price_pass_through);
  out << "|canadaSectorPricePassThrough="; append_array_identity(out, e.canada_sector_price_pass_through);

#define CAD_CACHE_PARAMETER(name) out << "|p." #name "=" << p.name
  CAD_CACHE_PARAMETER(neutral_rate); CAD_CACHE_PARAMETER(inflation_target);
  CAD_CACHE_PARAMETER(rate_inflation_response); CAD_CACHE_PARAMETER(rate_output_response);
  CAD_CACHE_PARAMETER(max_quarterly_rate_step); CAD_CACHE_PARAMETER(output_persistence);
  CAD_CACHE_PARAMETER(fiscal_demand_multiplier); CAD_CACHE_PARAMETER(real_rate_demand_sensitivity);
  CAD_CACHE_PARAMETER(productive_supply_multiplier); CAD_CACHE_PARAMETER(global_growth_sensitivity);
  CAD_CACHE_PARAMETER(inflation_persistence); CAD_CACHE_PARAMETER(inflation_expectations_weight);
  CAD_CACHE_PARAMETER(phillips_curve_slope); CAD_CACHE_PARAMETER(fx_pass_through);
  CAD_CACHE_PARAMETER(import_price_pass_through); CAD_CACHE_PARAMETER(oil_inflation_sensitivity);
  CAD_CACHE_PARAMETER(canada_trade_drag_scale); CAD_CACHE_PARAMETER(us_retaliation_drag_scale);
  CAD_CACHE_PARAMETER(tariff_revenue_elasticity_scale);
  CAD_CACHE_PARAMETER(network_supplier_demand_transmission);
  CAD_CACHE_PARAMETER(network_input_cost_incidence);
  CAD_CACHE_PARAMETER(network_downstream_cost_transmission);
  CAD_CACHE_PARAMETER(network_price_cost_pass_through);
  CAD_CACHE_PARAMETER(network_output_cost_base);
  CAD_CACHE_PARAMETER(network_output_cost_cyclical);
  CAD_CACHE_PARAMETER(network_jobs_output_base);
  CAD_CACHE_PARAMETER(network_jobs_output_exposure);
  CAD_CACHE_PARAMETER(output_shock_sd); CAD_CACHE_PARAMETER(inflation_shock_sd);
  CAD_CACHE_PARAMETER(output_inflation_shock_correlation);
  CAD_CACHE_PARAMETER(growth_shock_sd); CAD_CACHE_PARAMETER(us_growth_shock_sd);
  CAD_CACHE_PARAMETER(export_shock_sd); CAD_CACHE_PARAMETER(us_export_shock_sd);
  CAD_CACHE_PARAMETER(shock_tail_threshold); CAD_CACHE_PARAMETER(shock_tail_scale);
  CAD_CACHE_PARAMETER(stress_regime_shock_scale); CAD_CACHE_PARAMETER(uncertainty_scale);
#undef CAD_CACHE_PARAMETER
  return out.str();
}

namespace detail {

class Writer {
 public:
  void u8(std::uint8_t value) { data_.push_back(static_cast<char>(value)); }

  void u64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
      data_.push_back(static_cast<char>((value >> shift) & 0xffu));
  }

  void integer(int value) {
    u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
  }

  void number(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
  }

  void boolean(bool value) { u8(value ? 1u : 0u); }

  void string(const std::string& value) {
    u64(static_cast<std::uint64_t>(value.size()));
    data_.append(value);
  }

  void raw(std::string_view value) { data_.append(value.data(), value.size()); }

  template<std::size_t N>
  void numbers(const std::array<double, N>& values) {
    for (double value : values) number(value);
  }

  const std::string& data() const { return data_; }

 private:
  std::string data_;
};

class Reader {
 public:
  explicit Reader(std::string_view data) : data_(data) {}

  bool raw(std::string_view expected) {
    if (!need(expected.size())) return false;
    if (data_.substr(pos_, expected.size()) != expected) {
      ok_ = false;
      return false;
    }
    pos_ += expected.size();
    return true;
  }

  std::uint8_t u8() {
    if (!need(1)) return 0;
    return static_cast<std::uint8_t>(static_cast<unsigned char>(data_[pos_++]));
  }

  std::uint64_t u64() {
    if (!need(8)) return 0;
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(data_[pos_++])) << shift;
    }
    return value;
  }

  int integer() {
    const std::int64_t value = static_cast<std::int64_t>(u64());
    if (!ok_ || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) {
      ok_ = false;
      return 0;
    }
    return static_cast<int>(value);
  }

  double number() {
    const std::uint64_t bits = u64();
    double value = 0.0;
    if (ok_) std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  bool boolean() {
    const std::uint8_t value = u8();
    if (value > 1) ok_ = false;
    return value == 1;
  }

  std::string string() {
    const std::uint64_t size = u64();
    if (!ok_ || size > 16ull * 1024ull * 1024ull
        || size > static_cast<std::uint64_t>(data_.size() - pos_)) {
      ok_ = false;
      return {};
    }
    std::string out(data_.substr(pos_, static_cast<std::size_t>(size)));
    pos_ += static_cast<std::size_t>(size);
    return out;
  }

  template<std::size_t N>
  void numbers(std::array<double, N>& values) {
    for (double& value : values) value = number();
  }

  bool ok() const { return ok_; }
  bool at_end() const { return ok_ && pos_ == data_.size(); }

 private:
  bool need(std::size_t count) {
    if (!ok_ || count > data_.size() - pos_) {
      ok_ = false;
      return false;
    }
    return true;
  }

  std::string_view data_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

inline void write_sector_impact(Writer& w, const SectorImpact& value) {
  w.string(value.code); w.string(value.name);
#define CAD_WRITE_SECTOR(name) w.number(value.name)
  CAD_WRITE_SECTOR(canada_output); CAD_WRITE_SECTOR(canada_jobs); CAD_WRITE_SECTOR(canada_prices);
  CAD_WRITE_SECTOR(us_output); CAD_WRITE_SECTOR(us_jobs); CAD_WRITE_SECTOR(us_prices);
  CAD_WRITE_SECTOR(exposure); CAD_WRITE_SECTOR(us_applied_tariff);
  CAD_WRITE_SECTOR(canada_applied_tariff); CAD_WRITE_SECTOR(us_buyer_pass_through);
  CAD_WRITE_SECTOR(canada_buyer_pass_through); CAD_WRITE_SECTOR(canada_exporter_absorption);
  CAD_WRITE_SECTOR(us_exporter_absorption); CAD_WRITE_SECTOR(us_importer_absorption);
  CAD_WRITE_SECTOR(canada_importer_absorption); CAD_WRITE_SECTOR(canada_upstream_cost);
  CAD_WRITE_SECTOR(us_upstream_cost);
#undef CAD_WRITE_SECTOR
}

inline void read_sector_impact(Reader& r, SectorImpact& value) {
  value.code = r.string(); value.name = r.string();
#define CAD_READ_SECTOR(name) value.name = r.number()
  CAD_READ_SECTOR(canada_output); CAD_READ_SECTOR(canada_jobs); CAD_READ_SECTOR(canada_prices);
  CAD_READ_SECTOR(us_output); CAD_READ_SECTOR(us_jobs); CAD_READ_SECTOR(us_prices);
  CAD_READ_SECTOR(exposure); CAD_READ_SECTOR(us_applied_tariff);
  CAD_READ_SECTOR(canada_applied_tariff); CAD_READ_SECTOR(us_buyer_pass_through);
  CAD_READ_SECTOR(canada_buyer_pass_through); CAD_READ_SECTOR(canada_exporter_absorption);
  CAD_READ_SECTOR(us_exporter_absorption); CAD_READ_SECTOR(us_importer_absorption);
  CAD_READ_SECTOR(canada_importer_absorption); CAD_READ_SECTOR(canada_upstream_cost);
  CAD_READ_SECTOR(us_upstream_cost);
#undef CAD_READ_SECTOR
}

inline void write_scenario(Writer& w, const Scenario& value) {
  w.string(value.id); w.string(value.name); w.string(value.description);
#define CAD_WRITE_SCENARIO(name) w.number(value.name)
  CAD_WRITE_SCENARIO(first_move_bp); CAD_WRITE_SCENARIO(fiscal_impulse);
  CAD_WRITE_SCENARIO(productive_share); CAD_WRITE_SCENARIO(negotiated_relief);
  CAD_WRITE_SCENARIO(targeted_relief); CAD_WRITE_SCENARIO(diversification);
  CAD_WRITE_SCENARIO(score); CAD_WRITE_SCENARIO(boc_score); CAD_WRITE_SCENARIO(federal_score);
  CAD_WRITE_SCENARIO(canada_score); CAD_WRITE_SCENARIO(us_score);
  CAD_WRITE_SCENARIO(trade_posture_distance);
  w.boolean(value.anchor_win_win);
  CAD_WRITE_SCENARIO(inflation); CAD_WRITE_SCENARIO(growth); CAD_WRITE_SCENARIO(unemployment);
  CAD_WRITE_SCENARIO(us_growth); CAD_WRITE_SCENARIO(bilateral_growth_floor);
  w.boolean(value.sustained_bilateral_growth);
  CAD_WRITE_SCENARIO(debt_gdp); CAD_WRITE_SCENARIO(housing_gap); CAD_WRITE_SCENARIO(recession_risk);
  CAD_WRITE_SCENARIO(cost_of_living); CAD_WRITE_SCENARIO(real_income_growth);
  CAD_WRITE_SCENARIO(export_change); CAD_WRITE_SCENARIO(us_export_change);
  CAD_WRITE_SCENARIO(us_tariff_revenue_usd); CAD_WRITE_SCENARIO(us_tariff_revenue_cad);
  CAD_WRITE_SCENARIO(canada_tariff_revenue_cad); CAD_WRITE_SCENARIO(canada_tariff_revenue_usd);
  CAD_WRITE_SCENARIO(canada_trade_balance_cad); CAD_WRITE_SCENARIO(us_trade_balance_usd);
  CAD_WRITE_SCENARIO(trade_balance_gap_usd); CAD_WRITE_SCENARIO(trade_balance_progress);
  CAD_WRITE_SCENARIO(us_export_expansion_usd); CAD_WRITE_SCENARIO(canada_export_redirection_cad);
  w.boolean(value.zero_trade_deficit);
  CAD_WRITE_SCENARIO(debt_stress_p90); CAD_WRITE_SCENARIO(inflation_stress_p90);
  w.boolean(value.sector_verified);
#undef CAD_WRITE_SCENARIO
  w.numbers(value.applied_us_sector_coverage); w.numbers(value.applied_canada_sector_coverage);
  w.numbers(value.rates); w.numbers(value.inflation_path); w.numbers(value.growth_path);
  w.numbers(value.us_growth_path); w.numbers(value.debt_path); w.numbers(value.cost_path);
  w.numbers(value.export_path); w.numbers(value.us_export_path);
  w.numbers(value.fiscal_path); w.numbers(value.productive_investment_path);
  w.numbers(value.negotiated_relief_path); w.numbers(value.targeted_relief_path);
  w.numbers(value.diversification_path);
  w.u64(static_cast<std::uint64_t>(value.sectors.size()));
  for (const auto& sector : value.sectors) write_sector_impact(w, sector);
}

inline void read_scenario(Reader& r, Scenario& value) {
  value.id = r.string(); value.name = r.string(); value.description = r.string();
#define CAD_READ_SCENARIO(name) value.name = r.number()
  CAD_READ_SCENARIO(first_move_bp); CAD_READ_SCENARIO(fiscal_impulse);
  CAD_READ_SCENARIO(productive_share); CAD_READ_SCENARIO(negotiated_relief);
  CAD_READ_SCENARIO(targeted_relief); CAD_READ_SCENARIO(diversification);
  CAD_READ_SCENARIO(score); CAD_READ_SCENARIO(boc_score); CAD_READ_SCENARIO(federal_score);
  CAD_READ_SCENARIO(canada_score); CAD_READ_SCENARIO(us_score);
  CAD_READ_SCENARIO(trade_posture_distance);
  value.anchor_win_win = r.boolean();
  CAD_READ_SCENARIO(inflation); CAD_READ_SCENARIO(growth); CAD_READ_SCENARIO(unemployment);
  CAD_READ_SCENARIO(us_growth); CAD_READ_SCENARIO(bilateral_growth_floor);
  value.sustained_bilateral_growth = r.boolean();
  CAD_READ_SCENARIO(debt_gdp); CAD_READ_SCENARIO(housing_gap); CAD_READ_SCENARIO(recession_risk);
  CAD_READ_SCENARIO(cost_of_living); CAD_READ_SCENARIO(real_income_growth);
  CAD_READ_SCENARIO(export_change); CAD_READ_SCENARIO(us_export_change);
  CAD_READ_SCENARIO(us_tariff_revenue_usd); CAD_READ_SCENARIO(us_tariff_revenue_cad);
  CAD_READ_SCENARIO(canada_tariff_revenue_cad); CAD_READ_SCENARIO(canada_tariff_revenue_usd);
  CAD_READ_SCENARIO(canada_trade_balance_cad); CAD_READ_SCENARIO(us_trade_balance_usd);
  CAD_READ_SCENARIO(trade_balance_gap_usd); CAD_READ_SCENARIO(trade_balance_progress);
  CAD_READ_SCENARIO(us_export_expansion_usd); CAD_READ_SCENARIO(canada_export_redirection_cad);
  value.zero_trade_deficit = r.boolean();
  CAD_READ_SCENARIO(debt_stress_p90); CAD_READ_SCENARIO(inflation_stress_p90);
  value.sector_verified = r.boolean();
#undef CAD_READ_SCENARIO
  r.numbers(value.applied_us_sector_coverage); r.numbers(value.applied_canada_sector_coverage);
  r.numbers(value.rates); r.numbers(value.inflation_path); r.numbers(value.growth_path);
  r.numbers(value.us_growth_path); r.numbers(value.debt_path); r.numbers(value.cost_path);
  r.numbers(value.export_path); r.numbers(value.us_export_path);
  r.numbers(value.fiscal_path); r.numbers(value.productive_investment_path);
  r.numbers(value.negotiated_relief_path); r.numbers(value.targeted_relief_path);
  r.numbers(value.diversification_path);
  const std::uint64_t sectors = r.u64();
  if (!r.ok() || sectors > 256) return;
  value.sectors.resize(static_cast<std::size_t>(sectors));
  for (auto& sector : value.sectors) read_sector_impact(r, sector);
}

inline void write_robustness(Writer& w, const RobustnessSummary& value) {
  w.integer(value.parameter_draws); w.integer(value.recommendation_wins);
  w.integer(value.strategy_family_wins); w.number(value.recommendation_win_rate);
  w.number(value.strategy_family_win_rate); w.number(value.score_mean);
  w.number(value.score_p10); w.number(value.score_p90);
  w.string(value.classification); w.string(value.calibration_id);
  w.string(value.calibration_vintage); w.string(value.parameter_registry_id);
  w.string(value.methodology); w.string(value.structural_sampling_dependence);
  w.integer(value.sampled_parameter_count); w.integer(value.correlation_pair_count);
  w.boolean(value.correlation_matrix_valid); w.boolean(value.structural_parameters_active);
  w.boolean(value.common_random_numbers); w.boolean(value.sector_packages_reoptimized);
  w.boolean(value.policy_controls_reoptimized); w.boolean(value.parameter_bounds_active);
  w.boolean(value.parameter_provenance_complete);
  w.integer(value.policy_control_candidates_per_draw);
  w.u64(value.policy_control_candidates_examined); w.u64(value.policy_control_changes);
  w.number(value.reference_policy_control_retention_rate);
  w.integer(value.sector_frontiers_built); w.u64(value.nested_sector_optimizations);
  w.u64(value.nested_sector_candidates_examined); w.u64(value.nested_sector_finalists_resimulated);
  w.u64(value.sector_package_changes); w.number(value.reference_package_retention_rate);
}

inline void read_robustness(Reader& r, RobustnessSummary& value) {
  value.parameter_draws = r.integer(); value.recommendation_wins = r.integer();
  value.strategy_family_wins = r.integer(); value.recommendation_win_rate = r.number();
  value.strategy_family_win_rate = r.number(); value.score_mean = r.number();
  value.score_p10 = r.number(); value.score_p90 = r.number();
  value.classification = r.string(); value.calibration_id = r.string();
  value.calibration_vintage = r.string(); value.parameter_registry_id = r.string();
  value.methodology = r.string(); value.structural_sampling_dependence = r.string();
  value.sampled_parameter_count = r.integer(); value.correlation_pair_count = r.integer();
  value.correlation_matrix_valid = r.boolean(); value.structural_parameters_active = r.boolean();
  value.common_random_numbers = r.boolean(); value.sector_packages_reoptimized = r.boolean();
  value.policy_controls_reoptimized = r.boolean(); value.parameter_bounds_active = r.boolean();
  value.parameter_provenance_complete = r.boolean();
  value.policy_control_candidates_per_draw = r.integer();
  value.policy_control_candidates_examined = r.u64(); value.policy_control_changes = r.u64();
  value.reference_policy_control_retention_rate = r.number();
  value.sector_frontiers_built = r.integer(); value.nested_sector_optimizations = r.u64();
  value.nested_sector_candidates_examined = r.u64(); value.nested_sector_finalists_resimulated = r.u64();
  value.sector_package_changes = r.u64(); value.reference_package_retention_rate = r.number();
}

inline void write_recommendation(Writer& w, const WinWinRecommendation& value) {
  w.number(value.canada_priority); w.number(value.us_priority);
  w.number(value.gdp_growth_floor); w.number(value.risk_aversion);
  w.number(value.cooperation_ceiling); w.string(value.strategy_id); w.string(value.explanation);
  w.numbers(value.us_sector_coverage); w.numbers(value.canada_sector_coverage);
  w.numbers(value.us_sector_output); w.numbers(value.canada_sector_value);
  w.integer(value.sector_candidates_examined); w.integer(value.sector_pareto_frontier_size);
  w.integer(value.sector_finalists_resimulated); w.integer(value.policy_candidates_verified);
  w.integer(value.base_monte_carlo_draws); w.integer(value.verification_monte_carlo_draws);
  w.number(value.sector_grid_step); w.number(value.verified_canada_score);
  w.number(value.verified_us_score); w.number(value.baseline_canada_score);
  w.number(value.baseline_us_score); w.number(value.verified_min_sector_metric);
  w.number(value.user_anchor_welfare_tolerance); w.number(value.best_verified_score);
  w.number(value.selected_trade_posture_distance);
  w.boolean(value.user_anchor_selection_active); w.boolean(value.verified_win_win);
  w.boolean(value.global_search_complete); w.boolean(value.growth_constraint_met);
  w.boolean(value.independent_us_trade_channel); w.boolean(value.trade_balance_is_objective);
  w.boolean(value.mandate_weights_fixed); write_robustness(w, value.robustness);
  w.string(value.sector_search_method); w.string(value.trade_network_method);
}

inline void read_recommendation(Reader& r, WinWinRecommendation& value) {
  value.canada_priority = r.number(); value.us_priority = r.number();
  value.gdp_growth_floor = r.number(); value.risk_aversion = r.number();
  value.cooperation_ceiling = r.number(); value.strategy_id = r.string(); value.explanation = r.string();
  r.numbers(value.us_sector_coverage); r.numbers(value.canada_sector_coverage);
  r.numbers(value.us_sector_output); r.numbers(value.canada_sector_value);
  value.sector_candidates_examined = r.integer(); value.sector_pareto_frontier_size = r.integer();
  value.sector_finalists_resimulated = r.integer(); value.policy_candidates_verified = r.integer();
  value.base_monte_carlo_draws = r.integer(); value.verification_monte_carlo_draws = r.integer();
  value.sector_grid_step = r.number(); value.verified_canada_score = r.number();
  value.verified_us_score = r.number(); value.baseline_canada_score = r.number();
  value.baseline_us_score = r.number(); value.verified_min_sector_metric = r.number();
  value.user_anchor_welfare_tolerance = r.number(); value.best_verified_score = r.number();
  value.selected_trade_posture_distance = r.number();
  value.user_anchor_selection_active = r.boolean(); value.verified_win_win = r.boolean();
  value.global_search_complete = r.boolean(); value.growth_constraint_met = r.boolean();
  value.independent_us_trade_channel = r.boolean(); value.trade_balance_is_objective = r.boolean();
  value.mandate_weights_fixed = r.boolean(); read_robustness(r, value.robustness);
  value.sector_search_method = r.string(); value.trade_network_method = r.string();
}

inline void write_result(Writer& w, const Result& value) {
  w.string(value.regime); w.string(value.signal); w.string(value.rationale);
  w.number(value.data_confidence); w.number(value.neutral_rate); w.number(value.policy_gap);
  w.integer(value.candidates_examined); w.integer(value.allocations_examined);
  w.integer(value.gdp_floors_examined); write_recommendation(w, value.recommendation);
  w.u64(static_cast<std::uint64_t>(value.scenarios.size()));
  for (const auto& scenario : value.scenarios) write_scenario(w, scenario);
}

inline bool read_result(Reader& r, Result& value) {
  value.regime = r.string(); value.signal = r.string(); value.rationale = r.string();
  value.data_confidence = r.number(); value.neutral_rate = r.number(); value.policy_gap = r.number();
  value.candidates_examined = r.integer(); value.allocations_examined = r.integer();
  value.gdp_floors_examined = r.integer(); read_recommendation(r, value.recommendation);
  const std::uint64_t scenarios = r.u64();
  if (!r.ok() || scenarios > 4096) return false;
  value.scenarios.resize(static_cast<std::size_t>(scenarios));
  for (auto& scenario : value.scenarios) read_scenario(r, scenario);
  return r.ok();
}

inline std::string serialize_record(const std::string& key, const Result& result) {
  Writer writer;
  writer.raw("CADRSLT1");
  writer.string(key);
  write_result(writer, result);
  const std::uint64_t checksum = fnv1a64_bytes(writer.data());
  Writer checksum_writer;
  checksum_writer.raw(writer.data());
  checksum_writer.u64(checksum);
  return checksum_writer.data();
}

inline std::uint64_t trailing_u64(std::string_view bytes) {
  if (bytes.size() < 8) return 0;
  std::uint64_t value = 0;
  const std::size_t start = bytes.size() - 8;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(
        static_cast<unsigned char>(bytes[start + static_cast<std::size_t>(shift / 8)])) << shift;
  }
  return value;
}

inline bool deserialize_record(std::string_view bytes, const std::string& expected_key,
                               Result& result) {
  if (bytes.size() < 16) return false;
  const std::string_view payload = bytes.substr(0, bytes.size() - 8);
  if (fnv1a64_bytes(payload) != trailing_u64(bytes)) return false;
  Reader reader(payload);
  if (!reader.raw("CADRSLT1")) return false;
  if (reader.string() != expected_key) return false;
  return read_result(reader, result) && reader.at_end();
}

}  // namespace detail

struct Lookup {
  bool hit = false;
  bool persistent = false;
  std::string tier = "compute";
};

struct Stats {
  std::uint64_t memory_hits = 0;
  std::uint64_t disk_hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t coalesced_waits = 0;
  std::uint64_t computes = 0;
  std::uint64_t stores = 0;
  std::uint64_t corrupt_entries = 0;
  std::uint64_t disk_write_failures = 0;
  std::size_t memory_entries = 0;
  bool persistent_enabled = false;
};

class EvaluationResultCache {
 public:
  EvaluationResultCache(std::filesystem::path root = default_cache_root(),
                        std::size_t memory_entries = kDefaultMemoryEntries,
                        std::uint64_t disk_budget_bytes = kDefaultDiskBudgetBytes,
                        bool persistent_enabled = source_revision_known())
      : root_(std::move(root)),
        memory_capacity_(std::max<std::size_t>(1, memory_entries)),
        disk_budget_bytes_(disk_budget_bytes),
        persistent_enabled_(persistent_enabled) {}

  EvaluationResultCache(const EvaluationResultCache&) = delete;
  EvaluationResultCache& operator=(const EvaluationResultCache&) = delete;

  std::optional<Result> get(const std::string& key, Lookup* lookup = nullptr) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto found = memory_.find(key);
      if (found != memory_.end()) {
        lru_.splice(lru_.begin(), lru_, found->second);
        ++stats_.memory_hits;
        if (lookup) {
          lookup->hit = true;
          lookup->persistent = persistent_enabled_;
          lookup->tier = "memory";
        }
        return found->second->result;
      }
    }

    if (persistent_enabled_) {
      Result disk_result;
      const DiskRead disk = read_disk(key, disk_result);
      if (disk == DiskRead::valid) {
        std::lock_guard<std::mutex> lock(mutex_);
        insert_memory_locked(key, disk_result);
        ++stats_.disk_hits;
        if (lookup) {
          lookup->hit = true;
          lookup->persistent = true;
          lookup->tier = "disk";
        }
        return disk_result;
      }
      if (disk == DiskRead::corrupt) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.corrupt_entries;
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.misses;
    }
    if (lookup) {
      lookup->hit = false;
      lookup->persistent = persistent_enabled_;
      lookup->tier = "compute";
    }
    return std::nullopt;
  }

  template<class Function>
  Result get_or_compute(const std::string& key, Function&& function,
                        Lookup* lookup = nullptr) {
    if (auto cached = get(key, lookup)) return std::move(*cached);

    std::shared_future<Result> shared;
    std::shared_ptr<std::promise<Result>> promise;
    bool owner = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto memory_found = memory_.find(key);
      if (memory_found != memory_.end()) {
        lru_.splice(lru_.begin(), lru_, memory_found->second);
        ++stats_.memory_hits;
        if (lookup) {
          lookup->hit = true;
          lookup->persistent = persistent_enabled_;
          lookup->tier = "memory";
        }
        return memory_found->second->result;
      }
      const auto in_flight = inflight_.find(key);
      if (in_flight != inflight_.end()) {
        shared = in_flight->second;
        ++stats_.coalesced_waits;
      } else {
        promise = std::make_shared<std::promise<Result>>();
        shared = promise->get_future().share();
        inflight_.emplace(key, shared);
        ++stats_.computes;
        owner = true;
      }
    }

    if (!owner) {
      Result result = shared.get();
      if (lookup) {
        lookup->hit = true;
        lookup->persistent = persistent_enabled_;
        lookup->tier = "coalesced";
      }
      return result;
    }

    try {
      Result result = function();
      put(key, result);
      promise->set_value(result);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        inflight_.erase(key);
      }
      if (lookup) {
        lookup->hit = false;
        lookup->persistent = persistent_enabled_;
        lookup->tier = "compute";
      }
      return result;
    } catch (...) {
      promise->set_exception(std::current_exception());
      {
        std::lock_guard<std::mutex> lock(mutex_);
        inflight_.erase(key);
      }
      throw;
    }
  }

  void put(const std::string& key, const Result& result) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      insert_memory_locked(key, result);
      ++stats_.stores;
    }
    if (!persistent_enabled_) return;
    if (!write_disk(key, result)) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.disk_write_failures;
    }
  }

  Stats stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats out = stats_;
    out.memory_entries = lru_.size();
    out.persistent_enabled = persistent_enabled_;
    return out;
  }

  bool persistent_enabled() const { return persistent_enabled_; }
  const std::filesystem::path& root() const { return root_; }

 private:
  struct Entry {
    std::string key;
    Result result;
  };

  enum class DiskRead { missing, valid, corrupt };

  std::filesystem::path path_for_key(const std::string& key) const {
    return root_ / (hex64(fnv1a64_bytes(key)) + ".cache");
  }

  void insert_memory_locked(const std::string& key, const Result& result) {
    auto found = memory_.find(key);
    if (found != memory_.end()) {
      found->second->result = result;
      lru_.splice(lru_.begin(), lru_, found->second);
      return;
    }
    lru_.push_front(Entry{key, result});
    memory_[key] = lru_.begin();
    while (lru_.size() > memory_capacity_) {
      auto victim = std::prev(lru_.end());
      memory_.erase(victim->key);
      lru_.erase(victim);
    }
  }

  DiskRead read_disk(const std::string& key, Result& result) {
    try {
      const auto path = path_for_key(key);
      std::ifstream in(path, std::ios::binary | std::ios::ate);
      if (!in) return DiskRead::missing;
      const auto end = in.tellg();
      if (end <= 0 || static_cast<std::uint64_t>(end) > kMaxCacheFileBytes) {
        in.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return DiskRead::corrupt;
      }
      std::string bytes(static_cast<std::size_t>(end), '\0');
      in.seekg(0);
      in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      if (!in || !detail::deserialize_record(bytes, key, result)) {
        in.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return DiskRead::corrupt;
      }
      std::error_code ignored;
      std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
      return DiskRead::valid;
    } catch (...) {
      return DiskRead::missing;
    }
  }

  bool write_disk(const std::string& key, const Result& result) {
    try {
      std::filesystem::create_directories(root_);
      const auto path = path_for_key(key);
      auto temp = path;
      temp += ".tmp";
      const std::string bytes = detail::serialize_record(key, result);
      if (bytes.size() > kMaxCacheFileBytes) return false;

      std::FILE* file = std::fopen(temp.string().c_str(), "wb");
      if (!file) return false;
      const bool wrote = bytes.empty()
          || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
      const bool durable = wrote && durable_journal::flush_to_disk(file);
      const bool closed = std::fclose(file) == 0;
      if (!durable || !closed) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
      }

      std::error_code ignored;
      std::filesystem::remove(path, ignored);
      ignored.clear();
      std::filesystem::rename(temp, path, ignored);
      if (ignored) {
        std::filesystem::remove(temp, ignored);
        return false;
      }
      trim_disk();
      return true;
    } catch (...) {
      return false;
    }
  }

  void trim_disk() {
    if (disk_budget_bytes_ == 0) return;
    struct File {
      std::filesystem::path path;
      std::uint64_t size = 0;
      std::filesystem::file_time_type touched{};
    };
    std::vector<File> files;
    std::uint64_t total = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
      if (error) return;
      if (!entry.is_regular_file(error) || error) continue;
      if (entry.path().extension() != ".cache") continue;
      const auto size = entry.file_size(error);
      if (error) { error.clear(); continue; }
      const auto touched = entry.last_write_time(error);
      if (error) { error.clear(); continue; }
      files.push_back(File{entry.path(), static_cast<std::uint64_t>(size), touched});
      total += static_cast<std::uint64_t>(size);
    }
    if (total <= disk_budget_bytes_) return;
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) {
      return a.touched < b.touched;
    });
    for (const auto& file : files) {
      if (total <= disk_budget_bytes_) break;
      std::filesystem::remove(file.path, error);
      if (!error) total = total > file.size ? total - file.size : 0;
      error.clear();
    }
  }

  std::filesystem::path root_;
  std::size_t memory_capacity_ = kDefaultMemoryEntries;
  std::uint64_t disk_budget_bytes_ = kDefaultDiskBudgetBytes;
  bool persistent_enabled_ = false;
  mutable std::mutex mutex_;
  std::list<Entry> lru_;
  std::unordered_map<std::string, std::list<Entry>::iterator> memory_;
  std::unordered_map<std::string, std::shared_future<Result>> inflight_;
  Stats stats_;
};

}  // namespace cad::evaluation_cache
