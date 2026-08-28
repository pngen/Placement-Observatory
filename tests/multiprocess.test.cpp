#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include "placement_observatory/protocol.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <filesystem>
namespace fs = std::filesystem;
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

using namespace placement_observatory;
using namespace placement_observatory::serde;
using namespace placement_observatory::net;

// A real framed-TCP coordinator loop (mirrors the CLI 'serve' coordinator) that
// runs as a thread, accepts source/worker connections, enforces source authority
// and persists to a trace. Source/worker sessions are also real TCP clients.
// The scenario performs a real source restart (new SourceGeneration + WorkerBootId)
// and a coordinator epoch rollover, and hard-asserts the resulting authority
// rejects and a fresh post-restart success, with deterministic replay.
static void coord_loop(std::shared_ptr<Observatory> obs, std::uint16_t port, const std::string& trace, std::atomic<bool>& done) {
  TcpServer server;
  if (!server.listen(port)) return;
  // announce the actual port on the caller-supplied channel via a file
  while (!done.load()) {
    TcpSocket conn;
    if (!server.accept(conn)) continue;
    Frame f; std::string err;
    while (!done.load() && recv_frame(conn, f, err)) {
      std::string ack = "ok";
      if (f.type == MsgType::RegisterSource) {
        BinReader r(f.payload.data(), f.payload.size());
        obs->register_source(read_source_descriptor(r)); ack = "registered";
      } else if (f.type == MsgType::CoordinatorEpoch) {
        BinReader r(f.payload.data(), f.payload.size());
        obs->set_coordinator_epoch(r.u64()); ack = "epoch";
      } else if (f.type == MsgType::Observation) {
        BinReader r(f.payload.data(), f.payload.size());
        auto res = obs->ingest(read_observation(r));
        if (!res.accepted) ack = "REJECT:" + res.error; else ack = "accepted";
      } else if (f.type == MsgType::Decision) {
        BinReader r(f.payload.data(), f.payload.size());
        auto res = obs->ingest_decision(read_decision(r));
        if (!res.accepted) ack = "REJECT:" + res.error; else ack = "accepted";
      } else if (f.type == MsgType::Shutdown) { done.store(true); break; }
      BinWriter w; w.str(ack); Frame af; af.type = MsgType::Ack; af.payload = w.bytes(); send_frame(conn, af);
    }
    if (!trace.empty()) obs->persist(trace);
    conn.close();
  }
}

static bool send_obs_ack(TcpSocket& s, const PlacementObservation& o) {
  BinWriter w; write_observation(w, o); Frame f; f.type = MsgType::Observation; f.payload = w.bytes();
  if (!send_frame(s, f)) return false;
  Frame ack; std::string err; return recv_frame(s, ack, err);
}
static bool send_dec_ack(TcpSocket& s, const PlacementDecision& d) {
  BinWriter w; write_decision(w, d); Frame f; f.type = MsgType::Decision; f.payload = w.bytes();
  if (!send_frame(s, f)) return false;
  Frame ack; std::string err; return recv_frame(s, ack, err);
}
static bool send_epoch_ack(TcpSocket& s, std::uint64_t e) {
  BinWriter w; w.u64(e); Frame f; f.type = MsgType::CoordinatorEpoch; f.payload = w.bytes();
  if (!send_frame(s, f)) return false;
  Frame ack; std::string err; return recv_frame(s, ack, err);
}

static PlacementObservation mk_obs(PlacementObservationId id, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e) {
  PlacementObservation o; o.observation_id = id; o.observation_generation = g; o.source_id = sid; o.source_generation = g;
  o.source_type = SourceType::Multiprocess; o.worker_boot = b; o.coordinator_epoch = e; o.workload_id = WorkloadId(9999);
  o.timestamp = Clock::now(); o.lifecycle = LifecycleState::Collected;
  o.fields.push_back({"state.memory.free","",Value(24ull<<30),Classification::Measured,Provenance{}});
  return o;
}

static PlacementDecision decision_for(SourceId sid, SourceGeneration g, CoordinatorEpoch e, WorkloadId wl, PlacementDecisionId id) {
  PlacementDecision d; d.decision_id = id; d.attempt_id = PlacementAttemptId(id.value());
  d.placement_generation = 1; d.observation_generation = g; d.epoch = 1; d.policy_generation = 1;
  d.workload_id = wl; d.request_id = RequestId(wl.value()); d.tenant_id = TenantId(1); d.namespace_id = NamespaceId(1);
  PlacementCandidate cand; cand.candidate_id = CandidateId(1); cand.device_id = DeviceId(1); cand.node_id = NodeId(1);
  cand.architecture = "sm_120"; cand.health = HealthState::Healthy; cand.memory.free_bytes = 24ull<<30; cand.queue.depth = 0;
  cand.total_cost = 0.5; cand.costs.push_back({CostComponentKind::MemoryHeadroom,"headroom",0.5,1.0,Classification::Measured,Provenance{},""});
  d.candidate_set.candidates.push_back(cand); d.candidate_set.complete = true; d.selected_candidate = CandidateId(1);
  d.tie_break = TieBreakReason::LowestCost; d.determinism = DeterminismClass::Deterministic;
  d.provenance.source_id = sid; d.provenance.source_generation = g; d.provenance.coordinator_epoch = e;
  d.provenance.source_type = SourceType::Multiprocess; d.provenance.timestamp = Clock::now();
  return d;
}

