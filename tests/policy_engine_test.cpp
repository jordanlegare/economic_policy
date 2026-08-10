#include "policy_engine.hpp"
#include <cassert>
#include <iostream>
int main(){cad::PolicyEngine engine(42);auto r=engine.evaluate({});assert(r.scenarios.size()==5);assert(r.scenarios.front().score>=r.scenarios.back().score);for(const auto&s:r.scenarios){assert(s.inflation>0);assert(s.rates.size()==12);assert(s.recession_risk>=0&&s.recession_risk<=100);}auto json=cad::to_json(r);assert(json.find("\"scenarios\"")!=std::string::npos);std::cout<<"policy engine tests passed\n";}
