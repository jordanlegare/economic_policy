from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, value):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(value, encoding="utf-8")


def replace_once(path, old, new):
    value = text(path)
    count = value.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one occurrence, got {count}: {old[:120]!r}")
    write(path, value.replace(old, new, 1))


def replace_all(path, old, new, minimum=1):
    value = text(path)
    count = value.count(old)
    if count < minimum:
        raise RuntimeError(f"{path}: expected >= {minimum} occurrences, got {count}: {old[:120]!r}")
    write(path, value.replace(old, new))


def replace_between(path, start_marker, end_marker, replacement):
    value = text(path)
    start = value.find(start_marker)
    if start < 0:
        raise RuntimeError(f"{path}: start marker not found: {start_marker!r}")
    end = value.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f"{path}: end marker not found: {end_marker!r}")
    write(path, value[:start] + replacement + value[end:])


def replace_js_function(path, name, replacement):
    value = text(path)
    marker = f"function {name}("
    start = value.find(marker)
    if start < 0:
        raise RuntimeError(f"{path}: function {name} not found")
    next_fn = value.find("function ", start + len(marker))
    if next_fn < 0:
        raise RuntimeError(f"{path}: next function after {name} not found")
    write(path, value[:start] + replacement + value[next_fn:])


# ---------------------------------------------------------------------------
# Executable runtime configuration for model-design loss weights.
# ---------------------------------------------------------------------------
write("include/runtime_configuration.hpp", r'''#pragma once

#include "calibration_schema.hpp"
#include "policy_engine.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace cad {

struct DecisionLossCalibrationEntry {
  double baseline = 0.0;
  double sensitivity_low = 0.0;
  double sensitivity_high = 0.0;
};

struct DecisionLossCalibration {
  bool loaded = false;
  bool complete = false;
  int recognized_components = 0;
  DecisionLossWeights weights{};
  std::map<std::string, DecisionLossCalibrationEntry> entries;
};

inline bool assign_decision_loss_weight(DecisionLossWeights& w,
                                        const std::string& name,
                                        double value) {
  if (name == "boc_inflation") w.boc_inflation = value;
  else if (name == "boc_unemployment") w.boc_unemployment = value;
  else if (name == "boc_contraction") w.boc_contraction = value;
  else if (name == "boc_recession") w.boc_recession = value;
  else if (name == "federal_debt") w.federal_debt = value;
  else if (name == "federal_contraction") w.federal_contraction = value;
  else if (name == "federal_unemployment") w.federal_unemployment = value;
  else if (name == "federal_housing") w.federal_housing = value;
  else if (name == "us_exports") w.us_exports = value;
  else if (name == "us_inflation") w.us_inflation = value;
  else if (name == "us_growth") w.us_growth = value;
  else if (name == "us_retaliation") w.us_retaliation = value;
  else return false;
  return true;
}

inline DecisionLossCalibration load_decision_loss_calibration(const std::string& path) {
  DecisionLossCalibration out;
  std::ifstream in(path);
  if (!in) return out;
  out.loaded = true;
  std::string line;
  bool header = true;
  std::set<std::string> recognized;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (header) { header = false; continue; }
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 7) continue;
    const double baseline = calibration_detail::number(f[1]);
    const double low = calibration_detail::number(f[5]);
    const double high = calibration_detail::number(f[6]);
    if (!(baseline >= 0.0) || !(low >= 0.0) || !(high >= low)) continue;
    if (!assign_decision_loss_weight(out.weights, f[0], baseline)) continue;
    out.entries[f[0]] = {baseline, low, high};
    recognized.insert(f[0]);
  }
  out.recognized_components = static_cast<int>(recognized.size());
  out.complete = out.loaded && out.recognized_components == 12;
  return out;
}

inline bool decision_loss_sensitivity_contract_complete(
    const DecisionLossCalibration& calibration) {
  if (!calibration.complete) return false;
  for (const auto& item : calibration.entries) {
    const auto& e = item.second;
    if (!(e.baseline > 0.0)) return false;
    if (std::abs(e.sensitivity_low / e.baseline - 0.80) > 1e-9) return false;
    if (std::abs(e.sensitivity_high / e.baseline - 1.20) > 1e-9) return false;
  }
  return true;
}

}  // namespace cad
''')

# ---------------------------------------------------------------------------
# Policy-engine public contract: explicit search options, canonical Canada
# welfare, and aggregate tariff pass-through as runtime state.
# ---------------------------------------------------------------------------
replace_once(
    "include/policy_engine.hpp",
    "  double trade_elasticity = 0.65, border_friction = 2.0;\n",
    "  double trade_elasticity = 0.65, border_friction = 2.0;\n"
    "  double tariff_price_pass_through = 0.24;\n")
replace_once(
    "include/policy_engine.hpp",
    "  double score = 0.0, boc_score = 0.0, federal_score = 0.0, us_score = 0.0;\n",
    "  double score = 0.0, boc_score = 0.0, federal_score = 0.0;\n"
    "  double canada_score = 0.0, us_score = 0.0;\n")
replace_once(
    "include/policy_engine.hpp",
    "class PolicyEngine {\n public:\n",
    "struct EvaluationOptions {\n"
    "  bool exhaustive_policy_search = false;\n"
    "};\n\n"
    "inline EvaluationOptions production_evaluation_options() {\n"
    "  return EvaluationOptions{true};\n"
    "}\n\n"
    "class PolicyEngine {\n public:\n")
replace_once(
    "include/policy_engine.hpp",
    "  Result evaluate(const Economy& economy) const;\n",
    "  Result evaluate(const Economy& economy) const;\n"
    "  Result evaluate(const Economy& economy, EvaluationOptions options) const;\n")
replace_once(
    "include/policy_engine.hpp",
    "  Result evaluate_robust(const Economy& economy, int parameter_draws = 24) const;\n",
    "  Result evaluate_robust(const Economy& economy, int parameter_draws = 24) const;\n"
    "  Result evaluate_robust(const Economy& economy, int parameter_draws,\n"
    "                         EvaluationOptions options) const;\n")

