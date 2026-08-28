#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <limits>
#include <atomic>

using namespace placement_observatory;
using namespace placement_observatory::serde;
namespace fs = std::filesystem;

static std::string tempdir() {
  static std::atomic<unsigned> counter{0};
  auto p = fs::temp_directory_path() / ("po_core_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(p);
  return p.string();
}

static PlacementObservation make_obs(PlacementObservationId id, WorkloadId wl, SourceId sid,
                                     SourceGeneration gen, WorkerBootId boot, CoordinatorEpoch epoch) {
  PlacementObservation o;
  o.observation_id = id; o.observation_generation = 1; o.source_id = sid; o.source_generation = gen;
  o.workload_id = wl; o.worker_boot = boot; o.coordinator_epoch = epoch;
  o.timestamp = Clock::now(); o.lifecycle = LifecycleState::Collected;
  o.fields.push_back({"mem.free", "", Value(24ull<<30), Classification::Measured, Provenance{}});
  return o;
}

PO_TEST(authority_stale_generation) {
  Observatory obs; obs.set_coordinator_epoch(100);
  SourceDescriptor sd; sd.source_id = SourceId(7); sd.generation = 1; sd.type = SourceType::Synthetic; sd.name="s";
  obs.register_source(sd);
  // authoritative first observation
  auto a = obs.ingest(make_obs(PlacementObservationId(1), WorkloadId(1), SourceId(7), 1, 11, 100));
  PO_CHECK(a.accepted);
  // duplicate observation id rejected
  auto dup = obs.ingest(make_obs(PlacementObservationId(1), WorkloadId(1), SourceId(7), 1, 11, 100));
  PO_CHECK(!dup.accepted);
  // stale source generation rejected
  auto stale = obs.ingest(make_obs(PlacementObservationId(2), WorkloadId(1), SourceId(7), 0, 11, 100));
  PO_CHECK(!stale.accepted && stale.error.find("source generation") != std::string::npos);
  // stale worker boot (same generation, old boot) rejected
  auto badboot = obs.ingest(make_obs(PlacementObservationId(3), WorkloadId(1), SourceId(7), 1, 999, 100));
  PO_CHECK(!badboot.accepted && badboot.error.find("worker boot") != std::string::npos);
  // coordinator epoch rollover: OLD epoch rejected
  obs.set_coordinator_epoch(101);
  auto oldepoch = obs.ingest(make_obs(PlacementObservationId(4), WorkloadId(1), SourceId(7), 1, 11, 100));
  PO_CHECK(!oldepoch.accepted && oldepoch.error.find("coordinator epoch") != std::string::npos);
  // source restart: NEW generation + NEW boot accepted, old generation marked superseded
  auto rst = obs.ingest(make_obs(PlacementObservationId(5), WorkloadId(1), SourceId(7), 2, 22, 101));
  PO_CHECK(rst.accepted);
  PO_CHECK_EQ(obs.current_source_generation(SourceId(7)), 2u);
  // stale old-generation replay rejected
  auto oldgen = obs.ingest(make_obs(PlacementObservationId(6), WorkloadId(1), SourceId(7), 1, 11, 101));
  PO_CHECK(!oldgen.accepted && oldgen.error.find("source generation") != std::string::npos);
  // current generation with OLD boot rejected (stale worker boot)
  auto oldboot2 = obs.ingest(make_obs(PlacementObservationId(7), WorkloadId(1), SourceId(7), 2, 11, 101));
  PO_CHECK(!oldboot2.accepted && oldboot2.error.find("worker boot") != std::string::npos);
  // fresh current authority accepted and replay deterministic
  auto fresh = obs.ingest(make_obs(PlacementObservationId(8), WorkloadId(1), SourceId(7), 2, 22, 101));
  PO_CHECK(fresh.accepted);
  PO_CHECK_EQ(obs.current_source_generation(SourceId(7)), 2u);
}

PO_TEST(decision_ingest_validation) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(5), WorkloadId(5));
  auto r = obs.ingest_decision(d); PO_CHECK(r.accepted);
  // selected candidate missing from complete candidate set -> reject
  auto bad = po_scenario::memory_decision(PlacementDecisionId(6), WorkloadId(5));
  bad.candidate_set.complete = true; bad.selected_candidate = CandidateId(99); // not in candidate set
  auto r2 = obs.ingest_decision(bad); PO_CHECK(!r2.accepted && r2.error.find("selected candidate absent") != std::string::npos);
  // duplicate decision id -> reject
  auto r3 = obs.ingest_decision(d); PO_CHECK(!r3.accepted && r3.error.find("duplicate decision") != std::string::npos);
  // NaN cost -> reject
  auto nan = po_scenario::memory_decision(PlacementDecisionId(7), WorkloadId(5));
  nan.candidate_set.candidates[0].costs[0].cost = std::numeric_limits<double>::quiet_NaN();
  auto r4 = obs.ingest_decision(nan); PO_CHECK(!r4.accepted);
  // impossible memory -> reject
  auto im = po_scenario::memory_decision(PlacementDecisionId(8), WorkloadId(5));
  im.candidate_set.candidates[0].memory.used_bytes = 40ull<<30; im.candidate_set.candidates[0].memory.total_bytes = 32ull<<30;
  auto r5 = obs.ingest_decision(im); PO_CHECK(!r5.accepted);
}

