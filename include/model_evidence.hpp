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
  bool empirical_registry_complete = false;
  int statistically_anchored_parameter_count = 0;
  int direct_empirical_mapping_count = 0;
  double statistically_anchored_coverage = 0.0;
  double direct_empirical_mapping_coverage = 0.0;
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
    const std::vector<BacktestResult>& backtests,
    const EmpiricalStructuralEvidenceRegistry& empirical = {}) {
  const auto suite = summarize_backtests(backtests);
  const auto empirical_audit = audit_empirical_structural_evidence(empirical);
  ModelEvidenceStatus out;
  out.structural_registry_loaded = registry.loaded;
  out.structural_registry_complete = structural_parameter_registry_complete(registry);
  out.sampled_parameter_count = sampled_structural_parameter_count(registry);
  out.empirical_registry_loaded = empirical.loaded;
  out.empirical_registry_complete = empirical_structural_evidence_complete(empirical);
  out.statistically_anchored_parameter_count = empirical_audit.statistically_anchored_count;
  out.direct_empirical_mapping_count = empirical_audit.direct_mapping_count;
  out.statistically_anchored_coverage = empirical_audit.statistically_anchored_coverage;
  out.direct_empirical_mapping_coverage = empirical_audit.direct_mapping_coverage;
  out.historical_fixture_count = suite.fixture_count;
  out.valid_historical_fixture_count = suite.valid_count;
  out.historical_aggregate_permitted = suite.aggregate_diagnostics_permitted;
  return out;
}

std::string model_evidence_status_to_json(const ModelEvidenceStatus& status);
std::string historical_evidence_to_json(const std::vector<BacktestResult>& backtests);

}  // namespace cad
