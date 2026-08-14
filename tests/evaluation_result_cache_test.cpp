#include "evaluation_result_cache.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

cad::Result sample_result() {
  cad::Result result;
  result.regime = "cache-test";
  result.signal = "hold";
  result.rationale = "round-trip every persisted model artifact";
  result.data_confidence = 91.25;
  result.neutral_rate = 2.45;
  result.policy_gap = 0.30;
  result.candidates_examined = 288;
  result.allocations_examined = 1;
  result.gdp_floors_examined = 1;

  auto& recommendation = result.recommendation;
  recommendation.canada_priority = 55.0;
  recommendation.us_priority = 45.0;
  recommendation.gdp_growth_floor = -0.25;
  recommendation.risk_aversion = 61.0;
  recommendation.cooperation_ceiling = 72.0;
  recommendation.strategy_id = "cache-strategy";
  recommendation.explanation = "cached recommendation";
  recommendation.us_sector_coverage.fill(77.0);
  recommendation.canada_sector_coverage.fill(66.0);
  recommendation.us_sector_output.fill(12.5);
  recommendation.canada_sector_value.fill(13.5);
  recommendation.sector_candidates_examined = 40;
  recommendation.sector_pareto_frontier_size = 5;
  recommendation.sector_finalists_resimulated = 4;
  recommendation.policy_candidates_verified = 301;
  recommendation.base_monte_carlo_draws = 700;
  recommendation.verification_monte_carlo_draws = 2800;
  recommendation.sector_grid_step = 25.0;
  recommendation.verified_canada_score = 71.1;
  recommendation.verified_us_score = 69.2;
  recommendation.baseline_canada_score = 62.3;
  recommendation.baseline_us_score = 61.4;
  recommendation.verified_min_sector_metric = 4.5;
  recommendation.user_anchor_welfare_tolerance = 0.5;
  recommendation.best_verified_score = 74.6;
  recommendation.selected_trade_posture_distance = 3.25;
  recommendation.user_anchor_selection_active = true;
  recommendation.verified_win_win = true;
  recommendation.global_search_complete = true;
  recommendation.growth_constraint_met = true;
  recommendation.independent_us_trade_channel = true;
  recommendation.trade_balance_is_objective = false;
  recommendation.mandate_weights_fixed = true;
  recommendation.sector_search_method = "cache-sector-method";
  recommendation.trade_network_method = "cache-network-method";

  auto& robustness = recommendation.robustness;
  robustness.parameter_draws = 24;
  robustness.recommendation_wins = 19;
  robustness.strategy_family_wins = 21;
  robustness.recommendation_win_rate = 79.2;
  robustness.strategy_family_win_rate = 87.5;
  robustness.score_mean = 73.1;
  robustness.score_p10 = 68.4;
  robustness.score_p90 = 78.2;
  robustness.classification = "robust";
  robustness.calibration_id = "calibration-cache";
  robustness.calibration_vintage = "2026Q3";
  robustness.parameter_registry_id = "registry-cache";
  robustness.methodology = "cache-methodology";
  robustness.structural_sampling_dependence = "correlated";
  robustness.sampled_parameter_count = 37;
  robustness.correlation_pair_count = 4;
  robustness.correlation_matrix_valid = true;
  robustness.structural_parameters_active = true;
  robustness.common_random_numbers = true;
  robustness.sector_packages_reoptimized = true;
  robustness.policy_controls_reoptimized = true;
  robustness.parameter_bounds_active = true;
  robustness.parameter_provenance_complete = true;
  robustness.policy_control_candidates_per_draw = 288;
  robustness.policy_control_candidates_examined = 6912;
  robustness.policy_control_changes = 18;
  robustness.reference_policy_control_retention_rate = 0.25;
  robustness.sector_frontiers_built = 24;
  robustness.nested_sector_optimizations = 24;
  robustness.nested_sector_candidates_examined = 960;
  robustness.nested_sector_finalists_resimulated = 96;
  robustness.sector_package_changes = 11;
  robustness.reference_package_retention_rate = 0.54;

  cad::Scenario scenario;
  scenario.id = "cached-scenario";
  scenario.name = "Cached scenario";
  scenario.description = "scenario persistence contract";
  scenario.first_move_bp = -25.0;
  scenario.fiscal_impulse = 0.35;
  scenario.productive_share = 0.9;
  scenario.negotiated_relief = 67.0;
  scenario.targeted_relief = 0.11;
  scenario.diversification = 0.42;
  scenario.score = 74.0;
  scenario.boc_score = 71.0;
  scenario.federal_score = 72.0;
  scenario.canada_score = 73.0;
  scenario.us_score = 70.0;
  scenario.trade_posture_distance = 2.5;
  scenario.anchor_win_win = true;
  scenario.inflation = 2.1;
  scenario.growth = 1.9;
  scenario.unemployment = 6.1;
  scenario.us_growth = 2.0;
  scenario.bilateral_growth_floor = 0.4;
  scenario.sustained_bilateral_growth = true;
  scenario.debt_gdp = 43.2;
  scenario.housing_gap = 4.1;
  scenario.recession_risk = 12.0;
  scenario.cost_of_living = 2.3;
  scenario.real_income_growth = 1.2;
  scenario.export_change = -3.4;
  scenario.us_export_change = -1.2;
  scenario.us_tariff_revenue_usd = 10.1;
  scenario.us_tariff_revenue_cad = 13.9;
  scenario.canada_tariff_revenue_cad = 4.2;
  scenario.canada_tariff_revenue_usd = 3.1;
  scenario.canada_trade_balance_cad = 8.8;
  scenario.us_trade_balance_usd = -6.3;
  scenario.trade_balance_gap_usd = 6.3;
  scenario.trade_balance_progress = 42.0;
  scenario.us_export_expansion_usd = 5.0;
  scenario.canada_export_redirection_cad = 7.0;
  scenario.zero_trade_deficit = false;
  scenario.debt_stress_p90 = 48.0;
  scenario.inflation_stress_p90 = 3.2;
  scenario.sector_verified = true;
  scenario.applied_us_sector_coverage.fill(75.0);
  scenario.applied_canada_sector_coverage.fill(25.0);
  scenario.rates.fill(2.5);
  scenario.inflation_path.fill(2.2);
  scenario.growth_path.fill(1.8);
  scenario.us_growth_path.fill(2.0);
  scenario.debt_path.fill(43.0);
  scenario.cost_path.fill(2.4);
  scenario.export_path.fill(-2.0);
  scenario.us_export_path.fill(-1.0);
  scenario.fiscal_path.fill(0.3);
  scenario.productive_investment_path.fill(0.2);
  scenario.negotiated_relief_path.fill(50.0);
  scenario.targeted_relief_path.fill(0.1);
  scenario.diversification_path.fill(0.4);

  cad::SectorImpact sector;
  sector.code = "T01";
  sector.name = "Cache sector";
  sector.canada_output = -1.1;
  sector.canada_jobs = -0.8;
  sector.canada_prices = 0.4;
  sector.us_output = -0.5;
  sector.us_jobs = -0.3;
  sector.us_prices = 0.2;
  sector.exposure = 15.0;
  sector.us_applied_tariff = 25.0;
  sector.canada_applied_tariff = 10.0;
  sector.us_buyer_pass_through = 6.0;
  sector.canada_buyer_pass_through = 2.0;
  sector.canada_exporter_absorption = 3.0;
  sector.us_exporter_absorption = 1.0;
  sector.us_importer_absorption = 2.0;
  sector.canada_importer_absorption = 1.0;
  sector.canada_upstream_cost = 0.7;
  sector.us_upstream_cost = 0.6;
  scenario.sectors.push_back(sector);
  result.scenarios.push_back(scenario);
  return result;
}

}  // namespace

