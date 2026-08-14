#pragma once

#include "durable_journal.hpp"
#include "negotiation_support.hpp"
#include "robust_recommendation.hpp"
#include "room_action.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct MandateRule {
  std::string issue_id;
  double max_canada_move = 100.0;
  double min_us_move = 0.0;
  std::string authority = "delegation_discretion";
  bool hard_red_line = false;
  std::string note;
};

struct OfferRecord {
  unsigned long revision = 0;
  int round = 1;
  std::string side;
  std::string package_id;
  std::string note;
};

struct ConcessionRecord {
  unsigned long revision = 0;
  int round = 1;
  std::string side;
  std::string issue_id;
  double magnitude = 0.0;
  double estimated_own_cost = 0.0;
  double estimated_counterpart_value = 0.0;
  bool reciprocal = false;
  bool conditional = true;
  std::string note;
};

struct PlaybookRecord {
  unsigned long revision = 0;
  std::string issue_id;
  std::string trigger;
  std::string response;
  std::string authority = "delegation_discretion";
};

struct DebriefRecord {
  unsigned long revision = 0;
  int round = 1;
  std::string summary;
  std::string counterpart_signals;
  std::string unresolved;
  std::string next_actions;
};

struct CounterofferSuggestion {
  std::string category;
  std::string package_id;
  double joint_clear_probability = 0.0;
  double canada_cvar10_surplus = 0.0;
  double us_cvar10_surplus = 0.0;
  double max_regret = 0.0;
  std::string authority_status;
  std::string rationale;
};

namespace room_detail {

inline std::string esc(const std::string& value) {
  return negotiation_detail::escape_json(value);
}

inline const NegotiationPackage* find_package(const NegotiationAnalysis* analysis,
                                              const std::string& id) {
  if (!analysis) return nullptr;
  for (const auto& package : analysis->frontier) if (package.id == id) return &package;
  return nullptr;
}

inline const RobustPackageMetrics* find_metrics(const RobustRecommendationAnalysis* analysis,
                                                const std::string& id) {
  if (!analysis) return nullptr;
  for (const auto& metrics : analysis->packages) if (metrics.package_id == id) return &metrics;
  return nullptr;
}

struct Clearance {
  bool blocked = false;
  bool escalation = false;
  std::string authority = "within-delegation-mandate";
  std::string reason;
};

inline Clearance clearance(const NegotiationPackage& package,
                           const std::map<std::string, MandateRule>& rules) {
  Clearance out;
  for (const auto& issue : package.issues) {
    const auto it = rules.find(issue.id);
    if (it == rules.end()) continue;
    const auto& rule = it->second;
    const bool canada_exceeds = issue.canada_move > rule.max_canada_move + 1e-9;
    const bool us_below = issue.us_move + 1e-9 < rule.min_us_move;
    if (canada_exceeds || us_below) {
      if (rule.hard_red_line) {
        out.blocked = true;
        out.authority = "blocked-by-red-line";
      } else {
        out.escalation = true;
        out.authority = "requires-mandate-escalation";
      }
      if (!out.reason.empty()) out.reason += "; ";
      out.reason += issue.label + (canada_exceeds ? " exceeds Canada's authorized move" : " lacks required U.S. reciprocity");
    }
    if (issue.canada_move > 1e-9 && rule.authority != "delegation_discretion") {
      out.escalation = true;
      if (!out.blocked) out.authority = rule.authority;
      if (!out.reason.empty()) out.reason += "; ";
      out.reason += issue.label + " requires " + rule.authority;
    }
  }
  if (out.reason.empty()) out.reason = "Package is inside the recorded delegation mandate.";
  return out;
}

inline void append_unique(std::vector<CounterofferSuggestion>& out,
                          const std::string& category,
                          const NegotiationPackage* package,
                          const RobustPackageMetrics* metrics,
                          const std::map<std::string, MandateRule>& rules,
                          const std::string& rationale) {
  if (!package || !metrics) return;
  for (const auto& existing : out) if (existing.package_id == package->id) return;
  const auto auth = clearance(*package, rules);
  if (auth.blocked) return;
  out.push_back({category, package->id, metrics->joint_clear_probability,
      metrics->canada_cvar10_surplus, metrics->us_cvar10_surplus,
      metrics->max_regret, auth.authority, rationale + " " + auth.reason});
}

}  // namespace room_detail

class NegotiationRoom {
 public:
  explicit NegotiationRoom(std::string event_log_path = "runtime/negotiation-room.events")
      : event_log_path_(std::move(event_log_path)) {
    const std::vector<std::string> issues = {"us-tariff-relief", "canada-tariff-relief",
        "border-facilitation", "procurement", "supply-chain"};
    for (const auto& issue : issues) {
      MandateRule rule;
      rule.issue_id = issue;
      mandate_.emplace(issue, std::move(rule));
    }
    load();
  }

