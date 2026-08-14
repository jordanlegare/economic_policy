#pragma once

#include "negotiation_support.hpp"
#include "robust_recommendation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cad {

struct BargainingTermMapping {
  std::string term_id;
  bool production_mapped = false;
  std::string production_target;
  std::string unit_contract;
  std::string transformation;
  std::string provenance_class;
  std::string uncertainty_treatment;
};

struct FinalistResimulationRecord {
  std::string role;
  std::string package_id;
  std::string strategy_id;
  bool package_found = false;
  bool source_scenario_found = false;
  bool mapping_complete = false;
  bool baseline_available = false;
  bool executed = false;
  bool common_random_numbers = true;
  int verification_draws = 0;
  std::vector<std::string> unmapped_terms;

  double source_negotiated_relief = 0.0;
  double incremental_us_tariff_relief = 0.0;
  double incremental_canada_tariff_relief = 0.0;
  double materialized_us_headline_tariff = 0.0;
  double materialized_canada_headline_tariff = 0.0;

  double baseline_canada_score = 0.0;
  double baseline_us_score = 0.0;
  double resimulated_canada_score = 0.0;
  double resimulated_us_score = 0.0;
  double bilateral_growth_floor = 0.0;
  double recession_risk = 0.0;
  double debt_stress_p90 = 0.0;
  double inflation_stress_p90 = 0.0;

  bool canada_no_harm = false;
  bool us_no_harm = false;
  bool growth_constraint_met = false;
  bool data_integrity_pass = false;
  bool bargaining_robustness_passed = false;
  bool full_package_resimulated = false;
  bool verified_win_win = false;
};

struct FinalistResimulationAnalysis {
  std::string decision_authority = "second-stage-robustness";
  std::string methodology = "full-production-rerun-source-extraction";
  std::string source_scenario_selection = "immutable-strategy-id";
  std::string tail_risk_treatment = "existing-stochastic-diagnostics-and-score-penalty";
  bool common_random_numbers = true;
  bool unrelated_policy_candidates_are_incidentally_rerun = true;
  int target_verification_draws = 2800;
  std::size_t finalist_limit = 3;
  std::vector<BargainingTermMapping> mappings;
  std::vector<FinalistResimulationRecord> finalists;
};

