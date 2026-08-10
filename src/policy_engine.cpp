#include "policy_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>

namespace cad {
namespace {
double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
double sq(double x) { return x * x; }

Scenario simulate(const Economy& e, std::string id, std::string name, std::string description,
                  double move, double fiscal, double productive, std::uint64_t seed) {
  Scenario s{std::move(id), std::move(name), std::move(description)};
  s.first_move_bp = move; s.fiscal_impulse = fiscal; s.productive_share = productive;
  constexpr int draws = 700;
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
  double inf_sum=0, growth_sum=0, u_sum=0, debt_sum=0, house_sum=0, recessions=0;
  std::array<double,12> rp{}, ip{}, gp{}, dp{};
  for (int d=0; d<draws; ++d) {
    double rate=e.policy_rate, inf=e.core_inflation, gap=e.output_gap, u=e.unemployment;
    double debt=e.federal_debt_gdp, housing=e.housing_gap;
    bool recession=false;
    for (int q=0; q<12; ++q) {
      const double coordinated = productive * fiscal;
      const double rate_target = clamp(2.5 + 0.75*(inf-2.0) + 0.25*gap, 0.25, 7.0);
      if (q==0) rate=clamp(rate+move/100.0, 0.0, 8.0);
      else rate=clamp(rate+clamp(rate_target-rate,-0.25,0.25),0.0,8.0);
      const double demand = fiscal*(1.0-productive)*0.36 - (rate-2.5)*0.18;
      const double supply = coordinated*0.22 + e.productivity_growth*0.035;
      gap = 0.72*gap + demand + 0.08*(e.global_growth-2.7) + shock(rng)*0.16;
      const double fx = (e.usdcad-1.34)*0.35;
      inf = 0.68*inf + 0.32*e.inflation_expectations + 0.12*gap + fx
          - supply - 0.018*(e.oil_price-75.0) + shock(rng)*0.11;
      const double growth = clamp(1.75 + gap - 0.18*e.credit_spread + coordinated*0.24 + shock(rng)*0.25,-4.0,5.5);
      u=clamp(u-0.10*(growth-1.7)+shock(rng)*0.035,3.5,11.0);
      housing=clamp(0.78*housing - 1.15*(rate-2.5) + 0.08*(e.population_growth-1.2) + shock(rng)*0.5,-15,30);
      debt += (-(e.fiscal_balance_gdp) + fiscal*0.8 + 0.045*(rate-2.5)*debt - 0.18*growth)/4.0;
      recession = recession || growth < 0.0;
      rp[q]+=rate; ip[q]+=inf; gp[q]+=growth; dp[q]+=debt;
      if(q==11){ inf_sum+=inf; growth_sum+=growth; u_sum+=u; debt_sum+=debt; house_sum+=housing; }
    }
    if(recession) recessions++;
  }
  for(int q=0;q<12;++q){s.rates[q]=rp[q]/draws;s.inflation_path[q]=ip[q]/draws;s.growth_path[q]=gp[q]/draws;s.debt_path[q]=dp[q]/draws;}
  s.inflation=inf_sum/draws;s.growth=growth_sum/draws;s.unemployment=u_sum/draws;
  s.debt_gdp=debt_sum/draws;s.housing_gap=house_sum/draws;s.recession_risk=100.0*recessions/draws;
  const double mandate_loss=3.8*sq(s.inflation-2.0)+1.2*sq(std::max(0.0,s.unemployment-5.8))
      +0.7*sq(std::min(0.0,s.growth))+0.018*s.recession_risk;
  const double federal_loss=0.32*sq(std::max(0.0,s.debt_gdp-e.federal_debt_gdp))+0.7*sq(std::min(0.0,s.growth))
      +0.8*sq(std::max(0.0,s.unemployment-6.0))+0.012*sq(s.housing_gap);
  s.boc_score=100.0/(1.0+mandate_loss);s.federal_score=100.0/(1.0+federal_loss);
  // Nash-style balance rewards joint gains and penalizes one-sided outcomes.
  s.score=std::sqrt(s.boc_score*s.federal_score)-0.28*std::abs(s.boc_score-s.federal_score);
  return s;
}

std::string esc(const std::string& x){std::string o;for(char c:x){if(c=='"'||c=='\\')o+='\\';o+=c;}return o;}
template<size_t N> void array_json(std::ostringstream& o,const std::array<double,N>& a){o<<'[';for(size_t i=0;i<N;++i){if(i)o<<',';o<<a[i];}o<<']';}
}

Result PolicyEngine::evaluate(const Economy& e) const {
  Result r;
  if(e.core_inflation>3.2) r.regime="Inflation pressure";
  else if(e.credit_spread>2.25) r.regime="Financial stress";
  else if(e.gdp_growth<0) r.regime="Contraction";
  else if(e.output_gap<-.5) r.regime="Below potential";
  else r.regime="Balanced expansion";
  r.neutral_rate=clamp(2.35+0.16*(e.productivity_growth-1.0)+0.10*(e.global_growth-2.7),1.75,3.5);
  r.policy_gap=e.policy_rate-r.neutral_rate;
  r.data_confidence=clamp(92.0-4.0*std::abs(e.inflation-e.core_inflation)-2.0*std::abs(e.output_gap),70,97);
  r.scenarios.push_back(simulate(e,"hold","Independent hold","BoC holds initially; fiscal settings remain unchanged.",0,0,.5,seed_+1));
  r.scenarios.push_back(simulate(e,"tighten","Inflation guardrail","A 25 bp increase paired with neutral federal demand.",25,0,.5,seed_+2));
  r.scenarios.push_back(simulate(e,"relief","Broad-based relief","A 25 bp cut combined with temporary consumption support.",-25,.55,.20,seed_+3));
  r.scenarios.push_back(simulate(e,"winwin","Productivity compact","A measured rate path plus fiscally bounded housing, infrastructure and productivity investment.",0,.35,.90,seed_+4));
  r.scenarios.push_back(simulate(e,"consolidate","Disinflation compact","A hold paired with gradual program restraint and protected capital investment.",0,-.30,.80,seed_+5));
  std::sort(r.scenarios.begin(),r.scenarios.end(),[](const auto&a,const auto&b){return a.score>b.score;});
  const auto& best=r.scenarios.front();
  r.signal = best.first_move_bp>0 ? "Raise 25 bp" : best.first_move_bp<0 ? "Cut 25 bp" : "Hold & coordinate";
  r.rationale="The "+best.name+" has the strongest balanced outcome: it minimizes the larger institutional trade-offs rather than optimizing either government in isolation.";
  return r;
}

std::string to_json(const Result& r){
  std::ostringstream o;o<<std::fixed<<std::setprecision(3);
  o<<"{\"regime\":\""<<esc(r.regime)<<"\",\"signal\":\""<<esc(r.signal)<<"\",\"rationale\":\""<<esc(r.rationale)
   <<"\",\"confidence\":"<<r.data_confidence<<",\"neutralRate\":"<<r.neutral_rate<<",\"policyGap\":"<<r.policy_gap<<",\"scenarios\":[";
  for(size_t i=0;i<r.scenarios.size();++i){if(i)o<<',';const auto&s=r.scenarios[i];
    o<<"{\"id\":\""<<s.id<<"\",\"name\":\""<<esc(s.name)<<"\",\"description\":\""<<esc(s.description)
     <<"\",\"move\":"<<s.first_move_bp<<",\"fiscal\":"<<s.fiscal_impulse<<",\"score\":"<<s.score<<",\"bocScore\":"<<s.boc_score
     <<",\"federalScore\":"<<s.federal_score<<",\"inflation\":"<<s.inflation<<",\"growth\":"<<s.growth<<",\"unemployment\":"<<s.unemployment
     <<",\"debt\":"<<s.debt_gdp<<",\"housing\":"<<s.housing_gap<<",\"recessionRisk\":"<<s.recession_risk<<",\"rates\":";array_json(o,s.rates);
    o<<",\"inflationPath\":";array_json(o,s.inflation_path);o<<",\"growthPath\":";array_json(o,s.growth_path);o<<",\"debtPath\":";array_json(o,s.debt_path);o<<'}';
  }return o<<"]}",o.str();
}
} // namespace cad
