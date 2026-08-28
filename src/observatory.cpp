#include "placement_observatory/observatory.hpp"
#include "placement_observatory/serialize.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace placement_observatory {

namespace {
bool finite_cost(const PlacementDecision& d) {
  auto fin = [](double v){ return std::isfinite(v); };
  for (const auto& c : d.candidate_set.candidates) {
    if (!fin(c.total_cost)) return false;
    for (const auto& cc : c.costs) if (!fin(cc.cost) || !fin(cc.policy_weight)) return false;
  }
  for (const auto& cc : d.cost_components) if (!fin(cc.cost) || !fin(cc.policy_weight)) return false;
  for (const auto& sc : d.score_components) if (!fin(sc.value)) return false;
  return true;
}
bool impossible_memory(const PlacementCandidate& c) {
  if (c.memory.total_bytes > 0 && c.memory.used_bytes > c.memory.total_bytes) return true;
  if (c.memory.total_bytes > 0 && c.memory.free_bytes > c.memory.total_bytes) return true;
  return false;
}
std::vector<std::uint8_t> read_file(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::vector<std::uint8_t> b;
  char buf[65536];
  while (f) { f.read(buf, sizeof(buf)); const auto n = f.gcount(); if (n > 0) b.insert(b.end(), buf, buf + n); }
  return b;
}
void write_file(const std::string& p, const std::vector<std::uint8_t>& b) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
} // namespace

struct Observatory::Impl {
  explicit Impl(ObservatoryConfig c) : cfg(c) {}

  ObservatoryConfig cfg;
  mutable std::shared_mutex mtx;

  std::vector<PlacementObservation> obs;
  std::unordered_map<PlacementObservationId, std::size_t> obs_by_id;
  std::vector<PlacementDecision> dec;
  std::unordered_map<PlacementDecisionId, std::size_t> dec_by_id;
  std::unordered_map<PlacementDecisionId, PlacementOutcome> outcomes;
  std::unordered_map<SourceId, SourceDescriptor> sources;
  std::unordered_map<SourceId, SourceGeneration> src_gen;
  std::unordered_map<SourceId, WorkerBootId> src_boot;
  std::unordered_map<SourceId, ReliabilityClass> src_health;
  std::unordered_map<SourceId, Timestamp> src_last_seen;
  std::uint64_t coordinator = 0;

  std::unordered_map<WorkloadId, std::vector<std::size_t>> idx_obs_wl, idx_dec_wl;
  std::unordered_map<RequestId, std::vector<std::size_t>> idx_obs_req, idx_dec_req;
  std::unordered_map<NodeId, std::vector<std::size_t>> idx_obs_node, idx_dec_node;
  std::unordered_map<DeviceId, std::vector<std::size_t>> idx_obs_dev, idx_dec_dev;
  std::unordered_map<SourceId, std::vector<std::size_t>> idx_obs_src;
  std::unordered_map<SourceGeneration, std::vector<std::size_t>> idx_obs_gen;
  std::unordered_map<DecisionEpoch, std::vector<std::size_t>> idx_obs_epoch, idx_dec_epoch;
  std::unordered_map<NamespaceId, std::vector<std::size_t>> idx_obs_ns, idx_dec_ns;
  std::unordered_map<PolicyGeneration, std::vector<std::size_t>> idx_dec_pol;
  std::unordered_map<CandidateId, std::vector<std::size_t>> idx_dec_sel;
  std::unordered_map<PlacementAttemptId, std::size_t> idx_dec_attempt;
  std::unordered_map<PlacementAttemptId, std::vector<std::size_t>> idx_obs_attempt;

  std::uint64_t rejected = 0;
  std::uint64_t superseded = 0;
  std::uint64_t snapshot_count = 0;
  std::uint64_t events = 0;

