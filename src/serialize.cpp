#include "placement_observatory/serialize.hpp"
#include <cstring>

namespace placement_observatory::serde {

using json::JsonArray;
using json::JsonObject;
using json::JsonValue;

// ---------- binary: Value ----------
void write_value(BinWriter& w, const Value& v) {
  switch (v.kind()) {
    case ValueKind::None: w.u8(0); break;
    case ValueKind::Int: w.u8(1); w.i64(v.as_int()); break;
    case ValueKind::UInt: w.u8(2); w.u64(v.as_uint()); break;
    case ValueKind::Double: w.u8(3); w.f64(v.as_double()); break;
    case ValueKind::Bool: w.u8(4); w.boolean(v.as_bool()); break;
    case ValueKind::String: w.u8(5); w.str(v.as_string()); break;
  }
}
Value read_value(BinReader& r) {
  const std::uint8_t k = r.u8();
  switch (k) {
    case 0: return Value();
    case 1: return Value(r.i64());
    case 2: return Value(r.u64());
    case 3: return Value(r.f64());
    case 4: return Value(r.boolean());
    case 5: return Value(r.str());
    default: throw SerializationError(SerializationError::Kind::Corrupt, "bad value kind");
  }
}

// ---------- binary: Provenance ----------
void write_provenance(BinWriter& w, const Provenance& p) {
  w.u64(p.source_id.value()); w.u64(p.source_generation);
  w.u8(static_cast<std::uint8_t>(p.source_type));
  w.u8(static_cast<std::uint8_t>(p.method));
  w.u8(static_cast<std::uint8_t>(p.reliability));
  w.u8(static_cast<std::uint8_t>(p.classification));
  w.i64(p.timestamp.wall); w.u64(p.timestamp.mono); w.i64(p.timestamp.uncertainty_ns);
  w.u64(p.worker_boot); w.u64(p.coordinator_epoch);
  w.str(p.collection_error); w.str(p.normalized_field); w.str(p.raw_field);
}
Provenance read_provenance(BinReader& r) {
  Provenance p;
  p.source_id = SourceId(r.u64()); p.source_generation = r.u64();
  p.source_type = static_cast<SourceType>(r.u8());
  p.method = static_cast<CollectionMethod>(r.u8());
  p.reliability = static_cast<ReliabilityClass>(r.u8());
  p.classification = static_cast<Classification>(r.u8());
  p.timestamp.wall = r.i64(); p.timestamp.mono = r.u64(); p.timestamp.uncertainty_ns = r.i64();
  p.worker_boot = r.u64(); p.coordinator_epoch = r.u64();
  p.collection_error = r.str(); p.normalized_field = r.str(); p.raw_field = r.str();
  return p;
}

void write_confidence(BinWriter& w, const Confidence& c) {
  w.u8(static_cast<std::uint8_t>(c.cls)); w.f64(c.numerator); w.f64(c.denominator); w.str(c.derivation);
}
Confidence read_confidence(BinReader& r) {
  Confidence c; c.cls = static_cast<ConfidenceClass>(r.u8()); c.numerator = r.f64(); c.denominator = r.f64(); c.derivation = r.str();
  return c;
}

void write_measurement(BinWriter& w, const Measurement& m) {
  w.str(m.normalized_field); w.str(m.raw_field); write_value(w, m.value);
  w.u8(static_cast<std::uint8_t>(m.classification)); write_provenance(w, m.provenance);
}
Measurement read_measurement(BinReader& r) {
  Measurement m; m.normalized_field = r.str(); m.raw_field = r.str(); m.value = read_value(r);
  m.classification = static_cast<Classification>(r.u8()); m.provenance = read_provenance(r);
  return m;
}

// ---------- binary: descriptors ----------
void write_source_descriptor(BinWriter& w, const SourceDescriptor& s) {
  w.u64(s.source_id.value()); w.u64(s.generation); w.u8(static_cast<std::uint8_t>(s.type));
  w.str(s.name); w.u8(static_cast<std::uint8_t>(s.reliability)); w.str(s.endpoint);
  w.i64(s.last_seen.wall); w.u64(s.last_seen.mono); w.boolean(s.synthetic);
}
SourceDescriptor read_source_descriptor(BinReader& r) {
  SourceDescriptor s; s.source_id = SourceId(r.u64()); s.generation = r.u64(); s.type = static_cast<SourceType>(r.u8());
  s.name = r.str(); s.reliability = static_cast<ReliabilityClass>(r.u8()); s.endpoint = r.str();
  s.last_seen.wall = r.i64(); s.last_seen.mono = r.u64(); s.synthetic = r.boolean();
  return s;
}
void write_device_descriptor(BinWriter& w, const DeviceDescriptor& d) {
  w.u64(d.device_id.value()); w.u64(d.node_id.value()); w.u8(static_cast<std::uint8_t>(d.kind));
  w.str(d.vendor); w.str(d.model); w.str(d.architecture);
  w.u8(static_cast<std::uint8_t>(d.compute_capability_major)); w.u8(static_cast<std::uint8_t>(d.compute_capability_minor));
  w.u64(d.memory_bytes); w.u64(d.free_memory_bytes); w.u64(d.used_memory_bytes);
  w.i64(d.sm_count); w.f64(d.clock_mhz); w.u8(static_cast<std::uint8_t>(d.health)); w.boolean(d.synthetic);
}
DeviceDescriptor read_device_descriptor(BinReader& r) {
  DeviceDescriptor d; d.device_id = DeviceId(r.u64()); d.node_id = NodeId(r.u64()); d.kind = static_cast<DeviceKind>(r.u8());
  d.vendor = r.str(); d.model = r.str(); d.architecture = r.str();
  d.compute_capability_major = r.u8(); d.compute_capability_minor = r.u8();
  d.memory_bytes = r.u64(); d.free_memory_bytes = r.u64(); d.used_memory_bytes = r.u64();
  d.sm_count = static_cast<int>(r.i64()); d.clock_mhz = r.f64(); d.health = static_cast<HealthState>(r.u8()); d.synthetic = r.boolean();
  return d;
}
void write_node_descriptor(BinWriter& w, const NodeDescriptor& n) {
  w.u64(n.node_id.value()); w.str(n.hostname); w.i64(n.cpu_count); w.u64(n.memory_bytes);
  w.str(n.numa_architecture); write_vec(w, n.devices, [](BinWriter& ww, DeviceId id) { ww.u64(id.value()); });
  w.u8(static_cast<std::uint8_t>(n.health)); w.str(n.region); w.boolean(n.synthetic);
}
NodeDescriptor read_node_descriptor(BinReader& r) {
  NodeDescriptor n; n.node_id = NodeId(r.u64()); n.hostname = r.str(); n.cpu_count = static_cast<int>(r.i64()); n.memory_bytes = r.u64();
  n.numa_architecture = r.str(); n.devices = read_vec<DeviceId>(r, [](BinReader& rr) { return DeviceId(rr.u64()); });
  n.health = static_cast<HealthState>(r.u8()); n.region = r.str(); n.synthetic = r.boolean();
  return n;
}
void write_workload_descriptor(BinWriter& w, const WorkloadDescriptor& x) {
  w.u64(x.workload_id.value()); w.u64(x.request_id.value()); w.u64(x.tenant_id.value()); w.u64(x.namespace_id.value());
  w.str(x.name); w.u8(static_cast<std::uint8_t>(x.required_kind)); w.str(x.required_architecture); w.u64(x.memory_bytes);
  w.f64(x.predicted_service_seconds); w.u8(static_cast<std::uint8_t>(x.priority)); w.u8(static_cast<std::uint8_t>(x.slo));
  write_vec(w, x.capabilities, [](BinWriter& ww, const std::string& s) { ww.str(s); });
  w.boolean(x.synthetic);
}
WorkloadDescriptor read_workload_descriptor(BinReader& r) {
  WorkloadDescriptor x; x.workload_id = WorkloadId(r.u64()); x.request_id = RequestId(r.u64()); x.tenant_id = TenantId(r.u64()); x.namespace_id = NamespaceId(r.u64());
  x.name = r.str(); x.required_kind = static_cast<DeviceKind>(r.u8()); x.required_architecture = r.str(); x.memory_bytes = r.u64();
  x.predicted_service_seconds = r.f64(); x.priority = static_cast<PriorityClass>(r.u8()); x.slo = static_cast<SloClass>(r.u8());
  x.capabilities = read_vec<std::string>(r, [](BinReader& rr) { return rr.str(); });
  x.synthetic = r.boolean();
  return x;
}
void write_queue_descriptor(BinWriter& w, const QueueDescriptor& q) {
  w.str(q.queue_name); w.u8(static_cast<std::uint8_t>(q.state)); w.u32(q.depth);
  w.u64(q.oldest_age_ns); w.u64(q.head_wait_ns); w.u64(q.expected_wait_ns); w.u32(q.capacity);
}
QueueDescriptor read_queue_descriptor(BinReader& r) {
  QueueDescriptor q; q.queue_name = r.str(); q.state = static_cast<QueueState>(r.u8()); q.depth = r.u32();
  q.oldest_age_ns = r.u64(); q.head_wait_ns = r.u64(); q.expected_wait_ns = r.u64(); q.capacity = r.u32();
  return q;
}
void write_memory_descriptor(BinWriter& w, const MemoryDescriptor& m) {
  w.u64(m.total_bytes); w.u64(m.free_bytes); w.u64(m.used_bytes); w.u8(static_cast<std::uint8_t>(m.pressure)); w.f64(m.pressure_ratio);
}
MemoryDescriptor read_memory_descriptor(BinReader& r) {
  MemoryDescriptor m; m.total_bytes = r.u64(); m.free_bytes = r.u64(); m.used_bytes = r.u64(); m.pressure = static_cast<MemoryPressureLevel>(r.u8()); m.pressure_ratio = r.f64();
  return m;
}
void write_topology_descriptor(BinWriter& w, const TopologyDescriptor& t) {
  w.u8(static_cast<std::uint8_t>(t.type)); w.u64(t.from_node.value()); w.u64(t.to_node.value()); w.u64(t.from_device.value()); w.u64(t.to_device.value());
  w.u64(t.distance_units); w.str(t.interconnect_path); w.f64(t.transfer_cost);
}
TopologyDescriptor read_topology_descriptor(BinReader& r) {
  TopologyDescriptor t; t.type = static_cast<TopologyType>(r.u8()); t.from_node = NodeId(r.u64()); t.to_node = NodeId(r.u64()); t.from_device = DeviceId(r.u64()); t.to_device = DeviceId(r.u64());
  t.distance_units = r.u64(); t.interconnect_path = r.str(); t.transfer_cost = r.f64();
  return t;
}
void write_locality_descriptor(BinWriter& w, const LocalityDescriptor& l) {
  w.u8(static_cast<std::uint8_t>(l.type)); w.str(l.state_key); w.u64(l.device_id.value()); w.u64(l.node_id.value());
  w.boolean(l.present); w.f64(l.benefit);
}
LocalityDescriptor read_locality_descriptor(BinReader& r) {
  LocalityDescriptor l; l.type = static_cast<LocalityType>(r.u8()); l.state_key = r.str(); l.device_id = DeviceId(r.u64()); l.node_id = NodeId(r.u64()); l.present = r.boolean(); l.benefit = r.f64();
  return l;
}
void write_reservation_descriptor(BinWriter& w, const ReservationDescriptor& r0) {
  w.str(r0.reservation_key); w.u64(r0.tenant_id.value()); w.u64(r0.device_id.value()); w.u64(r0.node_id.value()); w.u64(r0.reserved_bytes); w.boolean(r0.active);
}
ReservationDescriptor read_reservation_descriptor(BinReader& r) {
  ReservationDescriptor r0; r0.reservation_key = r.str(); r0.tenant_id = TenantId(r.u64()); r0.device_id = DeviceId(r.u64()); r0.node_id = NodeId(r.u64()); r0.reserved_bytes = r.u64(); r0.active = r.boolean();
  return r0;
}
void write_deadline_descriptor(BinWriter& w, const DeadlineDescriptor& d) {
  w.i64(d.deadline_ns); w.i64(d.submitted_ns); w.u8(static_cast<std::uint8_t>(d.state)); w.f64(d.risk);
}
DeadlineDescriptor read_deadline_descriptor(BinReader& r) {
  DeadlineDescriptor d; d.deadline_ns = r.i64(); d.submitted_ns = r.i64(); d.state = static_cast<DeadlineState>(r.u8()); d.risk = r.f64();
  return d;
}

void write_constraint(BinWriter& w, const PlacementConstraint& c) {
  w.u8(static_cast<std::uint8_t>(c.cls)); w.u8(static_cast<std::uint8_t>(c.kind)); w.str(c.field); write_value(w, c.value);
  w.str(c.requirement_text); w.u8(static_cast<std::uint8_t>(c.classification)); write_provenance(w, c.provenance);
}
PlacementConstraint read_constraint(BinReader& r) {
  PlacementConstraint c; c.cls = static_cast<ConstraintClass>(r.u8()); c.kind = static_cast<ConstraintKind>(r.u8()); c.field = r.str(); c.value = read_value(r);
  c.requirement_text = r.str(); c.classification = static_cast<Classification>(r.u8()); c.provenance = read_provenance(r);
  return c;
}
void write_cost_component(BinWriter& w, const PlacementCostComponent& c) {
  w.u8(static_cast<std::uint8_t>(c.kind)); w.str(c.label); w.f64(c.cost); w.f64(c.policy_weight);
  w.u8(static_cast<std::uint8_t>(c.classification)); write_provenance(w, c.provenance); w.str(c.detail);
}
PlacementCostComponent read_cost_component(BinReader& r) {
  PlacementCostComponent c; c.kind = static_cast<CostComponentKind>(r.u8()); c.label = r.str(); c.cost = r.f64(); c.policy_weight = r.f64();
  c.classification = static_cast<Classification>(r.u8()); c.provenance = read_provenance(r); c.detail = r.str();
  return c;
}
void write_score_component(BinWriter& w, const PlacementScoreComponent& c) {
  w.u8(static_cast<std::uint8_t>(c.kind)); w.str(c.label); w.f64(c.value);
  w.u8(static_cast<std::uint8_t>(c.classification)); write_provenance(w, c.provenance);
}
PlacementScoreComponent read_score_component(BinReader& r) {
  PlacementScoreComponent c; c.kind = static_cast<ScoreComponentKind>(r.u8()); c.label = r.str(); c.value = r.f64();
  c.classification = static_cast<Classification>(r.u8()); c.provenance = read_provenance(r);
  return c;
}
void write_candidate(BinWriter& w, const PlacementCandidate& c) {
  w.u64(c.candidate_id.value()); w.u64(c.node_id.value()); w.u64(c.device_id.value()); w.u64(c.generation);
  w.str(c.architecture);
  write_vec(w, c.capabilities, [](BinWriter& ww, const std::string& s) { ww.str(s); });
  w.u8(static_cast<std::uint8_t>(c.health));
  write_memory_descriptor(w, c.memory); write_queue_descriptor(w, c.queue);
  write_vec(w, c.locality, [](BinWriter& ww, const LocalityDescriptor& l) { write_locality_descriptor(ww, l); });
  write_vec(w, c.costs, [](BinWriter& ww, const PlacementCostComponent& x) { write_cost_component(ww, x); });
  w.f64(c.total_cost);
  w.boolean(c.rejection_reason_text.has_value());
  if (c.rejection_reason_text) w.str(*c.rejection_reason_text);
  w.boolean(c.rejection_reason.has_value());
  if (c.rejection_reason) w.u8(static_cast<std::uint8_t>(*c.rejection_reason));
}
PlacementCandidate read_candidate(BinReader& r) {
  PlacementCandidate c; c.candidate_id = CandidateId(r.u64()); c.node_id = NodeId(r.u64()); c.device_id = DeviceId(r.u64()); c.generation = r.u64();
  c.architecture = r.str();
  c.capabilities = read_vec<std::string>(r, [](BinReader& rr) { return rr.str(); });
  c.health = static_cast<HealthState>(r.u8());
  c.memory = read_memory_descriptor(r); c.queue = read_queue_descriptor(r);
  c.locality = read_vec<LocalityDescriptor>(r, [](BinReader& rr) { return read_locality_descriptor(rr); });
  c.costs = read_vec<PlacementCostComponent>(r, [](BinReader& rr) { return read_cost_component(rr); });
  c.total_cost = r.f64();
  if (r.boolean()) c.rejection_reason_text = r.str();
  if (r.boolean()) c.rejection_reason = static_cast<RejectionReason>(r.u8());
  return c;
}
void write_candidate_set(BinWriter& w, const CandidateSet& s) {
  write_vec(w, s.candidates, [](BinWriter& ww, const PlacementCandidate& c) { write_candidate(ww, c); });
  w.boolean(s.complete); w.boolean(s.reconstructed); w.u8(static_cast<std::uint8_t>(s.classification));
  write_vec(w, s.source_chain, [](BinWriter& ww, const Provenance& p) { write_provenance(ww, p); });
  write_vec(w, s.missing_fields, [](BinWriter& ww, const std::string& s0) { ww.str(s0); });
}
CandidateSet read_candidate_set(BinReader& r) {
  CandidateSet s; s.candidates = read_vec<PlacementCandidate>(r, [](BinReader& rr) { return read_candidate(rr); });
  s.complete = r.boolean(); s.reconstructed = r.boolean(); s.classification = static_cast<Classification>(r.u8());
  s.source_chain = read_vec<Provenance>(r, [](BinReader& rr) { return read_provenance(rr); });
  s.missing_fields = read_vec<std::string>(r, [](BinReader& rr) { return rr.str(); });
  return s;
}
void write_outcome(BinWriter& w, const PlacementOutcome& o) {
  w.u64(o.decision_id.value()); w.u64(o.attempt_id.value()); w.u8(static_cast<std::uint8_t>(o.disposition));
  w.i64(o.start_delay_ns); w.i64(o.duration_ns); w.i64(o.predicted_duration_ns); w.u64(o.memory_used_bytes);
  w.str(o.error); w.u8(static_cast<std::uint8_t>(o.classification)); write_provenance(w, o.provenance);
}
PlacementOutcome read_outcome(BinReader& r) {
  PlacementOutcome o; o.decision_id = PlacementDecisionId(r.u64()); o.attempt_id = PlacementAttemptId(r.u64()); o.disposition = static_cast<OutcomeDisposition>(r.u8());
  o.start_delay_ns = r.i64(); o.duration_ns = r.i64(); o.predicted_duration_ns = r.i64(); o.memory_used_bytes = r.u64();
  o.error = r.str(); o.classification = static_cast<Classification>(r.u8()); o.provenance = read_provenance(r);
  return o;
}
void write_observation(BinWriter& w, const PlacementObservation& o) {
  w.u64(o.observation_id.value()); w.u64(o.observation_generation); w.i64(o.revision);
  w.u64(o.source_id.value()); w.u64(o.source_generation);
  w.u8(static_cast<std::uint8_t>(o.source_type));
  w.i64(o.timestamp.wall); w.u64(o.timestamp.mono); w.i64(o.timestamp.uncertainty_ns);
  w.u64(o.worker_boot); w.u64(o.coordinator_epoch);
  w.u64(o.workload_id.value()); w.u64(o.request_id.value()); w.u64(o.tenant_id.value()); w.u64(o.namespace_id.value());
  w.u64(o.node_id.value()); w.u64(o.device_id.value());
  w.u64(o.decision_id.value()); w.u64(o.attempt_id.value()); w.u64(o.epoch);
  w.u8(static_cast<std::uint8_t>(o.lifecycle));
  write_vec(w, o.fields, [](BinWriter& ww, const Measurement& m) { write_measurement(ww, m); });
  w.u8(static_cast<std::uint8_t>(o.reliability)); w.boolean(o.synthetic);
}
PlacementObservation read_observation(BinReader& r) {
  PlacementObservation o; o.observation_id = PlacementObservationId(r.u64()); o.observation_generation = r.u64(); o.revision = r.i64();
  o.source_id = SourceId(r.u64()); o.source_generation = r.u64();
  o.source_type = static_cast<SourceType>(r.u8());
  o.timestamp.wall = r.i64(); o.timestamp.mono = r.u64(); o.timestamp.uncertainty_ns = r.i64();
  o.worker_boot = r.u64(); o.coordinator_epoch = r.u64();
  o.workload_id = WorkloadId(r.u64()); o.request_id = RequestId(r.u64()); o.tenant_id = TenantId(r.u64()); o.namespace_id = NamespaceId(r.u64());
  o.node_id = NodeId(r.u64()); o.device_id = DeviceId(r.u64());
  o.decision_id = PlacementDecisionId(r.u64()); o.attempt_id = PlacementAttemptId(r.u64()); o.epoch = r.u64();
  o.lifecycle = static_cast<LifecycleState>(r.u8());
  o.fields = read_vec<Measurement>(r, [](BinReader& rr) { return read_measurement(rr); });
  o.reliability = static_cast<ReliabilityClass>(r.u8()); o.synthetic = r.boolean();
  return o;
}
void write_decision(BinWriter& w, const PlacementDecision& d) {
  w.u64(d.decision_id.value()); w.u64(d.attempt_id.value()); w.u64(d.placement_generation); w.u64(d.observation_generation);
  w.u64(d.epoch); w.u64(d.policy_generation);
  w.u64(d.workload_id.value()); w.u64(d.request_id.value()); w.u64(d.tenant_id.value()); w.u64(d.namespace_id.value());
  w.u64(d.node_id.value()); w.u64(d.device_id.value());
  write_candidate_set(w, d.candidate_set);
  w.u64(d.selected_candidate.value());
  write_vec(w, d.rejected_candidates, [](BinWriter& ww, CandidateId id) { ww.u64(id.value()); });
  write_vec(w, d.hard_constraints, [](BinWriter& ww, const PlacementConstraint& c) { write_constraint(ww, c); });
  write_vec(w, d.soft_preferences, [](BinWriter& ww, const PlacementConstraint& c) { write_constraint(ww, c); });
  write_vec(w, d.score_components, [](BinWriter& ww, const PlacementScoreComponent& c) { write_score_component(ww, c); });
  write_vec(w, d.cost_components, [](BinWriter& ww, const PlacementCostComponent& c) { write_cost_component(ww, c); });
  w.u8(static_cast<std::uint8_t>(d.tie_break)); w.str(d.tie_break_reason); w.str(d.selected_reason);
  w.u32(static_cast<std::uint32_t>(d.rejection_reasons.size()));
  for (const auto& [id, txt] : d.rejection_reasons) { w.u64(id.value()); w.str(txt); }
  write_confidence(w, d.confidence);
  w.u8(static_cast<std::uint8_t>(d.classification)); w.u8(static_cast<std::uint8_t>(d.determinism));
  write_provenance(w, d.provenance);
  w.boolean(d.outcome.has_value());
  if (d.outcome) write_outcome(w, *d.outcome);
  w.u8(static_cast<std::uint8_t>(d.lifecycle));
}
PlacementDecision read_decision(BinReader& r) {
  PlacementDecision d; d.decision_id = PlacementDecisionId(r.u64()); d.attempt_id = PlacementAttemptId(r.u64()); d.placement_generation = r.u64(); d.observation_generation = r.u64();
  d.epoch = r.u64(); d.policy_generation = r.u64();
  d.workload_id = WorkloadId(r.u64()); d.request_id = RequestId(r.u64()); d.tenant_id = TenantId(r.u64()); d.namespace_id = NamespaceId(r.u64());
  d.node_id = NodeId(r.u64()); d.device_id = DeviceId(r.u64());
  d.candidate_set = read_candidate_set(r);
  d.selected_candidate = CandidateId(r.u64());
  d.rejected_candidates = read_vec<CandidateId>(r, [](BinReader& rr) { return CandidateId(rr.u64()); });
  d.hard_constraints = read_vec<PlacementConstraint>(r, [](BinReader& rr) { return read_constraint(rr); });
  d.soft_preferences = read_vec<PlacementConstraint>(r, [](BinReader& rr) { return read_constraint(rr); });
  d.score_components = read_vec<PlacementScoreComponent>(r, [](BinReader& rr) { return read_score_component(rr); });
  d.cost_components = read_vec<PlacementCostComponent>(r, [](BinReader& rr) { return read_cost_component(rr); });
  d.tie_break = static_cast<TieBreakReason>(r.u8()); d.tie_break_reason = r.str(); d.selected_reason = r.str();
  const std::uint32_t nr = r.u32();
  for (std::uint32_t i = 0; i < nr; ++i) { const auto id = CandidateId(r.u64()); d.rejection_reasons[id] = r.str(); }
  d.confidence = read_confidence(r);
  d.classification = static_cast<Classification>(r.u8()); d.determinism = static_cast<DeterminismClass>(r.u8());
  d.provenance = read_provenance(r);
  if (r.boolean()) d.outcome = read_outcome(r);
  d.lifecycle = static_cast<LifecycleState>(r.u8());
  return d;
}

// ---------- canonical JSON ----------
static JsonValue obj_or(std::vector<std::pair<std::string, JsonValue>> items) {
  JsonObject o; for (auto& [k, v] : items) o.items.emplace_back(std::move(k), std::move(v));
  return JsonValue(std::move(o));
}
static JsonValue id_json(std::uint64_t v) { return JsonValue(v); }

JsonValue to_json(const Value& v) {
  switch (v.kind()) {
    case ValueKind::None: return JsonValue(nullptr);
    case ValueKind::Int: return JsonValue(v.as_int());
    case ValueKind::UInt: return JsonValue(v.as_uint());
    case ValueKind::Double: return JsonValue(v.as_double());
    case ValueKind::Bool: return JsonValue(v.as_bool());
    case ValueKind::String: return JsonValue(v.as_string());
  }
  return JsonValue(nullptr);
}
JsonValue to_json(const Provenance& p) {
  return obj_or({
    {"source_id", id_json(p.source_id.value())},
    {"source_generation", JsonValue(p.source_generation)},
    {"source_type", JsonValue(std::string(to_string(p.source_type)))},
    {"method", JsonValue(static_cast<int>(p.method))},
    {"reliability", JsonValue(std::string(to_string(p.reliability)))},
    {"classification", JsonValue(std::string(to_string(p.classification)))},
    {"timestamp_wall", JsonValue(p.timestamp.wall)},
    {"timestamp_mono", JsonValue(p.timestamp.mono)},
    {"uncertainty_ns", JsonValue(p.uncertainty_ns)},
    {"worker_boot", JsonValue(p.worker_boot)},
    {"coordinator_epoch", JsonValue(p.coordinator_epoch)},
    {"collection_error", JsonValue(p.collection_error)},
    {"normalized_field", JsonValue(p.normalized_field)},
    {"raw_field", JsonValue(p.raw_field)},
  });
}
JsonValue to_json(const Confidence& c) {
  return obj_or({
    {"class", JsonValue(std::string(to_string(c.cls)))},
    {"numerator", JsonValue(c.numerator)},
    {"denominator", JsonValue(c.denominator)},
    {"numeric", JsonValue(c.numeric())},
    {"derivation", JsonValue(c.derivation)},
  });
}
JsonValue to_json(const Measurement& m) {
  return obj_or({
    {"field", JsonValue(m.normalized_field)},
    {"raw", JsonValue(m.raw_field)},
    {"value", to_json(m.value)},
    {"classification", JsonValue(std::string(to_string(m.classification)))},
    {"provenance", to_json(m.provenance)},
  });
}
JsonValue to_json(const SourceDescriptor& s) {
  return obj_or({
    {"source_id", id_json(s.source_id.value())},
    {"generation", JsonValue(s.generation)},
    {"type", JsonValue(std::string(to_string(s.type)))},
    {"name", JsonValue(s.name)},
    {"reliability", JsonValue(std::string(to_string(s.reliability)))},
    {"endpoint", JsonValue(s.endpoint)},
    {"last_seen_wall", JsonValue(s.last_seen.wall)},
    {"synthetic", JsonValue(s.synthetic)},
  });
}
JsonValue to_json(const DeviceDescriptor& d) {
  return obj_or({
    {"device_id", id_json(d.device_id.value())},
    {"node_id", id_json(d.node_id.value())},
    {"kind", JsonValue(static_cast<int>(d.kind))},
    {"vendor", JsonValue(d.vendor)},
    {"model", JsonValue(d.model)},
    {"architecture", JsonValue(d.architecture)},
    {"compute_capability_major", JsonValue(d.compute_capability_major)},
    {"compute_capability_minor", JsonValue(d.compute_capability_minor)},
    {"memory_bytes", JsonValue(d.memory_bytes)},
    {"free_memory_bytes", JsonValue(d.free_memory_bytes)},
    {"used_memory_bytes", JsonValue(d.used_memory_bytes)},
    {"sm_count", JsonValue(d.sm_count)},
    {"clock_mhz", JsonValue(d.clock_mhz)},
    {"health", JsonValue(std::string(to_string(d.health)))},
    {"synthetic", JsonValue(d.synthetic)},
  });
}
JsonValue to_json(const NodeDescriptor& n) {
  JsonArray devs; for (const auto& dd : n.devices) devs.push_back(JsonValue(dd.value()));
  return obj_or({
    {"node_id", id_json(n.node_id.value())},
    {"hostname", JsonValue(n.hostname)},
    {"cpu_count", JsonValue(n.cpu_count)},
    {"memory_bytes", JsonValue(n.memory_bytes)},
    {"numa_architecture", JsonValue(n.numa_architecture)},
    {"devices", JsonValue(std::move(devs))},
    {"health", JsonValue(std::string(to_string(n.health)))},
    {"region", JsonValue(n.region)},
    {"synthetic", JsonValue(n.synthetic)},
  });
}
JsonValue to_json(const WorkloadDescriptor& w) {
  JsonArray caps; for (const auto& c : w.capabilities) caps.push_back(JsonValue(c));
  return obj_or({
    {"workload_id", id_json(w.workload_id.value())},
    {"request_id", id_json(w.request_id.value())},
    {"tenant_id", id_json(w.tenant_id.value())},
    {"namespace_id", id_json(w.namespace_id.value())},
    {"name", JsonValue(w.name)},
    {"required_kind", JsonValue(static_cast<int>(w.required_kind))},
    {"required_architecture", JsonValue(w.required_architecture)},
    {"memory_bytes", JsonValue(w.memory_bytes)},
    {"predicted_service_seconds", JsonValue(w.predicted_service_seconds)},
    {"priority", JsonValue(static_cast<int>(w.priority))},
    {"slo", JsonValue(static_cast<int>(w.slo))},
    {"capabilities", JsonValue(std::move(caps))},
    {"synthetic", JsonValue(w.synthetic)},
  });
}
JsonValue to_json(const QueueDescriptor& q) {
  return obj_or({
    {"queue_name", JsonValue(q.queue_name)},
    {"state", JsonValue(static_cast<int>(q.state))},
    {"depth", JsonValue(q.depth)},
    {"oldest_age_ns", JsonValue(q.oldest_age_ns)},
    {"head_wait_ns", JsonValue(q.head_wait_ns)},
    {"expected_wait_ns", JsonValue(q.expected_wait_ns)},
    {"capacity", JsonValue(q.capacity)},
  });
}
JsonValue to_json(const MemoryDescriptor& m) {
  return obj_or({
    {"total_bytes", JsonValue(m.total_bytes)},
    {"free_bytes", JsonValue(m.free_bytes)},
    {"used_bytes", JsonValue(m.used_bytes)},
    {"pressure", JsonValue(static_cast<int>(m.pressure))},
    {"pressure_ratio", JsonValue(m.pressure_ratio)},
  });
}
JsonValue to_json(const TopologyDescriptor& t) {
  return obj_or({
    {"type", JsonValue(static_cast<int>(t.type))},
    {"from_node", id_json(t.from_node.value())},
    {"to_node", id_json(t.to_node.value())},
    {"from_device", id_json(t.from_device.value())},
    {"to_device", id_json(t.to_device.value())},
    {"distance_units", JsonValue(t.distance_units)},
    {"interconnect_path", JsonValue(t.interconnect_path)},
    {"transfer_cost", JsonValue(t.transfer_cost)},
  });
}
JsonValue to_json(const LocalityDescriptor& l) {
  return obj_or({
    {"type", JsonValue(static_cast<int>(l.type))},
    {"state_key", JsonValue(l.state_key)},
    {"device_id", id_json(l.device_id.value())},
    {"node_id", id_json(l.node_id.value())},
    {"present", JsonValue(l.present)},
    {"benefit", JsonValue(l.benefit)},
  });
}
JsonValue to_json(const ReservationDescriptor& r0) {
  return obj_or({
    {"reservation_key", JsonValue(r0.reservation_key)},
    {"tenant_id", id_json(r0.tenant_id.value())},
    {"device_id", id_json(r0.device_id.value())},
    {"node_id", id_json(r0.node_id.value())},
    {"reserved_bytes", JsonValue(r0.reserved_bytes)},
    {"active", JsonValue(r0.active)},
  });
}
JsonValue to_json(const DeadlineDescriptor& d) {
  return obj_or({
    {"deadline_ns", JsonValue(d.deadline_ns)},
    {"submitted_ns", JsonValue(d.submitted_ns)},
    {"state", JsonValue(static_cast<int>(d.state))},
    {"risk", JsonValue(d.risk)},
  });
}
JsonValue to_json(const PlacementConstraint& c) {
  return obj_or({
    {"class", JsonValue(static_cast<int>(c.cls))},
    {"kind", JsonValue(static_cast<int>(c.kind))},
    {"field", JsonValue(c.field)},
    {"value", to_json(c.value)},
    {"requirement", JsonValue(c.requirement_text)},
    {"classification", JsonValue(std::string(to_string(c.classification)))},
    {"provenance", to_json(c.provenance)},
  });
}
JsonValue to_json(const PlacementCostComponent& c) {
  return obj_or({
    {"kind", JsonValue(static_cast<int>(c.kind))},
    {"label", JsonValue(c.label)},
    {"cost", JsonValue(c.cost)},
    {"policy_weight", JsonValue(c.policy_weight)},
    {"weighted", JsonValue(c.weighted())},
    {"classification", JsonValue(std::string(to_string(c.classification)))},
    {"provenance", to_json(c.provenance)},
    {"detail", JsonValue(c.detail)},
  });
}
JsonValue to_json(const PlacementScoreComponent& c) {
  return obj_or({
    {"kind", JsonValue(static_cast<int>(c.kind))},
    {"label", JsonValue(c.label)},
    {"value", JsonValue(c.value)},
    {"classification", JsonValue(std::string(to_string(c.classification)))},
    {"provenance", to_json(c.provenance)},
  });
}
JsonValue to_json(const PlacementCandidate& c) {
  JsonArray loc; for (const auto& l : c.locality) loc.push_back(to_json(l));
  JsonArray costs; for (const auto& x : c.costs) costs.push_back(to_json(x));
  JsonArray caps; for (const auto& s : c.capabilities) caps.push_back(JsonValue(s));
  return obj_or({
    {"candidate_id", id_json(c.candidate_id.value())},
    {"node_id", id_json(c.node_id.value())},
    {"device_id", id_json(c.device_id.value())},
    {"generation", JsonValue(c.generation)},
    {"architecture", JsonValue(c.architecture)},
    {"capabilities", JsonValue(std::move(caps))},
    {"health", JsonValue(std::string(to_string(c.health)))},
    {"memory", to_json(c.memory)},
    {"queue", to_json(c.queue)},
    {"locality", JsonValue(std::move(loc))},
    {"costs", JsonValue(std::move(costs))},
    {"total_cost", JsonValue(c.total_cost)},
    {"rejection_reason_text", c.rejection_reason_text ? JsonValue(*c.rejection_reason_text) : JsonValue(nullptr)},
    {"rejection_reason", c.rejection_reason ? JsonValue(static_cast<int>(*c.rejection_reason)) : JsonValue(nullptr)},
  });
}
JsonValue to_json(const CandidateSet& s) {
  JsonArray cands; for (const auto& c : s.candidates) cands.push_back(to_json(c));
  JsonArray chain; for (const auto& p : s.source_chain) chain.push_back(to_json(p));
  JsonArray missing; for (const auto& m : s.missing_fields) missing.push_back(JsonValue(m));
  return obj_or({
    {"candidates", JsonValue(std::move(cands))},
    {"complete", JsonValue(s.complete)},
    {"reconstructed", JsonValue(s.reconstructed)},
    {"classification", JsonValue(std::string(to_string(s.classification)))},
    {"source_chain", JsonValue(std::move(chain))},
    {"missing_fields", JsonValue(std::move(missing))},
  });
}
JsonValue to_json(const PlacementOutcome& o) {
  return obj_or({
    {"decision_id", id_json(o.decision_id.value())},
    {"attempt_id", id_json(o.attempt_id.value())},
    {"disposition", JsonValue(static_cast<int>(o.disposition))},
    {"start_delay_ns", JsonValue(o.start_delay_ns)},
    {"duration_ns", JsonValue(o.duration_ns)},
    {"predicted_duration_ns", JsonValue(o.predicted_duration_ns)},
    {"memory_used_bytes", JsonValue(o.memory_used_bytes)},
    {"error", JsonValue(o.error)},
    {"classification", JsonValue(std::string(to_string(o.classification)))},
    {"provenance", to_json(o.provenance)},
  });
}
JsonValue to_json(const PlacementObservation& o) {
  JsonArray fields; for (const auto& m : o.fields) fields.push_back(to_json(m));
  return obj_or({
    {"observation_id", id_json(o.observation_id.value())},
    {"observation_generation", JsonValue(o.observation_generation)},
    {"revision", JsonValue(o.revision)},
    {"source_id", id_json(o.source_id.value())},
    {"source_generation", JsonValue(o.source_generation)},
    {"source_type", JsonValue(std::string(to_string(o.source_type)))},
    {"timestamp_wall", JsonValue(o.timestamp.wall)},
    {"timestamp_mono", JsonValue(o.timestamp.mono)},
    {"worker_boot", JsonValue(o.worker_boot)},
    {"coordinator_epoch", JsonValue(o.coordinator_epoch)},
    {"workload_id", id_json(o.workload_id.value())},
    {"request_id", id_json(o.request_id.value())},
    {"tenant_id", id_json(o.tenant_id.value())},
    {"namespace_id", id_json(o.namespace_id.value())},
    {"node_id", id_json(o.node_id.value())},
    {"device_id", id_json(o.device_id.value())},
    {"decision_id", id_json(o.decision_id.value())},
    {"attempt_id", id_json(o.attempt_id.value())},
    {"epoch", JsonValue(o.epoch)},
    {"lifecycle", JsonValue(std::string(to_string(o.lifecycle)))},
    {"fields", JsonValue(std::move(fields))},
    {"reliability", JsonValue(std::string(to_string(o.reliability)))},
    {"synthetic", JsonValue(o.synthetic)},
  });
}
JsonValue to_json(const PlacementDecision& d) {
  JsonArray rejected; for (const auto& id : d.rejected_candidates) rejected.push_back(JsonValue(id.value()));
  JsonArray hard; for (const auto& c : d.hard_constraints) hard.push_back(to_json(c));
  JsonArray soft; for (const auto& c : d.soft_preferences) soft.push_back(to_json(c));
  JsonArray scores; for (const auto& c : d.score_components) scores.push_back(to_json(c));
  JsonArray costs; for (const auto& c : d.cost_components) costs.push_back(to_json(c));
  JsonObject rej; for (const auto& [id, txt] : d.rejection_reasons) rej.items.emplace_back(std::to_string(id.value()), JsonValue(txt));
  return obj_or({
    {"decision_id", id_json(d.decision_id.value())},
    {"attempt_id", id_json(d.attempt_id.value())},
    {"placement_generation", JsonValue(d.placement_generation)},
    {"observation_generation", JsonValue(d.observation_generation)},
    {"epoch", JsonValue(d.epoch)},
    {"policy_generation", JsonValue(d.policy_generation)},
    {"workload_id", id_json(d.workload_id.value())},
    {"request_id", id_json(d.request_id.value())},
    {"tenant_id", id_json(d.tenant_id.value())},
    {"namespace_id", id_json(d.namespace_id.value())},
    {"node_id", id_json(d.node_id.value())},
    {"device_id", id_json(d.device_id.value())},
    {"candidate_set", to_json(d.candidate_set)},
    {"selected_candidate", id_json(d.selected_candidate.value())},
    {"rejected_candidates", JsonValue(std::move(rejected))},
    {"hard_constraints", JsonValue(std::move(hard))},
    {"soft_preferences", JsonValue(std::move(soft))},
    {"score_components", JsonValue(std::move(scores))},
    {"cost_components", JsonValue(std::move(costs))},
    {"tie_break", JsonValue(static_cast<int>(d.tie_break))},
    {"tie_break_reason", JsonValue(d.tie_break_reason)},
    {"selected_reason", JsonValue(d.selected_reason)},
    {"rejection_reasons", JsonValue(std::move(rej))},
    {"confidence", to_json(d.confidence)},
    {"classification", JsonValue(std::string(to_string(d.classification)))},
    {"determinism", JsonValue(static_cast<int>(d.determinism))},
    {"provenance", to_json(d.provenance)},
    {"outcome", d.outcome ? to_json(*d.outcome) : JsonValue(nullptr)},
    {"lifecycle", JsonValue(std::string(to_string(d.lifecycle)))},
  });
}

JsonValue to_json(const PlacementExplanation& e) {
  JsonArray lines; for (const auto& l : e.lines) {
    JsonArray f; for (const auto& x : l.evidence_fields) f.push_back(JsonValue(x));
    lines.push_back(obj_or({
      {"question", JsonValue(l.question)}, {"answer", JsonValue(l.answer)},
      {"influence", JsonValue(l.influence)}, {"classification", JsonValue(std::string(to_string(l.classification)))},
      {"evidence_fields", JsonValue(std::move(f))}}));
  }
  JsonArray missing; for (const auto& m : e.missing_evidence) missing.push_back(JsonValue(m));
  return obj_or({
    {"decision_id", id_json(e.decision_id.value())}, {"workload_id", id_json(e.workload_id.value())},
    {"confidence", to_json(e.confidence)}, {"lines", JsonValue(std::move(lines))},
    {"next_alternative", id_json(e.next_alternative.value())},
    {"missing_evidence", JsonValue(std::move(missing))},
    {"determinism", JsonValue(static_cast<int>(e.determinism))}, {"summary", JsonValue(e.summary)},
  });
}
JsonValue to_json(const ReplayResult& r) {
  JsonArray mm; for (const auto& m : r.mismatches)
    mm.push_back(obj_or({{"field", JsonValue(m.field)}, {"expected", JsonValue(m.expected)}, {"actual", JsonValue(m.actual)}}));
  JsonArray miss; for (const auto& m : r.missing_required_evidence) miss.push_back(JsonValue(m));
  return obj_or({
    {"decision_id", id_json(r.decision_id.value())},
    {"replay_digest", JsonValue(r.replay_digest)}, {"decision_digest", JsonValue(r.decision_digest)},
    {"evidence_digest", JsonValue(r.evidence_digest)}, {"reproduced", JsonValue(r.reproduced)},
    {"mismatches", JsonValue(std::move(mm))}, {"missing_required_evidence", JsonValue(std::move(miss))},
    {"selected", id_json(r.selected.value())}, {"classification", JsonValue(std::string(to_string(r.classification)))},
  });
}
JsonValue to_json(const ComparisonResult& c) {
  JsonArray deltas; for (const auto& d : c.deltas)
    deltas.push_back(obj_or({{"field", JsonValue(d.field)}, {"before", JsonValue(d.before)}, {"after", JsonValue(d.after)}, {"changed_outcome", JsonValue(d.changed_outcome)}}));
  JsonArray ch; for (const auto& f : c.changed_outcome_fields) ch.push_back(JsonValue(f));
  return obj_or({{"a", id_json(c.a.value())}, {"b", id_json(c.b.value())}, {"deltas", JsonValue(std::move(deltas))},
                 {"selected_changed", JsonValue(c.selected_changed)}, {"changed_outcome_fields", JsonValue(std::move(ch))}});
}
JsonValue to_json(const CounterfactualResult& c) {
  JsonArray ins; for (const auto& i : c.changed_inputs)
    ins.push_back(obj_or({{"field", JsonValue(i.field)}, {"value", to_json(i.new_value)}, {"relative", JsonValue(i.relative)}}));
  JsonArray rank; for (const auto& id : c.resulting_ranking) rank.push_back(id_json(id.value()));
  return obj_or({{"decision_id", id_json(c.decision_id.value())}, {"changed_inputs", JsonValue(std::move(ins))},
                 {"resulting_ranking", JsonValue(std::move(rank))}, {"resulting_decision", id_json(c.resulting_decision.value())},
                 {"decision_changed", JsonValue(c.decision_changed)}, {"classification", JsonValue(std::string(to_string(c.classification)))},
                 {"note", JsonValue(c.note)}});
}
JsonValue to_json(const PlacementTimeline& t) {
  JsonArray entries; for (const auto& e : t.entries)
    entries.push_back(obj_or({{"observation_id", id_json(e.observation_id.value())}, {"decision_id", id_json(e.decision_id.value())},
        {"timestamp_wall", JsonValue(e.timestamp.wall)}, {"kind", JsonValue(e.kind)}, {"label", JsonValue(e.label)}}));
  return obj_or({{"workload_id", id_json(t.workload_id.value())}, {"entries", JsonValue(std::move(entries))}});
}
JsonValue to_json(const PlacementSnapshot& s) {
  JsonArray decs; for (const auto& d : s.decisions) decs.push_back(to_json(d));
  JsonArray obs; for (const auto& o : s.observations) obs.push_back(to_json(o));
  JsonObject health; for (const auto& [id, rc] : s.source_health) health.items.emplace_back(std::to_string(id.value()), JsonValue(std::string(to_string(rc))));
  return obj_or({{"at_wall", JsonValue(s.at.wall)}, {"decisions", JsonValue(std::move(decs))}, {"observations", JsonValue(std::move(obs))},
                 {"source_health", JsonValue(std::move(health))},
                 {"observation_count", JsonValue(s.observation_count)}, {"decision_count", JsonValue(s.decision_count)},
                 {"outcome_count", JsonValue(s.outcome_count)}});
}

// ---------- record envelope ----------
std::vector<std::uint8_t> encode_record(RecordKind kind, const std::uint8_t* body, std::size_t body_len, std::uint32_t version) {
  std::vector<std::uint8_t> prefix;
  {
    BinWriter w;
    w.u32(kRecordMagic); w.u32(version); w.u8(static_cast<std::uint8_t>(kind)); w.u64(body_len); w.raw(body, body_len);
    prefix = w.bytes();
  }
  Digest d = util::Sha256::digest(prefix.data(), prefix.size());
  BinWriter out; out.raw(prefix.data(), prefix.size()); out.raw(d.data(), d.size());
  return out.bytes();
}

std::vector<std::uint8_t> decode_record(std::span<const std::uint8_t> data, RecordKind& kind_out, std::uint32_t expected_version, std::size_t* consumed) {
  BinReader r(data.data(), data.size());
  const std::uint32_t magic = r.u32();
  if (magic != kRecordMagic) throw SerializationError(SerializationError::Kind::Corrupt, "bad record magic");
  const std::uint32_t ver = r.u32();
  if (ver != expected_version) throw SerializationError(SerializationError::Kind::UnknownVersion, "unknown record version");
  const std::uint8_t k = r.u8();
  if (k < 1 || k > 8) throw SerializationError(SerializationError::Kind::Corrupt, "unknown record kind");
  const std::uint64_t body_len = r.u64();
  constexpr std::uint64_t kMaxBody = 64ull * 1024 * 1024;
  if (body_len > kMaxBody) throw SerializationError(SerializationError::Kind::Corrupt, "record body beyond cap");
  const std::size_t header_len = 17; // magic(4)+version(4)+kind(1)+len(8)
  if (r.remaining() < body_len + 32ull) throw SerializationError(SerializationError::Kind::Truncated, "record truncated");
  const std::size_t total = header_len + static_cast<std::size_t>(body_len) + 32;
  if (consumed) { *consumed = total; }
  else if (data.size() != total) { throw SerializationError(SerializationError::Kind::TrailingData, "trailing data after record"); }
  Digest d = util::Sha256::digest(data.data(), header_len + static_cast<std::size_t>(body_len));
  if (std::memcmp(d.data(), data.data() + header_len + static_cast<std::size_t>(body_len), 32) != 0)
    throw SerializationError(SerializationError::Kind::Corrupt, "record checksum mismatch");
  kind_out = static_cast<RecordKind>(k);
  return std::vector<std::uint8_t>(data.data() + header_len, data.data() + header_len + static_cast<std::size_t>(body_len));
}

} // namespace placement_observatory::serde
