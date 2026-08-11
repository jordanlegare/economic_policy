#include "policy_engine.hpp"
#include "calibration.hpp"
#include "negotiation_support.hpp"
#include "negotiation_trade_alignment.hpp"
#include "trade_diplomacy_platform.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

std::string diplomatic_index() {
  auto html = read_file("web/index.html");
  const auto head = html.rfind("</head>");
  if (head != std::string::npos) {
    html.insert(head,
        "<link rel=\"stylesheet\" href=\"/diplomat.css\">"
        "<link rel=\"stylesheet\" href=\"/negotiation-model.css\">"
        "<link rel=\"stylesheet\" href=\"/trade-diplomacy.css\">"
        "<link rel=\"stylesheet\" href=\"/calibration.css\">");
  }
  const auto body = html.rfind("</body>");
  if (body != std::string::npos) {
    html.insert(body,
        "<script src=\"/diplomat.js\"></script>"
        "<script src=\"/negotiation-model.js\"></script>"
        "<script src=\"/trade-diplomacy.js\"></script>"
        "<script src=\"/calibration.js\"></script>");
  }
  return html;
}

double number(const std::string& body, const std::string& key, double fallback) {
  auto pos = body.find("\"" + key + "\"");
  if (pos == std::string::npos) return fallback;
  pos = body.find(':', pos);
  if (pos == std::string::npos) return fallback;
  try { return std::stod(body.substr(pos + 1)); } catch (...) { return fallback; }
}

cad::Economy parse(const std::string& body) {
  cad::Economy economy;
#define FIELD(k, f) economy.f = number(body, k, economy.f)
  FIELD("policyRate", policy_rate); FIELD("inflation", inflation);
  FIELD("coreInflation", core_inflation); FIELD("gdpGrowth", gdp_growth);
  FIELD("outputGap", output_gap); FIELD("unemployment", unemployment);
  FIELD("wageGrowth", wage_growth); FIELD("productivity", productivity_growth);
  FIELD("population", population_growth); FIELD("usdcad", usdcad);
  FIELD("oil", oil_price); FIELD("creditSpread", credit_spread);
  FIELD("housingGap", housing_gap); FIELD("householdDebt", household_debt_income);
  FIELD("fiscalBalance", fiscal_balance_gdp); FIELD("federalDebt", federal_debt_gdp);
  FIELD("programGrowth", program_growth); FIELD("taxImpulse", tax_impulse);
  FIELD("infrastructure", infrastructure_impulse); FIELD("globalGrowth", global_growth);
  FIELD("expectations", inflation_expectations); FIELD("usGrowth", us_growth);
  FIELD("usInflation", us_inflation); FIELD("usTariff", us_tariff_canada);
  FIELD("retaliatoryTariff", canada_retaliatory_tariff); FIELD("exportsUs", exports_to_us_share);
  FIELD("importsUs", imports_from_us_share); FIELD("exportsGdp", exports_gdp);
  FIELD("importContent", import_content_consumption); FIELD("tradeElasticity", trade_elasticity);
  FIELD("borderFriction", border_friction); FIELD("tariffRelief", tariff_relief);
  FIELD("diversification", trade_diversification); FIELD("bilateralExportsCad", canada_exports_to_us_cad);
  FIELD("bilateralImportsCad", canada_imports_from_us_cad); FIELD("canadaPriority", canada_priority);
  FIELD("usPriority", us_priority); FIELD("riskAversion", risk_aversion);
  FIELD("cooperationCeiling", cooperation_ceiling); FIELD("minimumBilateralGrowth", minimum_bilateral_growth);
#undef FIELD
  for (std::size_t i = 0; i < economy.us_sector_coverage.size(); ++i) {
    economy.us_sector_coverage[i] = number(body, "usSector" + std::to_string(i), economy.us_sector_coverage[i]);
    economy.canada_sector_coverage[i] = number(body, "canadaSector" + std::to_string(i), economy.canada_sector_coverage[i]);
  }
  return economy;
}

