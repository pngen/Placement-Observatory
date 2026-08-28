#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include "placement_observatory/protocol.hpp"
#include "placement_observatory/core/json.hpp"
#include "placement_observatory/compute.hpp"
#include <limits>
#include <atomic>
#include <filesystem>

using namespace placement_observatory;
using namespace placement_observatory::serde;
using namespace placement_observatory::net;

static std::string tdir() {
  static std::atomic<unsigned> c{0};
  auto p = std::filesystem::temp_directory_path() / ("po_adv_" + std::to_string(c.fetch_add(1)));
  std::filesystem::create_directories(p);
  return p.string();
}

PO_TEST(empty_observation) {
  Observatory obs;
  PlacementObservation o; // empty
  auto r = obs.ingest(o);
  PO_CHECK(!r.accepted && r.error == "invalid observation id");
}

PO_TEST(duplicate_observation_id) {
  Observatory obs;
  // build a minimal valid observation
  PlacementObservation a; a.observation_id = PlacementObservationId(5); a.observation_generation = 1; a.source_id = SourceId(1); a.source_generation = 1; a.worker_boot = 1;
  a.workload_id = WorkloadId(5); a.timestamp = Clock::now();
  PO_CHECK(obs.ingest(a).accepted);
  auto dup = obs.ingest(a); PO_CHECK(!dup.accepted && dup.error.find("duplicate") != std::string::npos);
}

PO_TEST(zero_and_rollback_generation) {
  Observatory obs;
  PlacementObservation o; o.observation_id = PlacementObservationId(6); o.observation_generation = 1; o.source_id = SourceId(2); o.source_generation = 0; o.timestamp = Clock::now();
  auto r = obs.ingest(o); PO_CHECK(!r.accepted && r.error.find("source generation") != std::string::npos);
}

PO_TEST(unknown_source_is_accepted_as_fresh) {
  Observatory obs;
  PlacementObservation o; o.observation_id = PlacementObservationId(7); o.observation_generation = 1; o.source_id = SourceId(3); o.source_generation = 1; o.worker_boot = 3; o.timestamp = Clock::now();
  auto r = obs.ingest(o); PO_CHECK(r.accepted);
}

PO_TEST(stale_source_generation_rejected_after_restart) {
  Observatory obs;
  auto make = [](PlacementObservationId id, SourceGeneration g, WorkerBootId b){ PlacementObservation o; o.observation_id=id; o.observation_generation=1; o.source_id=SourceId(4); o.source_generation=g; o.worker_boot=b; o.timestamp=Clock::now(); return o; };
  PO_CHECK(obs.ingest(make(PlacementObservationId(1), 1, 11)).accepted);
  PO_CHECK(obs.ingest(make(PlacementObservationId(2), 2, 22)).accepted); // restart
  auto stale = obs.ingest(make(PlacementObservationId(3), 1, 11)); PO_CHECK(!stale.accepted && stale.error.find("source generation") != std::string::npos);
  auto oldboot = obs.ingest(make(PlacementObservationId(4), 2, 11)); PO_CHECK(!oldboot.accepted && oldboot.error.find("worker boot") != std::string::npos);
}

PO_TEST(stale_coordinator_epoch_rejected) {
  Observatory obs; obs.set_coordinator_epoch(50);
  auto make = [](PlacementObservationId id, CoordinatorEpoch e){ PlacementObservation o; o.observation_id=id; o.observation_generation=1; o.source_id=SourceId(5); o.source_generation=1; o.worker_boot=1; o.coordinator_epoch=e; o.timestamp=Clock::now(); return o; };
  PO_CHECK(obs.ingest(make(PlacementObservationId(1), 50)).accepted);
  obs.set_coordinator_epoch(51);
  auto old = obs.ingest(make(PlacementObservationId(2), 50)); PO_CHECK(!old.accepted && old.error.find("coordinator epoch") != std::string::npos);
}

PO_TEST(selected_absent_from_complete_set) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(9), WorkloadId(9));
  d.selected_candidate = CandidateId(99);
  auto r = obs.ingest_decision(d); PO_CHECK(!r.accepted && r.error.find("selected candidate absent") != std::string::npos);
}

