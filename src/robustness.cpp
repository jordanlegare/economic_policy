#include "policy_engine.hpp"
#include "robustness.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

namespace cad {

Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {
  Result baseline = evaluate(economy);
  auto& summary = baseline.recommendation.robustness;
  summary.parameter_draws = std::max(0, parameter_draws);
  if (summary.parameter_draws == 0 || baseline.scenarios.empty()) {
    summary.classification = "not-evaluated";
    return baseline;
  }

  const std::string baseline_strategy = baseline.recommendation.strategy_id;
  const auto ensemble = draw_structural_parameters(
      parameters_, summary.parameter_draws,
      static_cast<std::uint64_t>(seed_) ^ 0x9e3779b97f4a7c15ULL);

  std::vector<double> selected_scores;
  selected_scores.reserve(ensemble.size());

  for (std::size_t i = 0; i < ensemble.size(); ++i) {
    // Use a distinct but deterministic macro seed for each structural draw.
    // Structural and path uncertainty remain separate because the parameter
    // ensemble is generated before the child engine runs its normal MC layer.
    const std::uint64_t child_seed = static_cast<std::uint64_t>(seed_)
        + 0x517cc1b727220a95ULL * static_cast<std::uint64_t>(i + 1);
    PolicyEngine child(child_seed, ensemble[i]);
    const Result draw = child.evaluate(economy);

    if (draw.recommendation.strategy_id == baseline_strategy)
      ++summary.recommendation_wins;

    const auto it = std::find_if(draw.scenarios.begin(), draw.scenarios.end(),
        [&](const Scenario& scenario) { return scenario.id == baseline_strategy; });
    if (it != draw.scenarios.end()) selected_scores.push_back(it->score);
  }

  summary.recommendation_win_rate = ensemble.empty() ? 0.0
      : static_cast<double>(summary.recommendation_wins)
          / static_cast<double>(ensemble.size());
  if (!selected_scores.empty()) {
    summary.score_mean = std::accumulate(selected_scores.begin(), selected_scores.end(), 0.0)
        / static_cast<double>(selected_scores.size());
    summary.score_p10 = robustness_quantile(selected_scores, 0.10);
    summary.score_p90 = robustness_quantile(selected_scores, 0.90);
  }
  summary.classification = classify_robustness(summary.recommendation_win_rate);
  return baseline;
}

}  // namespace cad
