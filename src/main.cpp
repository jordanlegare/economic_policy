#include "policy_engine.hpp"
#include "calibration.hpp"
#include "model_evidence.hpp"
#include "runtime_configuration.hpp"
#include "state_measurement.hpp"
#include "trade_network.hpp"
#include "negotiation_support.hpp"
#include "negotiation_trade_alignment.hpp"
#include "robust_recommendation.hpp"
#include "negotiation_room.hpp"
#include "structural_calibration.hpp"
#include "trade_diplomacy_platform.hpp"
#include "welfare_sensitivity.hpp"
#include "request_json.hpp"

#ifdef CAD_EMBEDDED_ASSETS
#include "embedded_assets.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
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
  if (result <= 32)
    std::cerr << "Unable to open the default browser automatically. Open " << url << " manually.\n";
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

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c != '"' && c != '\\') out.push_back(c);
    else out.push_back(c);
  }
  return out;
}

std::string error_json(const std::string& error) {
  return "{\"error\":\"" + json_escape(error) + "\"}";
}

bool parse_economy(const cad::request_json::Object& object,
                   cad::Economy& economy, std::string& error) {
  using cad::request_json::number_in_range;
#define FIELD(k, f, lo, hi) \
  if (!number_in_range(object, k, lo, hi, economy.f, error)) return false
  FIELD("policyRate", policy_rate, 0.0, 10.0);
  FIELD("inflation", inflation, -10.0, 30.0);
  FIELD("coreInflation", core_inflation, -10.0, 30.0);
  FIELD("gdpGrowth", gdp_growth, -30.0, 30.0);
  FIELD("outputGap", output_gap, -30.0, 30.0);
  FIELD("unemployment", unemployment, 0.0, 30.0);
  FIELD("wageGrowth", wage_growth, -20.0, 40.0);
  FIELD("productivity", productivity_growth, -20.0, 30.0);
  FIELD("population", population_growth, -10.0, 20.0);
  FIELD("usdcad", usdcad, 0.25, 5.0);
  FIELD("oil", oil_price, 0.0, 500.0);
  FIELD("creditSpread", credit_spread, 0.0, 25.0);
  FIELD("housingGap", housing_gap, -100.0, 100.0);
  FIELD("fiscalBalance", fiscal_balance_gdp, -50.0, 50.0);
  FIELD("federalDebt", federal_debt_gdp, 0.0, 500.0);
  FIELD("globalGrowth", global_growth, -30.0, 30.0);
  FIELD("expectations", inflation_expectations, -10.0, 30.0);
  FIELD("usGrowth", us_growth, -30.0, 30.0);
  FIELD("usInflation", us_inflation, -10.0, 30.0);
  FIELD("usTariff", us_tariff_canada, 0.0, 100.0);
  FIELD("retaliatoryTariff", canada_retaliatory_tariff, 0.0, 100.0);
  FIELD("exportsUs", exports_to_us_share, 0.0, 100.0);
  FIELD("importsUs", imports_from_us_share, 0.0, 100.0);
  FIELD("exportsGdp", exports_gdp, 0.0, 100.0);
  FIELD("importContent", import_content_consumption, 0.0, 100.0);
  FIELD("tradeElasticity", trade_elasticity, 0.01, 20.0);
  FIELD("borderFriction", border_friction, 0.0, 50.0);
  FIELD("tariffRelief", tariff_relief, 0.0, 5.0);
  FIELD("tariffPricePassThrough", tariff_price_pass_through, 0.0, 1.0);
  FIELD("diversification", trade_diversification, 0.0, 1.0);
  FIELD("bilateralExportsCad", canada_exports_to_us_cad, 0.0, 5000.0);
  FIELD("bilateralImportsCad", canada_imports_from_us_cad, 0.0, 5000.0);
  FIELD("canadaPriority", canada_priority, 0.0, 100.0);
  FIELD("usPriority", us_priority, 0.0, 100.0);
  FIELD("riskAversion", risk_aversion, 0.0, 100.0);
  FIELD("cooperationCeiling", cooperation_ceiling, 0.0, 100.0);
  FIELD("minimumBilateralGrowth", minimum_bilateral_growth, -15.0, 15.0);
#undef FIELD
  for (std::size_t i = 0; i < economy.us_sector_coverage.size(); ++i) {
    if (!number_in_range(object, "usSector" + std::to_string(i), 0.0, 100.0,
                         economy.us_sector_coverage[i], error)) return false;
    if (!number_in_range(object, "canadaSector" + std::to_string(i), 0.0, 100.0,
                         economy.canada_sector_coverage[i], error)) return false;
  }
  return true;
}