void respond(int fd, int status, const std::string& type, const std::string& body) {
  const char* text = status == 200 ? "OK" : status == 400 ? "Bad Request"
      : status == 413 ? "Payload Too Large" : "Not Found";
  std::ostringstream header;
  header << "HTTP/1.1 " << status << ' ' << text << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Cache-Control: no-store\r\n\r\n";
  const auto output = header.str() + body;
  send(fd, output.data(), output.size(), 0);
}

std::string download(const char* url) {
  std::string command = "curl -LfsS --max-time 4 '" + std::string(url) + "' 2>/dev/null";
  std::string out;
  char buffer[4096];
  if (FILE* pipe = popen(command.c_str(), "r")) {
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    pclose(pipe);
  }
  return out;
}

double latest_value(const std::string& json, double fallback) {
  auto pos = json.rfind("\"v\"");
  if (pos == std::string::npos) return fallback;
  pos = json.find(':', pos);
  if (pos == std::string::npos) return fallback;
  pos = json.find_first_of("-0123456789", pos);
  try { return pos == std::string::npos ? fallback : std::stod(json.substr(pos)); }
  catch (...) { return fallback; }
}

std::string live_baseline(const cad::CalibrationSnapshot& calibration) {
  cad::Economy economy = cad::apply_calibration(cad::Economy{}, calibration);
  const auto rate = download("https://www.bankofcanada.ca/valet/observations/V39079/json?recent=1");
  const auto fx = download("https://www.bankofcanada.ca/valet/observations/FXUSDCAD/json?recent=1");
  const auto wti = download("https://www.bankofcanada.ca/valet/observations/WTI/json?recent=1");
  const bool rate_live = !rate.empty(), fx_live = !fx.empty(), wti_live = !wti.empty();
  economy.policy_rate = latest_value(rate, economy.policy_rate);
  economy.usdcad = latest_value(fx, economy.usdcad);
  economy.oil_price = latest_value(wti, economy.oil_price);

  std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  const bool any_live = rate_live || fx_live || wti_live;
  std::ostringstream out;
  out << std::fixed << std::setprecision(3)
      << "{\"status\":\"" << (any_live ? "calibrated baseline plus live BoC observations" : "calibrated snapshot; live BoC fetch unavailable")
      << "\",\"asOf\":\"" << stamp << "\",\"settings\":{";
#define OUT(k, v) out << "\"" k "\":" << v << ','
  OUT("policyRate", economy.policy_rate); OUT("inflation", economy.inflation);
  OUT("coreInflation", economy.core_inflation); OUT("expectations", economy.inflation_expectations);
  OUT("gdpGrowth", economy.gdp_growth); OUT("outputGap", economy.output_gap);
  OUT("unemployment", economy.unemployment); OUT("wageGrowth", economy.wage_growth);
  OUT("productivity", economy.productivity_growth); OUT("population", economy.population_growth);
  OUT("creditSpread", economy.credit_spread); OUT("housingGap", economy.housing_gap);
  OUT("householdDebt", economy.household_debt_income); OUT("usdcad", economy.usdcad);
  OUT("oil", economy.oil_price); OUT("usTariff", economy.us_tariff_canada);
  OUT("retaliatoryTariff", economy.canada_retaliatory_tariff); OUT("exportsUs", economy.exports_to_us_share);
  OUT("importsUs", economy.imports_from_us_share); OUT("exportsGdp", economy.exports_gdp);
  OUT("importContent", economy.import_content_consumption); OUT("tradeElasticity", economy.trade_elasticity);
  OUT("borderFriction", economy.border_friction); OUT("usGrowth", economy.us_growth);
  OUT("usInflation", economy.us_inflation); OUT("fiscalBalance", economy.fiscal_balance_gdp);
  OUT("federalDebt", economy.federal_debt_gdp); OUT("programGrowth", economy.program_growth);
  OUT("infrastructure", economy.infrastructure_impulse); OUT("globalGrowth", economy.global_growth);
  OUT("minimumBilateralGrowth", economy.minimum_bilateral_growth);
#undef OUT
  out << "\"bilateralExportsCad\":" << economy.canada_exports_to_us_cad
      << ",\"bilateralImportsCad\":" << economy.canada_imports_from_us_cad
      << ",\"diversification\":" << economy.trade_diversification << "},"
      << "\"provenance\":{\"observedLive\":["
      << "{\"field\":\"policyRate\",\"source\":\"Bank of Canada Valet V39079\",\"live\":" << (rate_live ? "true" : "false") << "},"
      << "{\"field\":\"usdcad\",\"source\":\"Bank of Canada Valet FXUSDCAD\",\"live\":" << (fx_live ? "true" : "false") << "},"
      << "{\"field\":\"oil\",\"source\":\"Bank of Canada Valet WTI\",\"live\":" << (wti_live ? "true" : "false") << "}],"
      << "\"warning\":\"Fields not certified by the attached calibration snapshot remain model assumptions or user inputs.\"},"
      << "\"calibration\":" << cad::calibration_to_json(calibration) << "}";
  return out.str();
}

