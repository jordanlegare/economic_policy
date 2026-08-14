#include "server.hpp"

#include "calibration.hpp"
#include "http_request.hpp"
#include "live_baseline.hpp"
#include "model_evidence.hpp"
#include "negotiation_support.hpp"
#include "negotiation_trade_alignment.hpp"
#include "request_json.hpp"
#include "robust_recommendation.hpp"
#include "runtime_configuration.hpp"
#include "server_contracts.hpp"
#include "server_session.hpp"
#include "state_measurement.hpp"
#include "structural_calibration.hpp"
#include "thread_pool.hpp"
#include "trade_diplomacy_platform.hpp"
#include "trade_network.hpp"
#include "welfare_sensitivity.hpp"

#ifdef CAD_EMBEDDED_ASSETS
#include "embedded_assets.hpp"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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
#include <sys/time.h>
#include <unistd.h>
#endif

namespace cad::server {
namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void request_shutdown(int) {
  shutdown_requested = 1;
}

bool shutting_down() {
  return shutdown_requested != 0;
}

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

struct SocketGuard {
  explicit SocketGuard(socket_handle value) : value_(value) {}
  ~SocketGuard() { close_socket(value_); }
  SocketGuard(const SocketGuard&) = delete;
  SocketGuard& operator=(const SocketGuard&) = delete;
  socket_handle get() const { return value_; }
 private:
  socket_handle value_ = invalid_socket;
};

void set_socket_timeouts(socket_handle socket, int milliseconds) {
#ifdef _WIN32
  const DWORD timeout = static_cast<DWORD>(milliseconds);
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
      reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
      reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
  timeval timeout{};
  timeout.tv_sec = milliseconds / 1000;
  timeout.tv_usec = (milliseconds % 1000) * 1000;
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

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
  if (!file) return {};
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

void validate_static_assets() {
  for (const char* path : {
      "web/index.html", "web/app.css", "web/app.js", "web/evaluation-controller.js",
      "web/trade-incidence.js", "web/diplomat.css", "web/diplomat.js",
      "web/negotiation-model.css", "web/negotiation-model.js",
      "web/trade-diplomacy.css", "web/trade-diplomacy.js", "web/calibration.css",
      "web/calibration.js", "web/robust-room.css", "web/robust-room.js",
      "web/principal-briefing.css", "web/principal-briefing.js", "web/session.js"}) {
    if (read_file(path).empty()) throw std::runtime_error(std::string("missing web asset: ") + path);
  }
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

std::string diplomatic_index(bool auth_required) {
  auto html = read_file("web/index.html");
  const auto head = html.rfind("</head>");
  if (head != std::string::npos) {
    std::ostringstream injection;
    injection
        << "<link rel=\"stylesheet\" href=\"/diplomat.css\">"
        << "<link rel=\"stylesheet\" href=\"/negotiation-model.css\">"
        << "<link rel=\"stylesheet\" href=\"/trade-diplomacy.css\">"
        << "<link rel=\"stylesheet\" href=\"/calibration.css\">"
        << "<link rel=\"stylesheet\" href=\"/robust-room.css\">"
        << "<script>window.CAD_API_AUTH_REQUIRED=" << (auth_required ? "true" : "false") << ";</script>"
        << "<script src=\"/session.js\"></script>";
    html.insert(head, injection.str());
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

void send_all(socket_handle fd, const std::string& output) {
  std::size_t offset = 0;
  while (offset < output.size()) {
    const int chunk = static_cast<int>(
        std::min<std::size_t>(output.size() - offset, 1024 * 1024));
    const auto sent = ::send(fd, output.data() + offset, chunk, 0);
    if (sent <= 0) break;
    offset += static_cast<std::size_t>(sent);
  }
}

const char* status_text(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

void respond(socket_handle fd, int status, const std::string& type,
             const std::string& body, const std::string& extra_headers = {}) {
  std::ostringstream header;
  header << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Cache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "X-Frame-Options: DENY\r\n"
         << "Referrer-Policy: no-referrer\r\n";
  if (!extra_headers.empty()) header << extra_headers;
  header << "\r\n";
  send_all(fd, header.str() + body);
}

struct ReadResult {
  bool ok = false;
  int status = 400;
  std::string error;
  http::Request request;
};

ReadResult receive_request(socket_handle client) {
  constexpr std::size_t max_header_bytes = 32 * 1024;
  constexpr std::size_t max_body_bytes = 128 * 1024;
  constexpr std::size_t max_total_bytes = max_header_bytes + 4 + max_body_bytes;
  ReadResult out;
  std::string raw;
  raw.reserve(8192);
  char buffer[8192];
  bool have_head = false;
  std::size_t expected_size = 0;

  while (true) {
    const int count = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (count <= 0) {
      if (!have_head || raw.size() < expected_size) {
        out.error = "incomplete HTTP request";
        return out;
      }
      break;
    }
    raw.append(buffer, static_cast<std::size_t>(count));
    if (raw.size() > max_total_bytes) {
      out.status = 413;
      out.error = "request too large";
      return out;
    }

    if (!have_head) {
      const auto split = raw.find("\r\n\r\n");
      if (split == std::string::npos) {
        if (raw.size() > max_header_bytes) {
          out.status = 413;
          out.error = "request headers too large";
          return out;
        }
        continue;
      }
      const auto head = http::parse_head(
          std::string_view(raw).substr(0, split), max_header_bytes, max_body_bytes);
      if (!head.valid) {
        out.status = head.error == "request body too large" ? 413 : 400;
        out.error = head.error;
        return out;
      }
      expected_size = split + 4 + head.content_length;
      have_head = true;
    }

    if (raw.size() == expected_size) break;
    if (raw.size() > expected_size) {
      out.error = "unexpected bytes after HTTP body";
      return out;
    }
  }

  out.request = http::parse_request(raw, max_header_bytes, max_body_bytes);
  if (!out.request.valid) {
    out.error = out.request.error;
    return out;
  }
  out.ok = true;
  out.status = 200;
  return out;
}

std::size_t default_worker_count() {
  const unsigned hardware = std::thread::hardware_concurrency();
  const std::size_t candidate = hardware == 0 ? 4 : static_cast<std::size_t>(hardware);
  return std::clamp<std::size_t>(candidate, 2, 8);
}

bool api_path(const std::string& path) {
  return path == "/api" || path.rfind("/api/", 0) == 0;
}

bool authorized(const http::Request& request, bool required,
                const std::string& token) {
  if (!required || !api_path(request.path)) return true;
  const std::string expected = "Bearer " + token;
  return http::secure_equals(request.header("authorization"), expected);
}

class HistoricalEvidenceCache {
 public:
  std::vector<BacktestResult> get(const PolicyEngine& engine,
                                  const std::vector<std::string>& fixture_paths) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) {
      results_ = run_historical_evidence(engine, fixture_paths);
      ready_ = true;
    }
    return results_;
  }

 private:
  std::mutex mutex_;
  bool ready_ = false;
  std::vector<BacktestResult> results_;
};

struct RuntimeContext {
  RuntimeContext(CalibratedPolicyEngine calibrated_engine,
                 PolicyEngine structural_engine,
                 StructuralParameterRegistry structural_registry_value,
                 DecisionLossCalibration decision_loss_value,
                 StateMeasurementRegistry state_measurements_value,
                 std::vector<std::string> historical_paths,
                 Economy session_baseline,
                 bool auth_required_value,
                 std::string auth_token_value,
                 std::size_t workers)
      : engine(std::move(calibrated_engine)),
        evidence_engine(std::move(structural_engine)),
        structural_registry(std::move(structural_registry_value)),
        decision_loss(std::move(decision_loss_value)),
        state_measurements(std::move(state_measurements_value)),
        historical_fixture_paths(std::move(historical_paths)),
        sessions(runtime_root(), std::move(session_baseline)),
        baseline_cache(engine.snapshot(), decision_loss, state_measurements),
        auth_required(auth_required_value),
        auth_token(std::move(auth_token_value)),
        worker_count(workers) {}

  CalibratedPolicyEngine engine;
  PolicyEngine evidence_engine;
  StructuralParameterRegistry structural_registry;
  DecisionLossCalibration decision_loss;
  StateMeasurementRegistry state_measurements;
  std::vector<std::string> historical_fixture_paths;
  SessionStore sessions;
  LiveBaselineCache baseline_cache;
  HistoricalEvidenceCache historical_cache;
  ThreadPool* pool = nullptr;
  bool auth_required = false;
  std::string auth_token;
  std::size_t worker_count = 1;
};

Economy session_economy(const std::shared_ptr<SessionState>& session) {
  std::lock_guard<std::mutex> lock(session->mutex);
  return session->last_economy;
}

void handle_api(const http::Request& request, socket_handle client,
                RuntimeContext& context) {
  if (request.method == "POST" && !http::is_json_content_type(request)) {
    respond(client, 415, "application/json",
        error_json("POST API requests require Content-Type: application/json"));
    return;
  }

  request_json::Object json;
  if (request.method == "POST") {
    json = request_json::parse_object(request.body.empty() ? "{}" : request.body);
    if (!json.valid) {
      respond(client, 400, "application/json", error_json(json.error));
      return;
    }
  }

  if (request.path == "/api/health" && request.method == "GET") {
    const auto baseline = context.baseline_cache.status();
    const std::size_t queue_depth = context.pool ? context.pool->queued() : 0;
    const std::size_t queue_capacity = context.pool ? context.pool->capacity() : 0;
    const std::size_t active_jobs = context.pool ? context.pool->active() : 0;
    const std::size_t failed_jobs = context.pool ? context.pool->failed() : 0;
    std::ostringstream out;
    out << "{\"status\":\"ok\",\"workers\":" << context.worker_count
        << ",\"sessions\":" << context.sessions.size()
        << ",\"queueDepth\":" << queue_depth
        << ",\"queueCapacity\":" << queue_capacity
        << ",\"activeJobs\":" << active_jobs
        << ",\"failedJobs\":" << failed_jobs
        << ",\"baselineRefreshInProgress\":"
        << (baseline.refresh_in_progress ? "true" : "false")
        << ",\"baselineRefreshFailures\":" << baseline.total_failures
        << ",\"baselineConsecutiveFailures\":" << baseline.consecutive_failures
        << ",\"baselineLastAttempt\":\"" << json_escape(baseline.last_attempt) << "\""
        << ",\"baselineLastSuccess\":\"" << json_escape(baseline.last_success) << "\""
        << ",\"authRequired\":" << (context.auth_required ? "true" : "false") << "}";
    respond(client, 200, "application/json", out.str());
    return;
  }

  if (request.path == "/api/ready" && request.method == "GET") {
    const auto baseline = context.baseline_cache.status();
    const std::size_t queue_depth = context.pool ? context.pool->queued() : 0;
    const std::size_t queue_capacity = context.pool ? context.pool->capacity() : 0;
    const std::size_t active_jobs = context.pool ? context.pool->active() : 0;
    const std::size_t failed_jobs = context.pool ? context.pool->failed() : 0;
    const bool queue_available = context.pool && queue_depth < queue_capacity;
    const bool ready = !shutting_down() && queue_available;
    const char* reason = shutting_down() ? "shutting-down"
        : !context.pool ? "worker-pool-unavailable"
        : !queue_available ? "worker-queue-full" : "ready";
    std::ostringstream out;
    out << "{\"ready\":" << (ready ? "true" : "false")
        << ",\"reason\":\"" << reason << "\""
        << ",\"workers\":" << context.worker_count
        << ",\"queueDepth\":" << queue_depth
        << ",\"queueCapacity\":" << queue_capacity
        << ",\"activeJobs\":" << active_jobs
        << ",\"failedJobs\":" << failed_jobs
        << ",\"baselineRefreshInProgress\":"
        << (baseline.refresh_in_progress ? "true" : "false")
        << ",\"baselineRefreshFailures\":" << baseline.total_failures
        << ",\"baselineConsecutiveFailures\":" << baseline.consecutive_failures
        << ",\"baselineLastAttempt\":\"" << json_escape(baseline.last_attempt) << "\""
        << ",\"baselineLastSuccess\":\"" << json_escape(baseline.last_success) << "\""
        << ",\"baselineLastError\":\"" << json_escape(baseline.last_error) << "\"}";
    respond(client, ready ? 200 : 503, "application/json", out.str());
    return;
  }

  const std::string raw_session_id = request.header("x-cad-session-id");
  if (!raw_session_id.empty() && !valid_session_id(raw_session_id)) {
    respond(client, 400, "application/json", error_json("invalid X-CAD-Session-Id"));
    return;
  }
  std::shared_ptr<SessionState> session;
  try {
    session = context.sessions.get(raw_session_id);
  } catch (const std::exception& error) {
    respond(client, 400, "application/json", error_json(error.what()));
    return;
  }

  if (request.path == "/api/evaluate" && request.method == "POST") {
    Economy economy;
    std::string error;
    if (!parse_economy(json, economy, error)) {
      respond(client, 400, "application/json", error_json(error));
      return;
    }
    bool comparison_only = false;
    if (!request_json::boolean_value(
            json, "comparisonOnly", false, comparison_only, error)) {
      respond(client, 400, "application/json", error_json(error));
      return;
    }
    economy.loss_weights = context.decision_loss.weights;

    // Comparison-only requests have no session side effects and can run in
    // parallel. Stateful evaluations are ordered per session, but the admission
    // wait is bounded so one duplicate run cannot consume a worker indefinitely.
    std::unique_lock<std::timed_mutex> operation_lock(
        session->operation_mutex, std::defer_lock);
    if (!comparison_only
        && !operation_lock.try_lock_for(std::chrono::seconds(1))) {
      respond(client, 503, "application/json",
          error_json("this session already has an evaluation in progress; retry shortly"));
      return;
    }

    auto result = context.engine.evaluate(economy);
    if (comparison_only) {
      respond(client, 200, "application/json",
          attach_calibration_json(to_json(result), context.engine.snapshot()));
      return;
    }

    auto bargaining = analyze_negotiation(economy, result);
    align_negotiation_trade_channels(economy, result, bargaining);
    auto robustness = analyze_robust_recommendations(
        economy, result, bargaining, context.engine.snapshot());
    auto platform = build_trade_diplomacy_platform(economy, result, bargaining);
    {
      std::lock_guard<std::mutex> lock(session->mutex);
      session->last_economy = economy;
      session->last_bargaining = bargaining;
      session->last_robustness = robustness;
      session->has_evaluation = true;
    }
    auto with_calibration = attach_calibration_json(to_json(result), context.engine.snapshot());
    auto with_negotiation = attach_negotiation_json(with_calibration, bargaining);
    auto with_robustness = attach_robustness_json(with_negotiation, robustness);
    respond(client, 200, "application/json",
        attach_trade_diplomacy_json(with_robustness, platform));
    return;
  }

  if ((request.path == "/api/v2/robustness"
       || request.path == "/api/v2/robustness-batch")
      && request.method == "POST") {
    Economy economy = session_economy(session);
    std::string error;
    if (!parse_economy(json, economy, error)) {
      respond(client, 400, "application/json", error_json(error));
      return;
    }
    economy = apply_non_control_calibration(std::move(economy), context.engine.snapshot());
    economy.loss_weights = context.decision_loss.weights;
    const bool batch = request.path == "/api/v2/robustness-batch";
    int draws = batch ? 48 : 6;
    if (!request_integer(json, "parameterDraws", draws,
            batch ? 25 : 1, batch ? 128 : 24, draws, error)) {
      respond(client, 400, "application/json", error_json(error));
      return;
    }
    respond(client, 200, "application/json",
        robustness_to_json(context.evidence_engine.evaluate_robust(
            economy, draws, production_evaluation_options())));
    return;
  }

  if (request.path == "/api/v2/welfare" && request.method == "POST") {
    Economy economy = session_economy(session);
    std::string error;
    if (!parse_economy(json, economy, error)) {
      respond(client, 400, "application/json", error_json(error));
      return;
    }
    economy = apply_non_control_calibration(std::move(economy), context.engine.snapshot());
    economy.loss_weights = context.decision_loss.weights;
    respond(client, 200, "application/json",
        welfare_sensitivity_to_json(evaluate_welfare_sensitivity(
            context.evidence_engine, economy, {}, production_evaluation_options())));
    return;
  }

  if (request.path == "/api/v2/backtests" && request.method == "GET") {
    const auto backtests = context.historical_cache.get(
        context.evidence_engine, context.historical_fixture_paths);
    respond(client, 200, "application/json", historical_evidence_to_json(backtests));
    return;
  }

  if (request.path == "/api/v2/evidence-status" && request.method == "GET") {
    const auto backtests = context.historical_cache.get(
        context.evidence_engine, context.historical_fixture_paths);
    auto status = model_evidence_status(context.structural_registry, backtests);
    status.state_measurement_contract_complete =
        state_measurement_contract_complete(context.state_measurements);
    status.ready_state_measurement_count = ready_state_measurement_count(context.state_measurements);
    status.decision_loss_weights_complete = context.decision_loss.complete;
    status.decision_loss_weight_count = context.decision_loss.recognized_components;
    status.observed_calibration_completeness = context.engine.snapshot().completeness;
    status.observed_calibration_certified = context.engine.snapshot().completeness >= 95.0;
    status.canada_io_empirical = canada_trade_input_output_empirical();
    status.us_io_empirical = us_trade_input_output_empirical();
    respond(client, 200, "application/json", model_evidence_status_to_json(status));
    return;
  }

  if (request.path == "/api/v2/structural-registry" && request.method == "GET") {
    respond(client, 200, "application/json",
        structural_parameter_registry_to_json(context.structural_registry));
    return;
  }

  if (request.path == "/api/v2/state-measurements" && request.method == "GET") {
    respond(client, 200, "application/json",
        state_measurement_registry_to_json(context.state_measurements));
    return;
  }

  if (request.path == "/api/room" && request.method == "POST") {
    std::lock_guard<std::mutex> lock(session->mutex);
    const bool ok = session->room.apply_event(request.body,
        session->has_evaluation ? &session->last_bargaining : nullptr,
        session->has_evaluation ? &session->last_robustness : nullptr);
    respond(client, ok ? 200 : 400, "application/json", session->room_json());
    return;
  }

  if (request.path == "/api/room" && request.method == "GET") {
    std::lock_guard<std::mutex> lock(session->mutex);
    respond(client, 200, "application/json", session->room_json());
    return;
  }

  if (request.path == "/api/negotiation" && request.method == "POST") {
    std::lock_guard<std::mutex> lock(session->mutex);
    std::string error;
    if (!session->negotiation.update(json, error))
      respond(client, 400, "application/json", error_json(error));
    else
      respond(client, 200, "application/json", session->negotiation.json());
    return;
  }

  if (request.path == "/api/negotiation" && request.method == "GET") {
    std::lock_guard<std::mutex> lock(session->mutex);
    respond(client, 200, "application/json", session->negotiation.json());
    return;
  }

  if (request.path == "/api/calibration" && request.method == "GET") {
    respond(client, 200, "application/json", calibration_to_json(context.engine.snapshot()));
    return;
  }

  if (request.path == "/api/baseline" && request.method == "GET") {
    respond(client, 200, "application/json", context.baseline_cache.json());
    return;
  }

  respond(client, 404, "text/plain", "Not found");
}

void handle_client(socket_handle client, RuntimeContext& context) {
  SocketGuard socket(client);
  set_socket_timeouts(client, 15000);
  const auto read = receive_request(client);
  if (!read.ok) {
    respond(client, read.status, "application/json", error_json(read.error));
    return;
  }
  const auto& request = read.request;

  if (!authorized(request, context.auth_required, context.auth_token)) {
    respond(client, 401, "application/json", error_json("valid bearer token required"),
        "WWW-Authenticate: Bearer realm=\"CanadaPolicyStudio\"\r\n");
    return;
  }

  if (api_path(request.path)) {
    handle_api(request, client, context);
    return;
  }

  if (request.method != "GET") {
    respond(client, 405, "text/plain", "Method not allowed");
    return;
  }
  if (request.path == "/") {
    respond(client, 200, "text/html; charset=utf-8",
        diplomatic_index(context.auth_required));
  } else if (request.path == "/app.css") {
    respond(client, 200, "text/css", read_file("web/app.css"));
  } else if (request.path == "/app.js") {
    respond(client, 200, "application/javascript",
        read_file("web/app.js") + "\n" + read_file("web/evaluation-controller.js"));
  } else if (request.path == "/session.js") {
    respond(client, 200, "application/javascript", read_file("web/session.js"));
  } else if (request.path == "/trade-incidence.js") {
    respond(client, 200, "application/javascript", read_file("web/trade-incidence.js"));
  } else if (request.path == "/diplomat.css") {
    respond(client, 200, "text/css", read_file("web/diplomat.css"));
  } else if (request.path == "/diplomat.js") {
    respond(client, 200, "application/javascript", read_file("web/diplomat.js"));
  } else if (request.path == "/negotiation-model.css") {
    respond(client, 200, "text/css", read_file("web/negotiation-model.css"));
  } else if (request.path == "/negotiation-model.js") {
    respond(client, 200, "application/javascript", read_file("web/negotiation-model.js"));
  } else if (request.path == "/trade-diplomacy.css") {
    respond(client, 200, "text/css", read_file("web/trade-diplomacy.css"));
  } else if (request.path == "/trade-diplomacy.js") {
    respond(client, 200, "application/javascript", read_file("web/trade-diplomacy.js"));
  } else if (request.path == "/calibration.css") {
    respond(client, 200, "text/css", read_file("web/calibration.css"));
  } else if (request.path == "/calibration.js") {
    respond(client, 200, "application/javascript", read_file("web/calibration.js"));
  } else if (request.path == "/robust-room.css") {
    respond(client, 200, "text/css", read_file("web/robust-room.css"));
  } else if (request.path == "/robust-room.js") {
    respond(client, 200, "application/javascript", read_file("web/robust-room.js"));
  } else {
    respond(client, 404, "text/plain", "Not found");
  }
}

}  // namespace

int run(const ServerOptions& options) {
  shutdown_requested = 0;
  std::signal(SIGINT, request_shutdown);
  std::signal(SIGTERM, request_shutdown);
#ifndef _WIN32
  std::signal(SIGPIPE, SIG_IGN);
#endif
  if (options.port < 1 || options.port > 65535) {
    std::cerr << "Port must be between 1 and 65535\n";
    return 2;
  }
  if (options.bind_all && options.auth_token.size() < 16) {
    std::cerr << "--bind-all requires an access token of at least 16 characters via "
                 "--auth-token or CAD_POLICY_STUDIO_TOKEN\n";
    return 2;
  }
  if (options.workers < 0 || options.workers > 16) {
    std::cerr << "Worker count must be between 1 and 16, or 0 for automatic\n";
    return 2;
  }

  const std::size_t worker_count = options.workers == 0
      ? default_worker_count() : static_cast<std::size_t>(options.workers);

  SocketRuntime sockets;
  if (!sockets.ok()) {
    std::cerr << "Unable to initialize the network stack\n";
    return 1;
  }

  std::string calibrated_path;
  std::string structural_registry_path;
  std::string decision_loss_path;
  std::string state_measurement_path;
  std::vector<std::string> historical_fixture_paths;
  try {
    validate_static_assets();
    calibrated_path = materialized_data_path("data/calibration/current.snapshot.csv");
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
    std::cerr << "Unable to prepare runtime assets: " << error.what() << '\n';
    return 1;
  }

  const auto structural_registry = load_structural_parameter_registry(
      structural_registry_path);
  if (!structural_parameter_registry_complete(structural_registry)) {
    std::cerr << "Structural parameter registry is incomplete or invalid\n";
    return 1;
  }
  const auto structural_parameters = apply_structural_parameter_registry(
      StructuralParameters{}, structural_registry);
  const auto decision_loss = load_decision_loss_calibration(decision_loss_path);
  const auto state_measurements = load_state_measurement_registry(state_measurement_path);
  if (!decision_loss.complete || !decision_loss_sensitivity_contract_complete(decision_loss)) {
    std::cerr << "Decision-loss calibration is incomplete or inconsistent\n";
    return 1;
  }
  if (!state_measurement_contract_complete(state_measurements)) {
    std::cerr << "State-measurement contract is incomplete\n";
    return 1;
  }

  CalibratedPolicyEngine calibrated_engine(
      calibrated_path, 20260810, structural_parameters, structural_registry);
  PolicyEngine evidence_engine(20260810, structural_parameters, structural_registry);
  Economy session_baseline = apply_calibration(Economy{}, calibrated_engine.snapshot());
  session_baseline.loss_weights = decision_loss.weights;

  RuntimeContext context(
      std::move(calibrated_engine), std::move(evidence_engine), structural_registry,
      decision_loss, state_measurements, historical_fixture_paths,
      session_baseline, options.bind_all, options.auth_token, worker_count);

  socket_handle server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server == invalid_socket) {
    std::cerr << "Unable to create server socket\n";
    return 1;
  }
  SocketGuard server_guard(server);
  int yes = 1;
#ifdef _WIN32
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
      reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(options.bind_all ? INADDR_ANY : INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(options.port));
  if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
      || ::listen(server, 64) != 0) {
    std::cerr << "Unable to listen on port " << options.port << '\n';
    return 1;
  }

  const std::string local_url = "http://localhost:" + std::to_string(options.port);
  std::cout << "Canada–U.S. Diplomatic Policy Studio → "
            << (options.bind_all ? "http://0.0.0.0:" : "http://localhost:")
            << options.port << '\n'
            << "Workers: " << worker_count << " (bounded queue 64)\n"
            << "Session isolation: X-CAD-Session-Id → per-session room/evaluation state\n"
            << "API auth: " << (options.bind_all ? "required for network binding" : "loopback-only") << '\n'
            << "Calibration: " << context.engine.snapshot().grade << " ("
            << context.engine.snapshot().completeness << "% complete, as of "
            << context.engine.snapshot().as_of << ")\n"
            << "Structural registry: " << structural_registry.registry_id << " ("
            << sampled_structural_parameter_count(structural_registry) << " sampled parameters)\n"
            << "Decision loss weights: " << decision_loss.recognized_components << "/12 active\n"
            << "State measurements: " << ready_state_measurement_count(state_measurements)
            << " ready registry entries\n"
            << "U.S. IO network: "
            << (us_trade_input_output_empirical() ? "empirical" : "proxy pending BEA artifact") << '\n'
            << "Live baseline: asynchronous last-known-good cache; request path never waits on Bank of Canada curl fetches\n"
            << "Shutdown: SIGINT/SIGTERM stop admission, then drain queued/running requests\n";
  if (options.launch_browser) open_browser(local_url);

  {
    ThreadPool pool(worker_count, 64);
    context.pool = &pool;
    while (!shutting_down()) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(server, &readable);
      timeval wait{};
      wait.tv_sec = 0;
      wait.tv_usec = 250000;
#ifdef _WIN32
      const int ready = ::select(0, &readable, nullptr, nullptr, &wait);
#else
      const int ready = ::select(server + 1, &readable, nullptr, nullptr, &wait);
#endif
      if (ready < 0) {
#ifndef _WIN32
        if (errno == EINTR) continue;
#endif
        if (!shutting_down()) std::cerr << "Server accept poll failed\n";
        break;
      }
      if (ready == 0 || !FD_ISSET(server, &readable)) continue;

      const socket_handle client = ::accept(server, nullptr, nullptr);
      if (client == invalid_socket) {
        if (shutting_down()) break;
        continue;
      }
      if (!pool.submit([client, &context] { handle_client(client, context); })) {
        respond(client, 503, "application/json",
            error_json("server worker queue is full; retry shortly"));
        close_socket(client);
      }
    }
    context.pool = nullptr;
    std::cout << "Shutdown requested; draining queued and running requests...\n";
  }
  std::cout << "Shutdown complete\n";
  return 0;
}

}  // namespace cad::server
