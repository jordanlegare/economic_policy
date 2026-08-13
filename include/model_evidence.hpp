#pragma once

#include "backtest.hpp"
#include "backtest_suite.hpp"
#include "policy_engine.hpp"
#include "structural_calibration.hpp"

#include <string>
#include <vector>

namespace cad {

struct ModelEvidenceStatus {
  bool structural_registry_loaded = false;
  bool structural_registry_complete = false;
  int sampled_parameter_count = 0;
  int historical_fixture_count = 0;
  int valid_historical_fixture_count = 0;
  bool historical_aggregate_permitted = false;
  bool state_measurement_contract_complete = false;
  int ready_state_measurement_count = 0;
  bool decision_loss_weights_complete = false;
  int decision_loss_weight_count = 0;
  bool observed_calibration_certified = false;
  double observed_calibration_completeness = 0.0;
  bool canada_io_empirical = false;
  bool us_io_empirical = false;
};

inline std::vector<BacktestResult> run_historical_evidence(
    const PolicyEngine& engine, const std::vector<std::string>& fixture_paths) {
  std::vector<BacktestResult> out;
  out.reserve(fixture_paths.size());
  for (const auto& path : fixture_paths)
    out.push_back(run_backtest(engine, load_backtest_fixture(path)));
  return out;
}

inline ModelEvidenceStatus model_evidence_status(
    const StructuralParameterRegistry& registry,
    const std::vector<BacktestResult>& backtests) {
  const auto suite = summarize_backtests(backtests);
  ModelEvidenceStatus out;
  out.structural_registry_loaded = registry.loaded;
  out.structural_registry_complete = structural_parameter_registry_complete(registry);
  out.sampled_parameter_count = sampled_structural_parameter_count(registry);
  out.historical_fixture_count = suite.fixture_count;
  out.valid_historical_fixture_count = suite.valid_count;
  out.historical_aggregate_permitted = suite.aggregate_diagnostics_permitted;
  return out;
}

std::string model_evidence_status_to_json(const ModelEvidenceStatus& status);
std::string historical_evidence_to_json(const std::vector<BacktestResult>& backtests);

}  // namespace cad