  void index_obs(const PlacementObservation& o, std::size_t i) {
    if (o.workload_id.valid()) idx_obs_wl[o.workload_id].push_back(i);
    if (o.request_id.valid()) idx_obs_req[o.request_id].push_back(i);
    if (o.node_id.valid()) idx_obs_node[o.node_id].push_back(i);
    if (o.device_id.valid()) idx_obs_dev[o.device_id].push_back(i);
    if (o.source_id.valid()) idx_obs_src[o.source_id].push_back(i);
    idx_obs_gen[o.source_generation].push_back(i);
    idx_obs_epoch[o.epoch].push_back(i);
    if (o.namespace_id.valid()) idx_obs_ns[o.namespace_id].push_back(i);
    if (o.attempt_id.valid()) idx_obs_attempt[o.attempt_id].push_back(i);
  }
  void index_dec(const PlacementDecision& d, std::size_t i) {
    if (d.workload_id.valid()) idx_dec_wl[d.workload_id].push_back(i);
    if (d.request_id.valid()) idx_dec_req[d.request_id].push_back(i);
    if (d.node_id.valid()) idx_dec_node[d.node_id].push_back(i);
    if (d.device_id.valid()) idx_dec_dev[d.device_id].push_back(i);
    idx_dec_epoch[d.epoch].push_back(i);
    if (d.namespace_id.valid()) idx_dec_ns[d.namespace_id].push_back(i);
    idx_dec_pol[d.policy_generation].push_back(i);
    if (d.selected_candidate.valid()) idx_dec_sel[d.selected_candidate].push_back(i);
    if (d.attempt_id.valid()) idx_dec_attempt[d.attempt_id] = i;
  }

  std::string reject(std::string msg) { ++rejected; return msg; }
};

// ============================ source management ============================
Observatory::Observatory(ObservatoryConfig cfg) : impl_(std::make_unique<Impl>(cfg)) {}
Observatory::~Observatory() = default;
Observatory::Observatory(Observatory&&) noexcept = default;
Observatory& Observatory::operator=(Observatory&&) noexcept = default;

void Observatory::register_source(SourceDescriptor d) {
  std::unique_lock lock(impl_->mtx);
  auto it = impl_->src_gen.find(d.source_id);
  if (it == impl_->src_gen.end()) {
    impl_->src_gen[d.source_id] = d.generation;
    impl_->src_boot[d.source_id] = 0;
  } else if (d.generation > it->second) {
    // A generation increase is a source/worker restart: the previous trusted
    // worker boot is no longer valid, so re-adopt the boot from the first
    // observation of the new generation.
    impl_->src_gen[d.source_id] = d.generation;
    impl_->src_boot[d.source_id] = 0;
  }
  impl_->sources[d.source_id] = std::move(d);
  impl_->src_last_seen[d.source_id] = Clock::now();
}
void Observatory::update_source_health(SourceId id, ReliabilityClass rc, Timestamp now) {
  std::unique_lock lock(impl_->mtx);
  impl_->src_health[id] = rc;
  impl_->src_last_seen[id] = now;
}
std::vector<SourceDescriptor> Observatory::sources() const {
  std::shared_lock lock(impl_->mtx);
  std::vector<SourceDescriptor> v;
  v.reserve(impl_->sources.size());
  for (const auto& [id, d] : impl_->sources) v.push_back(d);
  return v;
}
ReliabilityClass Observatory::source_health(SourceId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->src_health.find(id);
  if (it != impl_->src_health.end()) return it->second;
  return ReliabilityClass::Unknown;
}
std::uint64_t Observatory::current_source_generation(SourceId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->src_gen.find(id);
  return it == impl_->src_gen.end() ? 0 : it->second;
}
void Observatory::set_coordinator_epoch(std::uint64_t epoch) {
  std::unique_lock lock(impl_->mtx);
  impl_->coordinator = epoch;
}
std::uint64_t Observatory::coordinator_epoch() const {
  std::shared_lock lock(impl_->mtx);
  return impl_->coordinator;
}

