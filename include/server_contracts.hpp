#pragma once

#include "durable_journal.hpp"
#include "policy_engine.hpp"
#include "request_json.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

namespace cad::server {

inline constexpr double kMaximumUsTariffPercent = 200.0;

inline std::string json_escape(const std::string& value) {
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

inline std::string error_json(const std::string& error) {
  return "{\"error\":\"" + json_escape(error) + "\"}";
}

inline bool valid_operation_id(const std::string& value) {
  if (value.empty() || value.size() > 128) return false;
  for (const unsigned char c : value) {
    if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':'))
      return false;
  }
  return true;
}

inline std::string canonical_scalar_object_json(const request_json::Object& object) {
  std::ostringstream out;
  out << std::setprecision(17) << '{';
  bool comma = false;
  for (const auto& [key, value] : object.values) {
    if (comma) out << ',';
    comma = true;
    out << '\"' << json_escape(key) << "\":";
    switch (value.kind) {
      case request_json::Kind::number:
        out << value.number_value;
        break;
      case request_json::Kind::boolean:
        out << (value.bool_value ? "true" : "false");
        break;
      case request_json::Kind::string:
        out << '\"' << json_escape(value.string_value) << '\"';
        break;
      case request_json::Kind::null_value:
        out << "null";
        break;
    }
  }
  out << '}';
  return out.str();
}

inline bool parse_economy(const request_json::Object& object,
                          Economy& economy, std::string& error) {
  using request_json::number_in_range;
#define CAD_SERVER_FIELD(k, f, lo, hi) \
  if (!number_in_range(object, k, lo, hi, economy.f, error)) return false
  CAD_SERVER_FIELD("policyRate", policy_rate, 0.0, 10.0);
  CAD_SERVER_FIELD("inflation", inflation, -10.0, 30.0);
  CAD_SERVER_FIELD("coreInflation", core_inflation, -10.0, 30.0);
  CAD_SERVER_FIELD("gdpGrowth", gdp_growth, -30.0, 30.0);
  CAD_SERVER_FIELD("outputGap", output_gap, -30.0, 30.0);
  CAD_SERVER_FIELD("unemployment", unemployment, 0.0, 30.0);
  CAD_SERVER_FIELD("wageGrowth", wage_growth, -20.0, 40.0);
  CAD_SERVER_FIELD("productivity", productivity_growth, -20.0, 30.0);
  CAD_SERVER_FIELD("population", population_growth, -10.0, 20.0);
  CAD_SERVER_FIELD("usdcad", usdcad, 0.25, 5.0);
  CAD_SERVER_FIELD("oil", oil_price, 0.0, 500.0);
  CAD_SERVER_FIELD("creditSpread", credit_spread, 0.0, 25.0);
  CAD_SERVER_FIELD("housingGap", housing_gap, -100.0, 100.0);
  CAD_SERVER_FIELD("fiscalBalance", fiscal_balance_gdp, -50.0, 50.0);
  CAD_SERVER_FIELD("federalDebt", federal_debt_gdp, 0.0, 500.0);
  CAD_SERVER_FIELD("globalGrowth", global_growth, -30.0, 30.0);
  CAD_SERVER_FIELD("expectations", inflation_expectations, -10.0, 30.0);
  CAD_SERVER_FIELD("usGrowth", us_growth, -30.0, 30.0);
  CAD_SERVER_FIELD("usInflation", us_inflation, -10.0, 30.0);
  CAD_SERVER_FIELD("usTariff", us_tariff_canada, 0.0, kMaximumUsTariffPercent);
  CAD_SERVER_FIELD("retaliatoryTariff", canada_retaliatory_tariff, 0.0, 100.0);
  CAD_SERVER_FIELD("exportsUs", exports_to_us_share, 0.0, 100.0);
  CAD_SERVER_FIELD("importsUs", imports_from_us_share, 0.0, 100.0);
  CAD_SERVER_FIELD("exportsGdp", exports_gdp, 0.0, 100.0);
  CAD_SERVER_FIELD("importContent", import_content_consumption, 0.0, 100.0);
  CAD_SERVER_FIELD("tradeElasticity", trade_elasticity, 0.01, 20.0);
  CAD_SERVER_FIELD("borderFriction", border_friction, 0.0, 50.0);
  CAD_SERVER_FIELD("tariffRelief", tariff_relief, 0.0, 5.0);
  CAD_SERVER_FIELD("tariffPricePassThrough", tariff_price_pass_through, 0.0, 1.0);
  CAD_SERVER_FIELD("diversification", trade_diversification, 0.0, 1.0);
  CAD_SERVER_FIELD("bilateralExportsCad", canada_exports_to_us_cad, 0.0, 5000.0);
  CAD_SERVER_FIELD("bilateralImportsCad", canada_imports_from_us_cad, 0.0, 5000.0);
  CAD_SERVER_FIELD("canadaPriority", canada_priority, 0.0, 100.0);
  CAD_SERVER_FIELD("usPriority", us_priority, 0.0, 100.0);
  CAD_SERVER_FIELD("riskAversion", risk_aversion, 0.0, 100.0);
  CAD_SERVER_FIELD("cooperationCeiling", cooperation_ceiling, 0.0, 100.0);
  CAD_SERVER_FIELD("minimumBilateralGrowth", minimum_bilateral_growth, -15.0, 15.0);
#undef CAD_SERVER_FIELD
  for (std::size_t i = 0; i < economy.us_sector_coverage.size(); ++i) {
    if (!number_in_range(object, "usSector" + std::to_string(i), 0.0, 100.0,
                         economy.us_sector_coverage[i], error)) return false;
    if (!number_in_range(object, "canadaSector" + std::to_string(i), 0.0, 100.0,
                         economy.canada_sector_coverage[i], error)) return false;
  }
  return true;
}

