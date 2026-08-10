#include "policy_engine.hpp"
#include <cassert>
#include <iostream>
int main(){
  cad::PolicyEngine engine(42);auto baseline=engine.evaluate({});
  assert(baseline.scenarios.size()==13);assert(baseline.candidates_examined==144);assert(baseline.scenarios.front().score>=baseline.scenarios.back().score);
  for(const auto&s:baseline.scenarios){assert(s.inflation>0);assert(s.rates.size()==12);assert(s.recession_risk>=0&&s.recession_risk<=100);assert(s.us_score>0);assert(s.debt_stress_p90>=s.debt_gdp);}
  cad::Economy shock;shock.us_tariff_canada=35;shock.canada_retaliatory_tariff=25;auto stressed=engine.evaluate(shock);
  auto find=[](const cad::Result&r,const char*id)->const cad::Scenario&{for(const auto&s:r.scenarios)if(s.id==id)return s;assert(false);return r.scenarios[0];};
  assert(find(stressed,"statusquo").export_change<find(baseline,"statusquo").export_change);
  assert(find(stressed,"compact").export_change>find(stressed,"statusquo").export_change);
  assert(find(stressed,"custom").description.find("Best of 144")!=std::string::npos);
  cad::Economy canada_first=shock;canada_first.canada_priority=100;canada_first.us_priority=10;
  cad::Economy us_first=shock;us_first.canada_priority=10;us_first.us_priority=100;
  auto ca_result=engine.evaluate(canada_first),us_result=engine.evaluate(us_first);
  assert(ca_result.scenarios.front().name!=us_result.scenarios.front().name||ca_result.scenarios.front().score!=us_result.scenarios.front().score);
  auto json=cad::to_json(stressed);assert(json.find("\"usScore\"")!=std::string::npos);assert(json.find("\"costOfLiving\"")!=std::string::npos);assert(json.find("\"candidatesExamined\":144")!=std::string::npos);
  std::cout<<"policy engine tests passed\n";
}
