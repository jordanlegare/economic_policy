#pragma once

#include "durable_journal.hpp"
#include "policy_engine.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace cad::server::evaluation_provenance {

inline std::string escape_json(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(static_cast<char>(c)); break;
    }
  }
  return out;
}

inline std::string effective_input_json(const Economy& e) {
  std::ostringstream out;
  out << std::setprecision(17) << '{';
  bool comma = false;
  auto number = [&](const char* key, double value) {
    if (comma) out << ',';
    comma = true;
    out << '"' << key << "\":" << value;
  };
  auto boolean = [&](const char* key, bool value) {
    if (comma) out << ',';
    comma = true;
    out << '"' << key << "\":" << (value ? "true" : "false");
  };
  auto values = [&](const char* key, const auto& array) {
    if (comma) out << ',';
    comma = true;
    out << '"' << key << "\":[";
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (i) out << ',';
      out << array[i];
    }
    out << ']';
  };

  number("policyRate", e.policy_rate);
  number("inflation", e.inflation);
  number("coreInflation", e.core_inflation);
  number("gdpGrowth", e.gdp_growth);
  number("outputGap", e.output_gap);
  number("unemployment", e.unemployment);
  number("wageGrowth", e.wage_growth);
  number("productivity", e.productivity_growth);
  number("population", e.population_growth);
  number("usdcad", e.usdcad);
  number("oil", e.oil_price);
  number("creditSpread", e.credit_spread);
  number("housingGap", e.housing_gap);
  number("householdDebtIncome", e.household_debt_income);
  number("fiscalBalance", e.fiscal_balance_gdp);
  number("federalDebt", e.federal_debt_gdp);
  number("programGrowth", e.program_growth);
  number("taxImpulse", e.tax_impulse);
  number("infrastructureImpulse", e.infrastructure_impulse);
  number("globalGrowth", e.global_growth);
  number("expectations", e.inflation_expectations);
  number("usGrowth", e.us_growth);
  number("usInflation", e.us_inflation);
  number("usTariff", e.us_tariff_canada);
  number("retaliatoryTariff", e.canada_retaliatory_tariff);
  number("exportsUs", e.exports_to_us_share);
  number("importsUs", e.imports_from_us_share);
  number("exportsGdp", e.exports_gdp);
  number("importContent", e.import_content_consumption);
  number("tradeElasticity", e.trade_elasticity);
  number("borderFriction", e.border_friction);
  number("tariffPricePassThrough", e.tariff_price_pass_through);
  number("bilateralExportsCad", e.canada_exports_to_us_cad);
  number("bilateralImportsCad", e.canada_imports_from_us_cad);
  number("tariffRelief", e.tariff_relief);
  number("diversification", e.trade_diversification);
  number("canadaPriority", e.canada_priority);
  number("usPriority", e.us_priority);
  number("riskAversion", e.risk_aversion);
  number("cooperationCeiling", e.cooperation_ceiling);
  number("minimumBilateralGrowth", e.minimum_bilateral_growth);
  number("lossBocInflation", e.loss_weights.boc_inflation);
  number("lossBocUnemployment", e.loss_weights.boc_unemployment);
  number("lossBocContraction", e.loss_weights.boc_contraction);
  number("lossBocRecession", e.loss_weights.boc_recession);
  number("lossFederalDebt", e.loss_weights.federal_debt);
  number("lossFederalContraction", e.loss_weights.federal_contraction);
  number("lossFederalUnemployment", e.loss_weights.federal_unemployment);
  number("lossFederalHousing", e.loss_weights.federal_housing);
  number("lossUsExports", e.loss_weights.us_exports);
  number("lossUsInflation", e.loss_weights.us_inflation);
  number("lossUsGrowth", e.loss_weights.us_growth);
  number("lossUsRetaliation", e.loss_weights.us_retaliation);
  boolean("exhaustivePolicySearch", e.exhaustive_policy_search);
  values("usSectorCoverage", e.us_sector_coverage);
  values("canadaSectorCoverage", e.canada_sector_coverage);
  values("usSectorTradeElasticity", e.us_sector_trade_elasticity);
  values("canadaSectorTradeElasticity", e.canada_sector_trade_elasticity);
  values("usSectorPricePassThrough", e.us_sector_price_pass_through);
  values("canadaSectorPricePassThrough", e.canada_sector_price_pass_through);
  out << '}';
  return out.str();
}

inline std::string fingerprint(const Economy& economy) {
  const std::string input = effective_input_json(economy);
  std::uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : input) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ull;
  }
  std::ostringstream out;
  out << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

inline std::string submission_json(const Economy& economy,
                                   const std::string& input_fingerprint,
                                   unsigned long negotiation_revision,
                                   const std::string& calibration_snapshot_id,
                                   const std::string& structural_registry_id) {
  std::ostringstream out;
  out << "{\"schemaVersion\":1,\"inputFingerprint\":\""
      << escape_json(input_fingerprint)
      << "\",\"negotiationRevision\":" << negotiation_revision
      << ",\"calibrationSnapshotId\":\"" << escape_json(calibration_snapshot_id)
      << "\",\"structuralRegistryId\":\"" << escape_json(structural_registry_id)
      << "\",\"input\":" << effective_input_json(economy) << '}';
  return out.str();
}

inline bool checkpoint_submission(const std::string& path,
                                  const Economy& economy,
                                  const std::string& input_fingerprint,
                                  unsigned long negotiation_revision,
                                  const std::string& calibration_snapshot_id,
                                  const std::string& structural_registry_id) {
  return durable_journal::append_line(path,
      submission_json(economy, input_fingerprint, negotiation_revision,
                      calibration_snapshot_id, structural_registry_id));
}

inline std::string attach_json(std::string base_json,
                               const std::string& input_fingerprint,
                               unsigned long negotiation_revision,
                               const std::string& calibration_snapshot_id,
                               const std::string& structural_registry_id,
                               bool stale = false) {
  if (base_json.size() < 2 || base_json.front() != '{' || base_json.back() != '}')
    return base_json;
  base_json.pop_back();
  std::ostringstream provenance;
  provenance << ",\"evaluationProvenance\":{\"inputFingerprint\":\""
             << escape_json(input_fingerprint)
             << "\",\"negotiationRevision\":" << negotiation_revision
             << ",\"calibrationSnapshotId\":\"" << escape_json(calibration_snapshot_id)
             << "\",\"structuralRegistryId\":\"" << escape_json(structural_registry_id)
             << "\",\"stale\":" << (stale ? "true" : "false") << "}}";
  base_json += provenance.str();
  return base_json;
}

}  // namespace cad::server::evaluation_provenance
