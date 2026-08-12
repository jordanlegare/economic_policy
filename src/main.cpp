#include "policy_engine.hpp"
#include "calibration.hpp"
#include "model_evidence.hpp"
#include "negotiation_support.hpp"
#include "negotiation_trade_alignment.hpp"
#include "robust_recommendation.hpp"
#include "negotiation_room.hpp"
#include "structural_calibration.hpp"
#include "trade_diplomacy_platform.hpp"
#include "welfare_sensitivity.hpp"

#ifdef CAD_EMBEDDED_ASSETS
#include "embedded_assets.hpp"
#endif

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using socket_handle = SOCKET;
constexpr socket_handle invalid_socket = INVALID_SOCKET;

struct SocketRuntime {
  SocketRuntime() : started_(WSAStartup(MAKEWORD(2, 2), &data_) == 0) {}
  ~SocketRuntime() { if (started_) WSACleanup(); }
  bool ok() const { return started_; }
 private:
  WSADATA data_{};
  bool started_ = false;
};

void close_socket(socket_handle value) {
  if (value != invalid_socket) closesocket(value);
}
#else
using socket_handle = int;
constexpr socket_handle invalid_socket = -1;

struct SocketRuntime {
  bool ok() const { return true; }
};

void close_socket(socket_handle value) {
  if (value != invalid_socket) close(value);
}
#endif

std::filesystem::path runtime_root() {
#ifdef _WIN32
  if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
    if (*local_app_data) return std::filesystem::path(local_app_data) / "CanadaPolicyStudio";
  }
#endif
  return std::filesystem::path("runtime");
}

std::string read_file(const std::string& path) {
#ifdef CAD_EMBEDDED_ASSETS
  if (const auto* embedded = cad::embedded::find(path)) return std::string(*embedded);
#endif
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

std::string materialized_data_path(const std::string& source_path) {
#ifdef CAD_EMBEDDED_ASSETS
  const std::string content = read_file(source_path);
  if (content.empty()) throw std::runtime_error("embedded data asset is empty: " + source_path);
  const auto path = runtime_root() / "embedded-data" / source_path;
  std::filesystem::create_directories(path.parent_path());

  bool needs_write = true;
  {
    std::ifstream existing(path, std::ios::binary);
    if (existing) {
      std::ostringstream current;
      current << existing.rdbuf();
      needs_write = current.str() != content;
    }
  }
  if (needs_write) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("unable to materialize embedded data asset: " + source_path);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw std::runtime_error("unable to write embedded data asset: " + source_path);
  }
  return path.string();
#else
  return source_path;
#endif
}

std::string calibration_path() {
  return materialized_data_path("data/calibration/current.snapshot.csv");
}

void open_browser(const std::string& url) {
#ifdef _WIN32
  const auto result = reinterpret_cast<std::intptr_t>(
      ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) std::cerr << "Unable to open the default browser automatically. Open " << url << " manually.\n";
#else
  (void)url;
#endif
}