# ---------------------------------------------------------------------------
# Split calibration into non-control production evidence and user-controlled
# tariffs/coverage. This is the core fix that keeps empirical coefficients live
# on every browser/V2 request without overwriting a what-if posture.
# ---------------------------------------------------------------------------
new_calibration_functions = r'''inline Economy apply_non_control_calibration(
    Economy economy, const CalibrationSnapshot& snapshot) {
  auto apply = [&](const char* key, double& field) {
    const auto* p = calibration_detail::parameter(snapshot, key);
    if (p && p->use_in_model && calibration_detail::trusted_kind(p->kind)) field = p->value;
  };
  apply("canada_exports_to_us_cad", economy.canada_exports_to_us_cad);
  apply("canada_imports_from_us_cad", economy.canada_imports_from_us_cad);
  apply("exports_to_us_share", economy.exports_to_us_share);
  apply("imports_from_us_share", economy.imports_from_us_share);
  apply("trade_elasticity", economy.trade_elasticity);
  apply("border_friction", economy.border_friction);

  // Pass-through is a production coefficient, not a negotiation control. The
  // snapshot therefore reattaches it server-side on every evaluation rather
  // than relying on the browser to round-trip hidden calibration state.
  if (const auto* pass = calibration_detail::parameter(
          snapshot, "tariff_price_pass_through_anchor")) {
    if (pass->kind == "empirically-estimated" && pass->value >= 0.0
        && pass->value <= 1.0)
      economy.tariff_price_pass_through = pass->value;
  }

  // The current sector elasticity evidence is a production-compatible sector
  // elasticity rather than a directional tariff-specific estimate, so it is
  // used in both import directions. The committed pass-through evidence is from
  // Canadian retaliatory-tariff incidence and is activated only for Canadian
  // imports; the U.S. direction deliberately continues to use the aggregate
  // anchor until compatible U.S. evidence exists.
  for (std::size_t i = 0; i < snapshot.sectors.size(); ++i) {
    const auto& sector = snapshot.sectors[i];
    if (sector.elasticity_kind == "empirically-estimated"
        && sector.trade_elasticity > 0.0) {
      economy.us_sector_trade_elasticity[i] = sector.trade_elasticity;
      economy.canada_sector_trade_elasticity[i] = sector.trade_elasticity;
    }
    if (calibration_detail::empirical_pass_through_kind(sector.pass_through_kind)
        && sector.price_pass_through >= 0.0 && sector.price_pass_through <= 1.0)
      economy.canada_sector_price_pass_through[i] = sector.price_pass_through;
  }
  return economy;
}

inline Economy apply_calibration(Economy economy, const CalibrationSnapshot& snapshot) {
  economy = apply_non_control_calibration(std::move(economy), snapshot);

  // The engine represents the frozen legal/effective tariff calibration as a
  // maximum applied goods rate plus per-sector coverage. Non-merchandise
  // sectors are explicitly zero rather than receiving fabricated tariff lines.
  if (snapshot.tariff_lines_complete) {
    double max_us = 0.0, max_ca = 0.0;
    for (const auto& s : snapshot.sectors) {
      max_us = std::max(max_us, s.us_effective_tariff);
      max_ca = std::max(max_ca, s.canada_effective_tariff);
    }
    if (max_us > 1e-9) {
      economy.us_tariff_canada = max_us;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.us_sector_coverage[i] = 100.0 * std::max(0.0, snapshot.sectors[i].us_effective_tariff) / max_us;
    }
    if (max_ca > 1e-9) {
      economy.canada_retaliatory_tariff = max_ca;
      for (std::size_t i = 0; i < snapshot.sectors.size(); ++i)
        economy.canada_sector_coverage[i] = 100.0 * std::max(0.0, snapshot.sectors[i].canada_effective_tariff) / max_ca;
    }
  }
  return economy;
}

'''
replace_between(
    "include/calibration_runtime.hpp",
    "inline Economy apply_calibration(Economy economy, const CalibrationSnapshot& snapshot) {",
    "inline std::string calibration_to_json",
    new_calibration_functions)
replace_once(
    "include/calibration_runtime.hpp",
    "  const Economy effective = apply_calibration(Economy{}, snapshot);\n",
    "  const Economy effective = apply_calibration(Economy{}, snapshot);\n"
    "  int sector_elasticity_overrides = 0;\n"
    "  int canada_pass_through_overrides = 0;\n"
    "  int us_pass_through_overrides = 0;\n"
    "  for (std::size_t i = 0; i < effective.us_sector_trade_elasticity.size(); ++i) {\n"
    "    if (effective.us_sector_trade_elasticity[i] > 0.0\n"
    "        || effective.canada_sector_trade_elasticity[i] > 0.0)\n"
    "      ++sector_elasticity_overrides;\n"
    "    if (effective.canada_sector_price_pass_through[i] > 0.0)\n"
    "      ++canada_pass_through_overrides;\n"
    "    if (effective.us_sector_price_pass_through[i] > 0.0)\n"
    "      ++us_pass_through_overrides;\n"
    "  }\n")
replace_once(
    "include/calibration_runtime.hpp",
    "  out << \"']}\"\n      << \",\\\"checks\\\":{\\\"officialTrade\\\":\"",
    "  out << \"'],\\\"runtimeActivation\\\":{\\\"aggregateTradeElasticity\\\":\" << effective.trade_elasticity\n"
    "      << \",\\\"aggregateTariffPricePassThrough\\\":\" << effective.tariff_price_pass_through\n"
    "      << \",\\\"sectorElasticityOverrideCount\\\":\" << sector_elasticity_overrides\n"
    "      << \",\\\"canadaPassThroughOverrideCount\\\":\" << canada_pass_through_overrides\n"
    "      << \",\\\"usPassThroughOverrideCount\\\":\" << us_pass_through_overrides << \"}}\"\n"
    "      << \",\\\"checks\\\":{\\\"officialTrade\\\":\"")
replace_once(
    "include/calibration_runtime.hpp",
    "    economy.exhaustive_policy_search = true;\n    Result result = base_.evaluate(economy);\n",
    "    economy = apply_non_control_calibration(std::move(economy), snapshot_);\n"
    "    Result result = base_.evaluate(economy, production_evaluation_options());\n")

# ---------------------------------------------------------------------------
# Runtime trade-network bounds and pass-through source of truth.
# ---------------------------------------------------------------------------
replace_once(
    "src/trade_network.cpp",
    "input.us_trade_elasticity[source], input.trade_elasticity, .10, 5.0);",
    "input.us_trade_elasticity[source], input.trade_elasticity, .10, 20.0);")
replace_once(
    "src/trade_network.cpp",
    "input.canada_trade_elasticity[source], input.trade_elasticity, .10, 5.0);",
    "input.canada_trade_elasticity[source], input.trade_elasticity, .10, 20.0);")

replace_once(
    "src/policy_engine.cpp",
    "constexpr double kTariffPricePassThroughAnchor = 0.24;\n",
    "")