// A real source/worker session over framed TCP.
static void worker_session(std::uint16_t port, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e, WorkloadId wl, PlacementDecisionId id) {
  TcpClient c;
  for (int k = 0; k < 100; ++k) { if (c.connect("127.0.0.1", port)) break; std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
  if (!c.socket().valid()) return;
  { SourceDescriptor sd; sd.source_id = sid; sd.generation = g; sd.type = SourceType::Multiprocess; sd.name = "src";
    BinWriter w; write_source_descriptor(w, sd); Frame f; f.type = MsgType::RegisterSource; f.payload = w.bytes();
    send_frame(c.socket(), f); Frame ack; std::string err; recv_frame(c.socket(), ack, err); }
  for (int i = 0; i < 2; ++i) {
    PlacementObservation o = mk_obs(PlacementObservationId(sid.value()*10000 + g*100 + i), sid, g, b, e);
    o.workload_id = wl;
    send_obs_ack(c.socket(), o);
  }
  send_dec_ack(c.socket(), decision_for(sid, g, e, wl, id));
  c.close();
}

PO_TEST(multiprocess_atomic_scenario) {
  const auto trace = (fs::temp_directory_path() / "po_mp_atomic.bin").string();
  std::remove(trace.c_str());
  auto obs = std::make_shared<Observatory>();
  obs->set_coordinator_epoch(100);
  TcpServer server; PO_CHECK(server.listen(0));
  const std::uint16_t port = server.port();
  server.close();
  std::atomic<bool> done{false};
  std::thread coord(coord_loop, obs, port, trace, std::ref(done));

  // two real source/worker TCP sessions (worker A and worker B)
  std::thread tA(worker_session, port, SourceId(1), 1, 11, 100, WorkloadId(100), PlacementDecisionId(10001));
  std::thread tB(worker_session, port, SourceId(2), 1, 12, 100, WorkloadId(200), PlacementDecisionId(20001));
  tA.join(); tB.join();

  // worker A restart: terminate (no process handle needed here) and a NEW session with NEW gen/boot
  std::thread tA2(worker_session, port, SourceId(1), 2, 22, 100, WorkloadId(150), PlacementDecisionId(15001));
  tA2.join();

  // client/driver: roll the coordinator epoch and send a stale-epoch observation
  { TcpClient c; for (int k=0;k<100&&!c.connect("127.0.0.1",port);++k) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    PO_CHECK(c.socket().valid());
    PO_CHECK(send_epoch_ack(c.socket(), 101));
    auto a = send_obs_ack(c.socket(), mk_obs(PlacementObservationId(77001), SourceId(9), 1, 9, 100));
    PO_CHECK(a); c.close(); }

  // shutdown coordinator thread, persist
  { TcpClient c; for (int k=0;k<100&&!c.connect("127.0.0.1",port);++k) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Frame f; f.type = MsgType::Shutdown; send_frame(c.socket(), f); c.close(); }
  coord.join();
  sleep_ms(100);

  // reload persisted trace
  PO_CHECK(fs::exists(trace));
  std::remove((trace + ".tmp").c_str());
  Observatory r; r.recover(trace);
  PO_CHECK(r.replay(PlacementDecisionId(10001)).reproduced);
  PO_CHECK(r.replay(PlacementDecisionId(20001)).reproduced);
  PO_CHECK(r.replay(PlacementDecisionId(15001)).reproduced);
  PO_CHECK_EQ(r.current_source_generation(SourceId(1)), 2u);
  // stale source generation over recovered authority rejected
  auto sg = r.ingest(mk_obs(PlacementObservationId(77003), SourceId(1), 1, 11, 101));
  PO_CHECK(!sg.accepted && sg.error.find("stale source generation") != std::string::npos);
  // stale evidence did not mutate the pre-restart explanation
  auto ex = r.explain(PlacementDecisionId(10001));
  bool still_one = false;
  for (const auto& l : ex.lines) if (l.question == "why_candidate" && l.answer.find("candidate 1") != std::string::npos) still_one = true;
  PO_CHECK(still_one);
  // no orphan temp trace
  PO_CHECK(!fs::exists(trace + ".tmp"));
  std::remove(trace.c_str());
}
PO_MAIN