// ============================ ingest ============================
IngestResult Observatory::ingest(PlacementObservation o) {
  std::unique_lock lock(impl_->mtx);
  const IngestResult ok{true, "", impl_->events};
  auto& I = *impl_;
  if (o.observation_id.nil()) return IngestResult{false, I.reject("invalid observation id"), I.events};
  if (o.source_id.nil()) return IngestResult{false, I.reject("invalid source id"), I.events};
  if (o.source_generation == 0) return IngestResult{false, I.reject("zero source generation"), I.events};
  if (o.observation_generation == 0) return IngestResult{false, I.reject("zero observation generation"), I.events};
  if (I.obs_by_id.count(o.observation_id)) return IngestResult{false, I.reject("duplicate observation id"), I.events};
  // source-generation authority: stale generation is rejected, and a worker boot
  // that does not match the trusted boot for the current generation is rejected.
  auto it = I.src_gen.find(o.source_id);
  const SourceGeneration cur = it == I.src_gen.end() ? 0 : it->second;
  auto itb = I.src_boot.find(o.source_id);
  const bool have_boot = itb != I.src_boot.end() && itb->second != 0;
  if (I.cfg.enforce_stale_source_generation && o.source_generation < cur)
    return IngestResult{false, I.reject("stale source generation"), I.events};
  if (I.cfg.enforce_stale_worker_boot && o.source_generation == cur && have_boot && o.worker_boot != itb->second)
    return IngestResult{false, I.reject("stale worker boot"), I.events};
  if (o.source_generation > cur) {
    I.src_gen[o.source_id] = o.source_generation;
    I.src_boot[o.source_id] = o.worker_boot;
    if (cur > 0) ++I.superseded;
  } else if (!have_boot) {
    I.src_boot[o.source_id] = o.worker_boot;  // adopt the first trusted boot
  }
  if (I.cfg.enforce_stale_coordinator_epoch && I.coordinator != 0 && o.coordinator_epoch != 0 && o.coordinator_epoch < I.coordinator)
    return IngestResult{false, I.reject("stale coordinator epoch"), I.events};
  // validate field values
  for (const auto& m : o.fields) {
    if (m.value.kind() == ValueKind::Double && !std::isfinite(m.value.as_double()))
      return IngestResult{false, I.reject("non-finite evidence value"), I.events};
    if ((m.normalized_field == "memory.free_bytes" || m.normalized_field == "memory.used_bytes" || m.normalized_field == "memory.total_bytes")
        && m.value.kind() == ValueKind::UInt && m.value.as_uint() > 0xFFFFFFFFFFFFFFFFull / 2)
      return IngestResult{false, I.reject("impossible memory value"), I.events};
  }
  const std::size_t idx = I.obs.size();
  o.lifecycle = LifecycleState::Correlated;
  I.obs.push_back(o);
  I.obs_by_id[o.observation_id] = idx;
  I.index_obs(o, idx);
  ++I.events;
  return IngestResult{true, "", I.events};
}

IngestResult Observatory::ingest_decision(PlacementDecision d) {
  std::unique_lock lock(impl_->mtx);
  auto& I = *impl_;
  if (d.decision_id.nil()) return IngestResult{false, I.reject("invalid decision id"), I.events};
  if (I.dec_by_id.count(d.decision_id)) return IngestResult{false, I.reject("duplicate decision id"), I.events};
  if (d.attempt_id.valid()) {
    auto ait = I.idx_dec_attempt.find(d.attempt_id);
    if (ait != I.idx_dec_attempt.end() && I.dec[ait->second].decision_id != d.decision_id)
      return IngestResult{false, I.reject("duplicate placement attempt"), I.events};
  }
  if (d.placement_generation == 0) return IngestResult{false, I.reject("zero placement generation"), I.events};
  if (d.selected_candidate.nil()) return IngestResult{false, I.reject("invalid selected candidate"), I.events};
  if (d.candidate_set.complete) {
    if (d.candidate_set.candidates.empty()) return IngestResult{false, I.reject("complete candidate set empty"), I.events};
    bool found = false;
    for (const auto& c : d.candidate_set.candidates) if (c.candidate_id == d.selected_candidate) { found = true; break; }
    if (!found) return IngestResult{false, I.reject("selected candidate absent from complete candidate set"), I.events};
  }
  if (d.candidate_set.complete && d.candidate_set.reconstructed)
    return IngestResult{false, I.reject("candidate set cannot be both complete and reconstructed"), I.events};
  if (!finite_cost(d)) return IngestResult{false, I.reject("non-finite cost or score component"), I.events};
  for (const auto& c : d.candidate_set.candidates) if (impossible_memory(c))
    return IngestResult{false, I.reject("impossible memory value"), I.events};
  if (I.cfg.enforce_stale_coordinator_epoch && I.coordinator != 0 && d.provenance.coordinator_epoch != 0 && d.provenance.coordinator_epoch < I.coordinator)
    return IngestResult{false, I.reject("stale coordinator epoch"), I.events};
  const std::size_t idx = I.dec.size();
  I.dec.push_back(d);
  I.dec_by_id[d.decision_id] = idx;
  I.index_dec(d, idx);
  ++I.events;
  return IngestResult{true, "", I.events};
}