replace_once(
    "src/policy_engine.cpp",
    "  input.price_pass_through = kTariffPricePassThroughAnchor;\n",
    "  input.price_pass_through = e.tariff_price_pass_through;\n")
replace_once(
    "src/policy_engine.cpp",
    "Result PolicyEngine::evaluate(const Economy& e) const {\n  Result r;\n",
    "Result PolicyEngine::evaluate(const Economy& economy) const {\n"
    "  return evaluate(economy, EvaluationOptions{economy.exhaustive_policy_search});\n"
    "}\n\n"
    "Result PolicyEngine::evaluate(const Economy& economy, EvaluationOptions options) const {\n"
    "  Economy e = economy;\n"
    "  e.exhaustive_policy_search = options.exhaustive_policy_search;\n"
    "  Result r;\n")
replace_once(
    "src/policy_engine.cpp",
    "  s.boc_score = 100.0 / (1.0 + mandate_loss);\n  s.federal_score = 100.0 / (1.0 + federal_loss);\n\n  const double us_inflation_pressure",
    "  s.boc_score = 100.0 / (1.0 + mandate_loss);\n"
    "  s.federal_score = 100.0 / (1.0 + federal_loss);\n"
    "  s.canada_score = std::sqrt(std::max(.01, s.boc_score)\n"
    "      * std::max(.01, s.federal_score));\n\n"
    "  const double us_inflation_pressure")
replace_all(
    "src/policy_engine.cpp",
    "std::sqrt(std::max(.01, s.boc_score) * std::max(.01, s.federal_score))",
    "s.canada_score",
    minimum=2)
replace_all(
    "src/policy_engine.cpp",
    "std::sqrt(std::max(.01, verified.boc_score)\n            * std::max(.01, verified.federal_score))",
    "verified.canada_score",
    minimum=1)
replace_once(
    "src/policy_engine.cpp",
    "    baseline_canada = std::sqrt(std::max(.01, starting_baseline.boc_score)\n        * std::max(.01, starting_baseline.federal_score));\n",
    "    baseline_canada = starting_baseline.canada_score;\n")
replace_once(
    "src/policy_engine.cpp",
    "  r.recommendation.verified_canada_score =\n      std::sqrt(std::max(.01, best.boc_score) * std::max(.01, best.federal_score));\n",
    "  r.recommendation.verified_canada_score = best.canada_score;\n")
replace_once(
    "src/policy_engine.cpp",
    "      << \",\\\"federalScore\\\":\" << s.federal_score\n      << \",\\\"usScore\\\":\" << s.us_score\n",
    "      << \",\\\"federalScore\\\":\" << s.federal_score\n"
    "      << \",\\\"canadaScore\\\":\" << s.canada_score\n"
    "      << \",\\\"usScore\\\":\" << s.us_score\n")
replace_once(
    "src/policy_engine.cpp",
    "    << \"\\\",\\\"confidence\\\":\" << r.data_confidence\n",
    "    << \"\\\",\\\"stateConsistency\\\":\" << r.data_confidence\n"
    "    << \",\\\"confidence\\\":\" << r.data_confidence\n")

# ---------------------------------------------------------------------------
# Explicit production search mode in robustness and welfare sensitivity.
# ---------------------------------------------------------------------------
replace_once(
    "src/decision_robustness.cpp",
    "Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {\n",
    "Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws) const {\n"
    "  return evaluate_robust(economy, parameter_draws,\n"
    "      EvaluationOptions{economy.exhaustive_policy_search});\n"
    "}\n\n"
    "Result PolicyEngine::evaluate_robust(const Economy& economy, int parameter_draws,\n"
    "                                     EvaluationOptions options) const {\n")
replace_once(
    "src/decision_robustness.cpp",
    "  Result baseline = evaluate(economy);\n",
    "  Result baseline = evaluate(economy, options);\n")
replace_once(
    "src/decision_robustness.cpp",
    "    Result draw = draw_engine.evaluate(economy);\n",
    "    Result draw = draw_engine.evaluate(economy, options);\n")

replace_once(
    "include/welfare_sensitivity.hpp",
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n    const PolicyEngine& engine, const Economy& economy,\n    const std::vector<WelfarePreferenceProfile>& profiles = {});\n",
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n"
    "    const PolicyEngine& engine, const Economy& economy,\n"
    "    const std::vector<WelfarePreferenceProfile>& profiles = {});\n"
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n"
    "    const PolicyEngine& engine, const Economy& economy,\n"
    "    const std::vector<WelfarePreferenceProfile>& profiles,\n"
    "    EvaluationOptions options);\n")
replace_once(
    "src/welfare_sensitivity.cpp",
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n    const PolicyEngine& engine, const Economy& economy,\n    const std::vector<WelfarePreferenceProfile>& profiles) {\n",
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n"
    "    const PolicyEngine& engine, const Economy& economy,\n"
    "    const std::vector<WelfarePreferenceProfile>& profiles) {\n"
    "  return evaluate_welfare_sensitivity(engine, economy, profiles,\n"
    "      EvaluationOptions{economy.exhaustive_policy_search});\n"
    "}\n\n"
    "WelfareSensitivitySummary evaluate_welfare_sensitivity(\n"
    "    const PolicyEngine& engine, const Economy& economy,\n"
    "    const std::vector<WelfarePreferenceProfile>& profiles,\n"
    "    EvaluationOptions options) {\n")
replace_once(
    "src/welfare_sensitivity.cpp",
    "  const Result reference_result = engine.evaluate(economy);\n",
    "  const Result reference_result = engine.evaluate(economy, options);\n")
replace_once(
    "src/welfare_sensitivity.cpp",
    "    const Result result = engine.evaluate(candidate);\n",
    "    const Result result = engine.evaluate(candidate, options);\n")

# ---------------------------------------------------------------------------
# Richer runtime evidence manifest.
# ---------------------------------------------------------------------------
replace_once(
    "include/model_evidence.hpp",
    "  bool historical_aggregate_permitted = false;\n",
    "  bool historical_aggregate_permitted = false;\n"
    "  bool state_measurement_contract_complete = false;\n"
    "  int ready_state_measurement_count = 0;\n"
    "  bool decision_loss_weights_complete = false;\n"
    "  int decision_loss_weight_count = 0;\n"
    "  bool observed_calibration_certified = false;\n"
    "  double observed_calibration_completeness = 0.0;\n"
    "  bool canada_io_empirical = false;\n"
    "  bool us_io_empirical = false;\n")
