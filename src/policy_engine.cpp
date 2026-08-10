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
                  double move, double fiscal, double productive, double deescalation,
                  double targeted_relief, double diversification, std::uint64_t seed) {
  Scenario s{std::move(id), std::move(name), std::move(description)};
  s.first_move_bp = move; s.fiscal_impulse = fiscal; s.productive_share = productive;
  constexpr int draws = 700;
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> shock(0.0, 1.0);
  double inf_sum=0, growth_sum=0, u_sum=0, debt_sum=0, house_sum=0, cost_sum=0, income_sum=0, export_sum=0, recessions=0;
  std::array<double,12> rp{}, ip{}, gp{}, dp{}, cp{}, xp{};
  std::vector<double> terminal_debt, terminal_inflation;
  for (int d=0; d<draws; ++d) {
    double rate=e.policy_rate, inf=e.core_inflation, gap=e.output_gap, u=e.unemployment;
    double debt=e.federal_debt_gdp, housing=e.housing_gap, export_change=0, cost=e.inflation;
    bool recession=false;
    for (int q=0; q<12; ++q) {
      const double coordinated = productive * fiscal;
      const double us_tariff = std::max(0.0, e.us_tariff_canada * (1.0-deescalation));
      const double ca_tariff = std::max(0.0, e.canada_retaliatory_tariff * (1.0-deescalation));
      const double exposed_exports = e.exports_to_us_share/100.0 * (1.0-clamp(diversification+e.trade_diversification,0.0,0.75));
      const double trade_drag = exposed_exports * e.exports_gdp/100.0 * e.trade_elasticity * (us_tariff+e.border_friction)/100.0;
      const double import_price = e.imports_from_us_share/100.0 * e.import_content_consumption/100.0 * ca_tariff;
      const double rate_target = clamp(2.5 + 0.75*(inf-2.0) + 0.25*gap, 0.25, 7.0);
      if (q==0) rate=clamp(rate+move/100.0, 0.0, 8.0);
      else rate=clamp(rate+clamp(rate_target-rate,-0.25,0.25),0.0,8.0);
      const double demand = fiscal*(1.0-productive)*0.36 - (rate-2.5)*0.18;
      const double supply = coordinated*0.22 + e.productivity_growth*0.035;
      export_change = -100.0*trade_drag + 0.35*(e.us_growth-2.0) + 2.0*diversification + shock(rng)*0.35;
      gap = 0.72*gap + demand - trade_drag + 0.08*(e.global_growth-2.7) + shock(rng)*0.16;
      const double fx = (e.usdcad-1.34)*0.35;
      inf = 0.68*inf + 0.32*e.inflation_expectations + 0.12*gap + fx
          - supply + 0.022*import_price - 0.018*(e.oil_price-75.0) + shock(rng)*0.11;
      const double growth = clamp(1.75 + gap - 0.18*e.credit_spread + coordinated*0.24 + shock(rng)*0.25,-4.0,5.5);
      u=clamp(u-0.10*(growth-1.7)+shock(rng)*0.035,3.5,11.0);
      housing=clamp(0.78*housing - 1.15*(rate-2.5) + 0.08*(e.population_growth-1.2) + shock(rng)*0.5,-15,30);
      const double relief_cost=targeted_relief+e.tariff_relief;
      debt += (-(e.fiscal_balance_gdp) + fiscal*0.8 + relief_cost*0.55 + 0.045*(rate-2.5)*debt - 0.18*growth)/4.0;
      cost=0.56*inf+0.22*std::max(0.0,housing/10.0)+0.14*std::max(0.0,e.wage_growth-growth)+0.08*import_price;
      recession = recession || growth < 0.0;
      rp[q]+=rate; ip[q]+=inf; gp[q]+=growth; dp[q]+=debt;cp[q]+=cost;xp[q]+=export_change;
      if(q==11){ inf_sum+=inf; growth_sum+=growth; u_sum+=u; debt_sum+=debt; house_sum+=housing;cost_sum+=cost;income_sum+=growth-cost+targeted_relief*.15;export_sum+=export_change;terminal_debt.push_back(debt);terminal_inflation.push_back(inf); }
    }
    if(recession) recessions++;
  }
  for(int q=0;q<12;++q){s.rates[q]=rp[q]/draws;s.inflation_path[q]=ip[q]/draws;s.growth_path[q]=gp[q]/draws;s.debt_path[q]=dp[q]/draws;s.cost_path[q]=cp[q]/draws;s.export_path[q]=xp[q]/draws;}
  s.inflation=inf_sum/draws;s.growth=growth_sum/draws;s.unemployment=u_sum/draws;
  s.debt_gdp=debt_sum/draws;s.housing_gap=house_sum/draws;s.recession_risk=100.0*recessions/draws;
  s.cost_of_living=cost_sum/draws;s.real_income_growth=income_sum/draws;s.export_change=export_sum/draws;
  std::sort(terminal_debt.begin(),terminal_debt.end());std::sort(terminal_inflation.begin(),terminal_inflation.end());
  s.debt_stress_p90=terminal_debt[draws*9/10];s.inflation_stress_p90=terminal_inflation[draws*9/10];
  const double mandate_loss=3.8*sq(s.inflation-2.0)+1.2*sq(std::max(0.0,s.unemployment-5.8))
      +0.7*sq(std::min(0.0,s.growth))+0.018*s.recession_risk;
  const double federal_loss=0.32*sq(std::max(0.0,s.debt_gdp-e.federal_debt_gdp))+0.7*sq(std::min(0.0,s.growth))
      +0.8*sq(std::max(0.0,s.unemployment-6.0))+0.012*sq(s.housing_gap);
  s.boc_score=100.0/(1.0+mandate_loss);s.federal_score=100.0/(1.0+federal_loss);
  const double us_loss=.55*sq(std::max(0.0,-s.export_change))+0.8*sq(std::max(0.0,e.us_inflation-2.0+e.us_tariff_canada*.025))+.25*sq(e.canada_retaliatory_tariff*(1.0-deescalation));
  s.us_score=100.0/(1.0+us_loss);
  // Three-party Nash welfare: no party can be hidden by two strong results.
  const double floor=std::min({s.boc_score,s.federal_score,s.us_score});
  s.score=.72*std::cbrt(s.boc_score*s.federal_score*s.us_score)+.28*floor;
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
  r.scenarios.push_back(simulate(e,"statusquo","Tariff status quo","Current tariffs persist; BoC and fiscal settings hold.",0,0,.5,0,0,0,seed_+1));
  r.scenarios.push_back(simulate(e,"retaliate","Symmetric retaliation","Canada matches trade barriers and supports affected demand.",0,.35,.25,0,.25,0,seed_+2));
  r.scenarios.push_back(simulate(e,"relief","Worker transition bridge","A measured cut and temporary, targeted tariff adjustment support.",-25,.30,.65,0,.35,.15,seed_+3));
  r.scenarios.push_back(simulate(e,"compact","North American compact","Mutual tariff removal, border facilitation and productive Canadian investment.",0,.25,.9,.85,.10,.20,seed_+4));
  r.scenarios.push_back(simulate(e,"diversify","Market diversification","Trade infrastructure and export-market diversification with a BoC hold.",0,.35,.9,0,.10,.45,seed_+5));
  r.scenarios.push_back(simulate(e,"guardrail","Inflation guardrail","A 25 bp increase and limited retaliation constrain tariff pass-through.",25,-.10,.75,.20,0,.10,seed_+6));
  r.scenarios.push_back(simulate(e,"supply","Cost-of-living supply plan","Housing, logistics and productivity investment with targeted household relief.",0,.40,.95,.35,.20,.25,seed_+7));
  r.scenarios.push_back(simulate(e,"stabilizer","Automatic stabilizers","Income insurance absorbs the trade shock while monetary policy remains data dependent.",0,.22,.35,0,.30,.08,seed_+8));
  r.scenarios.push_back(simulate(e,"eastwest","East–west trade corridor","Ports, rail and interprovincial trade reform accelerate non-US market access.",0,.48,.96,0,.08,.60,seed_+9));
  r.scenarios.push_back(simulate(e,"productivity","Productivity compact","Accelerated investment expensing, skills and competition policy lift supply capacity.",0,.32,1.0,.10,.05,.30,seed_+10));
  r.scenarios.push_back(simulate(e,"defence","Fiscal consolidation buffer","Spending restraint preserves debt capacity while the Bank cushions demand.",-25,-.22,.70,0,0,.12,seed_+11));
  r.scenarios.push_back(simulate(e,"sectoral","Sector-targeted response","Time-limited support protects tariff-exposed workers without broad retaliation.",0,.28,.62,.05,.48,.22,seed_+12));
  std::sort(r.scenarios.begin(),r.scenarios.end(),[](const auto&a,const auto&b){return a.score>b.score;});
  const auto& best=r.scenarios.front();
  r.signal = best.first_move_bp>0 ? "Raise 25 bp" : best.first_move_bp<0 ? "Cut 25 bp" : "Hold & coordinate";
  r.rationale="The "+best.name+" maximizes three-party Nash welfare across the BoC mandate, Canadian fiscal and household outcomes, and US trade-price outcomes; examine tail metrics before deciding.";
  return r;
}

