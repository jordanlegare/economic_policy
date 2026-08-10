#include "policy_engine.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
  cad::Economy defaults;
  assert(defaults.us_tariff_canada==50.0);assert(defaults.canada_priority==50.0);
  assert(defaults.us_priority==50.0);assert(defaults.risk_aversion==50.0);assert(defaults.cooperation_ceiling==50.0);
  cad::PolicyEngine engine(42);auto baseline=engine.evaluate(defaults);
  assert(baseline.scenarios.size()==14);assert(baseline.candidates_examined==288);assert(baseline.scenarios.front().score>=baseline.scenarios.back().score);
  assert(!baseline.recommendation.strategy_id.empty());assert(baseline.recommendation.canada_priority>=10&&baseline.recommendation.canada_priority<=100);
  assert(baseline.recommendation.us_priority>0&&baseline.recommendation.us_priority<100);assert(baseline.allocations_examined==101);assert(baseline.gdp_floors_examined==21);assert(baseline.recommendation.gdp_growth_floor>=0&&baseline.recommendation.gdp_growth_floor<=2);assert(std::fmod(baseline.recommendation.gdp_growth_floor*10,1.0)==0);assert(baseline.recommendation.cooperation_ceiling>=0&&baseline.recommendation.cooperation_ceiling<=100);
  assert(baseline.recommendation.canada_priority+baseline.recommendation.us_priority==100.0);
  bool varied=false;for(size_t i=0;i<baseline.recommendation.us_sector_coverage.size();++i){const double c=baseline.recommendation.us_sector_coverage[i];assert(c>=0&&c<=100);assert(baseline.recommendation.canada_sector_coverage[i]>=0&&baseline.recommendation.canada_sector_coverage[i]<=100);if(c<100)varied=true;}assert(varied);
  for(const auto&s:baseline.scenarios){assert(s.inflation>0);assert(s.rates.size()==12);assert(s.us_growth_path.size()==12);assert(s.recession_risk>=0&&s.recession_risk<=100);assert(s.us_score>0);assert(s.debt_stress_p90>=s.debt_gdp);assert(s.sectors.size()==20);}
  const auto& fiscal=baseline.scenarios.front();assert(fiscal.us_tariff_revenue_usd>=0);assert(fiscal.us_tariff_revenue_cad>=fiscal.us_tariff_revenue_usd);
  assert(fiscal.canada_tariff_revenue_cad>=0);assert(std::abs(fiscal.canada_trade_balance_cad/defaults.usdcad+fiscal.us_trade_balance_usd)<1e-9);
  assert(fiscal.zero_trade_deficit);assert(fiscal.trade_balance_gap_usd<0.05);assert(fiscal.us_export_expansion_usd>0);assert(fiscal.canada_export_redirection_cad>0);
  assert(baseline.scenarios.front().sustained_bilateral_growth);assert(baseline.scenarios.front().bilateral_growth_floor+1e-9>=baseline.recommendation.gdp_growth_floor);
  cad::Economy shock;shock.us_tariff_canada=60;shock.canada_retaliatory_tariff=25;auto stressed=engine.evaluate(shock);
  assert(stressed.recommendation.canada_priority+stressed.recommendation.us_priority==100.0);
  auto find=[](const cad::Result&r,const char*id)->const cad::Scenario&{for(const auto&s:r.scenarios)if(s.id==id)return s;assert(false);return r.scenarios[0];};
  assert(find(stressed,"statusquo").export_change<find(baseline,"statusquo").export_change);
  assert(find(stressed,"compact").export_change>find(stressed,"statusquo").export_change);
  assert(find(stressed,"statusquo").sectors[4].canada_output<find(baseline,"statusquo").sectors[4].canada_output);
  cad::Economy exempt=shock;exempt.us_sector_coverage[4]=0;
  auto exempted=engine.evaluate(exempt);
  assert(find(exempted,"statusquo").sectors[4].canada_output>find(stressed,"statusquo").sectors[4].canada_output);
  assert(find(stressed,"custom").description.find("Best of 288")!=std::string::npos);
  cad::Economy canada_first=shock;canada_first.canada_priority=100;canada_first.us_priority=10;
  cad::Economy us_first=shock;us_first.canada_priority=10;us_first.us_priority=100;
  auto ca_result=engine.evaluate(canada_first),us_result=engine.evaluate(us_first);
  assert(ca_result.scenarios.front().name!=us_result.scenarios.front().name||ca_result.scenarios.front().score!=us_result.scenarios.front().score);
  auto json=cad::to_json(stressed);assert(json.find("\"usScore\"")!=std::string::npos);assert(json.find("\"costOfLiving\"")!=std::string::npos);assert(json.find("\"candidatesExamined\":288")!=std::string::npos);assert(json.find("\"allocationsExamined\":101")!=std::string::npos);assert(json.find("\"recommendation\":{")!=std::string::npos);assert(json.find("\"sectors\":[")!=std::string::npos);assert(json.find("Manufacturing")!=std::string::npos);
  assert(json.find("\"usSectorCoverage\":[")!=std::string::npos);assert(json.find("\"usSectorOutput\":[")!=std::string::npos);
  assert(json.find("\"canadaSectorCoverage\":[")!=std::string::npos);assert(json.find("\"canadaSectorValue\":[")!=std::string::npos);
  assert(json.find("\"usGrowthPath\":[")!=std::string::npos);assert(json.find("\"sustainedBilateralGrowth\":true")!=std::string::npos);
  assert(json.find("\"gdpFloorsExamined\":21")!=std::string::npos);assert(json.find("\"gdpGrowthFloor\":")!=std::string::npos);
  assert(json.find("\"usTariffRevenueUsd\":")!=std::string::npos);assert(json.find("\"canadaTariffRevenueCad\":")!=std::string::npos);assert(json.find("\"tradeBalanceGapUsd\":")!=std::string::npos);
  assert(json.find("\"zeroTradeDeficit\":true")!=std::string::npos);assert(json.find("\"usExportExpansionUsd\":")!=std::string::npos);
  std::cout<<"policy engine tests passed\n";
}
