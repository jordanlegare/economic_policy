#include "policy_engine.hpp"
#include <cassert>
#include <iostream>
int main(){
  cad::PolicyEngine engine(42);auto baseline=engine.evaluate({});
  assert(baseline.scenarios.size()==7);assert(baseline.scenarios.front().score>=baseline.scenarios.back().score);
  for(const auto&s:baseline.scenarios){assert(s.inflation>0);assert(s.rates.size()==12);assert(s.recession_risk>=0&&s.recession_risk<=100);assert(s.us_score>0);assert(s.debt_stress_p90>=s.debt_gdp);}
  cad::Economy shock;shock.us_tariff_canada=35;shock.canada_retaliatory_tariff=25;auto stressed=engine.evaluate(shock);
  auto find=[](const cad::Result&r,const char*id)->const cad::Scenario&{for(const auto&s:r.scenarios)if(s.id==id)return s;assert(false);return r.scenarios[0];};
  assert(find(stressed,"statusquo").export_change<find(baseline,"statusquo").export_change);
  assert(find(stressed,"compact").export_change>find(stressed,"statusquo").export_change);
  auto json=cad::to_json(stressed);assert(json.find("\"usScore\"")!=std::string::npos);assert(json.find("\"costOfLiving\"")!=std::string::npos);
  std::cout<<"policy engine tests passed\n";
}
