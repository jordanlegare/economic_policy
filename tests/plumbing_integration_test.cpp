#include "calibration.hpp"
#include "runtime_configuration.hpp"
#include "state_measurement.hpp"
#include "trade_network.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  const auto snapshot = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  assert(snapshot.loaded);
  const auto e = cad::apply_calibration(cad::Economy{}, snapshot);

  // Snapshot -> Economy activation: empirical sector coefficients must not stop
  // at the provenance layer.
  assert(std::abs(e.us_sector_trade_elasticity[0] - 5.705) < 1e-9);
  assert(std::abs(e.us_sector_trade_elasticity[1] - 12.510) < 1e-9);
  assert(std::abs(e.canada_sector_trade_elasticity[4] - 7.167) < 1e-9);
  assert(std::abs(e.canada_sector_price_pass_through[0] - 0.24) < 1e-9);
  assert(std::abs(e.us_sector_price_pass_through[0]) < 1e-12);

  // Economy -> trade network activation: a >5 elasticity must survive runtime
  // admissibility rather than being silently clipped to the old 5.0 ceiling.
  cad::TradeNetworkInput empirical;
  empirical.us_headline_tariff = 5.0;
  empirical.canada_headline_tariff = 1.5;
  empirical.trade_elasticity = e.trade_elasticity;
  empirical.price_pass_through = e.tariff_price_pass_through;
  empirical.us_coverage = e.us_sector_coverage;
  empirical.canada_coverage = e.canada_sector_coverage;
  empirical.us_trade_elasticity = e.us_sector_trade_elasticity;
  empirical.canada_trade_elasticity = e.canada_sector_trade_elasticity;
  empirical.us_price_pass_through = e.us_sector_price_pass_through;
  empirical.canada_price_pass_through = e.canada_sector_price_pass_through;
  auto capped = empirical;
  capped.us_trade_elasticity[1] = 5.0;
  const auto high = cad::evaluate_trade_source(empirical, 1, 100.0, 100.0);
  const auto old_cap = cad::evaluate_trade_source(capped, 1, 100.0, 100.0);
  double high_drag = 0.0, capped_drag = 0.0;
  for (double v : high.canada_output) high_drag += std::abs(v);
  for (double v : old_cap.canada_output) capped_drag += std::abs(v);
  assert(high_drag > capped_drag * 1.5);

  // Model-design weights are executable configuration, not documentation-only.
  const auto losses = cad::load_decision_loss_calibration(
      "data/calibration/decision_loss_weights.csv");
  assert(losses.loaded);
  assert(losses.complete);
  assert(losses.recognized_components == 12);
  assert(cad::decision_loss_sensitivity_contract_complete(losses));
  assert(std::abs(losses.weights.boc_inflation - 3.8) < 1e-12);
  assert(std::abs(losses.weights.federal_debt - 0.32) < 1e-12);
  assert(std::abs(losses.weights.us_inflation - 0.8) < 1e-12);

  // Current-state measurement contract is loaded by production startup.
  const auto states = cad::load_state_measurement_registry(
      "data/calibration/state_measurement_registry.csv");
  assert(cad::state_measurement_contract_complete(states));
  assert(cad::ready_state_measurement_count(states) == 2);

  // The U.S. matrix remains an explicit evidence gap rather than a false claim.
  assert(cad::canada_trade_input_output_empirical());
  assert(!cad::us_trade_input_output_empirical());

  std::cout << "plumbing integration tests passed\n";
  return 0;
}