struct NegotiationState {
  unsigned long revision = 0;
  double us_tariff = 50, retaliatory_tariff = 5, canada_priority = 50, us_priority = 50;
  double risk_aversion = 50, cooperation_ceiling = 50;
  std::array<double, 20> canada_sectors{}, us_sectors{};
  std::string updated_by = "automatic allocation search";

  NegotiationState() { canada_sectors.fill(100); us_sectors.fill(100); }

  std::string json() const {
    std::ostringstream out;
    out << "{\"revision\":" << revision << ",\"updatedBy\":\"" << updated_by
        << "\",\"usTariff\":" << us_tariff << ",\"retaliatoryTariff\":" << retaliatory_tariff
        << ",\"canadaPriority\":" << canada_priority << ",\"usPriority\":" << us_priority
        << ",\"riskAversion\":" << risk_aversion << ",\"cooperationCeiling\":" << cooperation_ceiling;
    auto add = [&](const char* key, const auto& values) {
      out << ",\"" << key << "\":[";
      for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
      out << ']';
    };
    add("canadaSectors", canada_sectors); add("usSectors", us_sectors);
    out << '}';
    return out.str();
  }

  void update(const std::string& body) {
    const bool canada = body.find("\"actor\":\"canada\"") != std::string::npos;
    const bool us = body.find("\"actor\":\"us\"") != std::string::npos;
    const bool automatic = body.find("\"actor\":\"automatic\"") != std::string::npos;
    if (!canada && !us && !automatic) return;
    auto bounded = [&](const std::string& key, double fallback) {
      return std::clamp(number(body, key, fallback), 0.0, 100.0);
    };
    risk_aversion = bounded("riskAversion", risk_aversion);
    cooperation_ceiling = bounded("cooperationCeiling", cooperation_ceiling);
    if (canada) {
      retaliatory_tariff = std::min(60.0, bounded("retaliatoryTariff", retaliatory_tariff));
      canada_priority = bounded("canadaPriority", canada_priority); us_priority = 100 - canada_priority;
      updated_by = "Canada delegation";
    }
    if (us) {
      us_tariff = std::min(60.0, bounded("usTariff", us_tariff));
      us_priority = bounded("usPriority", us_priority); canada_priority = 100 - us_priority;
      updated_by = "U.S. delegation";
    }
    if (automatic) {
      us_tariff = std::min(60.0, bounded("usTariff", us_tariff));
      retaliatory_tariff = std::min(60.0, bounded("retaliatoryTariff", retaliatory_tariff));
      updated_by = "automatic win-win search";
    }
    if (canada || automatic)
      for (std::size_t i = 0; i < 20; ++i) canada_sectors[i] = bounded("canadaSector" + std::to_string(i), canada_sectors[i]);
    if (us || automatic)
      for (std::size_t i = 0; i < 20; ++i) us_sectors[i] = bounded("usSector" + std::to_string(i), us_sectors[i]);
    ++revision;
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
  const bool bind_all = argc > 2 && std::string(argv[2]) == "--bind-all";
  int server = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(bind_all ? INADDR_ANY : INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 16) < 0) {
    std::cerr << "Unable to listen on port " << port << '\n';
    return 1;
  }

