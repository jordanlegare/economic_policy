#pragma once

#include "negotiation_support.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct DiplomacyIssueTrack {
  std::string id;
  std::string label;
  std::string forum;
  std::string legal_reference;
  std::string objective;
  std::string verification;
  std::string link_group;
  double canada_priority = 0.0;
  double us_priority = 0.0;
  double joint_value = 0.0;
  double domestic_sensitivity = 0.0;
  double implementation_complexity = 0.0;
  bool parallel_track = false;
};

struct DiplomacyRobustnessCase {
  std::string id;
  std::string label;
  std::string description;
  std::string winner_package_id;
  double winner_score = 0.0;
};

struct DiplomacyRobustPackage {
  std::string package_id;
  std::string strategy_name;
  double worst_case_surplus = 0.0;
  double average_min_surplus = 0.0;
  double robust_score = 0.0;
  int case_wins = 0;
  bool clears_all_cases = false;
};

struct DiplomacyRoundStep {
  int order = 0;
  std::string phase;
  std::string objective;
  std::string package_id;
  std::string negotiating_move;
  std::string exit_criteria;
};

struct DiplomacyGuardrail {
  std::string id;
  std::string mechanism;
  std::string trigger;
  std::string evidence;
  std::string response;
  std::string cadence;
};

struct DiplomacyStakeholderGate {
  std::string country;
  std::string gate;
  std::string type;
  std::string rationale;
  double sensitivity = 0.0;
  bool formal_authority = false;
};

struct DiplomacyEvidenceItem {
  std::string source;
  std::string purpose;
  std::string status;
  std::string refresh_rule;
};

struct TradeDiplomacyPlatform {
  std::string mode = "Canada–United States trade diplomacy";
  std::string recommended_robust_package_id;
  std::string bridge_package_id;
  std::string readiness_label;
  std::string operating_doctrine;
  double operational_readiness = 0.0;
  double recommended_worst_case_surplus = 0.0;
  int robust_cases = 0;
  std::vector<DiplomacyIssueTrack> issue_tracks;
  std::vector<DiplomacyRobustnessCase> robustness_cases;
  std::vector<DiplomacyRobustPackage> robust_packages;
  std::vector<DiplomacyRoundStep> round_plan;
  std::vector<DiplomacyGuardrail> guardrails;
  std::vector<DiplomacyStakeholderGate> stakeholder_gates;
  std::vector<DiplomacyEvidenceItem> evidence_ledger;
};