IngestResult Observatory::ingest_outcome(PlacementOutcome o) {
  std::unique_lock lock(impl_->mtx);
  auto& I = *impl_;
  if (o.decision_id.nil()) return IngestResult{false, I.reject("invalid outcome decision id"), I.events};
  auto it = I.dec_by_id.find(o.decision_id);
  if (it == I.dec_by_id.end()) return IngestResult{false, I.reject("outcome before decision"), I.events};
  if (I.outcomes.count(o.decision_id)) return IngestResult{false, I.reject("duplicate outcome"), I.events};
  const PlacementDecision& d = I.dec[it->second];
  if (o.attempt_id.valid() && d.attempt_id.valid() && o.attempt_id != d.attempt_id)
    return IngestResult{false, I.reject("outcome wrong generation"), I.events};
  if (o.disposition == OutcomeDisposition::Unknown) return IngestResult{false, I.reject("unknown outcome disposition"), I.events};
  I.outcomes[o.decision_id] = o;
  ++I.events;
  return IngestResult{true, "", I.events};
}

// ============================ query ============================
std::optional<PlacementDecision> Observatory::decision(PlacementDecisionId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->dec_by_id.find(id);
  if (it == impl_->dec_by_id.end()) return std::nullopt;
  PlacementDecision d = impl_->dec[it->second];
  auto o = impl_->outcomes.find(id);
  if (o != impl_->outcomes.end()) d.outcome = o->second;
  return d;
}
std::optional<PlacementOutcome> Observatory::outcome(PlacementDecisionId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->outcomes.find(id);
  if (it == impl_->outcomes.end()) return std::nullopt;
  return it->second;
}
bool Observatory::has_observation(PlacementObservationId id) const {
  std::shared_lock lock(impl_->mtx);
  return impl_->obs_by_id.count(id) > 0;
}
CandidateSet Observatory::candidates(PlacementDecisionId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->dec_by_id.find(id);
  if (it == impl_->dec_by_id.end()) return CandidateSet{};
  return impl_->dec[it->second].candidate_set;
}
std::vector<PlacementObservation> Observatory::observations(const QueryFilter& f) const {
  std::shared_lock lock(impl_->mtx);
  std::vector<PlacementObservation> out;
  for (const auto& o : impl_->obs) {
    if (f.workload_id.valid() && o.workload_id != f.workload_id) continue;
    if (f.request_id.valid() && o.request_id != f.request_id) continue;
    if (f.decision_id.valid() && o.decision_id != f.decision_id) continue;
    if (f.attempt_id.valid() && o.attempt_id != f.attempt_id) continue;
    if (f.node_id.valid() && o.node_id != f.node_id) continue;
    if (f.device_id.valid() && o.device_id != f.device_id) continue;
    if (f.source_id.valid() && o.source_id != f.source_id) continue;
    if (f.source_generation != 0 && o.source_generation != f.source_generation) continue;
    if (f.epoch != 0 && o.epoch != f.epoch) continue;
    if (f.namespace_id.valid() && o.namespace_id != f.namespace_id) continue;
    if (f.t_begin != 0 && o.timestamp.wall < f.t_begin) continue;
    if (f.t_end != 0 && o.timestamp.wall > f.t_end) continue;
    out.push_back(o);
  }
  return out;
}
std::vector<PlacementDecision> Observatory::decisions(const QueryFilter& f) const {
  std::shared_lock lock(impl_->mtx);
  std::vector<PlacementDecision> out;
  for (const auto& d : impl_->dec) {
    if (f.workload_id.valid() && d.workload_id != f.workload_id) continue;
    if (f.request_id.valid() && d.request_id != f.request_id) continue;
    if (f.decision_id.valid() && d.decision_id != f.decision_id) continue;
    if (f.attempt_id.valid() && d.attempt_id != f.attempt_id) continue;
    if (f.node_id.valid() && d.node_id != f.node_id) continue;
    if (f.device_id.valid() && d.device_id != f.device_id) continue;
    if (f.policy_generation != 0 && d.policy_generation != f.policy_generation) continue;
    if (f.epoch != 0 && d.epoch != f.epoch) continue;
    if (f.namespace_id.valid() && d.namespace_id != f.namespace_id) continue;
    out.push_back(d);
  }
  return out;
}