namespace finalist_resimulation_detail {

inline double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline const Scenario* find_scenario(const Result& result, const std::string& id) {
  for (const auto& scenario : result.scenarios)
    if (scenario.id == id) return &scenario;
  return nullptr;
}

inline const NegotiationPackage* find_package(
    const NegotiationAnalysis& negotiation, const std::string& id) {
  if (negotiation.recommended.id == id) return &negotiation.recommended;
  for (const auto& package : negotiation.frontier)
    if (package.id == id) return &package;
  return nullptr;
}

inline RobustPackageMetrics const* find_metrics(
    const RobustRecommendationAnalysis& robustness, const std::string& id) {
  for (const auto& metrics : robustness.packages)
    if (metrics.package_id == id) return &metrics;
  return nullptr;
}

inline void update_package_verification(
    NegotiationAnalysis& negotiation, const FinalistResimulationRecord& record) {
  auto apply = [&](NegotiationPackage& package) {
    if (package.id != record.package_id) return;
    package.bargaining_robustness_passed = record.bargaining_robustness_passed;
    package.full_package_resimulated = record.full_package_resimulated;
    package.verified_win_win = record.verified_win_win;
  };
  apply(negotiation.recommended);
  for (auto& package : negotiation.frontier) apply(package);
}

inline std::vector<BargainingTermMapping> mapping_contract() {
  return {
      {"us-tariff-relief", true, "Economy.us_tariff_canada",
       "fraction of the residual U.S. headline tariff",
       "H_us' = H_us * (1 - incremental_relief); source Scenario.negotiated_relief remains unchanged and is applied downstream",
       "identity/accounting mapping",
       "inherits calibrated tariff state and existing stochastic trade uncertainty; no new coefficient"},
      {"canada-tariff-relief", true, "Economy.canada_retaliatory_tariff",
       "fraction of the residual Canadian headline tariff",
       "H_ca' = H_ca * (1 - incremental_relief); source Scenario.negotiated_relief remains unchanged and is applied downstream",
       "identity/accounting mapping",
       "inherits calibrated tariff state and existing stochastic trade uncertainty; no new coefficient"},
      {"border-facilitation", false, "unmapped",
       "0-100 bargaining move; no reviewed conversion to Economy.border_friction",
       "none",
       "reduced-form bargaining term only",
       "must be mapped/calibrated before full-package verification"},
      {"procurement", false, "unmapped",
       "0-100 bargaining move; no production demand/trade/fiscal control",
       "none",
       "reduced-form bargaining term only",
       "must be mapped/calibrated before full-package verification"},
      {"supply-chain", false, "unmapped",
       "0-100 bargaining move; no production investment/productivity/network control",
       "none",
       "reduced-form bargaining term only",
       "must be mapped/calibrated before full-package verification"}
  };
}

struct CandidateRole {
  std::string id;
  std::string role;
};

inline void append_unique(std::vector<CandidateRole>& values,
                          const std::string& id, const std::string& role,
                          std::size_t limit) {
  if (id.empty() || values.size() >= limit) return;
  for (const auto& value : values) if (value.id == id) return;
  values.push_back({id, role});
}

inline std::vector<CandidateRole> select_finalists(
    const RobustRecommendationAnalysis& robustness, std::size_t limit) {
  std::vector<CandidateRole> out;
  append_unique(out, robustness.recommended_package_id, "robust-primary", limit);

  const RobustPackageMetrics* bridge = nullptr;
  const RobustPackageMetrics* low_regret = nullptr;
  for (const auto& metrics : robustness.packages) {
    if (!bridge || metrics.joint_clear_probability > bridge->joint_clear_probability)
      bridge = &metrics;
    if (!low_regret || metrics.max_regret < low_regret->max_regret)
      low_regret = &metrics;
  }
  if (bridge) append_unique(out, bridge->package_id, "bridge", limit);
  if (low_regret) append_unique(out, low_regret->package_id, "low-regret", limit);
  return out;
}

inline void append_unmapped(std::vector<std::string>& values,
                            const char* id, double amplitude) {
  if (std::abs(amplitude) > 1e-9) values.emplace_back(id);
}

inline Economy materialize_mapped_terms(
    const Economy& economy, const NegotiationPackage& package,
    FinalistResimulationRecord& record) {
  Economy materialized = economy;
  const auto terms = robust_detail::package_terms(package);
  record.incremental_us_tariff_relief = clamp(terms.us_tariff_relief, 0.0, 1.0);
  record.incremental_canada_tariff_relief = clamp(terms.canada_tariff_relief, 0.0, 1.0);

  // The source scenario retains the upstream negotiated-relief path. Reducing
  // the headline tariff here means the production trade block computes:
  // headline * (1 - incremental residual relief) * (1 - upstream relief),
  // rather than applying either concession twice.
  materialized.us_tariff_canada = std::max(0.0,
      economy.us_tariff_canada * (1.0 - record.incremental_us_tariff_relief));
  materialized.canada_retaliatory_tariff = std::max(0.0,
      economy.canada_retaliatory_tariff
          * (1.0 - record.incremental_canada_tariff_relief));
  materialized.us_sector_coverage = package.us_sector_coverage;
  materialized.canada_sector_coverage = package.canada_sector_coverage;
  materialized.exhaustive_policy_search = true;

  record.materialized_us_headline_tariff = materialized.us_tariff_canada;
  record.materialized_canada_headline_tariff = materialized.canada_retaliatory_tariff;
  append_unmapped(record.unmapped_terms, "border-facilitation", terms.border_facilitation);
  append_unmapped(record.unmapped_terms, "procurement", terms.procurement_reciprocity);
  append_unmapped(record.unmapped_terms, "supply-chain", terms.supply_chain_commitment);
  record.mapping_complete = record.unmapped_terms.empty();
  return materialized;
}

inline std::string esc(const std::string& value) {
  return negotiation_detail::escape_json(value);
}

inline void string_array_json(std::ostringstream& out,
                              const std::vector<std::string>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out << ',';
    out << '"' << esc(values[i]) << '"';
  }
  out << ']';
}

}  // namespace finalist_resimulation_detail