  bool apply_event(const std::string& body, const NegotiationAnalysis* negotiation = nullptr,
                   const RobustRecommendationAnalysis* robustness = nullptr,
                   bool persist = true) {
    last_error_.clear();
    last_apply_replayed_ = false;
    const auto event = room_action::parse(body);
    if (!event.valid) {
      last_error_ = event.error;
      return false;
    }
    const std::string canonical = room_action::to_json(event);

    if (event.operation_id) {
      const auto existing = operation_fingerprints_.find(*event.operation_id);
      if (existing != operation_fingerprints_.end()) {
        if (existing->second == canonical) {
          last_apply_replayed_ = true;
          return true;
        }
        last_error_ = "operationId was already used for a different room action";
        return false;
      }
    }

    // Persistent room writes are transactional with respect to process memory:
    // stage the mutation in a detached copy, require a successful durable append,
    // then publish the staged state. A disk/path failure therefore cannot leave
    // memory ahead of durable history. If the process dies after append but before
    // assignment, replay on the next launch recovers the committed event.
    if (persist) {
      NegotiationRoom candidate = *this;
      if (!candidate.apply_event(canonical, negotiation, robustness, false)) {
        last_error_ = candidate.last_error_;
        return false;
      }
      if (!durable_journal::append_line(event_log_path_, canonical)) {
        last_error_ = "unable to durably append room event";
        return false;
      }
      *this = std::move(candidate);
      return true;
    }

    const auto& action = event.action;
    if (action == "set-round") {
      if (event.round) round_ = *event.round;
      if (event.phase) phase_ = *event.phase;
    } else if (action == "set-mandate" || action == "red-line") {
      auto& rule = mandate_[*event.issue_id];
      rule.issue_id = *event.issue_id;
      if (event.max_canada_move) rule.max_canada_move = *event.max_canada_move;
      if (event.min_us_move) rule.min_us_move = *event.min_us_move;
      if (event.authority) rule.authority = *event.authority;
      if (action == "red-line") rule.hard_red_line = true;
      else if (event.hard_red_line) rule.hard_red_line = *event.hard_red_line;
      if (event.note) rule.note = *event.note;
    } else if (action == "offer") {
      OfferRecord offer;
      offer.revision = revision_ + 1;
      offer.round = round_;
      offer.side = event.side.value_or("canada");
      offer.package_id = *event.package_id;
      offer.note = event.note.value_or("");
      if (negotiation && !room_detail::find_package(negotiation, offer.package_id)) {
        last_error_ = "offer packageId is not present in the current negotiation frontier";
        return false;
      }
      offers_.push_back(std::move(offer));
    } else if (action == "concession") {
      ConcessionRecord concession;
      concession.revision = revision_ + 1;
      concession.round = round_;
      concession.side = event.side.value_or("canada");
      concession.issue_id = *event.issue_id;
      concession.magnitude = event.magnitude.value_or(0.0);
      concession.estimated_own_cost = event.estimated_own_cost.value_or(0.0);
      concession.estimated_counterpart_value = event.estimated_counterpart_value.value_or(0.0);
      concession.reciprocal = event.reciprocal.value_or(false);
      concession.conditional = event.conditional.value_or(true);
      concession.note = event.note.value_or("");
      concessions_.push_back(std::move(concession));
    } else if (action == "playbook") {
      PlaybookRecord playbook;
      playbook.revision = revision_ + 1;
      playbook.issue_id = *event.issue_id;
      playbook.trigger = *event.trigger;
      playbook.response = *event.response;
      playbook.authority = event.authority.value_or("delegation_discretion");
      playbooks_.push_back(std::move(playbook));
    } else if (action == "debrief") {
      DebriefRecord debrief;
      debrief.revision = revision_ + 1;
      debrief.round = round_;
      debrief.summary = *event.summary;
      debrief.counterpart_signals = event.counterpart_signals.value_or("");
      debrief.unresolved = event.unresolved.value_or("");
      debrief.next_actions = event.next_actions.value_or("");
      debriefs_.push_back(std::move(debrief));
    } else {
      last_error_ = "unsupported room action";
      return false;
    }

    ++revision_;
    if (event.operation_id)
      operation_fingerprints_[*event.operation_id] = canonical;
    trim_history();
    (void)robustness;
    return true;
  }

  const std::string& last_error() const { return last_error_; }
  bool last_apply_replayed() const { return last_apply_replayed_; }
  std::size_t recovery_warning_count() const { return recovery_warning_count_; }

