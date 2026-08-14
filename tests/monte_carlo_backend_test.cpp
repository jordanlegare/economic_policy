#include "monte_carlo_backend.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

cad::monte_carlo::Input fixture(int draws) {
  cad::monte_carlo::Input in;
  in.draws = draws;
  in.move_bp = -25.0;
  in.productive_share = 0.65;
  in.policy_rate = 2.75;
  in.core_inflation = 2.6;
  in.output_gap = -0.4;
  in.unemployment = 6.4;
  in.federal_debt_gdp = 42.0;
  in.housing_gap = 7.0;
  in.us_growth = 2.0;
  in.gdp_growth = 1.6;
  in.population_growth = 2.1;
  in.credit_spread = 1.35;
  in.fiscal_balance_gdp = -1.2;
  in.wage_growth = 3.5;
  in.headline_inflation = 2.4;
  in.global_growth = 2.8;
  in.inflation_expectations = 2.2;
  in.oil_price = 74.0;
  in.border_friction = 2.0;
  in.fx_pressure = (1.38 - 1.34) * .35;
  in.parameters.global_growth_sensitivity = .08;

  for (std::size_t q = 0; q < cad::monte_carlo::kQuarterCount; ++q) {
    const double t = static_cast<double>(q) / 11.0;
    in.fiscal[q] = .30 * (1.0 - .6 * t);
    in.productive_investment[q] = .16 + .12 * t;
    in.targeted_relief[q] = .20 * (1.0 - .8 * t);
    in.diversification[q] = .04 + .16 * t;
    in.deescalation[q] = .15 + .35 * t;
    in.us_tariff[q] = 35.0 - 12.0 * t;
    in.canada_tariff[q] = 5.0 - 2.0 * t;
    in.trade_drag[q] = .42 - .12 * t;
    in.us_supply_chain_drag[q] = .08 - .02 * t;
    in.import_price[q] = .45 - .10 * t;
    in.supply[q] = .03 + .04 * t;
    in.relief_cost[q] = .16 * (1.0 - .5 * t);
    in.canada_export_quantity_ratio[q] = .95 + .02 * t;
    in.us_export_quantity_ratio[q] = .98 + .01 * t;
  }
  return in;
}

bool same_innovation(const cad::monte_carlo::Innovation& a,
                     const cad::monte_carlo::Innovation& b) {
  return a.export_z == b.export_z
      && a.us_export_z == b.us_export_z
      && a.output_z == b.output_z
      && a.inflation_z == b.inflation_z
      && a.growth_z == b.growth_z
      && a.us_growth_z == b.us_growth_z
      && a.unemployment_z == b.unemployment_z
      && a.housing_z == b.housing_z;
}

}  // namespace

int main() {
  constexpr std::uint64_t seed = 20260810;
  auto input = fixture(8);
  const auto innovations = cad::monte_carlo::generate_innovations(
      seed, input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto repeated = cad::monte_carlo::generate_innovations(
      seed, input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  assert(innovations.size() == static_cast<std::size_t>(input.draws) * 12);
  assert(innovations.size() == repeated.size());
  assert(innovations.identity() == repeated.identity());
  assert(innovations.data() == repeated.data());
  assert(innovations.storage_size() == innovations.size());
  for (std::size_t i = 0; i < innovations.size(); ++i)
    assert(same_innovation(innovations[i], repeated[i]));

  const auto base_bank = cad::monte_carlo::generate_innovations(
      seed + 1, cad::monte_carlo::kSharedInnovationBankMinimumDraws,
      -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto verification_bank = cad::monte_carlo::generate_innovations(
      seed + 1, cad::monte_carlo::kSharedInnovationBankDraws,
      -0.006249264169, 2.0, 1.75, 1.35, true);
  assert(base_bank.identity() == verification_bank.identity());
  assert(base_bank.data() == verification_bank.data());
  assert(base_bank.storage_size()
      == static_cast<std::size_t>(cad::monte_carlo::kSharedInnovationBankDraws) * 12);
  assert(base_bank.size()
      == static_cast<std::size_t>(cad::monte_carlo::kSharedInnovationBankMinimumDraws) * 12);
  assert(cad::monte_carlo::kAutoGpuMinimumDraws
      == cad::monte_carlo::kSharedInnovationBankMinimumDraws);

  const auto before_concurrent = cad::monte_carlo::status();
  constexpr std::size_t concurrent_lanes = 8;
  std::array<cad::monte_carlo::InnovationBank, concurrent_lanes> concurrent_banks{};
  std::atomic<std::size_t> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> workers;
  workers.reserve(concurrent_lanes);
  for (std::size_t lane = 0; lane < concurrent_lanes; ++lane) {
    workers.emplace_back([&, lane] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      const int draws = lane % 2 == 0
          ? cad::monte_carlo::kSharedInnovationBankMinimumDraws
          : cad::monte_carlo::kSharedInnovationBankDraws;
      concurrent_banks[lane] = cad::monte_carlo::generate_innovations(
          seed + 2, draws, -0.006249264169, 2.0, 1.75, 1.35, true);
    });
  }
  while (ready.load(std::memory_order_acquire) < concurrent_lanes)
    std::this_thread::yield();
  go.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();
  for (std::size_t lane = 1; lane < concurrent_lanes; ++lane) {
    assert(concurrent_banks[lane].identity() == concurrent_banks[0].identity());
    assert(concurrent_banks[lane].data() == concurrent_banks[0].data());
    assert(concurrent_banks[lane].storage_size() == concurrent_banks[0].storage_size());
  }
  const auto after_concurrent = cad::monte_carlo::status();
  assert(after_concurrent.innovation_bank_generations
      == before_concurrent.innovation_bank_generations + 1);
  assert(after_concurrent.innovation_bank_hits
      >= before_concurrent.innovation_bank_hits + concurrent_lanes - 1);

  const auto first = cad::monte_carlo::run_cpu(input, innovations);
  const auto second = cad::monte_carlo::run_cpu(input, innovations);
  assert(first.backend == "cpu");
  assert(!first.aggregate_encoded);
  assert(first.draws.size() == static_cast<std::size_t>(input.draws));
  assert(cad::monte_carlo::maximum_difference(first, second) == 0.0);
  assert(cad::monte_carlo::maximum_aggregate_difference(first, second) == 0.0);

  for (const auto& draw : first.draws) {
    assert(std::isfinite(draw.terminal_inflation));
    assert(std::isfinite(draw.terminal_growth));
    assert(std::isfinite(draw.terminal_debt));
    assert(draw.terminal_unemployment >= 3.5 && draw.terminal_unemployment <= 11.0);
    assert(draw.terminal_housing >= -15.0 && draw.terminal_housing <= 30.0);
    for (double rate : draw.rates) assert(rate >= 0.0 && rate <= 8.0);
  }

  auto changed = input;
  changed.move_bp = 25.0;
  const auto changed_result = cad::monte_carlo::run_cpu(changed, innovations);
  assert(cad::monte_carlo::maximum_difference(first, changed_result) > 0.0);
  assert(cad::monte_carlo::maximum_aggregate_difference(first, changed_result) > 0.0);

  const auto backend_status = cad::monte_carlo::status();
  assert(backend_status.innovation_bank_generations >= 1);
  assert(backend_status.innovation_bank_hits >= 2);

  std::cout << "Monte Carlo backend contract tests passed\n";
  return 0;
}