namespace diplomacy_detail {

inline double clamp_diplomacy(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

inline double issue_move(const NegotiationPackage& package, const std::string& id,
                         bool canada) {
  for (const auto& issue : package.issues) {
    if (issue.id == id) return canada ? issue.canada_move : issue.us_move;
  }
  return 0.0;
}

inline double average_move(const NegotiationPackage& package, bool canada) {
  if (package.issues.empty()) return 0.0;
  double sum = 0.0;
  for (const auto& issue : package.issues) sum += canada ? issue.canada_move : issue.us_move;
  return sum / package.issues.size();
}

struct StressSpec {
  const char* id;
  const char* label;
  const char* description;
  double canada_commitment_penalty;
  double us_commitment_penalty;
  double low_cooperation_penalty;
  double instability_penalty;
  double trade_relief_bonus;
};

constexpr StressSpec stress_specs[] = {
    {"baseline", "Baseline mandate", "Current modeled reservation values and implementation incentives.", 0.00, 0.00, 0.00, 0.04, 0.04},
    {"canada-hard", "Canada domestic constraint", "Canadian room to concede narrows because domestic implementation costs rise.", 0.12, 0.02, 0.02, 0.05, 0.03},
    {"us-hard", "U.S. domestic constraint", "U.S. room to concede narrows because domestic implementation costs rise.", 0.02, 0.12, 0.02, 0.05, 0.03},
    {"high-trade", "High trade sensitivity", "Tariff and market-access changes transmit more strongly through bilateral trade.", 0.03, 0.03, 0.00, 0.04, 0.11},
    {"low-cooperation", "Low cooperation", "Large or simultaneous moves become harder to sustain politically.", 0.04, 0.04, 0.12, 0.06, 0.02},
    {"implementation", "Implementation stress", "Weak verification or sequencing magnifies post-agreement deviation incentives.", 0.03, 0.03, 0.03, 0.16, 0.02}
};

inline double stress_score(const NegotiationPackage& package, const StressSpec& spec,
                           double* minimum_surplus = nullptr) {
  const double ca_move = average_move(package, true) / 100.0;
  const double us_move = average_move(package, false) / 100.0;
  const double combined_move = (ca_move + us_move) / 2.0;
  const double tariff_relief = (issue_move(package, "us-tariff-relief", false)
      + issue_move(package, "canada-tariff-relief", true)) / 200.0;
  const double instability = (100.0 - package.stability_score) / 100.0;

  const double canada = package.canada_surplus
      - 8.0 * spec.canada_commitment_penalty * ca_move
      - 5.0 * spec.low_cooperation_penalty * combined_move
      - 5.0 * spec.instability_penalty * instability
      + 4.0 * spec.trade_relief_bonus * tariff_relief;
  const double us = package.us_surplus
      - 8.0 * spec.us_commitment_penalty * us_move
      - 5.0 * spec.low_cooperation_penalty * combined_move
      - 5.0 * spec.instability_penalty * instability
      + 4.0 * spec.trade_relief_bonus * tariff_relief;
  const double floor = std::min(canada, us);
  if (minimum_surplus) *minimum_surplus = floor;
  return floor + 0.18 * std::sqrt(std::max(0.0, canada) * std::max(0.0, us))
      + 0.035 * package.stability_score;
}

inline std::vector<const NegotiationPackage*> package_pool(const NegotiationAnalysis& analysis) {
  std::vector<const NegotiationPackage*> pool;
  for (const auto& package : analysis.frontier) pool.push_back(&package);
  if (pool.empty() && !analysis.recommended.id.empty()) pool.push_back(&analysis.recommended);
  return pool;
}

inline DiplomacyIssueTrack make_issue(std::string id, std::string label, std::string forum,
                                      std::string legal_reference, std::string objective,
                                      std::string verification, std::string link_group,
                                      double ca_priority, double us_priority, double joint_value,
                                      double sensitivity, double complexity, bool parallel = false) {
  DiplomacyIssueTrack issue;
  issue.id = std::move(id); issue.label = std::move(label); issue.forum = std::move(forum);
  issue.legal_reference = std::move(legal_reference); issue.objective = std::move(objective);
  issue.verification = std::move(verification); issue.link_group = std::move(link_group);
  issue.canada_priority = clamp_diplomacy(ca_priority, 0.0, 100.0);
  issue.us_priority = clamp_diplomacy(us_priority, 0.0, 100.0);
  issue.joint_value = clamp_diplomacy(joint_value, 0.0, 100.0);
  issue.domestic_sensitivity = clamp_diplomacy(sensitivity, 0.0, 100.0);
  issue.implementation_complexity = clamp_diplomacy(complexity, 0.0, 100.0);
  issue.parallel_track = parallel;
  return issue;
}

inline std::string escape_platform_json(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    if (c == '\n') { out += "\\n"; continue; }
    if (c == '\r') { out += "\\r"; continue; }
    if (c == '\t') { out += "\\t"; continue; }
    out.push_back(c);
  }
  return out;
}

}  // namespace diplomacy_detail