replace_once(
    "src/model_evidence.cpp",
    "      << (s.historical_aggregate_permitted ? \"true\" : \"false\") << \"}\";\n",
    "      << (s.historical_aggregate_permitted ? \"true\" : \"false\")\n"
    "      << \",\\\"stateMeasurementContractComplete\\\":\"\n"
    "      << (s.state_measurement_contract_complete ? \"true\" : \"false\")\n"
    "      << \",\\\"readyStateMeasurementCount\\\":\" << s.ready_state_measurement_count\n"
    "      << \",\\\"decisionLossWeightsComplete\\\":\"\n"
    "      << (s.decision_loss_weights_complete ? \"true\" : \"false\")\n"
    "      << \",\\\"decisionLossWeightCount\\\":\" << s.decision_loss_weight_count\n"
    "      << \",\\\"observedCalibrationCertified\\\":\"\n"
    "      << (s.observed_calibration_certified ? \"true\" : \"false\")\n"
    "      << \",\\\"observedCalibrationCompleteness\\\":\"\n"
    "      << s.observed_calibration_completeness\n"
    "      << \",\\\"canadaIoEmpirical\\\":\" << (s.canada_io_empirical ? \"true\" : \"false\")\n"
    "      << \",\\\"usIoEmpirical\\\":\" << (s.us_io_empirical ? \"true\" : \"false\") << \"}\";\n")

# ---------------------------------------------------------------------------
# Server: executable weights, state-measurement registry, explicit V2 search,
# non-control calibration overlay, honest partial-live baseline, and evidence
# endpoints. Dead state knobs disappear from the public request/baseline surface.
# ---------------------------------------------------------------------------
replace_once(
    "src/main.cpp",
    "#include \"model_evidence.hpp\"\n",
    "#include \"model_evidence.hpp\"\n"
    "#include \"runtime_configuration.hpp\"\n"
    "#include \"state_measurement.hpp\"\n"
    "#include \"trade_network.hpp\"\n")
replace_once(
    "src/main.cpp",
    "  FIELD(\"housingGap\", housing_gap); FIELD(\"householdDebt\", household_debt_income);\n",
    "  FIELD(\"housingGap\", housing_gap);\n")
replace_once(
    "src/main.cpp",
    "  FIELD(\"programGrowth\", program_growth); FIELD(\"taxImpulse\", tax_impulse);\n  FIELD(\"infrastructure\", infrastructure_impulse); FIELD(\"globalGrowth\", global_growth);\n",
    "  FIELD(\"globalGrowth\", global_growth);\n")
replace_once(
    "src/main.cpp",
    "  FIELD(\"importContent\", import_content_consumption); FIELD(\"tradeElasticity\", trade_elasticity);\n  FIELD(\"borderFriction\", border_friction); FIELD(\"tariffRelief\", tariff_relief);\n",
    "  FIELD(\"importContent\", import_content_consumption); FIELD(\"tradeElasticity\", trade_elasticity);\n"
    "  FIELD(\"borderFriction\", border_friction); FIELD(\"tariffRelief\", tariff_relief);\n"
    "  FIELD(\"tariffPricePassThrough\", tariff_price_pass_through);\n")

new_live_baseline = r'''std::string live_baseline(const cad::CalibrationSnapshot& calibration,
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

'''
replace_between("src/main.cpp", "std::string live_baseline(", "struct NegotiationState", new_live_baseline)

replace_once(
    "src/main.cpp",
    "  std::string structural_registry_path;\n  std::vector<std::string> historical_fixture_paths;\n",
    "  std::string structural_registry_path;\n"
    "  std::string decision_loss_path;\n"
    "  std::string state_measurement_path;\n"
    "  std::vector<std::string> historical_fixture_paths;\n")
replace_once(
    "src/main.cpp",
    "    structural_registry_path = materialized_data_path(\n        \"data/calibration/structural_parameter_registry.csv\");\n",
    "    structural_registry_path = materialized_data_path(\n"
    "        \"data/calibration/structural_parameter_registry.csv\");\n"
    "    decision_loss_path = materialized_data_path(\n"
    "        \"data/calibration/decision_loss_weights.csv\");\n"
    "    state_measurement_path = materialized_data_path(\n"
    "        \"data/calibration/state_measurement_registry.csv\");\n")
replace_once(
    "src/main.cpp",
    "  const auto structural_parameters = cad::apply_structural_parameter_registry(\n      cad::StructuralParameters{}, structural_registry);\n  cad::PolicyEngine evidence_engine(20260810, structural_parameters, structural_registry);\n",
    "  const auto structural_parameters = cad::apply_structural_parameter_registry(\n"
    "      cad::StructuralParameters{}, structural_registry);\n"
    "  const auto decision_loss = cad::load_decision_loss_calibration(decision_loss_path);\n"
    "  const auto state_measurements = cad::load_state_measurement_registry(state_measurement_path);\n"
    "  if (!decision_loss.complete || !cad::decision_loss_sensitivity_contract_complete(decision_loss)) {\n"
    "    std::cerr << \"Decision-loss calibration is incomplete or inconsistent\\n\";\n"
    "    close_socket(server);\n"
    "    return 1;\n"
    "  }\n"
    "  if (!cad::state_measurement_contract_complete(state_measurements)) {\n"
    "    std::cerr << \"State-measurement contract is incomplete\\n\";\n"
    "    close_socket(server);\n"
    "    return 1;\n"
    "  }\n"
    "  cad::PolicyEngine evidence_engine(20260810, structural_parameters, structural_registry);\n")
replace_once(
    "src/main.cpp",
    "  cad::Economy last_economy = cad::apply_calibration(cad::Economy{}, engine.snapshot());\n",
    "  cad::Economy last_economy = cad::apply_calibration(cad::Economy{}, engine.snapshot());\n"
    "  last_economy.loss_weights = decision_loss.weights;\n")
replace_once(
    "src/main.cpp",
    "            << cad::sampled_structural_parameter_count(structural_registry) << \" sampled parameters)\\n\"\n            << \"Diplomat Room: local append-only persistence at \" << room_path << '\\n';\n",
    "            << cad::sampled_structural_parameter_count(structural_registry) << \" sampled parameters)\\n\"\n"
    "            << \"Decision loss weights: \" << decision_loss.recognized_components << \"/12 active\\n\"\n"
    "            << \"State measurements: \" << cad::ready_state_measurement_count(state_measurements) << \" ready registry entries\\n\"\n"
    "            << \"U.S. IO network: \" << (cad::us_trade_input_output_empirical() ? \"empirical\" : \"proxy pending BEA artifact\") << '\\n'\n"
    "            << \"Diplomat Room: local append-only persistence at \" << room_path << '\\n';\n")
