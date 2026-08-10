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
#undef FIELD
  return e;
}
void respond(int fd,int status,const std::string&type,const std::string&body){std::ostringstream h;h<<"HTTP/1.1 "<<status<<(status==200?" OK":" Not Found")<<"\r\nContent-Type: "<<type<<"\r\nContent-Length: "<<body.size()<<"\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";auto out=h.str()+body;send(fd,out.data(),out.size(),0);}
}

int main(int argc,char**argv){
  std::signal(SIGPIPE,SIG_IGN);int port=argc>1?std::stoi(argv[1]):8080;
  int server=socket(AF_INET,SOCK_STREAM,0),yes=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=INADDR_ANY;addr.sin_port=htons(port);
  if(bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0||listen(server,16)<0){std::cerr<<"Unable to listen on port "<<port<<"\n";return 1;}
  std::cout<<"Canada Policy Studio → http://localhost:"<<port<<"\n";
  cad::PolicyEngine engine;
  while(true){int client=accept(server,nullptr,nullptr);if(client<0)continue;std::string req;char buf[8192];ssize_t n;
    while((n=recv(client,buf,sizeof(buf),0))>0){req.append(buf,n);auto h=req.find("\r\n\r\n");if(h!=std::string::npos){size_t len=0,p=req.find("Content-Length:");if(p!=std::string::npos)len=std::stoul(req.substr(p+15));if(req.size()>=h+4+len)break;}}
    auto first=req.substr(0,req.find("\r\n"));auto split=req.find("\r\n\r\n");std::string body=split==std::string::npos?"":req.substr(split+4);
    if(first.rfind("POST /api/evaluate ",0)==0)respond(client,200,"application/json",cad::to_json(engine.evaluate(parse(body))));
    else if(first.rfind("GET / ",0)==0)respond(client,200,"text/html; charset=utf-8",read_file("web/index.html"));
    else if(first.rfind("GET /app.css ",0)==0)respond(client,200,"text/css",read_file("web/app.css"));
    else if(first.rfind("GET /app.js ",0)==0)respond(client,200,"application/javascript",read_file("web/app.js"));
    else respond(client,404,"text/plain","Not found");
    close(client);
  }
}
