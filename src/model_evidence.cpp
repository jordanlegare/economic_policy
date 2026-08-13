#include "model_evidence.hpp"

#include <iomanip>
#include <sstream>

namespace cad {

std::string model_evidence_status_to_json(const ModelEvidenceStatus& s) {
  std::ostringstream out;
  out << "{\"structuralRegistryLoaded\":" << (s.structural_registry_loaded ? "true" : "false")
      << ",\"structuralRegistryComplete\":" << (s.structural_registry_complete ? "true" : "false")
      << ",\"sampledParameterCount\":" << s.sampled_parameter_count
      << ",\"historicalFixtureCount\":" << s.historical_fixture_count
      << ",\"validHistoricalFixtureCount\":" << s.valid_historical_fixture_count
      << ",\"historicalAggregatePermitted\":"
      << (s.historical_aggregate_permitted ? "true" : "false")
      << ",\"stateMeasurementContractComplete\":"
      << (s.state_measurement_contract_complete ? "true" : "false")
      << ",\"readyStateMeasurementCount\":" << s.ready_state_measurement_count
      << ",\"decisionLossWeightsComplete\":"
      << (s.decision_loss_weights_complete ? "true" : "false")
      << ",\"decisionLossWeightCount\":" << s.decision_loss_weight_count
      << ",\"observedCalibrationCertified\":"
      << (s.observed_calibration_certified ? "true" : "false")
      << ",\"observedCalibrationCompleteness\":"
      << s.observed_calibration_completeness
      << ",\"canadaIoEmpirical\":" << (s.canada_io_empirical ? "true" : "false")
      << ",\"usIoEmpirical\":" << (s.us_io_empirical ? "true" : "false") << "}";
  return out.str();
}

std::string historical_evidence_to_json(const std::vector<BacktestResult>& backtests) {
  const auto suite = summarize_backtests(backtests);
  std::ostringstream out;
  out << "{\"summary\":" << backtest_suite_to_json(suite) << ",\"episodes\":[";
  for (std::size_t i = 0; i < backtests.size(); ++i) {
    if (i) out << ',';
    out << backtest_to_json(backtests[i]);
  }
  out << "]}";
  return out.str();
}

}  // namespace cad