PO_TEST(nan_and_inf_cost_rejected) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(10), WorkloadId(10));
  d.candidate_set.candidates[0].costs[0].cost = std::numeric_limits<double>::quiet_NaN();
  PO_CHECK(!obs.ingest_decision(d).accepted);
  auto d2 = po_scenario::memory_decision(PlacementDecisionId(11), WorkloadId(11));
  d2.candidate_set.candidates[0].costs[0].cost = std::numeric_limits<double>::infinity();
  PO_CHECK(!obs.ingest_decision(d2).accepted);
}

PO_TEST(impossible_memory_rejected) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(12), WorkloadId(12));
  d.candidate_set.candidates[0].memory.used_bytes = 40ull<<30; d.candidate_set.candidates[0].memory.total_bytes = 32ull<<30;
  PO_CHECK(!obs.ingest_decision(d).accepted);
}

PO_TEST(outcome_before_decision_and_duplicate_and_wrong_gen) {
  Observatory obs;
  PlacementOutcome o; o.decision_id = PlacementDecisionId(20); o.attempt_id = PlacementAttemptId(20); o.disposition = OutcomeDisposition::Succeeded;
  PO_CHECK(!obs.ingest_outcome(o).accepted && obs.ingest_outcome(o).error.find("outcome before decision") != std::string::npos);
  auto d = po_scenario::memory_decision(PlacementDecisionId(20), WorkloadId(20));
  o.provenance.timestamp = Clock::now();
  PO_CHECK(obs.ingest_decision(d).accepted);
  PO_CHECK(obs.ingest_outcome(o).accepted);
  PO_CHECK(!obs.ingest_outcome(o).accepted && obs.ingest_outcome(o).error.find("duplicate outcome") != std::string::npos);
  PlacementOutcome wrong = o; wrong.attempt_id = PlacementAttemptId(99);
  // separate decision
  Observatory obs2;
  PO_CHECK(obs2.ingest_decision(d).accepted);
  auto wr = obs2.ingest_outcome(wrong); // attempt mismatch
  PO_CHECK(!wr.accepted && wr.error.find("wrong generation") != std::string::npos);
}

PO_TEST(replay_missing_required_evidence_not_fabricated) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(21), WorkloadId(21));
  d.candidate_set.complete = false; d.candidate_set.reconstructed = true;
  d.candidate_set.missing_fields.push_back("memory.used_bytes");
  PO_CHECK(obs.ingest_decision(d).accepted);
  auto rep = obs.replay(PlacementDecisionId(21));
  PO_CHECK(!rep.missing_required_evidence.empty());
}

PO_TEST(protocol_rejects_malformed_frames) {
  std::vector<std::uint8_t> payload = {1,2,3,4};
  auto frame = encode_frame(MsgType::Observation, payload);
  // valid
  { std::size_t consumed=0; Frame f; std::string err; PO_CHECK(decode_frame(frame.data(), frame.size(), consumed, f, err)); PO_CHECK(f.type==MsgType::Observation); }
  // oversized
  { auto f2 = frame; f2[9]=0xFF; f2[10]=0xFF; f2[11]=0xFF; f2[12]=0xFF; std::size_t c=0; Frame f; std::string err;
    bool ok = decode_frame(f2.data(), f2.size(), c, f, err); PO_CHECK(!ok && err=="oversized frame"); }
  // truncated
  { auto f2 = frame; f2.resize(frame.size()-3); std::size_t c=0; Frame f; std::string err;
    PO_CHECK(!decode_frame(f2.data(), f2.size(), c, f, err)); PO_CHECK(err=="truncated frame"); }
  // unknown version
  { auto f2 = frame; f2[4]=0; f2[5]=0; f2[6]=0; f2[7]=9; std::size_t c=0; Frame f; std::string err;
    PO_CHECK(!decode_frame(f2.data(), f2.size(), c, f, err)); PO_CHECK(err=="unknown protocol version"); }
  // unknown message type
  { auto f2 = frame; f2[8]=200; std::size_t c=0; Frame f; std::string err;
    PO_CHECK(!decode_frame(f2.data(), f2.size(), c, f, err)); PO_CHECK(err=="unknown message type"); }
  // bad magic
  { auto f2 = frame; f2[0]^=0xFF; std::size_t c=0; Frame f; std::string err;
    PO_CHECK(!decode_frame(f2.data(), f2.size(), c, f, err)); PO_CHECK(err=="bad frame magic"); }
  // checksum mismatch
  { auto f2 = frame; f2[f2.size()-1]^=0xFF; std::size_t c=0; Frame f; std::string err;
    PO_CHECK(!decode_frame(f2.data(), f2.size(), c, f, err)); PO_CHECK(err=="frame checksum mismatch"); }
}

