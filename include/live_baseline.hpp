#pragma once

#include "calibration_runtime.hpp"
#include "runtime_configuration.hpp"
#include "state_measurement.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace cad::server {

struct LiveBaselineStatus {
  bool refresh_in_progress = false;
  std::size_t total_failures = 0;
  std::size_t consecutive_failures = 0;
  std::string last_attempt;
  std::string last_success;
  std::string last_error;
};

class LiveBaselineCache {
 public:
  LiveBaselineCache(CalibrationSnapshot calibration,
                    DecisionLossCalibration decision_loss,
                    StateMeasurementRegistry state_registry,
                    std::chrono::seconds refresh_interval = std::chrono::minutes(5))
      : calibration_(std::move(calibration)),
        decision_loss_(std::move(decision_loss)),
        state_registry_(std::move(state_registry)),
        refresh_interval_(refresh_interval) {
    cached_json_ = render("", "", "", true);
    worker_ = std::thread([this] { refresh_loop(); });
  }

  LiveBaselineCache(const LiveBaselineCache&) = delete;
  LiveBaselineCache& operator=(const LiveBaselineCache&) = delete;

  ~LiveBaselineCache() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  std::string json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cached_json_;
  }

  bool refresh_in_progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return refreshing_;
  }

  LiveBaselineStatus status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return LiveBaselineStatus{
        refreshing_, total_failures_, consecutive_failures_,
        last_attempt_, last_success_, last_error_};
  }

 private:
  static std::string download(const char* url) {
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

  static double latest_value(const std::string& json, double fallback) {
    auto pos = json.rfind("\"v\"");
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return fallback;
    pos = json.find_first_of("-0123456789", pos);
    try { return pos == std::string::npos ? fallback : std::stod(json.substr(pos)); }
    catch (...) { return fallback; }
  }

  static std::string utc_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return stamp;
  }

  std::string render(const std::string& rate, const std::string& fx,
                     const std::string& wti, bool warming) const {
    Economy economy = apply_calibration(Economy{}, calibration_);
    economy.loss_weights = decision_loss_.weights;
    const bool rate_live = !rate.empty();
    const bool fx_live = !fx.empty();
    const bool wti_live = !wti.empty();
    economy.policy_rate = latest_value(rate, economy.policy_rate);
    economy.usdcad = latest_value(fx, economy.usdcad);
    economy.oil_price = latest_value(wti, economy.oil_price);
    const int live_fields = static_cast<int>(rate_live) + static_cast<int>(fx_live)
        + static_cast<int>(wti_live);
    const bool any_live = live_fields > 0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "{\"status\":\"" << (any_live ? "live-partial" : "calibrated")
        << "\",\"statusDetail\":\""
        << (warming
            ? "calibrated baseline returned immediately while the live market cache refreshes asynchronously"
            : any_live
                ? "partial live baseline: last-known-good market observations are retained across transient feed failures; macro state fields remain explicitly calibrated/default inputs"
                : "calibrated/default baseline; live Bank of Canada market refresh unavailable")
        << "\",\"asOf\":\"" << utc_stamp()
        << "\",\"cache\":{\"refreshInProgress\":" << (warming ? "true" : "false")
        << ",\"refreshIntervalSeconds\":" << refresh_interval_.count() << "},\"settings\":{";
#define CAD_BASELINE_OUT(k, v) out << "\"" k "\":" << v << ','
    CAD_BASELINE_OUT("policyRate", economy.policy_rate);
    CAD_BASELINE_OUT("inflation", economy.inflation);
    CAD_BASELINE_OUT("coreInflation", economy.core_inflation);
    CAD_BASELINE_OUT("expectations", economy.inflation_expectations);
    CAD_BASELINE_OUT("gdpGrowth", economy.gdp_growth);
    CAD_BASELINE_OUT("outputGap", economy.output_gap);
    CAD_BASELINE_OUT("unemployment", economy.unemployment);
    CAD_BASELINE_OUT("wageGrowth", economy.wage_growth);
    CAD_BASELINE_OUT("productivity", economy.productivity_growth);
    CAD_BASELINE_OUT("population", economy.population_growth);
    CAD_BASELINE_OUT("creditSpread", economy.credit_spread);
    CAD_BASELINE_OUT("housingGap", economy.housing_gap);
    CAD_BASELINE_OUT("usdcad", economy.usdcad);
    CAD_BASELINE_OUT("oil", economy.oil_price);
    CAD_BASELINE_OUT("usTariff", economy.us_tariff_canada);
    CAD_BASELINE_OUT("retaliatoryTariff", economy.canada_retaliatory_tariff);
    CAD_BASELINE_OUT("exportsUs", economy.exports_to_us_share);
    CAD_BASELINE_OUT("importsUs", economy.imports_from_us_share);
    CAD_BASELINE_OUT("exportsGdp", economy.exports_gdp);
    CAD_BASELINE_OUT("importContent", economy.import_content_consumption);
    CAD_BASELINE_OUT("tradeElasticity", economy.trade_elasticity);
    CAD_BASELINE_OUT("borderFriction", economy.border_friction);
    CAD_BASELINE_OUT("tariffPricePassThrough", economy.tariff_price_pass_through);
    CAD_BASELINE_OUT("usGrowth", economy.us_growth);
    CAD_BASELINE_OUT("usInflation", economy.us_inflation);
    CAD_BASELINE_OUT("fiscalBalance", economy.fiscal_balance_gdp);
    CAD_BASELINE_OUT("federalDebt", economy.federal_debt_gdp);
    CAD_BASELINE_OUT("globalGrowth", economy.global_growth);
    CAD_BASELINE_OUT("minimumBilateralGrowth", economy.minimum_bilateral_growth);
#undef CAD_BASELINE_OUT
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
        << (state_measurement_contract_complete(state_registry_) ? "true" : "false")
        << ",\"stateMeasurementReadyCount\":" << ready_state_measurement_count(state_registry_)
        << ",\"decisionLossWeightsComplete\":" << (decision_loss_.complete ? "true" : "false")
        << ",\"observedLive\":["
        << "{\"field\":\"policyRate\",\"source\":\"Bank of Canada Valet V39079\",\"live\":" << (rate_live ? "true" : "false") << "},"
        << "{\"field\":\"usdcad\",\"source\":\"Bank of Canada Valet FXUSDCAD\",\"live\":" << (fx_live ? "true" : "false") << "},"
        << "{\"field\":\"oil\",\"source\":\"Bank of Canada Valet WTI\",\"live\":" << (wti_live ? "true" : "false") << "}],"
        << "\"fallbackStateFields\":[\"inflation\",\"coreInflation\",\"expectations\",\"gdpGrowth\",\"outputGap\",\"unemployment\",\"wageGrowth\",\"productivity\",\"population\",\"creditSpread\",\"housingGap\",\"usGrowth\",\"usInflation\",\"fiscalBalance\",\"federalDebt\",\"globalGrowth\"],"
        << "\"warning\":\"live-partial never means the full modeled state is observed live; fields not certified by a source remain calibrated defaults or explicit user inputs.\"},"
        << "\"calibration\":" << calibration_to_json(calibration_) << "}";
    return out.str();
  }

  void mark_failure(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    refreshing_ = false;
    ++total_failures_;
    ++consecutive_failures_;
    last_error_ = error.empty() ? "live baseline refresh failed" : error;
  }

  void refresh_once() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      refreshing_ = true;
      last_attempt_ = utc_stamp();
    }

    try {
      auto rate = std::async(std::launch::async, [] {
        return download("https://www.bankofcanada.ca/valet/observations/V39079/json?recent=1");
      });
      auto fx = std::async(std::launch::async, [] {
        return download("https://www.bankofcanada.ca/valet/observations/FXUSDCAD/json?recent=1");
      });
      auto wti = std::async(std::launch::async, [] {
        return download("https://www.bankofcanada.ca/valet/observations/WTI/json?recent=1");
      });

      const std::string new_rate = rate.get();
      const std::string new_fx = fx.get();
      const std::string new_wti = wti.get();
      if (new_rate.empty() && new_fx.empty() && new_wti.empty()) {
        mark_failure("all live Bank of Canada market feeds were unavailable");
        return;
      }

      std::string rate_value;
      std::string fx_value;
      std::string wti_value;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!new_rate.empty()) last_rate_ = new_rate;
        if (!new_fx.empty()) last_fx_ = new_fx;
        if (!new_wti.empty()) last_wti_ = new_wti;
        rate_value = last_rate_;
        fx_value = last_fx_;
        wti_value = last_wti_;
      }

      const std::string updated = render(rate_value, fx_value, wti_value, false);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_json_ = updated;
        refreshing_ = false;
        consecutive_failures_ = 0;
        last_error_.clear();
        last_success_ = utc_stamp();
      }
    } catch (const std::exception& error) {
      mark_failure(error.what());
    } catch (...) {
      mark_failure("unknown live baseline refresh exception");
    }
  }

  void refresh_loop() noexcept {
    while (true) {
      try {
        refresh_once();
      } catch (const std::exception& error) {
        mark_failure(error.what());
      } catch (...) {
        mark_failure("unknown live baseline worker exception");
      }
      std::unique_lock<std::mutex> lock(mutex_);
      if (wake_.wait_for(lock, refresh_interval_, [this] { return stopping_; })) return;
    }
  }

  CalibrationSnapshot calibration_;
  DecisionLossCalibration decision_loss_;
  StateMeasurementRegistry state_registry_;
  std::chrono::seconds refresh_interval_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::string cached_json_;
  std::string last_rate_;
  std::string last_fx_;
  std::string last_wti_;
  std::string last_attempt_;
  std::string last_success_;
  std::string last_error_;
  std::size_t total_failures_ = 0;
  std::size_t consecutive_failures_ = 0;
  bool refreshing_ = false;
  bool stopping_ = false;
};

}  // namespace cad::server