replace_once(
    "src/main.cpp",
    "      auto economy = parse(body);\n      auto result = engine.evaluate(economy);  // Mutates economy to the calibrated values actually simulated.\n",
    "      auto economy = parse(body);\n"
    "      economy.loss_weights = decision_loss.weights;\n"
    "      auto result = engine.evaluate(economy);  // Reattaches non-control calibration before solving.\n")
replace_all(
    "src/main.cpp",
    "            : cad::apply_calibration(parse(body), engine.snapshot());\n",
    "            : cad::apply_non_control_calibration(parse(body), engine.snapshot());\n",
    minimum=2)
replace_once(
    "src/main.cpp",
    "        const int requested = static_cast<int>(number(body, \"parameterDraws\", 6.0));\n        const int draws = std::clamp(requested, 1, 24);\n        respond(client, 200, \"application/json\",\n            cad::robustness_to_json(evidence_engine.evaluate_robust(economy, draws)));\n",
    "        economy.loss_weights = decision_loss.weights;\n"
    "        const int requested = static_cast<int>(number(body, \"parameterDraws\", 6.0));\n"
    "        const int draws = std::clamp(requested, 1, 24);\n"
    "        respond(client, 200, \"application/json\",\n"
    "            cad::robustness_to_json(evidence_engine.evaluate_robust(\n"
    "                economy, draws, cad::production_evaluation_options())));\n")
replace_once(
    "src/main.cpp",
    "      respond(client, 200, \"application/json\",\n          cad::welfare_sensitivity_to_json(\n              cad::evaluate_welfare_sensitivity(evidence_engine, economy)));\n",
    "      economy.loss_weights = decision_loss.weights;\n"
    "      respond(client, 200, \"application/json\",\n"
    "          cad::welfare_sensitivity_to_json(\n"
    "              cad::evaluate_welfare_sensitivity(\n"
    "                  evidence_engine, economy, {}, cad::production_evaluation_options())));\n")
replace_once(
    "src/main.cpp",
    "      respond(client, 200, \"application/json\",\n          cad::model_evidence_status_to_json(\n              cad::model_evidence_status(structural_registry, historical_backtests)));\n",
    "      auto status = cad::model_evidence_status(structural_registry, historical_backtests);\n"
    "      status.state_measurement_contract_complete =\n"
    "          cad::state_measurement_contract_complete(state_measurements);\n"
    "      status.ready_state_measurement_count = cad::ready_state_measurement_count(state_measurements);\n"
    "      status.decision_loss_weights_complete = decision_loss.complete;\n"
    "      status.decision_loss_weight_count = decision_loss.recognized_components;\n"
    "      status.observed_calibration_completeness = engine.snapshot().completeness;\n"
    "      status.observed_calibration_certified = engine.snapshot().completeness >= 95.0;\n"
    "      status.canada_io_empirical = cad::canada_trade_input_output_empirical();\n"
    "      status.us_io_empirical = cad::us_trade_input_output_empirical();\n"
    "      respond(client, 200, \"application/json\", cad::model_evidence_status_to_json(status));\n")
replace_once(
    "src/main.cpp",
    "    } else if (first.rfind(\"GET /api/v2/structural-registry \", 0) == 0) {\n      respond(client, 200, \"application/json\",\n          cad::structural_parameter_registry_to_json(structural_registry));\n",
    "    } else if (first.rfind(\"GET /api/v2/structural-registry \", 0) == 0) {\n"
    "      respond(client, 200, \"application/json\",\n"
    "          cad::structural_parameter_registry_to_json(structural_registry));\n"
    "    } else if (first.rfind(\"GET /api/v2/state-measurements \", 0) == 0) {\n"
    "      respond(client, 200, \"application/json\",\n"
    "          cad::state_measurement_registry_to_json(state_measurements));\n")
replace_once(
    "src/main.cpp",
    "      respond(client, 200, \"application/json\", live_baseline(engine.snapshot()));\n",
    "      respond(client, 200, \"application/json\",\n"
    "          live_baseline(engine.snapshot(), decision_loss, state_measurements));\n")

# ---------------------------------------------------------------------------
# Standalone packaging and regression test registration.
# ---------------------------------------------------------------------------
replace_once(
    "CMakeLists.txt",
    "    data/calibration/structural_parameter_registry.csv\n",
    "    data/calibration/structural_parameter_registry.csv\n"
    "    data/calibration/decision_loss_weights.csv\n")
replace_once(
    "CMakeLists.txt",
    "cad_add_test(final_realism_tests tests/final_realism_test.cpp final_realism)\n",
    "cad_add_test(final_realism_tests tests/final_realism_test.cpp final_realism)\n"
    "cad_add_test(plumbing_integration_tests tests/plumbing_integration_test.cpp plumbing_integration)\n"
    "set_tests_properties(plumbing_integration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})\n")

# ---------------------------------------------------------------------------
# Browser: remove the parallel economic response equations. Before a slider's
# server re-evaluation completes, show a pending state rather than inventing a
# client-side counterfactual. Final/verified rows use server-supplied sectors.
# ---------------------------------------------------------------------------
replace_between(
    "web/evaluation-controller.js",
    "  const sectorProfiles = [",
    "  function displayedCoverage()",
    "")
new_sector_metrics = r'''  function sectorMetrics(sector, usCoverage, canadaCoverage) {
    const rec = result?.recommendation;
    const scenario = result?.scenarios?.find(item => item.id === rec?.strategyId)
      || result?.scenarios?.[0];
    const row = scenario?.sectors?.[sector];
    if (!rec || !scenario || !row) return null;
    const recommendedCoverage = {
      us: finite(rec.usSectorCoverage?.[sector], usCoverage),
      canada: finite(rec.canadaSectorCoverage?.[sector], canadaCoverage)
    };
    const atVerifiedCoverage = Math.abs(finite(usCoverage) - recommendedCoverage.us) < .01
      && Math.abs(finite(canadaCoverage) - recommendedCoverage.canada) < .01;
    return {
      strategyId: scenario.id,
      canada: {...row.canada, score:finite(rec.canadaSectorValue?.[sector], 0)},
      us: {...row.us, score:finite(rec.usSectorOutput?.[sector], 0)},
      recommendedCoverage,
      verified: recommendationIsVerified(rec) && atVerifiedCoverage,
      pending: !atVerifiedCoverage,
      serverAuthoritative: true,
      insideSearchEnvelope: atVerifiedCoverage,
      searchEnvelope: null
    };
  }

'''
replace_between(
    "web/evaluation-controller.js",
    "  function economyContext()",
    "  function sectorSearchSummary",
    new_sector_metrics)