std::string to_json(const Result& r){
  std::ostringstream o;o<<std::fixed<<std::setprecision(3);
  o<<"{\"regime\":\""<<esc(r.regime)<<"\",\"signal\":\""<<esc(r.signal)<<"\",\"rationale\":\""<<esc(r.rationale)
   <<"\",\"confidence\":"<<r.data_confidence<<",\"neutralRate\":"<<r.neutral_rate<<",\"policyGap\":"<<r.policy_gap<<",\"scenarios\":[";
  for(size_t i=0;i<r.scenarios.size();++i){if(i)o<<',';const auto&s=r.scenarios[i];
    o<<"{\"id\":\""<<s.id<<"\",\"name\":\""<<esc(s.name)<<"\",\"description\":\""<<esc(s.description)
     <<"\",\"move\":"<<s.first_move_bp<<",\"fiscal\":"<<s.fiscal_impulse<<",\"score\":"<<s.score<<",\"bocScore\":"<<s.boc_score
     <<",\"federalScore\":"<<s.federal_score<<",\"usScore\":"<<s.us_score<<",\"inflation\":"<<s.inflation<<",\"growth\":"<<s.growth<<",\"unemployment\":"<<s.unemployment
     <<",\"debt\":"<<s.debt_gdp<<",\"housing\":"<<s.housing_gap<<",\"recessionRisk\":"<<s.recession_risk<<",\"rates\":";array_json(o,s.rates);
    o<<",\"costOfLiving\":"<<s.cost_of_living<<",\"realIncome\":"<<s.real_income_growth<<",\"exports\":"<<s.export_change<<",\"debtP90\":"<<s.debt_stress_p90<<",\"inflationP90\":"<<s.inflation_stress_p90;
    o<<",\"inflationPath\":";array_json(o,s.inflation_path);o<<",\"growthPath\":";array_json(o,s.growth_path);o<<",\"debtPath\":";array_json(o,s.debt_path);o<<",\"costPath\":";array_json(o,s.cost_path);o<<",\"exportPath\":";array_json(o,s.export_path);o<<'}';
  }return o<<"]}",o.str();
}
} // namespace cad
