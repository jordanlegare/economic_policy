#pragma once

#include "calibration_loader.hpp"
#include "evaluation_result_cache.hpp"
#include "user_anchor_selection.hpp"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace cad {

inline Economy apply_non_control_calibration(
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

inline std::string calibration_to_json(const CalibrationSnapshot& snapshot) {
  using calibration_detail::esc;
  const Economy effective = apply_calibration(Economy{}, snapshot);
  int sector_elasticity_overrides = 0;
  int canada_pass_through_overrides = 0;
  int us_pass_through_overrides = 0;
  for (std::size_t i = 0; i < effective.us_sector_trade_elasticity.size(); ++i) {
    if (effective.us_sector_trade_elasticity[i] > 0.0
        || effective.canada_sector_trade_elasticity[i] > 0.0)
      ++sector_elasticity_overrides;
    if (effective.canada_sector_price_pass_through[i] > 0.0)
      ++canada_pass_through_overrides;
    if (effective.us_sector_price_pass_through[i] > 0.0)
      ++us_pass_through_overrides;
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(4);
  out << "{\"snapshotId\":\"" << esc(snapshot.snapshot_id)
      << "\",\"asOf\":\"" << esc(snapshot.as_of)
      << "\",\"generatedAt\":\"" << esc(snapshot.generated_at)
      << "\",\"schemaVersion\":\"" << esc(snapshot.schema_version)
      << "\",\"loaded\":" << (snapshot.loaded ? "true" : "false")
      << ",\"grade\":\"" << esc(snapshot.grade)
      << "\",\"completeness\":" << snapshot.completeness
      << ",\"certifiedForEmpiricalUse\":" << (snapshot.completeness >= 95.0 ? "true" : "false")
      << ",\"calibrationScope\":\"merchandise-primary-and-manufacturing\""
      << ",\"effectiveState\":{\"usTariff\":" << effective.us_tariff_canada
      << ",\"retaliatoryTariff\":" << effective.canada_retaliatory_tariff
      << ",\"usSectorCoverage\":[";
  for (std::size_t i = 0; i < effective.us_sector_coverage.size(); ++i) {
    if (i) out << ',';
    out << effective.us_sector_coverage[i];
  }
  out << "],\"canadaSectorCoverage\":[";
  for (std::size_t i = 0; i < effective.canada_sector_coverage.size(); ++i) {
    if (i) out << ',';
    out << effective.canada_sector_coverage[i];
  }
  out << "],\"runtimeActivation\":{\"aggregateTradeElasticity\":" << effective.trade_elasticity
      << ",\"aggregateTariffPricePassThrough\":" << effective.tariff_price_pass_through
      << ",\"sectorElasticityOverrideCount\":" << sector_elasticity_overrides
      << ",\"canadaPassThroughOverrideCount\":" << canada_pass_through_overrides
      << ",\"usPassThroughOverrideCount\":" << us_pass_through_overrides << "}}"
      << ",\"checks\":{\"officialTrade\":" << (snapshot.official_trade_complete ? "true" : "false")
      << ",\"tariffLines\":" << (snapshot.tariff_lines_complete ? "true" : "false")
      << ",\"inputOutput\":" << (snapshot.input_output_complete ? "true" : "false")
      << ",\"originUtilization\":" << (snapshot.origin_utilization_complete ? "true" : "false")
      << ",\"elasticitiesEstimated\":" << (snapshot.elasticities_estimated ? "true" : "false")
      << ",\"passThroughEstimated\":" << (snapshot.pass_through_estimated ? "true" : "false") << "}"
      << ",\"sources\":[";
  for (std::size_t i = 0; i < snapshot.sources.size(); ++i) {
    if (i) out << ',';
    const auto& s = snapshot.sources[i];
    out << "{\"id\":\"" << esc(s.id) << "\",\"agency\":\"" << esc(s.agency)
        << "\",\"dataset\":\"" << esc(s.dataset) << "\",\"vintage\":\"" << esc(s.vintage)
        << "\",\"url\":\"" << esc(s.url) << "\",\"sha256\":\"" << esc(s.sha256)
        << "\",\"status\":\"" << esc(s.status) << "\"}";
  }
  out << "],\"measures\":[";
  for (std::size_t i = 0; i < snapshot.measures.size(); ++i) {
    if (i) out << ',';
    const auto& m = snapshot.measures[i];
    out << "{\"id\":\"" << esc(m.id) << "\",\"jurisdiction\":\"" << esc(m.jurisdiction)
        << "\",\"instrument\":\"" << esc(m.instrument) << "\",\"announced\":\"" << esc(m.announced)
        << "\",\"effectiveFrom\":\"" << esc(m.effective_from) << "\",\"effectiveTo\":\"" << esc(m.effective_to)
        << "\",\"rate\":\"" << esc(m.rate) << "\",\"scope\":\"" << esc(m.scope)
        << "\",\"sourceId\":\"" << esc(m.source_id) << "\",\"status\":\"" << esc(m.status) << "\"}";
  }
  out << "],\"warning\":\"";
  if (snapshot.completeness >= 95.0)
    out << "Observed-data calibration checks passed; legal interpretation and model uncertainty still require human review.";
  else
    out << "Calibration is incomplete. Missing official or estimated layers remain visible and model defaults must not be described as observed data.";
  out << "\"}";
  return out.str();
}

inline std::string attach_calibration_json(std::string base_json,
                                           const CalibrationSnapshot& snapshot) {
  if (base_json.size() < 2 || base_json.front() != '{' || base_json.back() != '}') return base_json;
  base_json.pop_back();
  base_json += ",\"calibration\":" + calibration_to_json(snapshot) + "}";
  return base_json;
}

inline std::string linked_bargaining_cache_namespace(
    const std::string& snapshot_id, const Economy& economy) {
  // evaluation_cache::make_key predates the finalist-only bargaining channels.
  // Namespace those internal reruns here so packages that differ only in
  // procurement or supply-chain state cannot alias the same cached production
  // solve. The public calibration snapshot identifier itself remains unchanged.
  std::ostringstream out;
  out << std::setprecision(17) << snapshot_id
      << "|linkedProcurementPp="
      << economy.trade_network_tuning.procurement_quantity_uplift_pp
      << "|linkedSupplyChainMitigation="
      << economy.trade_network_tuning.supply_chain_mitigation;
  return out.str();
}

class CalibratedPolicyEngine {
 public:
  explicit CalibratedPolicyEngine(std::string snapshot_path,
                                  std::uint64_t seed = 20260810,
                                  StructuralParameters structural_parameters = {},
                                  StructuralParameterRegistry structural_registry = {})
      : base_(seed, std::move(structural_parameters), std::move(structural_registry)),
        snapshot_(load_calibration_snapshot(snapshot_path)), path_(std::move(snapshot_path)),
        result_cache_(std::make_shared<evaluation_cache::EvaluationResultCache>()) {}

  Result evaluate(Economy& economy) const {
    // The calibration snapshot seeds /api/baseline. Once the browser submits a
    // scenario, those explicit controls are the state to solve; reapplying the
    // snapshot here would silently erase tariff/coverage what-if inputs.
    const Economy calibrated_baseline = apply_calibration(Economy{}, snapshot_);
    economy = apply_non_control_calibration(std::move(economy), snapshot_);
    const std::string cache_key = evaluation_cache::make_key(
        economy, base_.parameters(),
        linked_bargaining_cache_namespace(snapshot_.snapshot_id, economy),
        base_.parameter_registry().registry_id, true);
    Result result = result_cache_->get_or_compute(cache_key, [&] {
      return base_.evaluate(economy, production_evaluation_options());
    });
    // Initial calibrated opening remains pure maximum welfare. Once the user
    // changes the visible package, preserve that submitted scenario as the
    // anchor and use proximity only inside the declared 0.5-point welfare band.
    apply_user_anchor_selection(economy, calibrated_baseline, result);
    return result;
  }

  const CalibrationSnapshot& snapshot() const { return snapshot_; }
  const std::string& snapshot_path() const { return path_; }
  evaluation_cache::Stats cache_stats() const { return result_cache_->stats(); }
  bool persistent_cache_enabled() const { return result_cache_->persistent_enabled(); }
  const std::filesystem::path& result_cache_root() const { return result_cache_->root(); }

 private:
  PolicyEngine base_;
  CalibrationSnapshot snapshot_;
  std::string path_;
  std::shared_ptr<evaluation_cache::EvaluationResultCache> result_cache_;
};

}  // namespace cad