bool request_integer(const cad::request_json::Object& object, const std::string& key,
                     int fallback, int lo, int hi, int& out, std::string& error) {
  const auto* value = object.find(key);
  if (!value) {
    out = fallback;
    return true;
  }
  if (value->kind != cad::request_json::Kind::number
      || std::floor(value->number_value) != value->number_value) {
    error = key + " must be an integer";
    return false;
  }
  if (value->number_value < lo || value->number_value > hi) {
    error = key + " is outside its allowed range";
    return false;
  }
  out = static_cast<int>(value->number_value);
  return true;
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

std::string live_baseline(const cad::CalibrationSnapshot& calibration,
                          const cad::DecisionLossCalibration& decision_loss,
                          const cad::StateMeasurementRegistry& state_registry) {
  cad::Economy economy = cad::apply_calibration(cad::Economy{}, calibration);
  economy.loss_weights = decision_loss.weights;
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
  const int live_fields = static_cast<int>(rate_live) + static_cast<int>(fx_live)
      + static_cast<int>(wti_live);
  const bool any_live = live_fields > 0;
  std::ostringstream out;
  out << std::fixed << std::setprecision(3)
      << "{\"status\":\"" << (any_live ? "live-partial" : "calibrated")
      << "\",\"statusDetail\":\""
      << (any_live
          ? "partial live baseline: market feeds refreshed where available; macro state fields remain explicitly calibrated/default inputs"
          : "calibrated/default baseline; live Bank of Canada market fetch unavailable")
      << "\",\"asOf\":\"" << stamp << "\",\"settings\":{";
#define OUT(k, v) out << "\"" k "\":" << v << ','
  OUT("policyRate", economy.policy_rate); OUT("inflation", economy.inflation);
  OUT("coreInflation", economy.core_inflation); OUT("expectations", economy.inflation_expectations);
  OUT("gdpGrowth", economy.gdp_growth); OUT("outputGap", economy.output_gap);
  OUT("unemployment", economy.unemployment); OUT("wageGrowth", economy.wage_growth);
  OUT("productivity", economy.productivity_growth); OUT("population", economy.population_growth);
  OUT("creditSpread", economy.credit_spread); OUT("housingGap", economy.housing_gap);
  OUT("usdcad", economy.usdcad); OUT("oil", economy.oil_price);
  OUT("usTariff", economy.us_tariff_canada); OUT("retaliatoryTariff", economy.canada_retaliatory_tariff);
  OUT("exportsUs", economy.exports_to_us_share); OUT("importsUs", economy.imports_from_us_share);
  OUT("exportsGdp", economy.exports_gdp); OUT("importContent", economy.import_content_consumption);
  OUT("tradeElasticity", economy.trade_elasticity); OUT("borderFriction", economy.border_friction);
  OUT("tariffPricePassThrough", economy.tariff_price_pass_through);
  OUT("usGrowth", economy.us_growth); OUT("usInflation", economy.us_inflation);
  OUT("fiscalBalance", economy.fiscal_balance_gdp); OUT("federalDebt", economy.federal_debt_gdp);
  OUT("globalGrowth", economy.global_growth); OUT("minimumBilateralGrowth", economy.minimum_bilateral_growth);
#undef OUT
  out << "\"bilateralExportsCad\":" << economy.canada_exports_to_us_cad
      << ",\"bilateralImportsCad\":" << economy.canada_imports_from_us_cad
      << ",\"diversification\":" << economy.trade_diversification << "},"
      << "\"sources\":["
      << "{\"name\":\"Bank of Canada policy rate\",\"fields\":\"Policy rate · Valet V39079\",\"url\":\"https://www.bankofcanada.ca/valet/\"},"
      << "{\"name\":\"Bank of Canada USD/CAD\",\"fields\":\"Exchange rate · Valet FXUSDCAD\",\"url\":\"https://www.bankofcanada.ca/valet/\"},"
      << "{\"name\":\"Bank of Canada WTI\",\"fields\":\"WTI oil price · Valet WTI\",\"url\":\"https://www.bankofcanada.ca/valet/\"}],"
      << "\"provenance\":{\"liveFieldCount\":" << live_fields
      << ",\"liveEligibleFieldCount\":3"
      << ",\"stateMeasurementContractComplete\":"
      << (cad::state_measurement_contract_complete(state_registry) ? "true" : "false")
      << ",\"stateMeasurementReadyCount\":" << cad::ready_state_measurement_count(state_registry)
      << ",\"decisionLossWeightsComplete\":" << (decision_loss.complete ? "true" : "false")
      << ",\"observedLive\":["
      << "{\"field\":\"policyRate\",\"source\":\"Bank of Canada Valet V39079\",\"live\":" << (rate_live ? "true" : "false") << "},"
      << "{\"field\":\"usdcad\",\"source\":\"Bank of Canada Valet FXUSDCAD\",\"live\":" << (fx_live ? "true" : "false") << "},"
      << "{\"field\":\"oil\",\"source\":\"Bank of Canada Valet WTI\",\"live\":" << (wti_live ? "true" : "false") << "}],"
      << "\"fallbackStateFields\":[\"inflation\",\"coreInflation\",\"expectations\",\"gdpGrowth\",\"outputGap\",\"unemployment\",\"wageGrowth\",\"productivity\",\"population\",\"creditSpread\",\"housingGap\",\"usGrowth\",\"usInflation\",\"fiscalBalance\",\"federalDebt\",\"globalGrowth\"],"
      << "\"warning\":\"live-partial never means the full modeled state is observed live; fields not certified by a source remain calibrated defaults or explicit user inputs.\"},"
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

  bool update(const cad::request_json::Object& object, std::string& error) {
    const auto actor = object.string("actor");
    if (!actor || (*actor != "canada" && *actor != "us" && *actor != "automatic")) {
      error = "actor must be canada, us, or automatic";
      return false;
    }
    const bool canada = *actor == "canada";
    const bool us = *actor == "us";
    const bool automatic = *actor == "automatic";
    auto bounded = [&](const std::string& key, double lo, double hi, double& target) {
      return cad::request_json::number_in_range(object, key, lo, hi, target, error);
    };
    if (!bounded("riskAversion", 0.0, 100.0, risk_aversion)
        || !bounded("cooperationCeiling", 0.0, 100.0, cooperation_ceiling)) return false;
    if (canada) {
      if (!bounded("retaliatoryTariff", 0.0, 60.0, retaliatory_tariff)
          || !bounded("canadaPriority", 0.0, 100.0, canada_priority)) return false;
      us_priority = 100.0 - canada_priority;
      updated_by = "Canada delegation";
    }
    if (us) {
      if (!bounded("usTariff", 0.0, 60.0, us_tariff)
          || !bounded("usPriority", 0.0, 100.0, us_priority)) return false;
      canada_priority = 100.0 - us_priority;
      updated_by = "U.S. delegation";
    }
    if (automatic) {
      if (!bounded("usTariff", 0.0, 60.0, us_tariff)
          || !bounded("retaliatoryTariff", 0.0, 60.0, retaliatory_tariff)) return false;
      updated_by = "automatic win-win search";
    }
    if (canada || automatic) {
      for (std::size_t i = 0; i < canada_sectors.size(); ++i)
        if (!bounded("canadaSector" + std::to_string(i), 0.0, 100.0,
                     canada_sectors[i])) return false;
    }
    if (us || automatic) {
      for (std::size_t i = 0; i < us_sectors.size(); ++i)
        if (!bounded("usSector" + std::to_string(i), 0.0, 100.0,
                     us_sectors[i])) return false;
    }
    ++revision;
    return true;
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
  std::string decision_loss_path;
  std::string state_measurement_path;
  std::vector<std::string> historical_fixture_paths;
  try {
    calibrated_path = calibration_path();
    structural_registry_path = materialized_data_path(
        "data/calibration/structural_parameter_registry.csv");
    decision_loss_path = materialized_data_path(
        "data/calibration/decision_loss_weights.csv");
    state_measurement_path = materialized_data_path(
        "data/calibration/state_measurement_registry.csv");
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
  if (!cad::structural_parameter_registry_complete(structural_registry)) {
    std::cerr << "Structural parameter registry is incomplete or invalid\n";
    close_socket(server);
    return 1;
  }
  const auto structural_parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, structural_registry);
  const auto decision_loss = cad::load_decision_loss_calibration(decision_loss_path);
  const auto state_measurements = cad::load_state_measurement_registry(state_measurement_path);
  if (!decision_loss.complete || !cad::decision_loss_sensitivity_contract_complete(decision_loss)) {
    std::cerr << "Decision-loss calibration is incomplete or inconsistent\n";
    close_socket(server);
    return 1;
  }
  if (!cad::state_measurement_contract_complete(state_measurements)) {
    std::cerr << "State-measurement contract is incomplete\n";
    close_socket(server);
    return 1;
  }
  cad::PolicyEngine evidence_engine(20260810, structural_parameters, structural_registry);

  const auto room_path = (runtime_root() / "negotiation-room.events").string();
  cad::CalibratedPolicyEngine engine(
      calibrated_path, 20260810, structural_parameters, structural_registry);
  NegotiationState negotiation;
  cad::NegotiationRoom room(room_path);
  cad::NegotiationAnalysis last_bargaining;
  cad::RobustRecommendationAnalysis last_robustness;
  cad::Economy last_economy = cad::apply_calibration(cad::Economy{}, engine.snapshot());
  last_economy.loss_weights = decision_loss.weights;
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
            << "Decision loss weights: " << decision_loss.recognized_components << "/12 active\n"
            << "State measurements: " << cad::ready_state_measurement_count(state_measurements) << " ready registry entries\n"
            << "U.S. IO network: " << (cad::us_trade_input_output_empirical() ? "empirical" : "proxy pending BEA artifact") << '\n'
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
        respond(client, 413, "text/plain", "Request too large");
        rejected = true;
        break;
      }
      const auto headers = request.find("\r\n\r\n");
      if (headers != std::string::npos) {
        std::size_t length = 0;
        const auto pos = request.find("Content-Length:");
        if (pos != std::string::npos) {
          try { length = std::stoul(request.substr(pos + 15)); }
          catch (...) {
            respond(client, 400, "application/json", error_json("Invalid Content-Length"));
            rejected = true;
            break;
          }
        }
        if (length > max_request_bytes) {
          respond(client, 413, "text/plain", "Request body too large");
          rejected = true;
          break;
        }
        if (request.size() >= headers + 4 + length) break;
      }
    }
    if (rejected) {
      close_socket(client);
      continue;
    }

    const auto first = request.substr(0, request.find("\r\n"));
    const auto split = request.find("\r\n\r\n");
    const std::string body = split == std::string::npos ? "" : request.substr(split + 4);
    cad::request_json::Object json;
    if (first.rfind("POST ", 0) == 0) {
      json = cad::request_json::parse_object(body.empty() ? "{}" : body);
      if (!json.valid) {
        respond(client, 400, "application/json", error_json(json.error));
        close_socket(client);
        continue;
      }
    }

    if (first.rfind("POST /api/evaluate ", 0) == 0) {
      cad::Economy economy;
      std::string error;
      if (!parse_economy(json, economy, error)) {
        respond(client, 400, "application/json", error_json(error));
        close_socket(client);
        continue;
      }
      bool comparison_only = false;
      if (!cad::request_json::boolean_value(
              json, "comparisonOnly", false, comparison_only, error)) {
        respond(client, 400, "application/json", error_json(error));
        close_socket(client);
        continue;
      }
      economy.loss_weights = decision_loss.weights;
      auto result = engine.evaluate(economy);
      if (comparison_only) {
        respond(client, 200, "application/json",
            cad::attach_calibration_json(cad::to_json(result), engine.snapshot()));
      } else {
        auto bargaining = cad::analyze_negotiation(economy, result);
        cad::align_negotiation_trade_channels(economy, result, bargaining);
        auto robustness = cad::analyze_robust_recommendations(
            economy, result, bargaining, engine.snapshot());
        auto platform = cad::build_trade_diplomacy_platform(economy, result, bargaining);
        last_economy = economy;
        last_bargaining = bargaining;
        last_robustness = robustness;
        has_evaluation = true;
        auto with_calibration = cad::attach_calibration_json(cad::to_json(result), engine.snapshot());
        auto with_negotiation = cad::attach_negotiation_json(with_calibration, bargaining);
        auto with_robustness = cad::attach_robustness_json(with_negotiation, robustness);
        respond(client, 200, "application/json",
            cad::attach_trade_diplomacy_json(with_robustness, platform));
      }
    } else if (first.rfind("POST /api/v2/robustness ", 0) == 0) {
      cad::Economy economy = last_economy;
      std::string error;
      if (!body.empty()) {
        economy = cad::Economy{};
        if (!parse_economy(json, economy, error)) {
          respond(client, 400, "application/json", error_json(error));
          close_socket(client);
          continue;
        }
        economy = cad::apply_non_control_calibration(std::move(economy), engine.snapshot());
      }
      economy.loss_weights = decision_loss.weights;
      int draws = 6;
      if (!request_integer(json, "parameterDraws", 6, 1, 24, draws, error)) {
        respond(client, 400, "application/json", error_json(error));
        close_socket(client);
        continue;
      }
      respond(client, 200, "application/json",
          cad::robustness_to_json(evidence_engine.evaluate_robust(
              economy, draws, cad::production_evaluation_options())));
    } else if (first.rfind("POST /api/v2/welfare ", 0) == 0) {
      cad::Economy economy = last_economy;
      std::string error;
      if (!body.empty()) {
        economy = cad::Economy{};
        if (!parse_economy(json, economy, error)) {
          respond(client, 400, "application/json", error_json(error));
          close_socket(client);
          continue;
        }
        economy = cad::apply_non_control_calibration(std::move(economy), engine.snapshot());
      }
      economy.loss_weights = decision_loss.weights;
      respond(client, 200, "application/json",
          cad::welfare_sensitivity_to_json(
              cad::evaluate_welfare_sensitivity(
                  evidence_engine, economy, {}, cad::production_evaluation_options())));
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
      auto status = cad::model_evidence_status(structural_registry, historical_backtests);
      status.state_measurement_contract_complete =
          cad::state_measurement_contract_complete(state_measurements);
      status.ready_state_measurement_count = cad::ready_state_measurement_count(state_measurements);
      status.decision_loss_weights_complete = decision_loss.complete;
      status.decision_loss_weight_count = decision_loss.recognized_components;
      status.observed_calibration_completeness = engine.snapshot().completeness;
      status.observed_calibration_certified = engine.snapshot().completeness >= 95.0;
      status.canada_io_empirical = cad::canada_trade_input_output_empirical();
      status.us_io_empirical = cad::us_trade_input_output_empirical();
      respond(client, 200, "application/json", cad::model_evidence_status_to_json(status));
    } else if (first.rfind("GET /api/v2/structural-registry ", 0) == 0) {
      respond(client, 200, "application/json",
          cad::structural_parameter_registry_to_json(structural_registry));
    } else if (first.rfind("GET /api/v2/state-measurements ", 0) == 0) {
      respond(client, 200, "application/json",
          cad::state_measurement_registry_to_json(state_measurements));
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
      std::string error;
      if (!negotiation.update(json, error))
        respond(client, 400, "application/json", error_json(error));
      else
        respond(client, 200, "application/json", negotiation.json());
    } else if (first.rfind("GET /api/negotiation ", 0) == 0) {
      respond(client, 200, "application/json", negotiation.json());
    } else if (first.rfind("GET /api/calibration ", 0) == 0) {
      respond(client, 200, "application/json", cad::calibration_to_json(engine.snapshot()));
    } else if (first.rfind("GET /api/baseline ", 0) == 0) {
      respond(client, 200, "application/json",
          live_baseline(engine.snapshot(), decision_loss, state_measurements));
    } else if (first.rfind("GET / ", 0) == 0) {
      respond(client, 200, "text/html; charset=utf-8", diplomatic_index());
    } else if (first.rfind("GET /app.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/app.css"));
    } else if (first.rfind("GET /app.js ", 0) == 0) {
      respond(client, 200, "application/javascript",
          read_file("web/app.js") + "\n" + read_file("web/evaluation-controller.js"));
    } else if (first.rfind("GET /trade-incidence.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/trade-incidence.js"));
    } else if (first.rfind("GET /diplomat.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/diplomat.css"));
    } else if (first.rfind("GET /diplomat.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/diplomat.js"));
    } else if (first.rfind("GET /negotiation-model.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/negotiation-model.css"));
    } else if (first.rfind("GET /negotiation-model.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/negotiation-model.js"));
    } else if (first.rfind("GET /trade-diplomacy.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/trade-diplomacy.css"));
    } else if (first.rfind("GET /trade-diplomacy.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/trade-diplomacy.js"));
    } else if (first.rfind("GET /calibration.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/calibration.css"));
    } else if (first.rfind("GET /calibration.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/calibration.js"));
    } else if (first.rfind("GET /robust-room.css ", 0) == 0) {
      respond(client, 200, "text/css", read_file("web/robust-room.css"));
    } else if (first.rfind("GET /robust-room.js ", 0) == 0) {
      respond(client, 200, "application/javascript", read_file("web/robust-room.js"));
    } else {
      respond(client, 404, "text/plain", "Not found");
    }
    close_socket(client);
  }
}