std::string diplomatic_index() {
  auto html = read_file("web/index.html");
  const auto head = html.rfind("</head>");
  if (head != std::string::npos) {
    html.insert(head,
        "<link rel=\"stylesheet\" href=\"/diplomat.css\">"
        "<link rel=\"stylesheet\" href=\"/negotiation-model.css\">"
        "<link rel=\"stylesheet\" href=\"/trade-diplomacy.css\">"
        "<link rel=\"stylesheet\" href=\"/calibration.css\">"
        "<link rel=\"stylesheet\" href=\"/robust-room.css\">");
    const auto principal_head = html.rfind("</head>");
    html.insert(principal_head, "<style>" + read_file("web/principal-briefing.css") + "</style>");
  }
  const auto body = html.rfind("</body>");
  if (body != std::string::npos) {
    html.insert(body,
        "<script src=\"/diplomat.js\"></script>"
        "<script src=\"/negotiation-model.js\"></script>"
        "<script src=\"/trade-diplomacy.js\"></script>"
        "<script src=\"/calibration.js\"></script>"
        "<script src=\"/robust-room.js\"></script>");
    const auto principal_body = html.rfind("</body>");
    html.insert(principal_body, "<script>" + read_file("web/principal-briefing.js") + "</script>");
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

void send_all(socket_handle fd, const std::string& output) {
  std::size_t offset = 0;
  while (offset < output.size()) {
    const int chunk = static_cast<int>(std::min<std::size_t>(output.size() - offset, 1024 * 1024));
    const auto sent = ::send(fd, output.data() + offset, chunk, 0);
    if (sent <= 0) break;
    offset += static_cast<std::size_t>(sent);
  }
}

void respond(socket_handle fd, int status, const std::string& type, const std::string& body) {
  const char* text = status == 200 ? "OK" : status == 400 ? "Bad Request"
      : status == 413 ? "Payload Too Large" : "Not Found";
  std::ostringstream header;
  header << "HTTP/1.1 " << status << ' ' << text << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Cache-Control: no-store\r\n\r\n";
  send_all(fd, header.str() + body);
}

std::string download(const char* url) {
#ifdef _WIN32
  std::string command = "curl.exe -LfsS --max-time 4 \"" + std::string(url) + "\" 2>NUL";
  auto open_pipe = _popen;
  auto close_pipe = _pclose;
#else
  std::string command = "curl -LfsS --max-time 4 '" + std::string(url) + "' 2>/dev/null";
  auto open_pipe = popen;
  auto close_pipe = pclose;
#endif
  std::string out;
  char buffer[4096];
  if (FILE* pipe = open_pipe(command.c_str(), "r")) {
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    close_pipe(pipe);
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
      << "{\"status\":\"" << (any_live ? "live" : "calibrated")
      << "\",\"statusDetail\":\"" << (any_live ? "calibrated baseline plus live BoC observations" : "calibrated snapshot; live BoC fetch unavailable")
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
      << "\"sources\":["
      << "{\"name\":\"Bank of Canada policy rate\",\"fields\":\"Policy rate · Valet V39079\",\"url\":\"https://www.bankofcanada.ca/valet/\"},"
      << "{\"name\":\"Bank of Canada USD/CAD\",\"fields\":\"Exchange rate · Valet FXUSDCAD\",\"url\":\"https://www.bankofcanada.ca/valet/\"},"
      << "{\"name\":\"Bank of Canada WTI\",\"fields\":\"WTI oil price · Valet WTI\",\"url\":\"https://www.bankofcanada.ca/valet/\"}],"
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
#ifndef _WIN32
  std::signal(SIGPIPE, SIG_IGN);
#endif
  int port = 8080;
  bool port_set = false;
  bool bind_all = false;
#ifdef _WIN32
  bool launch_browser = true;
#else
  bool launch_browser = false;
#endif

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bind-all") bind_all = true;
    else if (arg == "--no-browser") launch_browser = false;
    else if (arg == "--browser") launch_browser = true;
    else if (!port_set) {
      try {
        port = std::stoi(arg);
        port_set = true;
      } catch (...) {
        std::cerr << "Unknown argument: " << arg << '\n';
        return 2;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return 2;
    }
  }
  if (port < 1 || port > 65535) {
    std::cerr << "Port must be between 1 and 65535\n";
    return 2;
  }

  SocketRuntime sockets;
  if (!sockets.ok()) {
    std::cerr << "Unable to initialize the network stack\n";
    return 1;
  }

  socket_handle server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server == invalid_socket) {
    std::cerr << "Unable to create server socket\n";
    return 1;
  }
  int yes = 1;
#ifdef _WIN32
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(bind_all ? INADDR_ANY : INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(server, 16) != 0) {
    std::cerr << "Unable to listen on port " << port << '\n';
    close_socket(server);
    return 1;
  }

  std::string calibrated_path;
  std::string structural_registry_path;
  std::vector<std::string> historical_fixture_paths;
  try {
    calibrated_path = calibration_path();
    structural_registry_path = materialized_data_path(
        "data/calibration/structural_parameter_registry.csv");
    historical_fixture_paths = {
        materialized_data_path("data/backtests/2015-01-20-oil-shock.csv"),
        materialized_data_path("data/backtests/2020-03-03-pandemic-onset.csv"),
        materialized_data_path("data/backtests/2022-07-12-inflation-tightening.csv")};
  } catch (const std::exception& error) {
    std::cerr << "Unable to prepare model data: " << error.what() << '\n';
    close_socket(server);
    return 1;
  }

  const auto structural_registry = cad::load_structural_parameter_registry(
      structural_registry_path);
  const auto structural_parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, structural_registry);
  cad::PolicyEngine evidence_engine(20260810, structural_parameters, structural_registry);

  const auto room_path = (runtime_root() / "negotiation-room.events").string();
  cad::CalibratedPolicyEngine engine(calibrated_path);
  NegotiationState negotiation;
  cad::NegotiationRoom room(room_path);
  cad::NegotiationAnalysis last_bargaining;
  cad::RobustRecommendationAnalysis last_robustness;
  cad::Economy last_economy = cad::apply_calibration(cad::Economy{}, engine.snapshot());
  std::vector<cad::BacktestResult> historical_backtests;
  bool historical_evidence_ready = false;
  bool has_evaluation = false;
  const std::string local_url = "http://localhost:" + std::to_string(port);
  std::cout << "Canada–U.S. Diplomatic Policy Studio → "
            << (bind_all ? "http://0.0.0.0:" : "http://localhost:") << port << '\n'
            << "Calibration: " << engine.snapshot().grade << " ("
            << engine.snapshot().completeness << "% complete, as of " << engine.snapshot().as_of << ")\n"
            << "Structural registry: " << structural_registry.registry_id << " ("
            << cad::sampled_structural_parameter_count(structural_registry) << " sampled parameters)\n"
            << "Diplomat Room: local append-only persistence at " << room_path << '\n';
  if (launch_browser) open_browser(local_url);

  constexpr std::size_t max_request_bytes = 128 * 1024;
  while (true) {
    const socket_handle client = ::accept(server, nullptr, nullptr);
    if (client == invalid_socket) continue;
    std::string request;
    char buffer[8192];
    int count;
    bool rejected = false;
    while ((count = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0)) > 0) {
      request.append(buffer, static_cast<std::size_t>(count));
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
    if (rejected) { close_socket(client); continue; }

    const auto first = request.substr(0, request.find("\r\n"));
    const auto split = request.find("\r\n\r\n");
    const std::string body = split == std::string::npos ? "" : request.substr(split + 4);

    if (first.rfind("POST /api/evaluate ", 0) == 0) {
      auto economy = parse(body);
      auto result = engine.evaluate(economy);  // Mutates economy to the calibrated values actually simulated.
      last_economy = economy;
      const bool comparison_only = body.find("\"comparisonOnly\":true") != std::string::npos
          || body.find("\"comparisonOnly\": true") != std::string::npos;
      if (comparison_only) {
        respond(client, 200, "application/json",
            cad::attach_calibration_json(cad::to_json(result), engine.snapshot()));
      } else {
        auto bargaining = cad::analyze_negotiation(economy, result);
        cad::align_negotiation_trade_channels(economy, result, bargaining);
        auto robustness = cad::analyze_robust_recommendations(economy, result, bargaining, engine.snapshot());
        auto platform = cad::build_trade_diplomacy_platform(economy, result, bargaining);
        last_bargaining = bargaining;
        last_robustness = robustness;
        has_evaluation = true;
        auto with_calibration = cad::attach_calibration_json(cad::to_json(result), engine.snapshot());
        auto with_negotiation = cad::attach_negotiation_json(with_calibration, bargaining);
        auto with_robustness = cad::attach_robustness_json(with_negotiation, robustness);
        respond(client, 200, "application/json", cad::attach_trade_diplomacy_json(with_robustness, platform));
      }
    } else if (first.rfind("POST /api/v2/robustness ", 0) == 0) {
      if (!cad::structural_parameter_registry_complete(structural_registry)) {
        respond(client, 400, "application/json",
            "{\"error\":\"structural parameter registry is incomplete\"}");
      } else {
        cad::Economy economy = body.empty()
            ? last_economy
            : cad::apply_calibration(parse(body), engine.snapshot());
        const int requested = static_cast<int>(number(body, "parameterDraws", 6.0));
        const int draws = std::clamp(requested, 1, 24);
        respond(client, 200, "application/json",
            cad::robustness_to_json(evidence_engine.evaluate_robust(economy, draws)));
      }
    } else if (first.rfind("POST /api/v2/welfare ", 0) == 0) {
      cad::Economy economy = body.empty()
          ? last_economy
          : cad::apply_calibration(parse(body), engine.snapshot());
      respond(client, 200, "application/json",
          cad::welfare_sensitivity_to_json(
              cad::evaluate_welfare_sensitivity(evidence_engine, economy)));
    } else if (first.rfind("GET /api/v2/backtests ", 0) == 0) {
      if (!historical_evidence_ready) {
        historical_backtests = cad::run_historical_evidence(
            evidence_engine, historical_fixture_paths);
        historical_evidence_ready = true;
      }
      respond(client, 200, "application/json",
          cad::historical_evidence_to_json(historical_backtests));
    } else if (first.rfind("GET /api/v2/evidence-status ", 0) == 0) {
      if (!historical_evidence_ready) {
        historical_backtests = cad::run_historical_evidence(
            evidence_engine, historical_fixture_paths);
        historical_evidence_ready = true;
      }
      respond(client, 200, "application/json",
          cad::model_evidence_status_to_json(
              cad::model_evidence_status(structural_registry, historical_backtests)));
    } else if (first.rfind("GET /api/v2/structural-registry ", 0) == 0) {
      respond(client, 200, "application/json",
          cad::structural_parameter_registry_to_json(structural_registry));
    } else if (first.rfind("POST /api/room ", 0) == 0) {
      const bool ok = room.apply_event(body,
          has_evaluation ? &last_bargaining : nullptr,
          has_evaluation ? &last_robustness : nullptr);
      respond(client, ok ? 200 : 400, "application/json",
          room.json(has_evaluation ? &last_bargaining : nullptr,
                    has_evaluation ? &last_robustness : nullptr));
    } else if (first.rfind("GET /api/room ", 0) == 0) {
      respond(client, 200, "application/json",
          room.json(has_evaluation ? &last_bargaining : nullptr,
                    has_evaluation ? &last_robustness : nullptr));
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
    else if (first.rfind("GET /app.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/app.js") + "\n" + read_file("web/evaluation-controller.js"));
    else if (first.rfind("GET /diplomat.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/diplomat.css"));
    else if (first.rfind("GET /diplomat.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/diplomat.js"));
    else if (first.rfind("GET /negotiation-model.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/negotiation-model.css"));
    else if (first.rfind("GET /negotiation-model.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/negotiation-model.js"));
    else if (first.rfind("GET /trade-diplomacy.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/trade-diplomacy.css"));
    else if (first.rfind("GET /trade-diplomacy.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/trade-diplomacy.js"));
    else if (first.rfind("GET /calibration.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/calibration.css"));
    else if (first.rfind("GET /calibration.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/calibration.js"));
    else if (first.rfind("GET /robust-room.css ", 0) == 0) respond(client, 200, "text/css", read_file("web/robust-room.css"));
    else if (first.rfind("GET /robust-room.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/robust-room.js"));
    else respond(client, 404, "text/plain", "Not found");
    close_socket(client);
  }
}