  std::vector<CounterofferSuggestion> counteroffers(
      const NegotiationAnalysis* negotiation,
      const RobustRecommendationAnalysis* robustness) const {
    using namespace room_detail;
    std::vector<CounterofferSuggestion> out;
    if (!negotiation || !robustness || robustness->packages.empty()) return out;

    auto package_for = [&](const RobustPackageMetrics* metrics) {
      return metrics ? find_package(negotiation, metrics->package_id) : nullptr;
    };
    auto metric_for_id = [&](const std::string& id) { return find_metrics(robustness, id); };

    const auto* robust_metric = metric_for_id(robustness->recommended_package_id);
    append_unique(out, "Robust default", package_for(robust_metric), robust_metric, mandate_,
        "Minimax-regret recommendation after the reservation-clearance gate.");

    const RobustPackageMetrics* hold = nullptr;
    const RobustPackageMetrics* bridge = nullptr;
    const RobustPackageMetrics* low_regret = nullptr;
    const RobustPackageMetrics* escalation = nullptr;
    for (const auto& metrics : robustness->packages) {
      const auto* package = package_for(&metrics);
      if (!package) continue;
      const auto auth = clearance(*package, mandate_);
      if (auth.blocked) continue;
      if (!auth.escalation) {
        if (!hold || metrics.canada_cvar10_surplus > hold->canada_cvar10_surplus) hold = &metrics;
        if (!bridge || metrics.joint_clear_probability > bridge->joint_clear_probability) bridge = &metrics;
        if (!low_regret || metrics.max_regret < low_regret->max_regret) low_regret = &metrics;
      } else if (!escalation || metrics.joint_clear_probability > escalation->joint_clear_probability) {
        escalation = &metrics;
      }
    }
    append_unique(out, "Hold firm", package_for(hold), hold, mandate_,
        "Protects Canada's modeled downside surplus while staying inside recorded authority.");
    append_unique(out, "Bridge", package_for(bridge), bridge, mandate_,
        "Maximizes the modeled probability that both sides clear their reservation values.");
    append_unique(out, "Low regret", package_for(low_regret), low_regret, mandate_,
        "Minimizes worst realized regret among packages inside the recorded mandate.");
    append_unique(out, "Escalate for authority", package_for(escalation), escalation, mandate_,
        "Economically attractive but requires authority beyond the delegation's recorded discretion.");
    return out;
  }