PO_TEST(explain_and_replay) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(10), WorkloadId(10));
  PO_CHECK(obs.ingest_decision(d).accepted);
  auto ex = obs.explain(PlacementDecisionId(10));
  PO_CHECK_EQ(ex.decision_id, PlacementDecisionId(10));
  PO_CHECK_EQ(ex.next_alternative, CandidateId(1)); // ranked second
  PO_CHECK(!ex.lines.empty());
  PO_CHECK(ex.summary.find("selected") != std::string::npos);
  auto rep = obs.replay(PlacementDecisionId(10));
  PO_CHECK(rep.reproduced);
  PO_CHECK_EQ(rep.selected, CandidateId(2));
  PO_CHECK(!rep.replay_digest.empty());
  // deterministic: same replay digest twice
  auto rep2 = obs.replay(PlacementDecisionId(10));
  PO_CHECK_EQ(rep.replay_digest, rep2.replay_digest);
}

PO_TEST(counterfactual_derived) {
  using namespace placement_observatory;
  Observatory obs;
  // decision where transfer cost is decisive: c1 low transfer, c2 high transfer
  std::vector<PlacementCandidate> cands;
  auto c1 = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5);
  po_scenario::add_cost(c1, CostComponentKind::TransferCost, 0.5, 1.0);
  auto c2 = po_scenario::candidate(CandidateId(2), DeviceId(2), 24ull<<30, 0, "sm_120", 3.0);
  po_scenario::add_cost(c2, CostComponentKind::TransferCost, 3.0, 1.0);
  cands.push_back(c1); cands.push_back(c2);
  auto d = po_scenario::decision(PlacementDecisionId(20), WorkloadId(20), RequestId(20), std::move(cands), CandidateId(1), 1, 0);
  PO_CHECK(obs.ingest_decision(d).accepted);
  // counterfactual: reduce candidate 2 transfer cost to 0 -> ranking flips, derived.
  std::vector<CounterfactualChange> ch;
  ch.push_back({"transfer_cost:2", Value(0.0), false});
  auto cr = obs.counterfactual(PlacementDecisionId(20), ch);
  PO_CHECK_EQ(cr.size(), 1u);
  PO_CHECK(cr[0].classification == Classification::Derived);
  PO_CHECK(cr[0].decision_changed);
  PO_CHECK_EQ(cr[0].resulting_decision, CandidateId(2));
  // Without a change, counterfactual is no-op.
  std::vector<CounterfactualChange> none;
  auto cr2 = obs.counterfactual(PlacementDecisionId(20), none);
  PO_CHECK(!cr2[0].decision_changed);
  PO_CHECK(cr2[0].classification == Classification::Derived);
}

PO_TEST(compare_two_decisions) {
  Observatory obs;
  auto d1 = po_scenario::memory_decision(PlacementDecisionId(30), WorkloadId(30));
  auto d2 = po_scenario::memory_decision(PlacementDecisionId(31), WorkloadId(30));
  d2.candidate_set.candidates[1].memory.free_bytes = 8ull<<30;  // change memory
  d2.policy_generation = 2;
  d2.selected_candidate = CandidateId(1);
  obs.ingest_decision(d1); obs.ingest_decision(d2);
  auto cmp = obs.compare(PlacementDecisionId(30), PlacementDecisionId(31));
  PO_CHECK(cmp.selected_changed);
  PO_CHECK(!cmp.deltas.empty());
  bool has_mem = false, has_pol = false;
  for (const auto& dd : cmp.deltas) { if (dd.field.find("memory.free_bytes") != std::string::npos) has_mem = true; if (dd.field=="policy_generation") has_pol=true; }
  PO_CHECK(has_mem); PO_CHECK(has_pol);
}

