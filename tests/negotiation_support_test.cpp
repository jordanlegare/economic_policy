#include "negotiation_support.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {

cad::Scenario scenario(std::string id, std::string name, double boc, double federal, double us,
                       double relief, double ca_exports, double us_growth) {
  cad::Scenario s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.boc_score = boc;
  s.federal_score = federal;
  s.us_score = us;
  s.negotiated_relief = relief;
  s.export_change = ca_exports;
  s.us_growth = us_growth;
  s.growth = 1.8;
  s.inflation = 2.2;
  s.recession_risk = 12.0;
  return s;
}

}  // namespace

int main() {
  cad::Economy economy;
  economy.cooperation_ceiling = 80.0;
  economy.risk_aversion = 50.0;
  economy.us_tariff_canada = 35.0;
  economy.canada_retaliatory_tariff = 10.0;

  cad::Result result;
  result.scenarios.push_back(scenario("statusquo", "Status quo", 58.0, 61.0, 54.0, 0.0, -6.0, 1.6));
  result.scenarios.push_back(scenario("diversify", "Diversification BATNA", 70.0, 74.0, 61.0, 10.0, -3.5, 1.8));
  result.scenarios.push_back(scenario("compact", "Negotiated compact", 82.0, 80.0, 83.0, 75.0, -1.5, 2.4));

  const auto analysis = cad::analyze_negotiation(economy, result);
  assert(analysis.candidates_examined == 3 * 243);
  assert(analysis.individually_rational_count > 0);
  assert(analysis.pareto_frontier_size > 0);
  assert(!analysis.frontier.empty());
  assert(analysis.canada_reservation >= analysis.canada_batna);
  assert(analysis.us_reservation >= analysis.us_batna);
  assert(analysis.recommended.individually_rational);
  assert(analysis.recommended.pareto_efficient);
  assert(analysis.recommended.issues.size() == 5);

  // The bargaining model carries genuinely separate bilateral trade channels.
  // Under asymmetric tariffs they must not collapse to the same value.
  assert(std::abs(analysis.recommended.canada_export_change
      - analysis.recommended.us_export_change) > 1e-6);

  const auto negotiation_json = cad::negotiation_to_json(analysis);
  assert(negotiation_json.find("\"batna\"") != std::string::npos);
  assert(negotiation_json.find("\"reservation\"") != std::string::npos);
  assert(negotiation_json.find("\"paretoFrontierSize\"") != std::string::npos);
  assert(negotiation_json.find("\"canadaExportChange\"") != std::string::npos);
  assert(negotiation_json.find("\"usExportChange\"") != std::string::npos);

  const auto combined = cad::attach_negotiation_json("{\"policy\":1}", analysis);
  assert(combined.find("\"negotiation\"") != std::string::npos);

  // A zero cooperation ceiling must bind both tariff-relief concessions even
  // though the optimizer may still link implementation or supply-chain issues.
  economy.cooperation_ceiling = 0.0;
  const auto closed = cad::analyze_negotiation(economy, result);
  for (const auto& package : closed.frontier) {
    assert(package.issues.size() >= 2);
    assert(std::abs(package.issues[0].us_move) < 1e-9);
    assert(std::abs(package.issues[1].canada_move) < 1e-9);
  }

  return 0;
}