inline TradeDiplomacyPlatform build_trade_diplomacy_platform(
    const Economy& economy, const Result& result, const NegotiationAnalysis& negotiation) {
  using namespace diplomacy_detail;
  TradeDiplomacyPlatform platform;
  const auto pool = package_pool(negotiation);
  if (pool.empty()) {
    platform.readiness_label = "No individually rational package available";
    platform.operating_doctrine = "Strengthen outside-option analysis and revisit mandates before exchanging concessions.";
    return platform;
  }

  // Robustness screen. This is deliberately not an acceptance-probability model.
  // It asks whether a package still protects both sides' modeled surplus under a
  // compact set of harder bargaining/implementation assumptions.
  struct Work { const NegotiationPackage* package; DiplomacyRobustPackage result; };
  std::vector<Work> work;
  for (const auto* package : pool) {
    Work item; item.package = package; item.result.package_id = package->id;
    item.result.strategy_name = package->strategy_name;
    item.result.worst_case_surplus = std::numeric_limits<double>::infinity();
    double min_sum = 0.0;
    for (const auto& spec : stress_specs) {
      double floor = 0.0; (void)stress_score(*package, spec, &floor);
      item.result.worst_case_surplus = std::min(item.result.worst_case_surplus, floor);
      min_sum += floor;
    }
    item.result.average_min_surplus = min_sum / static_cast<double>(std::size(stress_specs));
    item.result.clears_all_cases = item.result.worst_case_surplus >= -1e-9;
    item.result.robust_score = item.result.worst_case_surplus
        + 0.40 * item.result.average_min_surplus + 0.025 * package->stability_score
        + 0.015 * package->nash_gain;
    work.push_back(item);
  }

  for (const auto& spec : stress_specs) {
    DiplomacyRobustnessCase case_result;
    case_result.id = spec.id; case_result.label = spec.label; case_result.description = spec.description;
    double best = -std::numeric_limits<double>::infinity(); std::size_t best_index = 0;
    for (std::size_t i = 0; i < work.size(); ++i) {
      const double score = stress_score(*work[i].package, spec);
      if (score > best) { best = score; best_index = i; }
    }
    work[best_index].result.case_wins++;
    case_result.winner_package_id = work[best_index].package->id;
    case_result.winner_score = best;
    platform.robustness_cases.push_back(std::move(case_result));
  }
  platform.robust_cases = static_cast<int>(platform.robustness_cases.size());
  std::sort(work.begin(), work.end(), [](const Work& a, const Work& b) {
    if (a.result.clears_all_cases != b.result.clears_all_cases) return a.result.clears_all_cases > b.result.clears_all_cases;
    if (a.result.case_wins != b.result.case_wins) return a.result.case_wins > b.result.case_wins;
    return a.result.robust_score > b.result.robust_score;
  });
  for (const auto& item : work) platform.robust_packages.push_back(item.result);
  const auto* preferred = work.front().package;
  platform.recommended_robust_package_id = preferred->id;
  platform.recommended_worst_case_surplus = work.front().result.worst_case_surplus;
  if (work.size() > 1) platform.bridge_package_id = work[1].package->id;
  else platform.bridge_package_id = preferred->id;

  const double us_tariff_pressure = clamp_diplomacy(economy.us_tariff_canada * 1.6, 0.0, 100.0);
  const double ca_retaliation_pressure = clamp_diplomacy(economy.canada_retaliatory_tariff * 2.2, 0.0, 100.0);
  const double border_pressure = clamp_diplomacy(economy.border_friction * 18.0, 0.0, 100.0);
  const double instability = 100.0 - preferred->stability_score;
  const double supply_move = issue_move(*preferred, "supply-chain", true);
  const double procurement_move = issue_move(*preferred, "procurement", true);

  // Treaty-aware operational issue registry. “Parallel” tracks are deliberately
  // labelled as such so the tool does not imply that every bilateral issue is a
  // CUSMA/USMCA chapter obligation.
  platform.issue_tracks = {
      make_issue("market-access", "Goods market access and tariff measures", "CUSMA/USMCA + applicable parallel measures",
          "Goods market-access disciplines; verify the legal basis of each tariff measure",
          "Preserve predictable market access while creating reciprocal, sequenced off-ramps from trade restrictions.",
          "Tariff schedules, customs entries, exemption coverage and effective dates", "market-access",
          88 + .10 * us_tariff_pressure, 82 + .08 * ca_retaliation_pressure, 94, 88, 72),
      make_issue("origin-autos", "Rules of origin and automotive integration", "CUSMA/USMCA review track",
          "Rules-of-origin disciplines and automotive-specific origin requirements",
          "Strengthen North American content incentives without creating unworkable compliance burdens or supply bottlenecks.",
          "Origin certification, regional value content, sourcing and production records", "industrial-security",
          78, 92, 88 + .06 * supply_move, 86, 90),
      make_issue("steel-aluminum", "Steel, aluminum and derivative products", "Parallel bilateral / economic-security track",
          "Parallel trade-remedy and economic-security measures; legal basis must be identified measure by measure",
          "Convert recurring tariff escalation into a monitored capacity, origin and circumvention framework.",
          "Shipment origin, melt/pour data where applicable, capacity, transshipment and customs enforcement data", "industrial-security",
          82, 90, 82, 92, 88, true),
      make_issue("economic-security", "Economic security and non-market inputs", "CUSMA/USMCA review + parallel coordination",
          "Cross-cutting review cooperation; map each instrument to its actual legal authority",
          "Coordinate resilience rules while preventing third-country circumvention and preserving bilateral supply-chain efficiency.",
          "Beneficial ownership where lawful, origin, investment-screening interfaces, sourcing and traceability metrics", "industrial-security",
          84, 94, 91 + .05 * supply_move, 82, 94),
      make_issue("customs-regulatory", "Customs, border and regulatory compatibility", "CUSMA/USMCA implementation track",
          "Customs/trade facilitation, sectoral annexes and regulatory-cooperation disciplines",
          "Bank early joint gains by reducing border friction and aligning practical implementation where mandates allow.",
          "Release times, inspections, trusted-trader utilization, duplicate testing and regulatory service levels", "early-harvest",
          76 + .12 * border_pressure, 76 + .12 * border_pressure, 96, 42, 62),
      make_issue("agriculture", "Agriculture and agri-food market access", "CUSMA/USMCA review track",
          "Agriculture, sanitary/phytosanitary and applicable market-access disciplines",
          "Protect predictable access, address implementation frictions and isolate politically sensitive asks from unrelated files.",
          "Quota administration where applicable, border treatment, SPS measures and product-specific trade flows", "market-access",
          86, 90, 82, 96, 78),
      make_issue("digital-payments", "Digital trade, data and electronic payments", "CUSMA/USMCA review track",
          "Digital-trade and financial-services interfaces; confirm instrument-specific scope",
          "Preserve interoperable digital commerce while resolving payment, data and regulatory compatibility concerns.",
          "Authorization timelines, payment access, data-transfer restrictions and measurable compliance obligations", "modernization",
          68, 78, 84, 64, 76),
      make_issue("procurement", "Procurement and reciprocal access", "Parallel bilateral / instrument-specific track",
          "Do not assume bilateral CUSMA procurement coverage; map commitments to the applicable procurement instrument",
          "Use procurement as a linked bargaining instrument only where legal authority and domestic mandates support it.",
          "Tender eligibility, origin rules, covered entities and award data", "market-access",
          64 + .12 * procurement_move, 72 + .10 * procurement_move, 78, 90, 84, true),
      make_issue("energy-minerals", "Energy, critical minerals and strategic infrastructure", "CUSMA/USMCA + parallel economic-security track",
          "Multiple agreement and domestic-authority interfaces; require issue-specific legal mapping",
          "Create investable cross-border supply arrangements with credible permitting, infrastructure and security commitments.",
          "Project milestones, capacity, offtake, origin, permitting and infrastructure availability", "industrial-security",
          88, 86, 93, 70, 92),
      make_issue("labor-environment", "Labor and environment", "CUSMA/USMCA implementation/review track",
          "Labor and environment disciplines",
          "Prevent competitive-friction spillovers by pairing enforceable commitments with transparent implementation evidence.",
          "Complaint resolution, enforcement actions, implementation milestones and published compliance evidence", "standards",
          62, 72, 70, 72, 80),
      make_issue("dispute-enforcement", "Dispute prevention, enforcement and review", "CUSMA/USMCA governance + package implementation",
          "Dispute-settlement, committee and review architecture; legal review required for any new mechanism",
          "Design the bargain so compliance is cheaper than defection and disagreements are resolved before tariff escalation.",
          "Joint monitoring, notice-and-cure records, consultations, panel/committee records where applicable", "enforcement",
          84 + .10 * instability, 84 + .10 * instability, 96, 58, 94),
      make_issue("softwood-parallel", "Softwood lumber and other persistent bilateral files", "Parallel bilateral track",
          "Outside the core CUSMA bargaining model unless a specific legal linkage is established",
          "Keep persistent bilateral disputes visible without falsely treating them as interchangeable CUSMA concessions.",
          "Case-specific administrative, trade-remedy and settlement evidence", "parallel",
          68, 58, 54, 92, 86, true)
  };

  std::sort(platform.issue_tracks.begin(), platform.issue_tracks.end(), [](const auto& a, const auto& b) {
    const double av = .35*a.canada_priority + .35*a.us_priority + .30*a.joint_value - .12*a.implementation_complexity;
    const double bv = .35*b.canada_priority + .35*b.us_priority + .30*b.joint_value - .12*b.implementation_complexity;
    return av > bv;
  });

  platform.round_plan = {
      {1, "Mandate and facts", "Freeze the factual record, legal forum and delegation authorities before trading concessions.", "",
       "Confirm the baseline, outside options, red lines, issue ownership and which files are CUSMA/USMCA versus parallel bilateral tracks.",
       "No unresolved data-vintage conflict; every active issue has a legal/forum owner and a decision authority."},
      {2, "Early harvest", "Build momentum on high-joint-value, lower-sensitivity implementation issues.", preferred->id,
       "Lead with customs, border and regulatory compatibility plus measurable supply-chain cooperation; avoid spending tariff concessions before verification architecture is agreed.",
       "At least one reciprocal implementation deliverable with named evidence, timing and escalation path."},
      {3, "Conditional exchange", "Trade costly moves only through explicit issue linkage.", preferred->id,
       "Exchange tariff/retaliation relief conditionally against market-access, origin, border, procurement or security commitments; write every move as if/then rather than unilateral goodwill.",
       "Both delegations remain above reservation values under the Canada-hard and U.S.-hard robustness cases."},
      {4, "Package round", "Put a preferred package and credible bridge package on the table.", preferred->id,
       "Table the robust preferred package; hold the bridge package in reserve for a political or sector-specific constraint rather than collapsing immediately to the BATNA.",
       "Package clears individual rationality, has a documented enforcement plan and no unidentified parallel-track dependencies."},
      {5, "Closure architecture", "Turn political convergence into an implementable bargain.", preferred->id,
       "Sequence obligations, define verification data, notice-and-cure, consultation, review and proportionate snap-back/response mechanisms subject to legal authority.",
       "Legal/mandate gates are cleared, implementation owners accept the metrics and deviation incentives are addressed."},
      {6, "Post-agreement management", "Protect the deal from implementation drift and recurring escalation.", preferred->id,
       "Run a joint dashboard against agreed evidence; surface slippage before it becomes a tariff or dispute event and keep parallel disputes on separate ledgers unless formally linked.",
       "Regular review cadence functioning; exceptions and disputes have owners, evidence and cure paths."}
  };

  platform.guardrails = {
      {"reciprocal-tranches", "Reciprocal implementation tranches",
       "A party is asked to make a costly concession before the counterparty has delivered the linked commitment.",
       "Effective dates, tariff/customs implementation, regulatory notices and named implementation milestones.",
       "Pause the next tranche; use notice-and-cure before any proportionate reversion permitted by the governing authority.", "Per negotiated tranche"},
      {"origin-verification", "Origin and circumvention verification",
       "A rules-of-origin or economic-security benefit depends on traceability or exclusion of non-party free-riding.",
       "Origin records, sourcing, production, customs and anti-circumvention evidence appropriate to the sector.",
       "Escalate to technical consultation, targeted verification and only then the authorized dispute/remedy path.", "Continuous / risk-based"},
      {"border-service", "Border and regulatory service levels",
       "The package promises trade facilitation or regulatory compatibility.",
       "Clearance time, duplicate testing, inspection, trusted-trader and licensing/authorization service metrics.",
       "Technical remediation plan with an owner and deadline before reopening unrelated concessions.", "Monthly/quarterly operational review"},
      {"deviation-screen", "Defection and implementation-risk review",
       "Either delegation has a positive modeled unilateral-deviation gain or stability falls below 70/100.",
       "Negotiation stability score, missed milestones, unresolved exceptions and implementation-cost evidence.",
       "Strengthen sequencing, verification, escrow-like conditionality where lawful, consultation, or proportionate safeguard design.", "Before signature and each major implementation phase"},
      {"sunset-review", "Periodic package review without automatic renegotiation",
       "Material economic/security conditions change or an agreed review point is reached.",
       "Trade flows, investment, compliance, supply-chain resilience, dispute frequency and distributional impacts.",
       "Targeted adjustment first; reopen the full package only under the agreed governance process.", "Negotiated review cadence"}
  };

  platform.stakeholder_gates = {
      {"Canada", "Domestic negotiating mandate", "mandate", "Confirm the delegation has authority for the contemplated concessions and linked files.", 100, true},
      {"Canada", "Legal and treaty review", "legal", "Map each commitment to the correct treaty, statute, regulation or parallel instrument before it is offered.", 100, true},
      {"Canada", "Federal–provincial/territorial coordination", "domestic-coalition", "Identify sub-federal implementation, political and sector exposure early rather than after package formation.", 82, false},
      {"Canada", "Indigenous, labor, industry and affected-community engagement", "stakeholder", "Surface implementation, distributional and rights considerations before they become late-stage blockers.", 76, false},
      {"United States", "Domestic negotiating mandate", "mandate", "Confirm authority and interagency alignment for contemplated concessions and enforcement mechanisms.", 100, true},
      {"United States", "Legal and treaty review", "legal", "Map each commitment to the correct agreement and domestic authority before it is treated as executable.", 100, true},
      {"United States", "Congressional consultation where applicable", "domestic-coalition", "Flag commitments that require or materially benefit from legislative consultation or implementation.", 90, false},
      {"United States", "State, labor, agriculture and industry implementation review", "stakeholder", "Identify geographically and sectorally concentrated implementation risk.", 78, false},
      {"Joint", "Binational implementation owners", "implementation", "Every negotiated obligation needs an owner, evidence source, cadence and escalation path on both sides.", 94, true}
  };

  platform.evidence_ledger = {
      {"Economic baseline feeds and documented fallback baseline", "Macro and market starting conditions", "mixed live/cached by field", "Refresh before each decision round and display vintage explicitly."},
      {"CUSMA/USMCA agreement text and committee/governance materials", "Legal/forum mapping", "authoritative reference required", "Legal team verifies the exact article/instrument for every commitment before offer or signature."},
      {"Government consultation and joint-review records", "Current stated review themes and stakeholder concerns", "public reference, not a mandate", "Refresh before each round; do not infer undisclosed government preferences."},
      {"Customs, trade, investment and sector operating data", "Verification and implementation metrics", "not yet fully automated", "Bind each agreed obligation to a named data owner and reproducible calculation."},
      {"Model assumptions, stress cases and negotiation utility functions", "Analytical provenance", "version-controlled", "Record commit/model version with every briefing and decision snapshot."}
  };

  const double stakeholder_burden = 82.0;
  const double robustness_component = work.front().result.clears_all_cases ? 100.0
      : clamp_diplomacy(55.0 + 8.0 * work.front().result.worst_case_surplus, 0.0, 100.0);
  platform.operational_readiness = clamp_diplomacy(
      .34 * preferred->stability_score + .32 * robustness_component
      + .18 * (100.0 - .35 * stakeholder_burden) + .16 * 78.0, 0.0, 100.0);
  if (!work.front().result.clears_all_cases) platform.readiness_label = "Rework package before mandate use";
  else if (preferred->stability_score < 70.0) platform.readiness_label = "Analytically viable; enforcement work required";
  else if (platform.operational_readiness >= 75.0) platform.readiness_label = "Ready for structured delegation review";
  else platform.readiness_label = "Promising; mandate and implementation gates remain";

  platform.operating_doctrine =
      "Use the model to expand and discipline the option set, not to manufacture political authority. "
      "Keep CUSMA/USMCA issues legally separated from parallel bilateral files unless negotiators deliberately link them; "
      "prefer packages that remain individually rational under harder mandates; exchange costly concessions conditionally; "
      "and require an owner, evidence source, verification cadence and lawful response path for every material commitment.";
  (void)result;
  return platform;
}