  cad::CalibratedPolicyEngine engine("data/calibration/current.snapshot.csv");
  NegotiationState negotiation;
  std::cout << "Canada–U.S. Diplomatic Policy Studio → "
            << (bind_all ? "http://0.0.0.0:" : "http://localhost:") << port << '\n'
            << "Calibration: " << engine.snapshot().grade << " ("
            << engine.snapshot().completeness << "% complete, as of " << engine.snapshot().as_of << ")\n";

  constexpr std::size_t max_request_bytes = 128 * 1024;
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) continue;
    std::string request;
    char buffer[8192];
    ssize_t count;
    bool rejected = false;
    while ((count = recv(client, buffer, sizeof(buffer), 0)) > 0) {
      request.append(buffer, count);
      if (request.size() > max_request_bytes) {
        respond(client, 413, "text/plain", "Request too large"); rejected = true; break;
      }
      const auto headers = request.find("\r\n\r\n");
      if (headers != std::string::npos) {
        std::size_t length = 0;
        const auto pos = request.find("Content-Length:");
        if (pos != std::string::npos) {
          try { length = std::stoul(request.substr(pos + 15)); }
          catch (...) { respond(client, 400, "text/plain", "Invalid Content-Length"); rejected = true; break; }
        }
        if (length > max_request_bytes) {
          respond(client, 413, "text/plain", "Request body too large"); rejected = true; break;
        }
        if (request.size() >= headers + 4 + length) break;
      }
    }
    if (rejected) { close(client); continue; }

    const auto first = request.substr(0, request.find("\r\n"));
    const auto split = request.find("\r\n\r\n");
    const std::string body = split == std::string::npos ? "" : request.substr(split + 4);

    if (first.rfind("POST /api/evaluate ", 0) == 0) {
      auto economy = parse(body);
      auto result = engine.evaluate(economy);  // Mutates economy to the calibrated values actually simulated.
      auto bargaining = cad::analyze_negotiation(economy, result);
      cad::align_negotiation_trade_channels(economy, result, bargaining);
      auto platform = cad::build_trade_diplomacy_platform(economy, result, bargaining);
      auto with_calibration = cad::attach_calibration_json(cad::to_json(result), engine.snapshot());
      auto with_negotiation = cad::attach_negotiation_json(with_calibration, bargaining);
      respond(client, 200, "application/json", cad::attach_trade_diplomacy_json(with_negotiation, platform));
    } else if (first.rfind("POST /api/negotiation ", 0) == 0) {
      negotiation.update(body); respond(client, 200, "application/json", negotiation.json());
    } else if (first.rfind("GET /api/negotiation ", 0) == 0) {
      respond(client, 200, "application/json", negotiation.json());
    } else if (first.rfind("GET /api/calibration ", 0) == 0) {
      respond(client, 200, "application/json", cad::calibration_to_json(engine.snapshot()));
    } else if (first.rfind("GET /api/baseline ", 0) == 0) {
      respond(client, 200, "application/json", live_baseline(engine.snapshot()));
    } else if (first.rfind("GET / ", 0) == 0) {
      respond(client, 200, "text/html; charset=utf-8", diplomatic_index());
    } else if (first.rfind("GET /app.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/app.css"));
    else if (first.rfind("GET /app.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/app.js"));
    else if (first.rfind("GET /diplomat.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/diplomat.css"));
    else if (first.rfind("GET /diplomat.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/diplomat.js"));
    else if (first.rfind("GET /negotiation-model.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/negotiation-model.css"));
    else if (first.rfind("GET /negotiation-model.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/negotiation-model.js"));
    else if (first.rfind("GET /trade-diplomacy.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/trade-diplomacy.css"));
    else if (first.rfind("GET /trade-diplomacy.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/trade-diplomacy.js"));
    else if (first.rfind("GET /calibration.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/calibration.css"));
    else if (first.rfind("GET /calibration.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/calibration.js"));
    else respond(client, 404, "text/plain", "Not found");
    close(client);
  }
}