  std::string json(const NegotiationAnalysis* negotiation = nullptr,
                   const RobustRecommendationAnalysis* robustness = nullptr) const {
    using room_detail::esc;
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "{\"sessionId\":\"local-room-1\",\"revision\":" << revision_
        << ",\"round\":" << round_ << ",\"phase\":\"" << esc(phase_)
        << "\",\"persistence\":{\"mode\":\"fsync-append-only-event-log\",\"path\":\""
        << esc(event_log_path_) << "\",\"eventSchemaVersion\":1,\"writeFailureRejectsMutation\":true"
        << ",\"operationIdDeduplication\":true,\"recoveryWarnings\":" << recovery_warning_count_
        << ",\"secureForProtectedInformation\":false},\"mandate\":[";
    std::size_t index = 0;
    for (const auto& item : mandate_) {
      if (index++) out << ',';
      const auto& rule = item.second;
      out << "{\"issueId\":\"" << esc(rule.issue_id) << "\",\"maxCanadaMove\":" << rule.max_canada_move
          << ",\"minUsMove\":" << rule.min_us_move << ",\"authority\":\"" << esc(rule.authority)
          << "\",\"hardRedLine\":" << (rule.hard_red_line ? "true" : "false")
          << ",\"note\":\"" << esc(rule.note) << "\"}";
    }
    out << "],\"offers\":[";
    for (std::size_t i = 0; i < offers_.size(); ++i) {
      if (i) out << ',';
      const auto& offer = offers_[i];
      const auto* metrics = room_detail::find_metrics(robustness, offer.package_id);
      out << "{\"revision\":" << offer.revision << ",\"round\":" << offer.round
          << ",\"side\":\"" << esc(offer.side) << "\",\"packageId\":\"" << esc(offer.package_id)
          << "\",\"note\":\"" << esc(offer.note) << "\",\"currentJointClearProbability\":"
          << (metrics ? metrics->joint_clear_probability : 0.0) << '}';
    }
    out << "],\"concessions\":[";
    double canada_given = 0.0, us_given = 0.0;
    for (std::size_t i = 0; i < concessions_.size(); ++i) {
      if (i) out << ',';
      const auto& c = concessions_[i];
      const double cost = c.estimated_own_cost > 0.0 ? c.estimated_own_cost : c.magnitude;
      if (c.side == "us") us_given += cost; else canada_given += cost;
      out << "{\"revision\":" << c.revision << ",\"round\":" << c.round
          << ",\"side\":\"" << esc(c.side) << "\",\"issueId\":\"" << esc(c.issue_id)
          << "\",\"magnitude\":" << c.magnitude << ",\"estimatedOwnCost\":" << c.estimated_own_cost
          << ",\"estimatedCounterpartValue\":" << c.estimated_counterpart_value
          << ",\"reciprocal\":" << (c.reciprocal ? "true" : "false")
          << ",\"conditional\":" << (c.conditional ? "true" : "false")
          << ",\"note\":\"" << esc(c.note) << "\"}";
    }
    const double reciprocity_ratio = canada_given > 1e-9 ? us_given / canada_given : (us_given > 0.0 ? 999.0 : 1.0);
    out << "],\"concessionBalance\":{\"canadaGiven\":" << canada_given << ",\"usGiven\":" << us_given
        << ",\"usToCanadaRatio\":" << reciprocity_ratio << "},\"playbooks\":[";
    for (std::size_t i = 0; i < playbooks_.size(); ++i) {
      if (i) out << ',';
      const auto& p = playbooks_[i];
      out << "{\"revision\":" << p.revision << ",\"issueId\":\"" << esc(p.issue_id)
          << "\",\"trigger\":\"" << esc(p.trigger) << "\",\"response\":\"" << esc(p.response)
          << "\",\"authority\":\"" << esc(p.authority) << "\"}";
    }
    out << "],\"debriefs\":[";
    for (std::size_t i = 0; i < debriefs_.size(); ++i) {
      if (i) out << ',';
      const auto& d = debriefs_[i];
      out << "{\"revision\":" << d.revision << ",\"round\":" << d.round
          << ",\"summary\":\"" << esc(d.summary) << "\",\"counterpartSignals\":\"" << esc(d.counterpart_signals)
          << "\",\"unresolved\":\"" << esc(d.unresolved) << "\",\"nextActions\":\"" << esc(d.next_actions) << "\"}";
    }
    const auto suggestions = counteroffers(negotiation, robustness);
    out << "],\"counteroffers\":[";
    for (std::size_t i = 0; i < suggestions.size(); ++i) {
      if (i) out << ',';
      const auto& s = suggestions[i];
      out << "{\"category\":\"" << esc(s.category) << "\",\"packageId\":\"" << esc(s.package_id)
          << "\",\"jointClearProbability\":" << s.joint_clear_probability
          << ",\"canadaCvar10Surplus\":" << s.canada_cvar10_surplus
          << ",\"usCvar10Surplus\":" << s.us_cvar10_surplus << ",\"maxRegret\":" << s.max_regret
          << ",\"authorityStatus\":\"" << esc(s.authority_status)
          << "\",\"rationale\":\"" << esc(s.rationale) << "\"}";
    }
    out << "],\"automaticPlaybook\":[";
    bool comma = false;
    if (robustness && !robustness->recommended_package_id.empty()) {
      const auto* m = room_detail::find_metrics(robustness, robustness->recommended_package_id);
      if (m && m->joint_clear_probability < robustness->required_joint_clear_probability) {
        out << "{\"trigger\":\"Robust package misses the joint-clearance gate\",\"response\":\"Use conditional sequencing, verification and reciprocal tranches rather than presenting the package as settled.\"}";
        comma = true;
      }
    }
    if (canada_given > us_given * 1.20 && canada_given > 1e-9) {
      if (comma) out << ',';
      out << "{\"trigger\":\"Canada concession ledger is more than 20% ahead\",\"response\":\"Do not add an unconditional Canadian concession before obtaining measurable reciprocity.\"}";
    }
    out << "],\"warning\":\"Room state is a local research workflow. Do not store classified, protected, privileged or negotiation-sensitive records without an accredited security environment.\"}";
    return out.str();
  }

 private:
  void trim_history() {
    auto trim = [](auto& values) {
      constexpr std::size_t limit = 500;
      if (values.size() > limit) values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(limit));
    };
    trim(offers_); trim(concessions_); trim(playbooks_); trim(debriefs_);
  }

  void load() {
    for (const auto& line : durable_journal::read_lines(event_log_path_)) {
      if (!apply_event(line, nullptr, nullptr, false)) ++recovery_warning_count_;
    }
    last_error_.clear();
    last_apply_replayed_ = false;
  }

  std::string event_log_path_;
  unsigned long revision_ = 0;
  int round_ = 1;
  std::string phase_ = "preparation";
  std::map<std::string, MandateRule> mandate_;
  std::vector<OfferRecord> offers_;
  std::vector<ConcessionRecord> concessions_;
  std::vector<PlaybookRecord> playbooks_;
  std::vector<DebriefRecord> debriefs_;
  std::map<std::string, std::string> operation_fingerprints_;
  std::size_t recovery_warning_count_ = 0;
  std::string last_error_;
  bool last_apply_replayed_ = false;
};

}  // namespace cad