replace_once(
    "web/evaluation-controller.js",
    "  let sectorPolicyCache = {result:null, scenarioId:'', terms:null};\n",
    "")
replace_once(
    "web/evaluation-controller.js",
    "      sectorPolicyCache = {result:null, scenarioId:'', terms:null};\n",
    "")
replace_once(
    "web/evaluation-controller.js",
    "  window.SectorResponseModel = {\n    coverageLevels,\n    sectorUtility,\n    policyTermsForScenario,\n    sectorMetrics\n  };\n",
    "  window.SectorResponseModel = {sectorMetrics};\n")
replace_once(
    "web/evaluation-controller.js",
    "    const live = (baseline.provenance?.observedLive || []).some(item => item.live);\n",
    "    const live = Number(baseline.provenance?.liveFieldCount || 0);\n")
replace_once(
    "web/evaluation-controller.js",
    "    if (status) status.textContent = live ? 'Live official feeds' : 'Documented calibrated baseline';\n    if (sync) sync.textContent = live ? 'Official feeds synchronized' : 'Calibration snapshot active';\n",
    "    if (status) status.textContent = live ? `Partial live official feeds · ${live}/3 market fields` : 'Documented calibrated/default baseline';\n"
    "    if (sync) sync.textContent = live ? 'Partial official-feed refresh' : 'Calibration snapshot active';\n")

new_update_party_metric = "function updatePartySectorMetric(i,target){if(!target)return;const metrics=window.EvaluationController?.sectorMetrics?.(i,positions.us[i],positions.canada[i]);if(!metrics){target.textContent='Awaiting verified server result';return}if(metrics.pending){target.textContent='Pending verified server re-evaluation';if(target.dataset)target.dataset.state='pending';return}const side=negotiator==='us'?'us':'canada',m=metrics[side]||{};target.textContent=`Verified server result · output ${signed(Number(m.output||0),'%')} · jobs ${signed(Number(m.jobs||0),'%')} · prices ${signed(Number(m.prices||0),'%')} · score ${fmt(Number(m.score||0),0)}/100`;if(target.dataset)target.dataset.state='verified'}\n"
replace_js_function("web/app.js", "updatePartySectorMetric", new_update_party_metric)
replace_once(
    "web/app.js",
    "b.status==='live'?'Live official feeds':'Documented cached baseline'",
    "b.status==='live-partial'?'Partial live official feeds':'Documented calibrated/default baseline'")
replace_once(
    "web/app.js",
    "b.status==='live'?'Official feeds synchronized':'Baseline fallback active'",
    "b.status==='live-partial'?'Partial official-feed refresh':'Baseline fallback active'")
replace_once(
    "web/app.js",
    "$('#confidence').textContent=fmt(result.confidence,0)+'%'",
    "$('#confidence').textContent=fmt(result.stateConsistency??result.confidence,0)+'%'")
replace_once(
    "web/app.js",
    "$('#searchCount').textContent=`${result.candidatesExamined} generated mixes + 13 expert strategies × ${result.allocationsExamined} allocations × ${result.gdpFloorsExamined} GDP floors evaluated`",
    "$('#searchCount').textContent=`${result.candidatesExamined} generated mixes + 13 expert strategies · fixed bilateral weights · ${result.gdpFloorsExamined} explicit GDP floor evaluated`")
replace_once(
    "web/app.js",
    "$('#balanceProgressLabel').textContent=`${fmt(best.tradeBalanceProgress,0)}% toward zero from the pre-policy baseline`;$('#balanceActions').textContent=best.zeroTradeDeficit?`Target reached: US$${fmt(best.usExportExpansionUsd)}B in additional U.S. exports plus C$${fmt(best.canadaExportRedirectionCad)}B in redirected Canadian exports.`:`Best current action mix: US$${fmt(best.usExportExpansionUsd)}B in additional U.S. exports plus C$${fmt(best.canadaExportRedirectionCad)}B in redirected Canadian exports.`;",
    "$('#balanceProgressLabel').textContent=`${fmt(best.tradeBalanceProgress,0)}% change toward zero versus the pre-policy accounting balance`;$('#balanceActions').textContent='Diagnostic only · bilateral balance is not a welfare objective and the optimizer does not manufacture offsetting exports or redirection flows.';")
replace_once(
    "web/app.js",
    "Canada ${fmt((best.bocScore+best.federalScore)/2,0)}/100",
    "Canada ${fmt(best.canadaScore,0)}/100")

replace_once(
    "web/diplomat.js",
    "  const canadaScore = scenario => (n(scenario.bocScore) + n(scenario.federalScore)) / 2;\n",
    "  const canadaScore = scenario => n(scenario.canadaScore);\n")

replace_once("web/index.html", "MODEL DATA CONFIDENCE", "STATE-CONSISTENCY INDICATOR")
replace_once("web/index.html", "Opening deal · all bilateral allocations searched", "Opening deal · fixed bilateral outcome weights")
replace_once("web/index.html", "Automatic opening · GDP floor + bilateral allocation search · always 100%", "Automatic opening · fixed outcome weights + explicit GDP floor · always 100%")
replace_once("web/index.html", "Tariff revenue and the path to balanced trade", "Tariff revenue and bilateral balance diagnostics")
replace_once("web/index.html", "SHARED ZERO-DEFICIT TARGET", "BILATERAL BALANCE DIAGNOSTIC")
replace_once(
    "web/index.html",
    "Receipts equal the effective sector-weighted tariff multiplied by the post-elasticity import base. Because bilateral exports for one nation are imports for the other, both deficits reach zero only when the shared bilateral balance reaches zero. The zero-deficit compact splits the adjustment equally between additional U.S. exports to Canada and Canadian exports redirected to other markets, then ranks it only if both economies maintain positive quarterly growth.",
    "Receipts equal the effective sector-weighted tariff multiplied by the post-elasticity import base. The bilateral balance is an accounting diagnostic only: the welfare objective does not reward movement toward zero, and the engine does not manufacture offsetting exports, imports, or redirection flows to hit a target.")

