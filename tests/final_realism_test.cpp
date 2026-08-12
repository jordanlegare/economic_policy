#include "policy_dynamics.hpp"
#include "policy_engine.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

const cad::Scenario& scenario(const cad::Result& result, const std::string& id) {
  for (const auto& s : result.scenarios) if (s.id == id) return s;
  assert(false && "scenario not found");
  return result.scenarios.front();
}

}  // namespace

int main() {
  const auto path = cad::build_policy_implementation_paths(1.0, 0.6, 40.0, 0.5, 0.2);
  assert(std::abs(path.fiscal[0] - 0.65) < 1e-12);
  assert(std::abs(path.fiscal[2] - 1.0) < 1e-12);
  assert(path.fiscal[11] < path.fiscal[4]);
  assert(std::abs(path.productive_investment[0] - 0.12) < 1e-12);
  assert(std::abs(path.productive_investment[4] - 0.60) < 1e-12);
  assert(std::abs(path.negotiated_relief[0] - 14.0) < 1e-12);
  assert(std::abs(path.negotiated_relief[3] - 40.0) < 1e-12);
  assert(path.targeted_relief[11] < path.targeted_relief[1]);
  assert(path.diversification[0] < path.diversification[8]);
  assert(std::abs(path.diversification[8] - 0.2) < 1e-12);

  cad::StructuralParameters p;
  p.shock_tail_threshold = 2.0;
  p.shock_tail_scale = 1.75;
  p.stress_regime_shock_scale = 1.35;
  assert(std::abs(cad::regime_tail_innovation(1.5, p, false) - 1.5) < 1e-12);
  assert(std::abs(cad::regime_tail_innovation(2.1, p, false) - 2.1 * 1.75) < 1e-12);
  assert(std::abs(cad::regime_tail_innovation(-2.1, p, true)
      - (-2.1 * 1.75 * 1.35)) < 1e-12);

  cad::Economy normal;
  normal.us_tariff_canada = 5.0;
  normal.canada_retaliatory_tariff = 1.5;
  normal.credit_spread = 1.3;
  normal.gdp_growth = 1.0;
  assert(!cad::macro_stress_regime(normal));
  normal.us_tariff_canada = 50.0;
  assert(cad::macro_stress_regime(normal));

  // Production evaluation must expose the deterministic implementation paths.
  cad::Economy economy;
  economy.exhaustive_policy_search = false;
  cad::PolicyEngine engine(20260810);
  const auto base = engine.evaluate(economy);
  const auto& status = scenario(base, "statusquo");
  assert(std::abs(status.fiscal_path[0] - .65 * status.fiscal_impulse) < 1e-12);
  assert(std::abs(status.productive_investment_path[4]
      - status.fiscal_impulse * status.productive_share) < 1e-12);
  assert(std::abs(status.negotiated_relief_path[3] - status.negotiated_relief) < 1e-12);
  assert(std::abs(status.diversification_path[8] - status.diversification) < 1e-12);

  const auto json = cad::to_json(base);
  assert(json.find("\"fiscalPath\":[") != std::string::npos);
  assert(json.find("\"productiveInvestmentPath\":[") != std::string::npos);
  assert(json.find("\"negotiatedReliefPath\":[") != std::string::npos);
  assert(json.find("\"targetedReliefPath\":[") != std::string::npos);
  assert(json.find("\"diversificationPath\":[") != std::string::npos);

  // Internal decision coefficients are typed and sensitivity-visible, but the
  // institutional mandate flag remains fixed because the optimizer never tunes
  // them as policy controls.
  cad::Economy reweighted = economy;
  reweighted.loss_weights.boc_inflation *= 1.5;
  const auto weighted = engine.evaluate(reweighted);
  const auto& base_status = scenario(base, "statusquo");
  const auto& weighted_status = scenario(weighted, "statusquo");
  assert(std::abs(base_status.boc_score - weighted_status.boc_score) > 1e-8);
  assert(base.recommendation.mandate_weights_fixed);
  assert(weighted.recommendation.mandate_weights_fixed);
  return 0;
}