PO_TEST(json_rejects_malformed) {
  bool threw = false;
  try { (void)json::parse("[1,2"); } catch (const json::JsonParseError&) { threw = true; }
  PO_CHECK(threw);
  threw = false;
  try { (void)json::parse("{"); } catch (const json::JsonParseError&) { threw = true; }
  PO_CHECK(threw);
}

PO_TEST(corrupt_truncate_version_trailing_binary) {
  auto d = po_scenario::memory_decision(PlacementDecisionId(30), WorkloadId(30));
  std::vector<std::uint8_t> body; { BinWriter w; write_decision(w, d); body = w.bytes(); }
  auto rec = encode_record(RecordKind::Decision, body.data(), body.size());
  { RecordKind k; auto b = decode_record(std::span<const std::uint8_t>(rec.data(), rec.size()), k); PO_CHECK(k == RecordKind::Decision); }
  { auto r2 = rec; r2.back()^=0xFF; RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(r2.data(), r2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::Corrupt); }
    PO_CHECK(threw); }
  { auto r2 = rec; r2.resize(r2.size()-7); RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(r2.data(), r2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::Truncated); }
    PO_CHECK(threw); }
  { auto r2 = rec; r2[5]=0x08; RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(r2.data(), r2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::UnknownVersion); }
    PO_CHECK(threw); }
  { auto r2 = rec; r2.push_back(0x42); RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(r2.data(), r2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::TrailingData); }
    PO_CHECK(threw); }
}

PO_TEST(conflicting_sources_preserved_and_orphan_cleanup) {
  auto dir = tdir();
  Observatory obs;
  auto make = [](PlacementObservationId id, SourceId s, std::uint64_t free){ PlacementObservation o; o.observation_id=id; o.observation_generation=1; o.source_id=s; o.source_generation=1; o.timestamp=Clock::now(); o.workload_id=WorkloadId(50); o.fields.push_back({"device.memory.free_bytes","",Value(free),Classification::Measured,Provenance{}}); return o; };
  obs.ingest(make(PlacementObservationId(1), SourceId(1), 24ull<<30));
  obs.ingest(make(PlacementObservationId(2), SourceId(2), 16ull<<30));
  PO_CHECK_EQ(obs.observations().size(), 2u); // both sources preserved
  // orphan temp trace cleanup: persist writes tmp then renames; no .tmp remains
  const auto trace = (std::filesystem::path(dir) / "t.bin").string();
  obs.persist(trace);
  PO_CHECK(std::filesystem::exists(trace));
  PO_CHECK(!std::filesystem::exists(trace + ".tmp"));
  std::filesystem::remove_all(dir);
}


PO_TEST(duplicate_placement_attempt_rejected) {
  Observatory obs;
  auto d1 = po_scenario::memory_decision(PlacementDecisionId(501), WorkloadId(501));
  d1.attempt_id = PlacementAttemptId(777);
  PO_CHECK(obs.ingest_decision(d1).accepted);
  // a different decision reusing the same placement attempt id is invalid
  auto d2 = po_scenario::memory_decision(PlacementDecisionId(502), WorkloadId(502));
  d2.attempt_id = PlacementAttemptId(777);
  auto r = obs.ingest_decision(d2);
  PO_CHECK(!r.accepted && r.error.find("duplicate placement attempt") != std::string::npos);
}