# Enrich the calibration/evidence panel with runtime-plumbing status and the
# explicit U.S.-IO evidence boundary.
replace_once(
    "web/calibration.js",
    "      const r = await jsonFetch('/api/v2/structural-registry', {cache:'no-store'});\n      const c = r.calibrationCompleteness || {};\n",
    "      const [r, runtime, state] = await Promise.all([\n"
    "        jsonFetch('/api/v2/structural-registry', {cache:'no-store'}),\n"
    "        jsonFetch('/api/v2/evidence-status', {cache:'no-store'}),\n"
    "        jsonFetch('/api/v2/state-measurements', {cache:'no-store'})\n"
    "      ]);\n"
    "      const c = r.calibrationCompleteness || {};\n")
replace_once(
    "web/calibration.js",
    "      const cards = structuralCards(c);\n",
    "      const plumbingPass = runtime.decisionLossWeightsComplete && runtime.stateMeasurementContractComplete;\n"
    "      const cards = structuralCards(c) + `\n"
    "        <div><span>Runtime plumbing</span><b>${plumbingPass?'PASS':'CHECK'}</b><small>${Number(runtime.decisionLossWeightCount||0)}/12 loss weights · ${Number(runtime.readyStateMeasurementCount||0)} state-measurement contracts active</small></div>\n"
    "        <div><span>U.S. production network</span><b>${runtime.usIoEmpirical?'BEA empirical':'PROXY PENDING'}</b><small>${runtime.usIoEmpirical?'certified U.S. IO artifact active':'Canadian coefficients remain an explicitly labelled structural proxy'}</small></div>`;\n")

# ---------------------------------------------------------------------------
# Tests: calibration must prove runtime activation; browser must prove that it
# no longer contains a second economic response model.
# ---------------------------------------------------------------------------
replace_once(
    "tests/calibration_test.cpp",
    "  assert(std::abs(current_calibrated.trade_elasticity - 0.65) < 1e-9);\n",
    "  assert(std::abs(current_calibrated.trade_elasticity - 0.65) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.tariff_price_pass_through - 0.24) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.us_sector_trade_elasticity[0] - 5.705) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.us_sector_trade_elasticity[1] - 12.510) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.canada_sector_trade_elasticity[4] - 7.167) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.canada_sector_price_pass_through[0] - 0.24) < 1e-9);\n"
    "  assert(std::abs(current_calibrated.us_sector_price_pass_through[0]) < 1e-9);\n")
replace_once(
    "tests/calibration_test.cpp",
    "  assert(certified_json.find(\"\\\"retaliatoryTariff\\\":1.5000\") != std::string::npos);\n",
    "  assert(certified_json.find(\"\\\"retaliatoryTariff\\\":1.5000\") != std::string::npos);\n"
    "  assert(certified_json.find(\"\\\"sectorElasticityOverrideCount\\\":3\") != std::string::npos);\n"
    "  assert(certified_json.find(\"\\\"canadaPassThroughOverrideCount\\\":3\") != std::string::npos);\n")

# Replace the live-client-response test block with server-authority assertions.
test_path = "tests/evaluation_controller_test.js"
test_value = text(test_path)
test_value = test_value.replace(
    "  scenarios:[{id:'compact',name:'Compact',growth:1.7}],",
    "  scenarios:[{id:'compact',name:'Compact',growth:1.7,sectors:Array.from({length:20},(_,i)=>({canada:{output:-1-i/100,jobs:-.5,prices:.2},us:{output:-.4,jobs:-.2,prices:.1}}))}],")
test_value = test_value.replace(
    "    canadaSectorCoverage:Array(20).fill(75),\n",
    "    canadaSectorCoverage:Array(20).fill(75),\n"
    "    canadaSectorValue:Array(20).fill(61),\n"
    "    usSectorOutput:Array(20).fill(64),\n")
start = test_value.find("  // The delegation table must expose a live bilateral response surface")
end_marker = "  global.result.scenarios[0].id = savedScenarioId;\n"
end = test_value.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("evaluation_controller_test.js: legacy sector-response test block not found")
end += len(end_marker)
replacement = r'''  // The browser must never run a second economic model. It may display the
  // last server-verified sector result, but a coverage edit is explicitly
  // pending until the authoritative server evaluation completes.
  const verifiedMetric = window.EvaluationController.sectorMetrics(0, 50, 75);
  const pendingMetric = window.EvaluationController.sectorMetrics(0, 75, 75);
  assert(verifiedMetric && pendingMetric, 'server-authoritative sector metrics must be exposed');
  assert.strictEqual(verifiedMetric.serverAuthoritative, true);
  assert.strictEqual(verifiedMetric.verified, true);
  assert.strictEqual(verifiedMetric.pending, false);
  assert.strictEqual(verifiedMetric.canada.output, -1);
  assert.strictEqual(verifiedMetric.canada.score, 61);
  assert.strictEqual(pendingMetric.serverAuthoritative, true);
  assert.strictEqual(pendingMetric.verified, false);
  assert.strictEqual(pendingMetric.pending, true,
    'slider changes must be labelled pending instead of being simulated in JavaScript');
  assert.strictEqual(pendingMetric.canada.output, verifiedMetric.canada.output,
    'pending UI state must not fabricate a client-side counterfactual');

  const controllerSource = fs.readFileSync('web/evaluation-controller.js','utf8');
  assert(!controllerSource.includes('const sectorProfiles = ['),
    'sector profile/economic constants must not be duplicated in the browser controller');
  assert(!controllerSource.includes('function sectorUtility('),
    'browser controller must not contain a parallel sector utility equation');
'''
test_value = test_value[:start] + replacement + test_value[end:]
test_value = test_value.replace(
    "  assert(appSource.includes(\"updatePartySectorMetric(i,input.parentElement.querySelector('.sector-deal-metric'))\"),\n    'party slider input must repaint its metric before re-optimization completes');",
    "  assert(appSource.includes('Pending verified server re-evaluation'),\n    'party slider must show a pending server state instead of a local economic counterfactual');")
write(test_path, test_value)