// ============================ analytics ============================
PlacementExplanation Observatory::explain(PlacementDecisionId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->dec_by_id.find(id);
  if (it == impl_->dec_by_id.end()) return PlacementExplanation{id, WorkloadId(), Confidence{}};
  const PlacementDecision& d = impl_->dec[it->second];
  RankingResult r = rank_candidates(d);
  return build_explanation(d, r);
}
std::vector<CounterfactualResult> Observatory::counterfactual(PlacementDecisionId id, const std::vector<CounterfactualChange>& changes) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->dec_by_id.find(id);
  if (it == impl_->dec_by_id.end()) return {};
  return { placement_observatory::counterfactual(impl_->dec[it->second], changes) };
}
ComparisonResult Observatory::compare(PlacementDecisionId a, PlacementDecisionId b) const {
  std::shared_lock lock(impl_->mtx);
  auto ia = impl_->dec_by_id.find(a);
  auto ib = impl_->dec_by_id.find(b);
  if (ia == impl_->dec_by_id.end() || ib == impl_->dec_by_id.end()) return ComparisonResult{a, b};
  return compare_decisions(impl_->dec[ia->second], impl_->dec[ib->second]);
}
ReplayResult Observatory::replay(PlacementDecisionId id) const {
  std::shared_lock lock(impl_->mtx);
  auto it = impl_->dec_by_id.find(id);
  if (it == impl_->dec_by_id.end()) {
    ReplayResult r; r.decision_id = id; r.reproduced = false; r.missing_required_evidence.push_back("unknown decision");
    return r;
  }
  return replay_decision(impl_->dec[it->second]);
}
ReplayResult Observatory::replay_decision(const PlacementDecision& d) const { return placement_observatory::replay_decision(d); }
PlacementSnapshot Observatory::snapshot() const {
  std::shared_lock lock(impl_->mtx);
  ++impl_->snapshot_count;
  PlacementSnapshot s;
  s.at = Clock::now();
  s.decisions = impl_->dec;
  s.observations = impl_->obs;
  for (const auto& [id, rc] : impl_->src_health) s.source_health[id] = rc;
  s.observation_count = impl_->obs.size();
  s.decision_count = impl_->dec.size();
  s.outcome_count = impl_->outcomes.size();
  return s;
}
PlacementTimeline Observatory::timeline(WorkloadId workload) const {
  std::shared_lock lock(impl_->mtx);
  PlacementTimeline t; t.workload_id = workload;
  for (const auto& o : impl_->obs) {
    if (o.workload_id != workload) continue;
    t.entries.push_back({o.observation_id, o.decision_id, o.timestamp, "observation", o.source_id.str()});
  }
  for (const auto& d : impl_->dec) {
    if (d.workload_id != workload) continue;
    t.entries.push_back({PlacementObservationId(), d.decision_id, d.provenance.timestamp, "decision", d.selected_candidate.str()});
  }
  std::sort(t.entries.begin(), t.entries.end(), [](const TimelineEntry& a, const TimelineEntry& b) {
    if (a.timestamp.mono != b.timestamp.mono && a.timestamp.mono != 0 && b.timestamp.mono != 0) return a.timestamp.mono < b.timestamp.mono;
    if (a.timestamp.wall != b.timestamp.wall) return a.timestamp.wall < b.timestamp.wall;
    if (a.kind != b.kind) return a.kind < b.kind;
    return a.observation_id.value() < b.observation_id.value();
  });
  return t;
}
Stats Observatory::stats() const {
  std::shared_lock lock(impl_->mtx);
  Stats s;
  s.observation_count = impl_->obs.size();
  s.decision_count = impl_->dec.size();
  s.outcome_count = impl_->outcomes.size();
  s.source_count = impl_->sources.size();
  s.superseded_count = impl_->superseded;
  s.rejected_count = impl_->rejected;
  s.event_count = impl_->events;
  s.snapshot_count = impl_->snapshot_count;
  s.wall_clock_now = Clock::now_wall();
  return s;
}

