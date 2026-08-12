#pragma once

#include "backtest.hpp"
#include "backtest_suite.hpp"
#include "empirical_calibration.hpp"
#include "policy_engine.hpp"
#include "structural_calibration.hpp"

#include <string>
#include <vector>

namespace cad {

struct ModelEvidenceStatus {
  bool structural_registry_loaded = false;
  bool structural_registry_complete = false;
  int sampled_parameter_count = 0;
  bool empirical_registry_loaded = false;
  int empirical_evidence_anchored = 0;
  int empirical_estimable_parameters = 23;
  double empirical_evidence_anchor_rate = 0.0;
  double empirical_direct_anchor_rate = 0.0;
  bool empirical_exceeds_quarter_threshold = false;
  int historical_fixture_count = 0;
  int valid_historical_fixture_count = 0;
  bool historical_aggregate_permitted = false;
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
    const EmpiricalCalibrationRegistry& empirical,
    const std::vector<BacktestResult>& backtests) {
  const auto suite = summarize_backtests(backtests);
  const auto empirical_audit = audit_empirical_calibration(registry, empirical);
  ModelEvidenceStatus out;
  out.structural_registry_loaded = registry.loaded;
  out.structural_registry_complete = structural_parameter_registry_complete(registry);
  out.sampled_parameter_count = sampled_structural_parameter_count(registry);
  out.empirical_registry_loaded = empirical.loaded;
  out.empirical_evidence_anchored = empirical_audit.evidence_anchored;
  out.empirical_estimable_parameters = empirical_audit.estimable_parameters;
  out.empirical_evidence_anchor_rate = empirical_audit.evidence_anchor_rate;
  out.empirical_direct_anchor_rate = empirical_audit.direct_anchor_rate;
  out.empirical_exceeds_quarter_threshold = empirical_audit.exceeds_quarter_threshold;
  out.historical_fixture_count = suite.fixture_count;
  out.valid_historical_fixture_count = suite.valid_count;
  out.historical_aggregate_permitted = suite.aggregate_diagnostics_permitted;
  return out;
}

// Backward-compatible status path for callers that have not yet loaded the
// empirical registry. Structural and historical evidence remain available;
// empirical fields stay explicitly unavailable rather than being fabricated.
inline ModelEvidenceStatus model_evidence_status(
    const StructuralParameterRegistry& registry,
    const std::vector<BacktestResult>& backtests) {
  return model_evidence_status(registry, EmpiricalCalibrationRegistry{}, backtests);
}

std::string model_evidence_status_to_json(const ModelEvidenceStatus& status);
std::string historical_evidence_to_json(const std::vector<BacktestResult>& backtests);

}  // namespace cad