write("tests/plumbing_integration_test.cpp", r'''#include "calibration.hpp"
#include "runtime_configuration.hpp"
#include "state_measurement.hpp"
#include "trade_network.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  const auto snapshot = cad::load_calibration_snapshot("data/calibration/current.snapshot.csv");
  assert(snapshot.loaded);
  const auto e = cad::apply_calibration(cad::Economy{}, snapshot);

  // Snapshot -> Economy activation: empirical sector coefficients must not stop
  // at the provenance layer.
  assert(std::abs(e.us_sector_trade_elasticity[0] - 5.705) < 1e-9);
  assert(std::abs(e.us_sector_trade_elasticity[1] - 12.510) < 1e-9);
  assert(std::abs(e.canada_sector_trade_elasticity[4] - 7.167) < 1e-9);
  assert(std::abs(e.canada_sector_price_pass_through[0] - 0.24) < 1e-9);
  assert(std::abs(e.us_sector_price_pass_through[0]) < 1e-12);

  // Economy -> trade network activation: a >5 elasticity must survive runtime
  // admissibility rather than being silently clipped to the old 5.0 ceiling.
  cad::TradeNetworkInput empirical;
  empirical.us_headline_tariff = 5.0;
  empirical.canada_headline_tariff = 1.5;
  empirical.trade_elasticity = e.trade_elasticity;
  empirical.price_pass_through = e.tariff_price_pass_through;
  empirical.us_coverage = e.us_sector_coverage;
  empirical.canada_coverage = e.canada_sector_coverage;
  empirical.us_trade_elasticity = e.us_sector_trade_elasticity;
  empirical.canada_trade_elasticity = e.canada_sector_trade_elasticity;
  empirical.us_price_pass_through = e.us_sector_price_pass_through;
  empirical.canada_price_pass_through = e.canada_sector_price_pass_through;
  auto capped = empirical;
  capped.us_trade_elasticity[1] = 5.0;
  const auto high = cad::evaluate_trade_source(empirical, 1, 100.0, 100.0);
  const auto old_cap = cad::evaluate_trade_source(capped, 1, 100.0, 100.0);
  double high_drag = 0.0, capped_drag = 0.0;
  for (double v : high.canada_output) high_drag += std::abs(v);
  for (double v : old_cap.canada_output) capped_drag += std::abs(v);
  assert(high_drag > capped_drag * 1.5);

  // Model-design weights are executable configuration, not documentation-only.
  const auto losses = cad::load_decision_loss_calibration(
      "data/calibration/decision_loss_weights.csv");
  assert(losses.loaded);
  assert(losses.complete);
  assert(losses.recognized_components == 12);
  assert(cad::decision_loss_sensitivity_contract_complete(losses));
  assert(std::abs(losses.weights.boc_inflation - 3.8) < 1e-12);
  assert(std::abs(losses.weights.federal_debt - 0.32) < 1e-12);
  assert(std::abs(losses.weights.us_inflation - 0.8) < 1e-12);

  // Current-state measurement contract is loaded by production startup.
  const auto states = cad::load_state_measurement_registry(
      "data/calibration/state_measurement_registry.csv");
  assert(cad::state_measurement_contract_complete(states));
  assert(cad::ready_state_measurement_count(states) == 2);

  // The U.S. matrix remains an explicit evidence gap rather than a false claim.
  assert(cad::canada_trade_input_output_empirical());
  assert(!cad::us_trade_input_output_empirical());

  std::cout << "plumbing integration tests passed\n";
  return 0;
}
''')

# ---------------------------------------------------------------------------
# README: align documentation with the repaired plumbing and remaining U.S.-IO
# evidence boundary.
# ---------------------------------------------------------------------------
replace_once(
    "README.md",
    "- **Directional tariff incidence:** U.S.-import and Canadian-import directions can carry independent sector trade elasticities and price pass-through values. Missing or reference-only sector estimates fall back to declared aggregate anchors rather than being silently promoted into production.\n",
    "- **Directional tariff incidence:** U.S.-import and Canadian-import directions can carry independent sector trade elasticities and price pass-through values. Production-compatible sector elasticities in the certified snapshot are reattached server-side on every evaluation; Canadian retaliatory-tariff pass-through evidence is activated only in the supported Canadian-import direction. Missing/reference-only or directionally incompatible estimates fall back to the declared aggregate anchor.\n")
replace_once(
    "README.md",
    "- **Internet baseline:** `GET /api/baseline` refreshes available Bank of Canada Valet observations at page load, exposes source metadata, and reports whether a documented fallback baseline is being used.\n",
    "- **Current-state baseline:** `GET /api/baseline` refreshes the three supported Bank of Canada Valet market observations at page load and reports `live-partial` when any are available. Field-level provenance explicitly identifies the remaining macro state as calibrated/default input rather than labelling the whole state live. The production server also loads and exposes the state-measurement contract at `GET /api/v2/state-measurements`.\n"
    "- **Executable model-design configuration:** `data/calibration/decision_loss_weights.csv` is loaded at startup, validated as a complete 12-component ±20% sensitivity contract, and applied to every production/V2 economy rather than merely documenting duplicated C++ constants.\n"
    "- **Deterministic V2 search plumbing:** robustness and welfare endpoints explicitly request the production exhaustive search mode; their optimization path no longer depends on whether a prior `/api/evaluate` call mutated cached state or whether the V2 request body was empty.\n"
    "- **Server-authoritative sector UI:** browser sector sliders no longer run a parallel economic response equation. Until a changed posture is re-evaluated by the C++ engine, the UI marks the sector result pending instead of fabricating an immediate client-side counterfactual.\n")
replace_once(
    "README.md",
    "- **Fiscal block:** balance and debt dynamics, program growth, fiscal impulse and the supply benefit of productive investment.\n",
    "- **Fiscal block:** balance and debt dynamics, the searched fiscal impulse and the supply benefit of productive investment. Legacy `program_growth`, `tax_impulse`, `infrastructure_impulse`, and `household_debt_income` members remain ABI-compatible internal placeholders but are no longer accepted or advertised as production request-state controls until explicit equations are implemented.\n")

# ---------------------------------------------------------------------------
# Source-level sanity checks before the workflow compiles/tests.
# ---------------------------------------------------------------------------
controller = text("web/evaluation-controller.js")
if "function sectorUtility(" in controller or "const sectorProfiles = [" in controller:
    raise RuntimeError("parallel browser economic model still present")
main_source = text("src/main.cpp")
for forbidden in ['FIELD("programGrowth"', 'FIELD("taxImpulse"', 'FIELD("infrastructure"', 'FIELD("householdDebt"']:
    if forbidden in main_source:
        raise RuntimeError(f"dead public request field still present: {forbidden}")
if "production_evaluation_options()" not in main_source:
    raise RuntimeError("production V2 search option not wired")

# Remove the transformer and workflow themselves before the final commit, so the
# PR contains only production/test/documentation changes.
for helper in [ROOT / "tools/plumbing_hardening_once.py",
               ROOT / ".github/workflows/plumbing-hardening-once.yml"]:
    if helper.exists():
        helper.unlink()

print("plumbing hardening transform complete")
