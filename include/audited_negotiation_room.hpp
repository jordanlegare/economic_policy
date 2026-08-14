#pragma once

#include "durable_journal.hpp"
#include "negotiation_room.hpp"
#include "request_json.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cad {

class AuditedNegotiationRoom : public NegotiationRoom {
 public:
  explicit AuditedNegotiationRoom(std::string event_log_path = {})
      : NegotiationRoom(event_log_path),
        event_log_path_(std::move(event_log_path)),
        snapshot_log_path_(event_log_path_.empty()
            ? std::string{} : event_log_path_ + ".offer-snapshots.jsonl") {
    load_accepted_offer_events();
    if (!snapshot_log_path_.empty())
      snapshot_lines_ = durable_journal::read_lines(snapshot_log_path_);
  }

  void set_evaluation_context(std::string evaluation_fingerprint,
                              std::string calibration_snapshot_id) {
    evaluation_fingerprint_ = std::move(evaluation_fingerprint);
    calibration_snapshot_id_ = std::move(calibration_snapshot_id);
  }

  bool apply_event(const std::string& body,
                   const NegotiationAnalysis* negotiation = nullptr,
                   const RobustRecommendationAnalysis* robustness = nullptr,
                   bool persist = true) {
    audit_last_error_.clear();
    auto event = room_action::parse(body);
    if (!persist || !event.valid || event.action != "offer")
      return NegotiationRoom::apply_event(body, negotiation, robustness, persist);

    // Legacy clients may omit operationId. Canonicalize those offers with an
    // internal replay-safe key before either fsync append. Explicit client IDs
    // continue to provide true retry idempotence; generated IDs provide the
    // crash-recovery identity needed to bind the snapshot to its room event.
    std::string persisted_body = body;
    bool generated_operation_id = false;
    if (!event.operation_id || event.operation_id->empty()) {
      const std::string generated = "offer-auto-"
          + std::to_string(snapshot_lines_.size() + 1);
      persisted_body = canonical_offer_body(event, generated);
      event = room_action::parse(persisted_body);
      generated_operation_id = true;
      if (!event.valid || !event.operation_id) {
        audit_last_error_ = "unable to canonicalize durable offer operation identity";
        return false;
      }
    }
    if (!negotiation || !event.package_id)
      return NegotiationRoom::apply_event(persisted_body, negotiation, robustness, persist);

    const NegotiationPackage* package = find_package(*negotiation, *event.package_id);
    if (!package)
      return NegotiationRoom::apply_event(persisted_body, negotiation, robustness, persist);

    // Validate the complete room transition, including red lines, mandate state
    // and operation-id replay semantics, without mutating the durable room.
    NegotiationRoom validator = static_cast<const NegotiationRoom&>(*this);
    if (!validator.apply_event(persisted_body, negotiation, robustness, false)) {
      audit_last_error_ = validator.last_error();
      return false;
    }

    // A replayed explicit operation already has the economics captured by the
    // original accepted event. Do not rewrite history using the current solve.
    if (validator.last_apply_replayed())
      return NegotiationRoom::apply_event(
          persisted_body, negotiation, robustness, persist);

    const std::string snapshot = make_offer_snapshot(
        event, *package, robustness, generated_operation_id);
    if (!snapshot_log_path_.empty()
        && !durable_journal::append_line(snapshot_log_path_, snapshot)) {
      audit_last_error_ = "unable to durably append immutable offer snapshot";
      return false;
    }

    // The snapshot is durable before the offer event. If the room journal write
    // fails, the snapshot is an orphan and is intentionally hidden on replay;
    // no accepted new-schema offer can therefore exist without a prior durable
    // economics snapshot.
    if (!NegotiationRoom::apply_event(
            persisted_body, negotiation, robustness, persist))
      return false;

    snapshot_lines_.push_back(snapshot);
    remember_accepted_offer(event);
    return true;
  }

  const std::string& last_error() const {
    return audit_last_error_.empty()
        ? NegotiationRoom::last_error() : audit_last_error_;
  }

  std::string json(const NegotiationAnalysis* negotiation = nullptr,
                   const RobustRecommendationAnalysis* robustness = nullptr) const {
    std::string out = NegotiationRoom::json(negotiation, robustness);
    if (out.empty() || out.back() != '}') return out;
    out.pop_back();
    out += ",\"offerSnapshots\":[";

    const auto visible = visible_snapshots();
    for (std::size_t i = 0; i < visible.size(); ++i) {
      if (i) out += ',';
      out += visible[i];
    }
    out += "],\"offerSnapshotAudit\":{\"schemaVersion\":\"offer-snapshot-v1\""
        ",\"persistence\":\"fsync-before-room-event\""
        ",\"operationIdPolicy\":\"client-or-server-generated\""
        ",\"newOfferSnapshotContract\":\"self-contained\""
        ",\"legacyOffersMayLackSnapshot\":true"
        ",\"snapshotCount\":" + std::to_string(visible.size()) + "}}";
    return out;
  }

 private:
  static const NegotiationPackage* find_package(
      const NegotiationAnalysis& negotiation, const std::string& package_id) {
    if (negotiation.recommended.id == package_id) return &negotiation.recommended;
    for (const auto& package : negotiation.frontier)
      if (package.id == package_id) return &package;
    return nullptr;
  }