PO_TEST(persistence_recovery_and_corruption) {
  Observatory obs;
  auto d = po_scenario::memory_decision(PlacementDecisionId(40), WorkloadId(40));
  obs.ingest_decision(d);
  PlacementOutcome o; o.decision_id = PlacementDecisionId(40); o.attempt_id = PlacementAttemptId(40);
  o.disposition = OutcomeDisposition::Succeeded; o.duration_ns = 1234; o.provenance = po_scenario::prov(SourceId(1),1,Classification::Measured);
  obs.ingest_outcome(o);
  const auto dir = tempdir();
  const auto trace = (fs::path(dir) / "trace.bin").string();
  obs.persist(trace);
  Observatory obs2;
  const auto n = obs2.recover(trace);
  PO_CHECK(n >= 2); // decision + outcome
  auto d2 = obs2.decision(PlacementDecisionId(40));
  PO_CHECK(d2.has_value());
  PO_CHECK_EQ(d2->selected_candidate, CandidateId(2));
  auto rep = obs2.replay(PlacementDecisionId(40));
  PO_CHECK(rep.reproduced);
  auto out = obs2.outcome(PlacementDecisionId(40));
  PO_CHECK(out.has_value());
  PO_CHECK_EQ(out->duration_ns, 1234);

  // serde-level corrupt / truncate / version / trailing rejection
  std::vector<std::uint8_t> body; { BinWriter w; write_decision(w, d); body = w.bytes(); }
  auto rec = encode_record(RecordKind::Decision, body.data(), body.size());
  // valid
  { RecordKind k; auto b = decode_record(std::span<const std::uint8_t>(rec.data(), rec.size()), k); PO_CHECK(k==RecordKind::Decision); }
  // corrupt a checksum byte
  { auto rec2 = rec; rec2[rec2.size()-1] ^= 0xFF; RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(rec2.data(), rec2.size()), k); } catch (const SerializationError& e) { threw = (e.kind()==SerializationError::Kind::Corrupt); }
    PO_CHECK(threw); }
  // truncate
  { auto rec2 = rec; rec2.resize(rec2.size()-5); RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(rec2.data(), rec2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::Truncated); }
    PO_CHECK(threw); }
  // unknown version
  { auto rec2 = rec; rec2[5] = 0x09; RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(rec2.data(), rec2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::UnknownVersion); }
    PO_CHECK(threw); }
  // trailing data
  { auto rec2 = rec; rec2.push_back(0xAA); RecordKind k; bool threw=false;
    try { decode_record(std::span<const std::uint8_t>(rec2.data(), rec2.size()), k); } catch (const SerializationError& e) { threw=(e.kind()==SerializationError::Kind::TrailingData); }
    PO_CHECK(threw); }

  // corrupt a mid-record byte in a persisted trace: recover must stop cleanly and keep earlier records.
  { 
    auto bytes = [&](){ std::ifstream f(trace, std::ios::binary); return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }();
    if (bytes.size() > 20) { bytes[20] ^= 0xFF; std::ofstream f(trace, std::ios::binary|std::ios::trunc); f.write((const char*)bytes.data(), bytes.size()); }
    Observatory obs3; obs3.recover(trace); // must not throw
  }
  fs::remove_all(dir);
}

PO_TEST(snapshot_timeline_stats_and_index) {
  Observatory obs;
  for (int i = 0; i < 3; ++i) {
    auto d = po_scenario::memory_decision(PlacementDecisionId(100+i), WorkloadId(50));
    obs.ingest_decision(d);
    obs.ingest(make_obs(PlacementObservationId(200+i), WorkloadId(50), SourceId(3), 1, 9, 0));
  }
  auto s = obs.stats();
  PO_CHECK_EQ(s.decision_count, 3u);
  PO_CHECK_EQ(s.observation_count, 3u);
  auto snap = obs.snapshot();
  PO_CHECK_EQ(snap.decisions.size(), 3u);
  PO_CHECK_EQ(snap.observations.size(), 3u);
  auto t = obs.timeline(WorkloadId(50));
  PO_CHECK(!t.entries.empty());
  auto ds = obs.decisions(QueryFilter{ .workload_id = WorkloadId(50) });
  PO_CHECK_EQ(ds.size(), 3u);
  auto df = obs.decisions(QueryFilter{ .workload_id = WorkloadId(99999) });
  PO_CHECK(df.empty());
  auto cands = obs.candidates(PlacementDecisionId(100));
  PO_CHECK(!cands.candidates.empty());
}

PO_TEST(missing_evidence_and_partial_decision) {
  Observatory obs;
  using namespace placement_observatory;
  std::vector<PlacementCandidate> cands;
  auto c = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.0);
  cands.push_back(c); // no cost evidence -> partial
  PlacementDecision d = po_scenario::decision(PlacementDecisionId(200), WorkloadId(60), RequestId(60), std::move(cands), CandidateId(1), 1, 0);
  d.candidate_set.complete = false; d.candidate_set.reconstructed = true;
  d.candidate_set.classification = Classification::Derived;
  d.candidate_set.missing_fields.push_back("memory.used_bytes");
  PO_CHECK(obs.ingest_decision(d).accepted);
  auto rep = obs.replay(PlacementDecisionId(200));
  // Missing required evidence is reported, not fabricated.
  bool hasMissing = false;
  for (const auto& m : rep.missing_required_evidence) if (m.find("cost") != std::string::npos) hasMissing = true;
  PO_CHECK(hasMissing);
  auto ex = obs.explain(PlacementDecisionId(200));
  PO_CHECK(ex.confidence.cls == ConfidenceClass::Reconstructed || ex.confidence.cls == ConfidenceClass::PartialEvidence);
}

PO_MAIN