template<class Engine>
FinalistResimulationAnalysis verify_bargaining_finalists(
    Engine& engine, const Economy& economy, const Result& result,
    NegotiationAnalysis& negotiation,
    const RobustRecommendationAnalysis& robustness,
    std::size_t finalist_limit = 3) {
  using namespace finalist_resimulation_detail;
  FinalistResimulationAnalysis analysis;
  analysis.finalist_limit = std::max<std::size_t>(1, finalist_limit);
  analysis.target_verification_draws = std::max(
      1, result.recommendation.verification_monte_carlo_draws);
  analysis.mappings = mapping_contract();

  const auto selected = select_finalists(robustness, analysis.finalist_limit);
  analysis.finalists.reserve(selected.size());
  for (const auto& candidate : selected) {
    FinalistResimulationRecord record;
    record.role = candidate.role;
    record.package_id = candidate.id;
    record.baseline_canada_score = result.recommendation.baseline_canada_score;
    record.baseline_us_score = result.recommendation.baseline_us_score;
    record.baseline_available = record.baseline_canada_score > 0.0
        && record.baseline_us_score > 0.0;

    const auto* package = find_package(negotiation, candidate.id);
    record.package_found = package != nullptr;
    const auto* metrics = find_metrics(robustness, candidate.id);
    record.bargaining_robustness_passed = metrics && metrics->clears_probability_gate;
    if (!package) {
      update_package_verification(negotiation, record);
      analysis.finalists.push_back(std::move(record));
      continue;
    }

    record.strategy_id = package->strategy_id;
    const auto* source = find_scenario(result, package->strategy_id);
    record.source_scenario_found = source != nullptr;
    if (source) record.source_negotiated_relief = source->negotiated_relief;
    Economy materialized = materialize_mapped_terms(economy, *package, record);

    if (source && record.mapping_complete) {
      // Re-run the production engine under the fully materialized tariff state,
      // then extract the immutable source strategy by ID. The rerun may compute
      // unrelated policy candidates as an implementation cost; their ranking is
      // never allowed to replace the second-stage robust decision authority.
      auto rerun = engine.evaluate(materialized);
      const auto* resimulated = find_scenario(rerun, package->strategy_id);
      if (resimulated) {
        record.executed = true;
        record.verification_draws = rerun.recommendation.verification_monte_carlo_draws;
        record.resimulated_canada_score = resimulated->canada_score;
        record.resimulated_us_score = resimulated->us_score;
        record.bilateral_growth_floor = resimulated->bilateral_growth_floor;
        record.recession_risk = resimulated->recession_risk;
        record.debt_stress_p90 = resimulated->debt_stress_p90;
        record.inflation_stress_p90 = resimulated->inflation_stress_p90;
        record.canada_no_harm = record.baseline_available
            && record.resimulated_canada_score + 1e-9 >= record.baseline_canada_score;
        record.us_no_harm = record.baseline_available
            && record.resimulated_us_score + 1e-9 >= record.baseline_us_score;
        record.growth_constraint_met = resimulated->bilateral_growth_floor + 1e-9
            >= economy.minimum_bilateral_growth;
        record.data_integrity_pass = result.recommendation.independent_us_trade_channel
            && !result.recommendation.trade_balance_is_objective
            && result.recommendation.mandate_weights_fixed
            && package->macro_base_verified
            && package->sector_posture_verified
            && package->bargaining_terms_screened;
        record.full_package_resimulated = true;
        record.verified_win_win = record.bargaining_robustness_passed
            && record.canada_no_harm && record.us_no_harm
            && record.growth_constraint_met && record.data_integrity_pass;
      }
    }

    update_package_verification(negotiation, record);
    analysis.finalists.push_back(std::move(record));
  }
  return analysis;
}