inline bool request_integer(const request_json::Object& object, const std::string& key,
                            int fallback, int lo, int hi, int& out,
                            std::string& error) {
  const auto* value = object.find(key);
  if (!value) {
    out = fallback;
    return true;
  }
  if (value->kind != request_json::Kind::number
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

class NegotiationState {
 public:
  explicit NegotiationState(std::string event_log_path = {})
      : event_log_path_(std::move(event_log_path)) {
    canada_sectors_.fill(100.0);
    us_sectors_.fill(100.0);
    load();
  }

  unsigned long revision() const { return revision_; }
  bool last_update_replayed() const { return last_update_replayed_; }
  std::size_t recovery_warning_count() const { return recovery_warning_count_; }

  void apply_to(Economy& economy) const {
    economy.us_tariff_canada = us_tariff_;
    economy.canada_retaliatory_tariff = retaliatory_tariff_;
    economy.canada_priority = canada_priority_;
    economy.us_priority = us_priority_;
    economy.risk_aversion = risk_aversion_;
    economy.cooperation_ceiling = cooperation_ceiling_;
    economy.canada_sector_coverage = canada_sectors_;
    economy.us_sector_coverage = us_sectors_;
  }

  std::string json() const {
    std::ostringstream out;
    out << "{\"revision\":" << revision_ << ",\"updatedBy\":\""
        << json_escape(updated_by_) << "\",\"usTariff\":" << us_tariff_
        << ",\"retaliatoryTariff\":" << retaliatory_tariff_
        << ",\"canadaPriority\":" << canada_priority_
        << ",\"usPriority\":" << us_priority_
        << ",\"riskAversion\":" << risk_aversion_
        << ",\"cooperationCeiling\":" << cooperation_ceiling_;
    auto add = [&](const char* key, const auto& values) {
      out << ",\"" << key << "\":[";
      for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
      }
      out << ']';
    };
    add("canadaSectors", canada_sectors_);
    add("usSectors", us_sectors_);
    out << ",\"persistence\":{\"mode\":\"fsync-append-only-event-log\""
        << ",\"operationIdDeduplication\":true,\"recoveryWarnings\":"
        << recovery_warning_count_ << "}}";
    return out.str();
  }

  bool update(const request_json::Object& object, std::string& error) {
    error.clear();
    last_update_replayed_ = false;
    std::string operation_id;
    if (!extract_operation_id(object, operation_id, error)) return false;
    const std::string canonical = canonical_scalar_object_json(object);

    if (!operation_id.empty()) {
      const auto existing = operation_fingerprints_.find(operation_id);
      if (existing != operation_fingerprints_.end()) {
        if (existing->second == canonical) {
          last_update_replayed_ = true;
          return true;
        }
        error = "operationId was already used for a different negotiation update";
        return false;
      }
    }

    NegotiationState candidate = *this;
    candidate.last_update_replayed_ = false;
    if (!candidate.update_in_place(object, error)) return false;

    if (!event_log_path_.empty()
        && !durable_journal::append_line(event_log_path_, canonical)) {
      error = "unable to durably append negotiation update";
      return false;
    }
    if (!operation_id.empty())
      candidate.operation_fingerprints_[operation_id] = canonical;
    *this = std::move(candidate);
    return true;
  }

 private:
  static bool extract_operation_id(const request_json::Object& object,
                                   std::string& operation_id,
                                   std::string& error) {
    const auto* scalar = object.find("operationId");
    if (!scalar) return true;
    if (scalar->kind != request_json::Kind::string) {
      error = "operationId must be a string";
      return false;
    }
    operation_id = scalar->string_value;
    if (!valid_operation_id(operation_id)) {
      error = "operationId contains unsupported characters";
      return false;
    }
    return true;
  }

  bool update_in_place(const request_json::Object& object, std::string& error) {
    const auto actor = object.string("actor");
    if (!actor || (*actor != "canada" && *actor != "us" && *actor != "automatic")) {
      error = "actor must be canada, us, or automatic";
      return false;
    }
    const bool canada = *actor == "canada";
    const bool us = *actor == "us";
    const bool automatic = *actor == "automatic";
    auto bounded = [&](const std::string& key, double lo, double hi, double& target) {
      return request_json::number_in_range(object, key, lo, hi, target, error);
    };
    if (!bounded("riskAversion", 0.0, 100.0, risk_aversion_)
        || !bounded("cooperationCeiling", 0.0, 100.0, cooperation_ceiling_)) return false;
    if (canada) {
      if (!bounded("retaliatoryTariff", 0.0, 60.0, retaliatory_tariff_)
          || !bounded("canadaPriority", 0.0, 100.0, canada_priority_)) return false;
      us_priority_ = 100.0 - canada_priority_;
      updated_by_ = "Canada delegation";
    }
    if (us) {
      if (!bounded("usTariff", 0.0, kMaximumUsTariffPercent, us_tariff_)
          || !bounded("usPriority", 0.0, 100.0, us_priority_)) return false;
      canada_priority_ = 100.0 - us_priority_;
      updated_by_ = "U.S. delegation";
    }
    if (automatic) {
      if (!bounded("usTariff", 0.0, kMaximumUsTariffPercent, us_tariff_)
          || !bounded("retaliatoryTariff", 0.0, 60.0, retaliatory_tariff_)) return false;
      updated_by_ = "automatic win-win search";
    }
    if (canada || automatic) {
      for (std::size_t i = 0; i < canada_sectors_.size(); ++i)
        if (!bounded("canadaSector" + std::to_string(i), 0.0, 100.0,
                     canada_sectors_[i])) return false;
    }
    if (us || automatic) {
      for (std::size_t i = 0; i < us_sectors_.size(); ++i)
        if (!bounded("usSector" + std::to_string(i), 0.0, 100.0,
                     us_sectors_[i])) return false;
    }
    ++revision_;
    return true;
  }

  void load() {
    if (event_log_path_.empty()) return;
    for (const auto& line : durable_journal::read_lines(event_log_path_)) {
      const auto object = request_json::parse_object(line);
      if (!object.valid) {
        ++recovery_warning_count_;
        continue;
      }
      std::string error;
      std::string operation_id;
      if (!extract_operation_id(object, operation_id, error)) {
        ++recovery_warning_count_;
        continue;
      }
      const std::string canonical = canonical_scalar_object_json(object);
      if (!operation_id.empty()) {
        const auto existing = operation_fingerprints_.find(operation_id);
        if (existing != operation_fingerprints_.end()) {
          if (existing->second != canonical) ++recovery_warning_count_;
          continue;
        }
      }

      NegotiationState candidate = *this;
      if (!candidate.update_in_place(object, error)) {
        ++recovery_warning_count_;
        continue;
      }
      if (!operation_id.empty())
        candidate.operation_fingerprints_[operation_id] = canonical;
      *this = std::move(candidate);
    }
    last_update_replayed_ = false;
  }

  std::string event_log_path_;
  unsigned long revision_ = 0;
  double us_tariff_ = 50.0;
  double retaliatory_tariff_ = 5.0;
  double canada_priority_ = 50.0;
  double us_priority_ = 50.0;
  double risk_aversion_ = 50.0;
  double cooperation_ceiling_ = 50.0;
  std::array<double, 20> canada_sectors_{};
  std::array<double, 20> us_sectors_{};
  std::string updated_by_ = "automatic allocation search";
  std::map<std::string, std::string> operation_fingerprints_;
  std::size_t recovery_warning_count_ = 0;
  bool last_update_replayed_ = false;
};

}  // namespace cad::server