  static const RobustPackageMetrics* find_metrics(
      const RobustRecommendationAnalysis* robustness,
      const std::string& package_id) {
    if (!robustness) return nullptr;
    for (const auto& metrics : robustness->packages)
      if (metrics.package_id == package_id) return &metrics;
    return nullptr;
  }

  static std::string escaped(const std::string& value) {
    return room_detail::esc(value);
  }

  static std::string canonical_offer_body(
      const room_action::Event& event, const std::string& operation_id) {
    std::ostringstream out;
    out << "{\"action\":\"offer\""
        << ",\"operationId\":\"" << escaped(operation_id) << "\"";
    if (event.side)
      out << ",\"side\":\"" << escaped(*event.side) << "\"";
    if (event.package_id)
      out << ",\"packageId\":\"" << escaped(*event.package_id) << "\"";
    if (event.note)
      out << ",\"note\":\"" << escaped(*event.note) << "\"";
    out << '}';
    return out.str();
  }

  static std::string issue_snapshot(const NegotiationPackage& package) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < package.issues.size(); ++i) {
      if (i) out << '|';
      out << package.issues[i].id << ':'
          << package.issues[i].canada_move << ':'
          << package.issues[i].us_move;
    }
    return out.str();
  }

  std::string make_offer_snapshot(
      const room_action::Event& event,
      const NegotiationPackage& package,
      const RobustRecommendationAnalysis* robustness,
      bool generated_operation_id) const {
    const auto* metrics = find_metrics(robustness, package.id);
    const std::string operation_id = event.operation_id.value_or("");
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\"schemaVersion\":\"offer-snapshot-v1\""
        << ",\"operationId\":\"" << escaped(operation_id) << "\""
        << ",\"operationIdGenerated\":"
        << (generated_operation_id ? "true" : "false")
        << ",\"packageId\":\"" << escaped(package.id) << "\""
        << ",\"paretoRankAtOffer\":" << package.pareto_rank
        << ",\"evaluationFingerprint\":\"" << escaped(evaluation_fingerprint_) << "\""
        << ",\"calibrationSnapshotId\":\"" << escaped(calibration_snapshot_id_) << "\""
        << ",\"strategyId\":\"" << escaped(package.strategy_id) << "\""
        << ",\"issueTermsSnapshot\":\"" << escaped(issue_snapshot(package)) << "\""
        << ",\"canadaUtility\":" << package.canada_utility
        << ",\"usUtility\":" << package.us_utility
        << ",\"canadaSurplus\":" << package.canada_surplus
        << ",\"usSurplus\":" << package.us_surplus
        << ",\"macroBaseVerified\":" << (package.macro_base_verified ? "true" : "false")
        << ",\"sectorPostureVerified\":" << (package.sector_posture_verified ? "true" : "false")
        << ",\"bargainingTermsScreened\":" << (package.bargaining_terms_screened ? "true" : "false")
        << ",\"fullPackageResimulated\":" << (package.full_package_resimulated ? "true" : "false")
        << ",\"robustMetricsPresent\":" << (metrics ? "true" : "false")
        << ",\"bargainingRobustnessPassed\":"
        << (metrics && metrics->clears_probability_gate ? "true" : "false")
        << ",\"robustSamples\":" << (metrics ? metrics->samples : 0)
        << ",\"jointClearProbability\":" << (metrics ? metrics->joint_clear_probability : 0.0)
        << ",\"canadaCvar10Surplus\":" << (metrics ? metrics->canada_cvar10_surplus : 0.0)
        << ",\"usCvar10Surplus\":" << (metrics ? metrics->us_cvar10_surplus : 0.0)
        << ",\"maxRegret\":" << (metrics ? metrics->max_regret : 0.0)
        << '}';
    return out.str();
  }

  void remember_accepted_offer(const room_action::Event& event) {
    if (event.operation_id && !event.operation_id->empty())
      accepted_operation_ids_.insert(*event.operation_id);
  }

  void load_accepted_offer_events() {
    if (event_log_path_.empty()) return;
    for (const auto& line : durable_journal::read_lines(event_log_path_)) {
      const auto event = room_action::parse(line);
      if (event.valid && event.action == "offer") remember_accepted_offer(event);
    }
  }

  bool snapshot_is_visible(const std::string& line) const {
    const auto object = request_json::parse_object(line);
    if (!object.valid) return false;
    const auto operation_id = object.string("operationId").value_or("");
    if (operation_id.empty()) return false;
    return accepted_operation_ids_.count(operation_id) != 0;
  }

  std::vector<std::string> visible_snapshots() const {
    std::map<std::string, std::string> by_operation;
    for (const auto& line : snapshot_lines_) {
      if (!snapshot_is_visible(line)) continue;
      const auto object = request_json::parse_object(line);
      const auto operation_id = object.string("operationId").value_or("");
      by_operation[operation_id] = line;
    }
    std::vector<std::string> out;
    out.reserve(by_operation.size());
    for (const auto& [_, line] : by_operation) out.push_back(line);
    return out;
  }

  std::string event_log_path_;
  std::string snapshot_log_path_;
  std::string evaluation_fingerprint_;
  std::string calibration_snapshot_id_;
  std::string audit_last_error_;
  std::vector<std::string> snapshot_lines_;
  std::unordered_set<std::string> accepted_operation_ids_;
};

}  // namespace cad