inline std::string finalist_resimulation_to_json(
    const FinalistResimulationAnalysis& analysis) {
  using namespace finalist_resimulation_detail;
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "{\"decisionAuthority\":\"" << esc(analysis.decision_authority)
      << "\",\"methodology\":\"" << esc(analysis.methodology)
      << "\",\"sourceScenarioSelection\":\"" << esc(analysis.source_scenario_selection)
      << "\",\"tailRiskTreatment\":\"" << esc(analysis.tail_risk_treatment)
      << "\",\"commonRandomNumbers\":" << (analysis.common_random_numbers ? "true" : "false")
      << ",\"unrelatedPolicyCandidatesIncidentallyRerun\":"
      << (analysis.unrelated_policy_candidates_are_incidentally_rerun ? "true" : "false")
      << ",\"targetVerificationDraws\":" << analysis.target_verification_draws
      << ",\"finalistLimit\":" << analysis.finalist_limit
      << ",\"termMappings\":[";
  for (std::size_t i = 0; i < analysis.mappings.size(); ++i) {
    if (i) out << ',';
    const auto& mapping = analysis.mappings[i];
    out << "{\"termId\":\"" << esc(mapping.term_id)
        << "\",\"productionMapped\":" << (mapping.production_mapped ? "true" : "false")
        << ",\"productionTarget\":\"" << esc(mapping.production_target)
        << "\",\"unitContract\":\"" << esc(mapping.unit_contract)
        << "\",\"transformation\":\"" << esc(mapping.transformation)
        << "\",\"provenanceClass\":\"" << esc(mapping.provenance_class)
        << "\",\"uncertaintyTreatment\":\"" << esc(mapping.uncertainty_treatment)
        << "\"}";
  }
  out << "],\"finalists\":[";
  for (std::size_t i = 0; i < analysis.finalists.size(); ++i) {
    if (i) out << ',';
    const auto& record = analysis.finalists[i];
    out << "{\"role\":\"" << esc(record.role)
        << "\",\"packageId\":\"" << esc(record.package_id)
        << "\",\"strategyId\":\"" << esc(record.strategy_id)
        << "\",\"packageFound\":" << (record.package_found ? "true" : "false")
        << ",\"sourceScenarioFound\":" << (record.source_scenario_found ? "true" : "false")
        << ",\"mappingComplete\":" << (record.mapping_complete ? "true" : "false")
        << ",\"baselineAvailable\":" << (record.baseline_available ? "true" : "false")
        << ",\"executed\":" << (record.executed ? "true" : "false")
        << ",\"verificationDraws\":" << record.verification_draws
        << ",\"unmappedTerms\":";
    string_array_json(out, record.unmapped_terms);
    out << ",\"sourceNegotiatedRelief\":" << record.source_negotiated_relief
        << ",\"incrementalUsTariffRelief\":" << record.incremental_us_tariff_relief
        << ",\"incrementalCanadaTariffRelief\":" << record.incremental_canada_tariff_relief
        << ",\"materializedUsHeadlineTariff\":" << record.materialized_us_headline_tariff
        << ",\"materializedCanadaHeadlineTariff\":" << record.materialized_canada_headline_tariff
        << ",\"baselineCanadaScore\":" << record.baseline_canada_score
        << ",\"baselineUsScore\":" << record.baseline_us_score
        << ",\"resimulatedCanadaScore\":" << record.resimulated_canada_score
        << ",\"resimulatedUsScore\":" << record.resimulated_us_score
        << ",\"bilateralGrowthFloor\":" << record.bilateral_growth_floor
        << ",\"recessionRisk\":" << record.recession_risk
        << ",\"debtP90\":" << record.debt_stress_p90
        << ",\"inflationP90\":" << record.inflation_stress_p90
        << ",\"canadaNoHarm\":" << (record.canada_no_harm ? "true" : "false")
        << ",\"usNoHarm\":" << (record.us_no_harm ? "true" : "false")
        << ",\"growthConstraintMet\":" << (record.growth_constraint_met ? "true" : "false")
        << ",\"dataIntegrityPass\":" << (record.data_integrity_pass ? "true" : "false")
        << ",\"bargainingRobustnessPassed\":"
        << (record.bargaining_robustness_passed ? "true" : "false")
        << ",\"fullPackageResimulated\":"
        << (record.full_package_resimulated ? "true" : "false")
        << ",\"verifiedWinWin\":" << (record.verified_win_win ? "true" : "false")
        << '}';
  }
  out << "]}";
  return out.str();
}

inline std::string attach_finalist_resimulation_json(
    std::string base_json, const FinalistResimulationAnalysis& analysis) {
  if (base_json.size() < 2 || base_json.front() != '{' || base_json.back() != '}')
    return base_json;
  base_json.pop_back();
  base_json += ",\"finalistResimulation\":"
      + finalist_resimulation_to_json(analysis) + "}";
  return base_json;
}

}  // namespace cad