// ============================ persistence ============================
void Observatory::persist(const std::string& path) const {
  std::vector<std::uint8_t> bytes;
  std::vector<PlacementObservation> obs_c;
  std::vector<PlacementDecision> dec_c;
  std::unordered_map<SourceId, SourceDescriptor> src_c;
  std::unordered_map<PlacementDecisionId, PlacementOutcome> out_c;
  std::uint64_t coord_c = 0;
  {
    std::shared_lock lock(impl_->mtx);
    obs_c = impl_->obs; dec_c = impl_->dec; src_c = impl_->sources; out_c = impl_->outcomes; coord_c = impl_->coordinator;
  }
  auto append = [&](serde::RecordKind kind, const std::vector<std::uint8_t>& body) {
    const auto rec = serde::encode_record(kind, body.data(), body.size());
    bytes.insert(bytes.end(), rec.begin(), rec.end());
  };
  { serde::BinWriter w; w.u64(coord_c); append(serde::RecordKind::Epoch, w.bytes()); }
  for (const auto& [id, d] : src_c) { serde::BinWriter w; serde::write_source_descriptor(w, d); append(serde::RecordKind::SourceDescriptor, w.bytes()); }
  for (const auto& o : obs_c) { serde::BinWriter w; serde::write_observation(w, o); append(serde::RecordKind::Observation, w.bytes()); }
  for (const auto& d : dec_c) { serde::BinWriter w; serde::write_decision(w, d); append(serde::RecordKind::Decision, w.bytes()); }
  for (const auto& [id, o] : out_c) { serde::BinWriter w; serde::write_outcome(w, o); append(serde::RecordKind::Outcome, w.bytes()); }
  // Atomic replacement: write temp then rename.
  const std::string tmp = path + ".tmp";
  write_file(tmp, bytes);
  std::remove(path.c_str());
  std::rename(tmp.c_str(), path.c_str());
}

std::size_t Observatory::recover(const std::string& path) {
  std::unique_lock lock(impl_->mtx);
  auto& I = *impl_;
  const std::vector<std::uint8_t> bytes = read_file(path);
  if (bytes.empty()) return 0;
  std::size_t off = 0;
  std::size_t loaded = 0;
  while (off < bytes.size()) {
    serde::RecordKind kind{};
    std::size_t consumed = 0;
    std::vector<std::uint8_t> body;
    try {
      body = serde::decode_record(std::span<const std::uint8_t>(bytes.data() + off, bytes.size() - off), kind, serde::kRecordVersion, &consumed);
    } catch (const serde::SerializationError&) {
      // corrupt / truncated / unknown version / trailing: stop loading here.
      break;
    }
    off += consumed;
    try {
      serde::BinReader r(body.data(), body.size());
      if (kind == serde::RecordKind::Epoch) { I.coordinator = r.u64(); }
      else if (kind == serde::RecordKind::SourceDescriptor) {
        auto d = serde::read_source_descriptor(r);
        I.sources[d.source_id] = d;
        if (I.src_gen.find(d.source_id) == I.src_gen.end() || d.generation > I.src_gen[d.source_id]) I.src_gen[d.source_id] = d.generation;
      }
      else if (kind == serde::RecordKind::Observation) {
        auto o = serde::read_observation(r);
        if (I.obs_by_id.count(o.observation_id)) continue;
        const std::size_t idx = I.obs.size();
        I.obs.push_back(o); I.obs_by_id[o.observation_id] = idx; I.index_obs(o, idx);
        if (o.source_generation > I.src_gen[o.source_id]) { I.src_gen[o.source_id] = o.source_generation; I.src_boot[o.source_id] = o.worker_boot; }
      }
      else if (kind == serde::RecordKind::Decision) {
        auto d = serde::read_decision(r);
        if (I.dec_by_id.count(d.decision_id)) continue;
        const std::size_t idx = I.dec.size();
        I.dec.push_back(d); I.dec_by_id[d.decision_id] = idx; I.index_dec(d, idx);
      }
      else if (kind == serde::RecordKind::Outcome) {
        auto o = serde::read_outcome(r);
        if (!I.dec_by_id.count(o.decision_id)) continue;
        if (I.outcomes.count(o.decision_id)) continue;
        I.outcomes[o.decision_id] = o;
      }
    } catch (const serde::SerializationError&) { break; }
    ++loaded;
  }
  return loaded;
}

} // namespace placement_observatory