int main() {
  namespace cache = cad::evaluation_cache;
  const auto root = std::filesystem::path("runtime") / "test-evaluation-result-cache";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  cad::Economy economy;
  cad::StructuralParameters parameters;
  const std::string key = cache::make_key(
      economy, parameters, "snapshot-A", "registry-A", true);
  auto changed = economy;
  changed.us_tariff_canada += 1.0;
  assert(key != cache::make_key(changed, parameters, "snapshot-A", "registry-A", true));
  assert(key != cache::make_key(economy, parameters, "snapshot-B", "registry-A", true));

  const cad::Result expected = sample_result();
  std::atomic<int> computes{0};
  {
    cache::EvaluationResultCache result_cache(root, 2, 16ull * 1024ull * 1024ull, true);
    cache::Lookup first_lookup;
    const auto first = result_cache.get_or_compute(key, [&] {
      ++computes;
      return expected;
    }, &first_lookup);
    assert(computes.load() == 1);
    assert(!first_lookup.hit);
    assert(cad::to_json(first) == cad::to_json(expected));

    cache::Lookup memory_lookup;
    const auto memory = result_cache.get_or_compute(key, [&] {
      ++computes;
      return cad::Result{};
    }, &memory_lookup);
    assert(computes.load() == 1);
    assert(memory_lookup.hit);
    assert(memory_lookup.tier == "memory");
    assert(cad::to_json(memory) == cad::to_json(expected));
  }

  // A fresh cache object simulates a process restart and must hydrate from SSD.
  {
    cache::EvaluationResultCache restarted(root, 2, 16ull * 1024ull * 1024ull, true);
    cache::Lookup disk_lookup;
    const auto disk = restarted.get_or_compute(key, [&] {
      ++computes;
      return cad::Result{};
    }, &disk_lookup);
    assert(computes.load() == 1);
    assert(disk_lookup.hit);
    assert(disk_lookup.tier == "disk");
    assert(cad::to_json(disk) == cad::to_json(expected));
  }

  // Corruption is a cache miss, never a source of model output.
  const auto disk_path = root / (cache::hex64(cache::fnv1a64_bytes(key)) + ".cache");
  {
    std::ofstream corrupt(disk_path, std::ios::binary | std::ios::trunc);
    corrupt << "corrupt";
  }
  {
    cache::EvaluationResultCache restarted(root, 2, 16ull * 1024ull * 1024ull, true);
    const auto recovered = restarted.get_or_compute(key, [&] {
      ++computes;
      return expected;
    });
    assert(computes.load() == 2);
    assert(cad::to_json(recovered) == cad::to_json(expected));
    assert(restarted.stats().corrupt_entries == 1);
  }

  // Identical concurrent misses collapse into one solve.
  const std::string concurrent_key = key + "|concurrent";
  std::atomic<int> concurrent_computes{0};
  cache::EvaluationResultCache concurrent_cache(root, 2, 16ull * 1024ull * 1024ull, false);
  auto work = [&] {
    return concurrent_cache.get_or_compute(concurrent_key, [&] {
      ++concurrent_computes;
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      return expected;
    });
  };
  cad::Result left;
  cad::Result right;
  std::thread first([&] { left = work(); });
  std::thread second([&] { right = work(); });
  first.join();
  second.join();
  assert(concurrent_computes.load() == 1);
  assert(cad::to_json(left) == cad::to_json(expected));
  assert(cad::to_json(right) == cad::to_json(expected));
  assert(concurrent_cache.stats().coalesced_waits >= 1);

  std::filesystem::remove_all(root, ignored);
  return 0;
}