inline std::string trade_diplomacy_json(const TradeDiplomacyPlatform& p) {
  using diplomacy_detail::escape_platform_json;
  std::ostringstream o; o << std::fixed << std::setprecision(3);
  o << "{\"mode\":\"" << escape_platform_json(p.mode)
    << "\",\"recommendedRobustPackageId\":\"" << escape_platform_json(p.recommended_robust_package_id)
    << "\",\"bridgePackageId\":\"" << escape_platform_json(p.bridge_package_id)
    << "\",\"readinessLabel\":\"" << escape_platform_json(p.readiness_label)
    << "\",\"operationalReadiness\":" << p.operational_readiness
    << ",\"recommendedWorstCaseSurplus\":" << p.recommended_worst_case_surplus
    << ",\"robustCases\":" << p.robust_cases
    << ",\"operatingDoctrine\":\"" << escape_platform_json(p.operating_doctrine) << "\",\"issueTracks\":[";
  for (std::size_t i=0;i<p.issue_tracks.size();++i) { if(i)o<<','; const auto& x=p.issue_tracks[i];
    o << "{\"id\":\""<<escape_platform_json(x.id)<<"\",\"label\":\""<<escape_platform_json(x.label)
      <<"\",\"forum\":\""<<escape_platform_json(x.forum)<<"\",\"legalReference\":\""<<escape_platform_json(x.legal_reference)
      <<"\",\"objective\":\""<<escape_platform_json(x.objective)<<"\",\"verification\":\""<<escape_platform_json(x.verification)
      <<"\",\"linkGroup\":\""<<escape_platform_json(x.link_group)<<"\",\"canadaPriority\":"<<x.canada_priority
      <<",\"usPriority\":"<<x.us_priority<<",\"jointValue\":"<<x.joint_value<<",\"domesticSensitivity\":"<<x.domestic_sensitivity
      <<",\"implementationComplexity\":"<<x.implementation_complexity<<",\"parallelTrack\":"<<(x.parallel_track?"true":"false")<<'}'; }
  o << "],\"robustnessCases\":[";
  for(std::size_t i=0;i<p.robustness_cases.size();++i){if(i)o<<',';const auto& x=p.robustness_cases[i];o<<"{\"id\":\""<<escape_platform_json(x.id)<<"\",\"label\":\""<<escape_platform_json(x.label)<<"\",\"description\":\""<<escape_platform_json(x.description)<<"\",\"winnerPackageId\":\""<<escape_platform_json(x.winner_package_id)<<"\",\"winnerScore\":"<<x.winner_score<<'}';}
  o << "],\"robustPackages\":[";
  for(std::size_t i=0;i<p.robust_packages.size();++i){if(i)o<<',';const auto& x=p.robust_packages[i];o<<"{\"packageId\":\""<<escape_platform_json(x.package_id)<<"\",\"strategyName\":\""<<escape_platform_json(x.strategy_name)<<"\",\"worstCaseSurplus\":"<<x.worst_case_surplus<<",\"averageMinSurplus\":"<<x.average_min_surplus<<",\"robustScore\":"<<x.robust_score<<",\"caseWins\":"<<x.case_wins<<",\"clearsAllCases\":"<<(x.clears_all_cases?"true":"false")<<'}';}
  o << "],\"roundPlan\":[";
  for(std::size_t i=0;i<p.round_plan.size();++i){if(i)o<<',';const auto& x=p.round_plan[i];o<<"{\"order\":"<<x.order<<",\"phase\":\""<<escape_platform_json(x.phase)<<"\",\"objective\":\""<<escape_platform_json(x.objective)<<"\",\"packageId\":\""<<escape_platform_json(x.package_id)<<"\",\"negotiatingMove\":\""<<escape_platform_json(x.negotiating_move)<<"\",\"exitCriteria\":\""<<escape_platform_json(x.exit_criteria)<<"\"}";}
  o << "],\"guardrails\":[";
  for(std::size_t i=0;i<p.guardrails.size();++i){if(i)o<<',';const auto& x=p.guardrails[i];o<<"{\"id\":\""<<escape_platform_json(x.id)<<"\",\"mechanism\":\""<<escape_platform_json(x.mechanism)<<"\",\"trigger\":\""<<escape_platform_json(x.trigger)<<"\",\"evidence\":\""<<escape_platform_json(x.evidence)<<"\",\"response\":\""<<escape_platform_json(x.response)<<"\",\"cadence\":\""<<escape_platform_json(x.cadence)<<"\"}";}
  o << "],\"stakeholderGates\":[";
  for(std::size_t i=0;i<p.stakeholder_gates.size();++i){if(i)o<<',';const auto& x=p.stakeholder_gates[i];o<<"{\"country\":\""<<escape_platform_json(x.country)<<"\",\"gate\":\""<<escape_platform_json(x.gate)<<"\",\"type\":\""<<escape_platform_json(x.type)<<"\",\"rationale\":\""<<escape_platform_json(x.rationale)<<"\",\"sensitivity\":"<<x.sensitivity<<",\"formalAuthority\":"<<(x.formal_authority?"true":"false")<<'}';}
  o << "],\"evidenceLedger\":[";
  for(std::size_t i=0;i<p.evidence_ledger.size();++i){if(i)o<<',';const auto& x=p.evidence_ledger[i];o<<"{\"source\":\""<<escape_platform_json(x.source)<<"\",\"purpose\":\""<<escape_platform_json(x.purpose)<<"\",\"status\":\""<<escape_platform_json(x.status)<<"\",\"refreshRule\":\""<<escape_platform_json(x.refresh_rule)<<"\"}";}
  o << "]}"; return o.str();
}

inline std::string attach_trade_diplomacy_json(std::string base_json,
                                                const TradeDiplomacyPlatform& platform) {
  if (!base_json.empty() && base_json.back() == '}') base_json.pop_back();
  return base_json + ",\"tradeDiplomacy\":" + trade_diplomacy_json(platform) + "}";
}

}  // namespace cad
