#include "placement_observatory/compute.hpp"
#include "placement_observatory/serialize.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

namespace placement_observatory {

const char* digest_format() noexcept { return "Placement-Observatory/1 canonical-json SHA-256"; }

namespace {

// Evaluate one hard constraint against a candidate. Returns violation + reason.
struct Eval { bool violation = false; std::string reason; };
Eval eval_constraint(const PlacementCandidate& c, const PlacementConstraint& hc) {
  Eval out;
  const std::string& f = hc.field;
  const bool is_num = hc.value.kind() == ValueKind::Int || hc.value.kind() == ValueKind::UInt || hc.value.kind() == ValueKind::Double;
  if (f == "memory.total_bytes" && is_num) {
    if (c.memory.total_bytes < hc.value.as_uint()) { out.violation = true; out.reason = "insufficient total memory"; }
  } else if (f == "memory.free_bytes" && is_num) {
    if (c.memory.free_bytes < hc.value.as_uint()) { out.violation = true; out.reason = "insufficient free memory headroom"; }
  } else if (f == "queue.depth" && is_num) {
    // Hard cap on queue depth.
    if (c.queue.depth > hc.value.as_uint()) { out.violation = true; out.reason = "queue depth exceeds hard cap"; }
  } else if (f == "device.architecture" && hc.value.kind() == ValueKind::String) {
    if (c.architecture != hc.value.as_string()) { out.violation = true; out.reason = "architecture mismatch"; }
  } else if (f == "capability" && hc.value.kind() == ValueKind::String) {
    const auto& cap = hc.value.as_string();
    if (std::find(c.capabilities.begin(), c.capabilities.end(), cap) == c.capabilities.end()) { out.violation = true; out.reason = "missing capability: " + cap; }
  } else if (f == "device.health" || f == "health") {
    if (c.health != HealthState::Healthy && c.health != HealthState::Degraded) { out.violation = true; out.reason = "device not healthy"; }
  } else {
    // Constraint references a field we cannot evaluate. We must NOT fabricate a
    // violation; record it as not-evaluable at the ranking level.
    out.reason = "not-evaluable:" + f;
  }
  return out;
}

double weighted_total(const PlacementCandidate& c) {
  double s = 0.0;
  for (const auto& cost : c.costs) s += cost.cost * cost.policy_weight;
  return s;
}

std::vector<double> cost_signature(const PlacementCandidate& c) {
  std::vector<double> v;
  for (const auto& cost : c.costs) v.push_back(cost.kind == CostComponentKind::StateLocalityBenefit ? -cost.cost : cost.cost);
  return v;
}

bool cost_less(const PlacementCandidate& a, const PlacementCandidate& b) {
  const double ta = weighted_total(a);
  const double tb = weighted_total(b);
  if (ta != tb) return ta < tb;
  const auto sa = cost_signature(a);
  const auto sb = cost_signature(b);
  if (sa != sb) return sa < sb;
  return a.candidate_id.value() < b.candidate_id.value();
}

bool has_cost_evidence(const PlacementCandidate& c) { return !c.costs.empty(); }

} // namespace

RankingResult rank_candidates(const PlacementDecision& d) {
  RankingResult res;
  std::vector<const PlacementCandidate*> survivors;
  bool has_costs = false;
  for (const auto& c : d.candidate_set.candidates) {
    if (c.costs.size() > 0) has_costs = true;
    std::string reject_reason;
    bool rejected = false;
    for (const auto& hc : d.hard_constraints) {
      Eval e = eval_constraint(c, hc);
      if (e.violation) { reject_reason = e.reason; rejected = true; break; }
      if (e.reason.rfind("not-evaluable:", 0) == 0) {
        // The constraint could not be evaluated; do not fabricate a violation.
        if (reject_reason.empty()) res.missing.push_back("hard-constraint-unverifiable:" + e.reason.substr(14));
      }
    }
    if (rejected) {
      res.rejections[c.candidate_id] = reject_reason.empty() ? "rejected" : reject_reason;
      continue;
    }
    survivors.push_back(&c);
  }
  // Rank survivors; those lacking cost evidence are placed last (flagged).
  std::stable_sort(survivors.begin(), survivors.end(), [&](const PlacementCandidate* a, const PlacementCandidate* b) {
    const bool ea = has_cost_evidence(*a);
    const bool eb = has_cost_evidence(*b);
    if (ea != eb) return ea;   // candidates WITH cost evidence first
    if (!ea) return a->candidate_id.value() < b->candidate_id.value(); // both no-cost: by id
    if (cost_less(*a, *b)) return true;
    if (cost_less(*b, *a)) return false;
    return a->candidate_id.value() < b->candidate_id.value();
  });
  for (const auto* c : survivors) {
    res.ranked.push_back(c->candidate_id);
    res.totals[c->candidate_id] = weighted_total(*c);
    if (!has_cost_evidence(*c)) res.missing.push_back("candidate " + std::to_string(c->candidate_id.value()) + " has no cost evidence");
  }
  if (!survivors.empty()) res.selected = survivors.front()->candidate_id;
  if (survivors.empty()) res.selected = CandidateId();
  // Deterministic note
  res.note = has_costs ? "ranked-by-weighted-cost-with-component-vector-tiebreak" : "no-cost-evidence-ranked-last";
  for (const auto& m : d.candidate_set.missing_fields) res.missing.push_back(m);
  return res;
}

PlacementExplanation build_explanation(const PlacementDecision& d, const RankingResult& r) {
  PlacementExplanation ex;
  ex.decision_id = d.decision_id;
  ex.workload_id = d.workload_id;
  ex.determinism = d.determinism;

  // Confidence: structured, derived from completeness.
  std::uint64_t total_evidence = 0;
  std::uint64_t measured_evidence = 0;
  for (const auto& c : d.candidate_set.candidates) {
    total_evidence += 1 + c.costs.size();
    if (!c.costs.empty()) ++measured_evidence;
  }
  for (const auto& m : d.hard_constraints) if (m.classification == Classification::Measured) ++measured_evidence;
  for (const auto& m : d.soft_preferences) if (m.classification == Classification::Measured) ++measured_evidence;
  total_evidence += d.hard_constraints.size() + d.soft_preferences.size();
  double ratio = total_evidence > 0 ? static_cast<double>(measured_evidence) / static_cast<double>(total_evidence) : 0.0;
  ConfidenceClass cls;
  if (d.candidate_set.complete && ratio >= 0.95 && r.missing.empty()) cls = ConfidenceClass::CompleteMeasured;
  else if (ratio >= 0.6 && !d.candidate_set.reconstructed) cls = ConfidenceClass::StrongMixedEvidence;
  else if (d.candidate_set.reconstructed) cls = ConfidenceClass::Reconstructed;
  else if (ratio > 0.2) cls = ConfidenceClass::PartialEvidence;
  else cls = ConfidenceClass::InsufficientEvidence;
  ex.confidence.cls = cls;
  ex.confidence.numerator = static_cast<double>(measured_evidence);
  ex.confidence.denominator = static_cast<double>(total_evidence);
  ex.confidence.derivation = "measured-evidence/total-evidence over candidate set + constraints";

  // Next alternative.
  if (r.ranked.size() >= 2) ex.next_alternative = r.ranked[1];

  // Question lines.
  auto line = [&](std::string q, std::string a, double infl, Classification cl, std::vector<std::string> f) {
    ex.lines.push_back({std::move(q), std::move(a), infl, cl, std::move(f)});
  };

  // Selected candidate: which device/node.
  const PlacementCandidate* sel = nullptr;
  for (const auto& c : d.candidate_set.candidates) if (c.candidate_id == d.selected_candidate) { sel = &c; break; }
  if (sel) line("why_candidate", "workload " + d.workload_id.str() + " selected candidate " + d.selected_candidate.str() + " on device " +
      sel->device_id.str() + " (" + sel->architecture + ")", 1.0, Classification::Measured, {"selected_candidate", "device"});
  else line("why_candidate", "selected candidate " + d.selected_candidate.str(), 1.0, Classification::Unknown, {"selected_candidate"});

  // Hard constraints cut.
  std::uint64_t ncut = 0;
  for (const auto& [id, reason] : r.rejections) if (reason.find("not-evaluable") == std::string::npos) ++ncut;
  line("hard_constraints", std::to_string(ncut) + " candidate(s) eliminated by hard constraints; " +
      std::to_string(r.rejections.size()) + " total excluded", static_cast<double>(ncut) / (ncut + r.ranked.size() + 1),
      r.rejections.empty() ? Classification::Unknown : Classification::Measured,
      {"hard_constraints", "rejected_candidates"});

  // Decisive soft cost.
  const PlacementCandidate* first_alt = nullptr;
  if (r.ranked.size() >= 2) {
    for (const auto& c : d.candidate_set.candidates) if (c.candidate_id == r.ranked[1]) { first_alt = &c; break; }
  }
  if (sel && first_alt) {
    double sel_cost = weighted_total(*sel);
    double alt_cost = weighted_total(*first_alt);
    line("soft_costs", "selected total weighted cost " + std::to_string(sel_cost) + " vs next " + std::to_string(alt_cost),
        std::fabs(sel_cost - alt_cost) / (std::fabs(sel_cost) + std::fabs(alt_cost) + 1e-9), Classification::Measured, {"cost_components"});
  }

  // Locality / queue / memory / topology decisiveness.
  if (sel) {
    const auto& loc = sel->locality;
    if (!loc.empty()) {
      double best = 0.0;
      for (const auto& l : loc) best = std::max(best, l.benefit);
      line("locality", "candidate " + sel->candidate_id.str() + " carries locality benefit " + std::to_string(best),
           best / (1.0 + best), Classification::Measured, {"locality"});
    }
    line("queue", "candidate " + sel->candidate_id.str() + " queue depth " + std::to_string(sel->queue.depth) +
        " expected_wait_ns " + std::to_string(sel->queue.expected_wait_ns), 0.0, Classification::Measured, {"queue"});
    line("memory", "candidate " + sel->candidate_id.str() + " free_memory_bytes " + std::to_string(sel->memory.free_bytes) +
        " (ratio " + std::to_string(sel->memory.pressure_ratio) + ")", 0.0, Classification::Measured, {"memory"});
  }

  // Deterministic tie-break.
  line("tie_break", "tie-break rule: " + d.tie_break_reason, 0.0, Classification::Derived, {"tie_break"});

  // Missing evidence.
  for (const auto& m : r.missing) ex.missing_evidence.push_back(m);
  for (const auto& m : d.candidate_set.missing_fields) ex.missing_evidence.push_back(m);
  line("missing_evidence", std::to_string(ex.missing_evidence.size()) + " piece(s) of evidence missing", 0.0, Classification::Unknown, {"missing_evidence"});

  // Confidence line.
  line("confidence", "structured confidence " + std::string(to_string(cls)), 0.0, Classification::Derived, {"confidence"});

  std::string summary = "decision " + d.decision_id.str() + " selected " + d.selected_candidate.str();
  if (cls == ConfidenceClass::InsufficientEvidence || cls == ConfidenceClass::PartialEvidence) summary += " with partial/insufficient evidence";
  ex.summary = summary;
  return ex;
}

ReplayResult replay_decision(const PlacementDecision& d) {
  ReplayResult out;
  out.decision_id = d.decision_id;
  out.selected = d.selected_candidate;

  RankingResult r = rank_candidates(d);
  // Evidence digest over the candidate set + constraints (the input).
  auto evidence_hash = [](){
    // Compose canonical JSON of a restricted "replay input" projection.
    return std::string();
  };
  {
    serde::Digest ed = serde::canonical_digest(d.candidate_set);
    out.evidence_digest = serde::digest_hex(ed);
  }
  {
    serde::Digest dd = serde::canonical_digest(d);
    out.decision_digest = serde::digest_hex(dd);
  }
  // Replay digest of the deterministic reproduction input.
  {
    auto rej = serde::to_json(d);
    rej.set("_reproduced_selected", json::JsonValue(r.selected.value()));
    std::string canon = json::to_string(rej, true, -1);
    out.replay_digest = util::hex(util::Sha256::digest(canon));
  }

  out.reproduced = (r.selected == d.selected_candidate);
  if (!out.reproduced) {
    out.mismatches.push_back({"selected_candidate", d.selected_candidate.str(), r.selected.str()});
  }
  for (const auto& m : r.missing) out.missing_required_evidence.push_back(m);
  for (const auto& m : d.candidate_set.missing_fields) out.missing_required_evidence.push_back(m);
  if (d.candidate_set.candidates.empty()) {
    out.reproduced = false;
    out.missing_required_evidence.push_back("empty candidate set");
    if (out.mismatches.empty()) out.mismatches.push_back({"candidate_set", "non-empty", "empty"});
  }
  out.classification = Classification::Derived;
  return out;
}

ComparisonResult compare_decisions(const PlacementDecision& a, const PlacementDecision& b) {
  ComparisonResult out;
  out.a = a.decision_id; out.b = b.decision_id;
  auto add = [&](std::string f, std::string before, std::string after, bool co) {
    out.deltas.push_back({std::move(f), std::move(before), std::move(after), co});
    if (co) out.changed_outcome_fields.push_back(out.deltas.back().field);
  };
  if (a.policy_generation != b.policy_generation) add("policy_generation", std::to_string(a.policy_generation), std::to_string(b.policy_generation), true);
  if (a.selected_candidate != b.selected_candidate) { add("selected_candidate", a.selected_candidate.str(), b.selected_candidate.str(), true); out.selected_changed = true; }
  if (a.candidate_set.candidates.size() != b.candidate_set.candidates.size()) add("candidate_count", std::to_string(a.candidate_set.candidates.size()), std::to_string(b.candidate_set.candidates.size()), true);
  else {
    for (std::size_t i = 0; i < a.candidate_set.candidates.size(); ++i) {
      const auto& ca = a.candidate_set.candidates[i];
      const auto& cb = b.candidate_set.candidates[i];
      if (ca.candidate_id != cb.candidate_id) add("candidate[" + std::to_string(i) + "].id", ca.candidate_id.str(), cb.candidate_id.str(), true);
      if (ca.memory.free_bytes != cb.memory.free_bytes) add("candidate[" + std::to_string(i) + "].memory.free_bytes", std::to_string(ca.memory.free_bytes), std::to_string(cb.memory.free_bytes), true);
      if (ca.memory.total_bytes != cb.memory.total_bytes) add("candidate[" + std::to_string(i) + "].memory.total_bytes", std::to_string(ca.memory.total_bytes), std::to_string(cb.memory.total_bytes), true);
      if (ca.queue.depth != cb.queue.depth) add("candidate[" + std::to_string(i) + "].queue.depth", std::to_string(ca.queue.depth), std::to_string(cb.queue.depth), true);
      if (ca.architecture != cb.architecture) add("candidate[" + std::to_string(i) + "].architecture", ca.architecture, cb.architecture, true);
      if (weighted_total(ca) != weighted_total(cb)) add("candidate[" + std::to_string(i) + "].total_weighted_cost", std::to_string(weighted_total(ca)), std::to_string(weighted_total(cb)), true);
    }
  }
  if (a.hard_constraints.size() != b.hard_constraints.size()) add("hard_constraint_count", std::to_string(a.hard_constraints.size()), std::to_string(b.hard_constraints.size()), true);
  if (a.cost_components.size() != b.cost_components.size()) add("cost_component_count", std::to_string(a.cost_components.size()), std::to_string(b.cost_components.size()), true);
  if (a.tie_break != b.tie_break) add("tie_break", std::to_string(static_cast<int>(a.tie_break)), std::to_string(static_cast<int>(b.tie_break)), true);
  const bool oa = a.outcome.has_value(), ob = b.outcome.has_value();
  if (oa != ob) add("outcome_present", oa ? "true" : "false", ob ? "true" : "false", true);
  else if (oa && ob) {
    if (a.outcome->duration_ns != b.outcome->duration_ns) add("outcome.duration_ns", std::to_string(a.outcome->duration_ns), std::to_string(b.outcome->duration_ns), true);
    if (a.outcome->start_delay_ns != b.outcome->start_delay_ns) add("outcome.start_delay_ns", std::to_string(a.outcome->start_delay_ns), std::to_string(b.outcome->start_delay_ns), true);
  }
  return out;
}

CounterfactualResult counterfactual(const PlacementDecision& base, const std::vector<CounterfactualChange>& changes) {
  CounterfactualResult out;
  out.decision_id = base.decision_id;
  out.changed_inputs = changes;
  out.classification = Classification::Derived;

  // Clone candidate set with changes applied (candidates are copied; counts are
  // "derived" evidence so the semantics are preserved but never historical).
  PlacementDecision d = base;
  for (auto& cand : d.candidate_set.candidates) {
    for (const auto& ch : changes) {
      // field target may be "<field>:<candidate_id>" or a bare field applied to first match.
      std::string target = ch.field;
      std::size_t pos = target.rfind(':');
      CandidateId target_id;
      bool scoped = false;
      if (pos != std::string::npos) {
        std::string tail = target.substr(pos + 1);
        bool all_digits = !tail.empty(); for (char c : tail) if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
        if (all_digits) { target_id = CandidateId(std::stoull(tail)); scoped = true; target = target.substr(0, pos); }
      }
      if (scoped && cand.candidate_id != target_id) continue;
      const double dv = ch.new_value.kind() == ValueKind::Double ? ch.new_value.as_double()
                      : ch.new_value.kind() == ValueKind::Int ? static_cast<double>(ch.new_value.as_int())
                      : ch.new_value.kind() == ValueKind::UInt ? static_cast<double>(ch.new_value.as_uint()) : 0.0;
      if (target == "memory.free_bytes") {
        if (ch.relative) cand.memory.free_bytes = static_cast<std::uint64_t>(std::max(0.0, static_cast<double>(cand.memory.free_bytes) + dv));
        else cand.memory.free_bytes = static_cast<std::uint64_t>(std::max(0.0, dv));
        cand.memory.used_bytes = cand.memory.total_bytes > cand.memory.free_bytes ? cand.memory.total_bytes - cand.memory.free_bytes : 0;
      } else if (target == "queue.depth") {
        if (ch.relative) cand.queue.depth = static_cast<std::uint32_t>(std::max(0, static_cast<int>(cand.queue.depth) + static_cast<int>(dv)));
        else cand.queue.depth = static_cast<std::uint32_t>(std::max(0, static_cast<int>(dv)));
      } else if (target == "memory.total_bytes") {
        cand.memory.total_bytes = static_cast<std::uint64_t>(std::max(0.0, dv));
      } else if (target == "transfer_cost") {
        for (auto& ccost : cand.costs) if (ccost.kind == CostComponentKind::TransferCost) ccost.cost = dv;
      }
    }
  }
  RankingResult r = rank_candidates(d);
  out.resulting_ranking = r.ranked;
  out.resulting_decision = r.selected;
  out.decision_changed = (r.selected != base.selected_candidate);
  out.note = "counterfactual derived ranking";
  return out;
}

} // namespace placement_observatory
