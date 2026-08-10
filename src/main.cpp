#include "policy_engine.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <fstream>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <array>
#include <algorithm>

namespace {
std::string read_file(const std::string& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
double number(const std::string& body,const std::string& key,double fallback){
  auto p=body.find("\""+key+"\"");if(p==std::string::npos)return fallback;p=body.find(':',p);if(p==std::string::npos)return fallback;
  try{return std::stod(body.substr(p+1));}catch(...){return fallback;}
}
cad::Economy parse(const std::string& b){cad::Economy e;
#define FIELD(k,f) e.f=number(b,k,e.f)
  FIELD("policyRate",policy_rate);FIELD("inflation",inflation);FIELD("coreInflation",core_inflation);FIELD("gdpGrowth",gdp_growth);
  FIELD("outputGap",output_gap);FIELD("unemployment",unemployment);FIELD("wageGrowth",wage_growth);FIELD("productivity",productivity_growth);
  FIELD("population",population_growth);FIELD("usdcad",usdcad);FIELD("oil",oil_price);FIELD("creditSpread",credit_spread);
  FIELD("housingGap",housing_gap);FIELD("householdDebt",household_debt_income);FIELD("fiscalBalance",fiscal_balance_gdp);
  FIELD("federalDebt",federal_debt_gdp);FIELD("programGrowth",program_growth);FIELD("taxImpulse",tax_impulse);
  FIELD("infrastructure",infrastructure_impulse);FIELD("globalGrowth",global_growth);FIELD("expectations",inflation_expectations);
  FIELD("usGrowth",us_growth);FIELD("usInflation",us_inflation);FIELD("usTariff",us_tariff_canada);FIELD("retaliatoryTariff",canada_retaliatory_tariff);
  FIELD("exportsUs",exports_to_us_share);FIELD("importsUs",imports_from_us_share);FIELD("exportsGdp",exports_gdp);FIELD("importContent",import_content_consumption);
  FIELD("tradeElasticity",trade_elasticity);FIELD("borderFriction",border_friction);FIELD("tariffRelief",tariff_relief);FIELD("diversification",trade_diversification);
  FIELD("canadaPriority",canada_priority);FIELD("usPriority",us_priority);FIELD("riskAversion",risk_aversion);FIELD("cooperationCeiling",cooperation_ceiling);
#undef FIELD
  for(size_t i=0;i<e.us_sector_coverage.size();++i){
    e.us_sector_coverage[i]=number(b,"usSector"+std::to_string(i),e.us_sector_coverage[i]);
    e.canada_sector_coverage[i]=number(b,"canadaSector"+std::to_string(i),e.canada_sector_coverage[i]);
  }
  return e;
}
void respond(int fd,int status,const std::string&type,const std::string&body){std::ostringstream h;h<<"HTTP/1.1 "<<status<<(status==200?" OK":" Not Found")<<"\r\nContent-Type: "<<type<<"\r\nContent-Length: "<<body.size()<<"\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";auto out=h.str()+body;send(fd,out.data(),out.size(),0);}
std::string download(const char* url){
  std::string cmd="curl -LfsS --max-time 4 '"+std::string(url)+"' 2>/dev/null";std::string out;char b[4096];
  if(FILE* p=popen(cmd.c_str(),"r")){while(fgets(b,sizeof(b),p))out+=b;pclose(p);}return out;
}
double latest_value(const std::string& json,double fallback){
  auto p=json.rfind("\"v\"");if(p==std::string::npos)return fallback;p=json.find(':',p);if(p==std::string::npos)return fallback;
  p=json.find_first_of("-0123456789",p);try{return p==std::string::npos?fallback:std::stod(json.substr(p));}catch(...){return fallback;}
}
std::string live_baseline(){
  cad::Economy e;auto rate=download("https://www.bankofcanada.ca/valet/observations/V39079/json?recent=1");
  auto fx=download("https://www.bankofcanada.ca/valet/observations/FXUSDCAD/json?recent=1");
  auto wti=download("https://www.bankofcanada.ca/valet/observations/WTI/json?recent=1");
  e.policy_rate=latest_value(rate,e.policy_rate);e.usdcad=latest_value(fx,e.usdcad);e.oil_price=latest_value(wti,e.oil_price);
  const bool live=!rate.empty()&&!fx.empty();std::time_t now=std::time(nullptr);char stamp[32];std::strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",std::gmtime(&now));
  std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<"{\"status\":\""<<(live?"live":"cached fallback")<<"\",\"asOf\":\""<<stamp<<"\",\"settings\":{";
#define OUT(k,v) o<<"\"" k "\":"<<v<<','
  OUT("policyRate",e.policy_rate);OUT("inflation",e.inflation);OUT("coreInflation",e.core_inflation);OUT("expectations",e.inflation_expectations);OUT("gdpGrowth",e.gdp_growth);OUT("outputGap",e.output_gap);OUT("unemployment",e.unemployment);OUT("wageGrowth",e.wage_growth);OUT("productivity",e.productivity_growth);OUT("population",e.population_growth);OUT("creditSpread",e.credit_spread);OUT("housingGap",e.housing_gap);OUT("householdDebt",e.household_debt_income);OUT("usdcad",e.usdcad);OUT("oil",e.oil_price);OUT("retaliatoryTariff",e.canada_retaliatory_tariff);OUT("exportsUs",e.exports_to_us_share);OUT("importsUs",e.imports_from_us_share);OUT("exportsGdp",e.exports_gdp);OUT("importContent",e.import_content_consumption);OUT("tradeElasticity",e.trade_elasticity);OUT("borderFriction",e.border_friction);OUT("usGrowth",e.us_growth);OUT("usInflation",e.us_inflation);OUT("fiscalBalance",e.fiscal_balance_gdp);OUT("federalDebt",e.federal_debt_gdp);OUT("programGrowth",e.program_growth);OUT("infrastructure",e.infrastructure_impulse);OUT("globalGrowth",e.global_growth);
#undef OUT
  o<<"\"diversification\":"<<e.trade_diversification<<"},\"sources\":[{\"name\":\"Bank of Canada Valet API\",\"url\":\"https://www.bankofcanada.ca/valet/docs\",\"fields\":\"Policy rate, USD/CAD, oil\"},{\"name\":\"Statistics Canada\",\"url\":\"https://www.statcan.gc.ca/en/subjects-start/economic_accounts\",\"fields\":\"National accounts and labour baseline\"},{\"name\":\"Department of Finance Canada\",\"url\":\"https://www.canada.ca/en/department-finance.html\",\"fields\":\"Fiscal and trade assumptions\"}]}";return o.str();
}
struct NegotiationState {
  unsigned long revision=0;
  double us_tariff=50,retaliatory_tariff=5,canada_priority=50,us_priority=50;
  double risk_aversion=50,cooperation_ceiling=50;
  std::array<double,20> canada_sectors{},us_sectors{};
  std::string updated_by="automatic allocation search";
  NegotiationState(){canada_sectors.fill(100);us_sectors.fill(100);}
  std::string json() const {
    std::ostringstream o;o<<"{\"revision\":"<<revision<<",\"updatedBy\":\""<<updated_by<<"\",\"usTariff\":"<<us_tariff
      <<",\"retaliatoryTariff\":"<<retaliatory_tariff<<",\"canadaPriority\":"<<canada_priority<<",\"usPriority\":"<<us_priority
      <<",\"riskAversion\":"<<risk_aversion<<",\"cooperationCeiling\":"<<cooperation_ceiling;
    auto add=[&](const char*key,const auto& values){o<<",\""<<key<<"\":[";for(size_t i=0;i<values.size();++i){if(i)o<<',';o<<values[i];}o<<']';};
    add("canadaSectors",canada_sectors);add("usSectors",us_sectors);o<<'}';return o.str();
  }
  void update(const std::string& body){
    const bool canada=body.find("\"actor\":\"canada\"")!=std::string::npos;
    const bool us=body.find("\"actor\":\"us\"")!=std::string::npos;
    const bool automatic=body.find("\"actor\":\"automatic\"")!=std::string::npos;
    if(!canada&&!us&&!automatic)return;
    const auto bounded=[&](const std::string& key,double fallback){return std::clamp(number(body,key,fallback),0.0,100.0);};
    risk_aversion=bounded("riskAversion",risk_aversion);
    cooperation_ceiling=bounded("cooperationCeiling",cooperation_ceiling);
    if(canada){retaliatory_tariff=std::min(60.0,bounded("retaliatoryTariff",retaliatory_tariff));canada_priority=bounded("canadaPriority",canada_priority);us_priority=100-canada_priority;updated_by="Minister LeBlanc";}
    if(us){us_tariff=std::min(60.0,bounded("usTariff",us_tariff));us_priority=bounded("usPriority",us_priority);canada_priority=100-us_priority;updated_by="Mr. Greer";}
    if(automatic){us_tariff=std::min(60.0,bounded("usTariff",us_tariff));retaliatory_tariff=std::min(60.0,bounded("retaliatoryTariff",retaliatory_tariff));updated_by="automatic win-win search";}
    if(canada||automatic)for(size_t i=0;i<canada_sectors.size();++i)canada_sectors[i]=bounded("canadaSector"+std::to_string(i),canada_sectors[i]);
    if(us||automatic)for(size_t i=0;i<us_sectors.size();++i)us_sectors[i]=bounded("usSector"+std::to_string(i),us_sectors[i]);
    ++revision;
  }
};
}

int main(int argc,char**argv){
  std::signal(SIGPIPE,SIG_IGN);int port=argc>1?std::stoi(argv[1]):8080;
  int server=socket(AF_INET,SOCK_STREAM,0),yes=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=INADDR_ANY;addr.sin_port=htons(port);
  if(bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0||listen(server,16)<0){std::cerr<<"Unable to listen on port "<<port<<"\n";return 1;}
  std::cout<<"Canada Policy Studio → http://localhost:"<<port<<"\n";
  cad::PolicyEngine engine;
  NegotiationState negotiation;
  while(true){int client=accept(server,nullptr,nullptr);if(client<0)continue;std::string req;char buf[8192];ssize_t n;
    while((n=recv(client,buf,sizeof(buf),0))>0){req.append(buf,n);auto h=req.find("\r\n\r\n");if(h!=std::string::npos){size_t len=0,p=req.find("Content-Length:");if(p!=std::string::npos)len=std::stoul(req.substr(p+15));if(req.size()>=h+4+len)break;}}
    auto first=req.substr(0,req.find("\r\n"));auto split=req.find("\r\n\r\n");std::string body=split==std::string::npos?"":req.substr(split+4);
    if(first.rfind("POST /api/evaluate ",0)==0)respond(client,200,"application/json",cad::to_json(engine.evaluate(parse(body))));
    else if(first.rfind("POST /api/negotiation ",0)==0){negotiation.update(body);respond(client,200,"application/json",negotiation.json());}
    else if(first.rfind("GET /api/negotiation ",0)==0)respond(client,200,"application/json",negotiation.json());
    else if(first.rfind("GET /api/baseline ",0)==0)respond(client,200,"application/json",live_baseline());
    else if(first.rfind("GET / ",0)==0)respond(client,200,"text/html; charset=utf-8",read_file("web/index.html"));
    else if(first.rfind("GET /app.css ",0)==0)respond(client,200,"text/css",read_file("web/app.css"));
    else if(first.rfind("GET /app.js ",0)==0)respond(client,200,"application/javascript",read_file("web/app.js"));
    else respond(client,404,"text/plain","Not found");
    close(client);
  }
}