PO_TEST(contradictory_hard_constraints_handled) {
  using namespace placement_observatory;
  std::vector<PlacementCandidate> cands;
  auto c = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5);
  po_scenario::add_cost(c, CostComponentKind::MemoryHeadroom, 0.5, 1.0);
  cands.push_back(c);
  auto d = po_scenario::decision(PlacementDecisionId(503), WorkloadId(503), RequestId(503), cands, CandidateId(1));
  // two mutually-exclusive hard architecture constraints
  PlacementConstraint a; a.cls = ConstraintClass::Hard; a.kind = ConstraintKind::Architecture; a.field = "device.architecture"; a.value = Value("sm_120"); a.classification = Classification::Measured;
  PlacementConstraint b; b.cls = ConstraintClass::Hard; b.kind = ConstraintKind::Architecture; b.field = "device.architecture"; b.value = Value("x86-64"); b.classification = Classification::Measured;
  d.hard_constraints.push_back(a); d.hard_constraints.push_back(b);
  // The record is valid and accepted; the deterministic ranking must reject the
  // candidate rather than fabricating a selection.
  Observatory obs; PO_CHECK(obs.ingest_decision(d).accepted);
  RankingResult rk = rank_candidates(d);
  PO_CHECK(rk.rejections.count(CandidateId(1)) > 0);
  PO_CHECK(rk.ranked.empty());
}


PO_TEST(invalid_topology_identity_handled) {
  using namespace placement_observatory;
  Observatory obs;
  std::vector<PlacementCandidate> cands;
  auto c = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5);
  po_scenario::add_cost(c, CostComponentKind::MemoryHeadroom, 0.5, 1.0);
  TopologyDescriptor bad; bad.type = TopologyType::Unknown; bad.from_node = NodeId(0); bad.to_node = NodeId(0);
  c.locality.push_back({LocalityType::Unknown, "", DeviceId(0), NodeId(0), false, 0.0});
  cands.push_back(c);
  auto d = po_scenario::decision(PlacementDecisionId(504), WorkloadId(504), RequestId(504), cands, CandidateId(1));
  PO_CHECK(obs.ingest_decision(d).accepted);
  // deterministic: ranking does not crash and candidate survives with no fabricated locality benefit.
  RankingResult rk = rank_candidates(d);
  PO_CHECK(rk.selected == CandidateId(1));
  auto rep = obs.replay(PlacementDecisionId(504));
  PO_CHECK(rep.reproduced);
}

PO_TEST(policy_generation_mismatch_reported) {
  using namespace placement_observatory;
  Observatory obs;
  auto d1 = po_scenario::memory_decision(PlacementDecisionId(505), WorkloadId(505));
  d1.policy_generation = 1;
  auto d2 = po_scenario::memory_decision(PlacementDecisionId(506), WorkloadId(505));
  d2.policy_generation = 9;
  obs.ingest_decision(d1); obs.ingest_decision(d2);
  auto cmp = obs.compare(PlacementDecisionId(505), PlacementDecisionId(506));
  bool has_pol = false;
  for (const auto& dd : cmp.deltas) if (dd.field == "policy_generation") has_pol = true;
  PO_CHECK(has_pol);
}

PO_TEST(stale_queue_memory_topology_evidence_rejected) {
  Observatory obs;
  auto make = [](PlacementObservationId id, SourceGeneration g, WorkerBootId b, const std::string& field){
    PlacementObservation o; o.observation_id = id; o.observation_generation = g; o.source_id = SourceId(6); o.source_generation = g;
    o.source_type = SourceType::Multiprocess; o.worker_boot = b; o.timestamp = Clock::now(); o.workload_id = WorkloadId(600);
    o.lifecycle = LifecycleState::Collected;
    o.fields.push_back({field, "", Value(1), Classification::Measured, Provenance{}});
    return o;
  };
  PO_CHECK(obs.ingest(make(PlacementObservationId(1), 1, 11, "queue.depth")).accepted);
  PO_CHECK(obs.ingest(make(PlacementObservationId(2), 2, 22, "memory.free_bytes")).accepted); // restart
  // stale queue/memory/topology evidence from the superseded generation is rejected
  auto rq = obs.ingest(make(PlacementObservationId(3), 1, 11, "queue.depth"));
  PO_CHECK(!rq.accepted && rq.error.find("stale source generation") != std::string::npos);
  auto rm = obs.ingest(make(PlacementObservationId(4), 1, 11, "memory.free_bytes"));
  PO_CHECK(!rm.accepted && rm.error.find("stale source generation") != std::string::npos);
  auto rt = obs.ingest(make(PlacementObservationId(5), 1, 11, "topology.link"));
  PO_CHECK(!rt.accepted && rt.error.find("stale source generation") != std::string::npos);
}

PO_MAIN