// placement-observatory CLI: po <command> [options]
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/serialize.hpp"
#include "placement_observatory/protocol.hpp"
#include "placement_observatory/providers.hpp"
#include "placement_observatory/core/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <chrono>

using namespace placement_observatory;
using placement_observatory::net::Frame;
using placement_observatory::net::MsgType;

namespace {

void print_help() {
  std::printf(
    "Placement Observatory CLI\n"
    "Usage: po <command> [options]\n\n"
    "Commands:\n"
    "  ingest      Ingest observation/decision/outcome JSON from a file into a trace\n"
    "  observe     Emit a real host/system observation into a trace\n"
    "  decisions   List decisions in a trace (JSON)\n"
    "  candidates  Show a decision's candidate set (JSON)\n"
    "  timeline    Show a workload timeline (JSON)\n"
    "  snapshot    Show a consistent snapshot (JSON)\n"
    "  explain     Explain a decision (JSON)\n"
    "  compare     Compare two decisions (JSON)\n"
    "  replay      Deterministically replay a decision (JSON)\n"
    "  counterfactual  Evaluate a counterfactual (JSON)\n"
    "  sources     List sources (JSON)\n"
    "  health      Source health (JSON)\n"
    "  stats       Observatory statistics (JSON)\n"
    "  inspect     Inspect a record (JSON)\n"
    "  recover     Recover/verify a persisted trace\n"
    "  bench       Run a measured benchmark\n"
    "  serve       Run the multiprocess coordinator/collector\n"
    "  worker      Run a multiprocess source/worker\n"
    "  client      Run a multiprocess client/driver\n"
    "  help        This help\n");
}

std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& def = "") {
  for (std::size_t i = 0; i < a.size(); ++i) if (a[i] == key && i + 1 < a.size()) return a[i + 1];
  return def;
}
bool flag(const std::vector<std::string>& a, const std::string& key) {
  for (const auto& s : a) if (s == key) return true;
  return false;
}
std::uint64_t au64(const std::string& s) {
  if (s.empty()) return 0;
  const char* p = s.c_str(); char* e = nullptr; auto v = std::strtoull(p, &e, 10); (void)e; return v;
}

json::JsonValue jstr(const std::string& s) { return json::JsonValue(s); }
json::JsonValue jnum(std::uint64_t v) { return json::JsonValue(v); }

} // namespace

// ---------------------------------------------------------------------------
// Distributed command implementations
// ---------------------------------------------------------------------------
static int cmd_serve(const std::vector<std::string>& a) {
  const std::uint16_t port = static_cast<std::uint16_t>(au64(arg(a, "--port", "3377")));
  const std::string trace = arg(a, "--trace");
  const std::uint64_t epoch = au64(arg(a, "--epoch", "0"));
  Observatory obs;
  if (!trace.empty()) { obs.recover(trace); }
  obs.set_coordinator_epoch(epoch);
  net::TcpServer server;
  if (!server.listen(port)) { std::printf("ERR: cannot listen\n"); return 1; }
  std::printf("PORT %u\n", static_cast<unsigned>(server.port())); std::fflush(stdout);
  std::printf("EPOCH %llu\n", (unsigned long long)epoch); std::fflush(stdout);
  const std::string portfile = arg(a, "--portfile");
  if (!portfile.empty()) { std::ofstream pf(portfile); pf << static_cast<unsigned>(server.port()); pf.flush(); }
  net::TcpSocket conn;
  while (true) {
    if (!server.accept(conn)) { std::printf("ERR: accept\n"); return 1; }
    net::Frame f;
    std::string err;
    std::vector<net::TcpSocket> workers;
    bool done = false;
    while (!done && net::recv_frame(conn, f, err)) {
      std::vector<std::uint8_t> ack_payload;
      std::string ack = "ok";
      if (f.type == MsgType::RegisterSource) {
        serde::BinReader r(f.payload.data(), f.payload.size());
        SourceDescriptor sd = serde::read_source_descriptor(r);
        obs.register_source(sd);
        ack = "registered " + sd.name;
      } else if (f.type == MsgType::CoordinatorEpoch) {
        serde::BinReader r(f.payload.data(), f.payload.size());
        obs.set_coordinator_epoch(r.u64());
        ack = "epoch";
      } else if (f.type == MsgType::Observation) {
        serde::BinReader r(f.payload.data(), f.payload.size());
        PlacementObservation o = serde::read_observation(r);
        auto res = obs.ingest(o);
        if (!res.accepted) ack = "REJECT:" + res.error; else ack = "accepted";
      } else if (f.type == MsgType::Decision) {
        serde::BinReader r(f.payload.data(), f.payload.size());
        PlacementDecision d = serde::read_decision(r);
        auto res = obs.ingest_decision(d);
        if (!res.accepted) ack = "REJECT:" + res.error; else ack = "accepted";
      } else if (f.type == MsgType::Outcome) {
        serde::BinReader r(f.payload.data(), f.payload.size());
        PlacementOutcome o = serde::read_outcome(r);
        auto res = obs.ingest_outcome(o);
        if (!res.accepted) ack = "REJECT:" + res.error; else ack = "accepted";
      } else if (f.type == MsgType::Shutdown) { done = true; break; }
      else { ack = "REJECT:unhandled"; }
      serde::BinWriter w; w.str(ack);
      net::Frame af; af.type = MsgType::Ack; af.payload = w.bytes();
      net::send_frame(conn, af);
    }
    if (!trace.empty()) obs.persist(trace);
    conn.close();
  }
  return 0;
}

static int cmd_client(const std::vector<std::string>& a) {
  const std::string host = arg(a, "--coord", "127.0.0.1");
  const std::uint16_t port = static_cast<std::uint16_t>(au64(arg(a, "--port", "3377")));
  net::TcpClient client;
  if (!client.connect(host, port)) { std::printf("ERR: connect\n"); return 1; }
  const SourceId sid(au64(arg(a, "--source", "1")));
  const SourceGeneration gen = au64(arg(a, "--gen", "1"));
  const WorkerBootId boot = au64(arg(a, "--boot", "1"));
  const CoordinatorEpoch epoch = au64(arg(a, "--epoch", "0"));
  // register
  { SourceDescriptor sd; sd.source_id = sid; sd.generation = gen; sd.type = SourceType::Multiprocess; sd.name = arg(a,"--name","client");
    serde::BinWriter w; serde::write_source_descriptor(w, sd);
    net::Frame f; f.type = MsgType::RegisterSource; f.payload = w.bytes(); net::send_frame(client.socket(), f);
    net::Frame r; std::string e; net::recv_frame(client.socket(), r, e); }
  // send observations listed by --obs directives
  // Each conn has its own thread; but CLI is one-shot. Send probes then read acks.
  const std::string probes = arg(a, "--probes");
  if (!probes.empty()) {
    // The multiprocess scenario drives authority probes through the library's
    // net client directly; a raw --probes JSON payload is optional.
    (void)json::parse(probes);
  }
  // send one observation with specified authority
  PlacementObservation o; o.observation_id = PlacementObservationId(9001); o.observation_generation = 1;
  o.source_id = sid; o.source_generation = gen; o.source_type = SourceType::Multiprocess;
  o.worker_boot = boot; o.coordinator_epoch = epoch; o.timestamp = Clock::now();
  o.workload_id = WorkloadId(9001); o.lifecycle = LifecycleState::Collected;
  o.fields.push_back({"probe", "", Value(1), Classification::Measured, Provenance{}});
  { serde::BinWriter w; serde::write_observation(w, o);
    net::Frame f; f.type = MsgType::Observation; f.payload = w.bytes(); net::send_frame(client.socket(), f);
    net::Frame r; std::string e;
    if (net::recv_frame(client.socket(), r, e)) {
      serde::BinReader rr(r.payload.data(), r.payload.size()); std::string ack = rr.str();
      std::printf("ACK %s\n", ack.c_str());
    } else std::printf("NOACK %s\n", e.c_str());
  }
  return 0;
}

static int cmd_worker(const std::vector<std::string>& a) {
  const std::string host = arg(a, "--coord", "127.0.0.1");
  const std::uint16_t port = static_cast<std::uint16_t>(au64(arg(a, "--port", "3377")));
  const SourceId sid(au64(arg(a, "--source", "1")));
  const SourceGeneration gen = au64(arg(a, "--gen", "1"));
  const WorkerBootId boot = au64(arg(a, "--boot", "1"));
  const CoordinatorEpoch epoch = au64(arg(a, "--epoch", "0"));
  const std::string role = arg(a, "--role", "worker");
  net::TcpClient client;
  if (!client.connect(host, port)) { std::printf("ERR connect\n"); return 1; }
  { SourceDescriptor sd; sd.source_id = sid; sd.generation = gen; sd.type = SourceType::Multiprocess; sd.name = role;
    serde::BinWriter w; serde::write_source_descriptor(w, sd);
    net::Frame f; f.type = MsgType::RegisterSource; f.payload = w.bytes(); net::send_frame(client.socket(), f);
    net::Frame r; std::string e; net::recv_frame(client.socket(), r, e); }
  // Send a bounded evidence script for the workload.
  const std::uint64_t wid = au64(arg(a, "--workload", "1"));
  for (int i = 0; i < 2; ++i) {
    PlacementObservation o; o.observation_id = PlacementObservationId(sid.value() * 10000 + gen * 100 + i); o.observation_generation = gen;
    o.source_id = sid; o.source_generation = gen; o.source_type = SourceType::Multiprocess;
    o.worker_boot = boot; o.coordinator_epoch = epoch; o.timestamp = Clock::now();
    o.workload_id = WorkloadId(wid); o.epoch = 1; o.lifecycle = LifecycleState::Collected;
    o.fields.push_back({"state.memory.free", "", Value(24ull<<30), Classification::Measured, Provenance{}});
    net::Frame f; f.type = MsgType::Observation; serde::BinWriter w; serde::write_observation(w, o); f.payload = w.bytes();
    net::send_frame(client.socket(), f); net::Frame r; std::string e; net::recv_frame(client.socket(), r, e);
  }
  PlacementDecision d; d.decision_id = PlacementDecisionId(wid * 100 + 1); d.attempt_id = PlacementAttemptId(wid * 100 + 1);
  d.placement_generation = 1; d.observation_generation = 1; d.epoch = 1; d.policy_generation = 1;
  d.workload_id = WorkloadId(wid); d.request_id = RequestId(wid);
  PlacementCandidate c; c.candidate_id = CandidateId(1); c.device_id = DeviceId(1); c.node_id = NodeId(1);
  c.architecture = "sm_120"; c.health = HealthState::Healthy; c.memory.free_bytes = 24ull<<30; c.queue.depth = 0;
  d.candidate_set.candidates.push_back(c); d.candidate_set.complete = true; d.selected_candidate = CandidateId(1);
  d.tie_break = TieBreakReason::LowestCost; d.determinism = DeterminismClass::Deterministic;
  d.provenance.source_id = sid; d.provenance.source_generation = gen; d.provenance.coordinator_epoch = epoch;
  d.provenance.source_type = SourceType::Multiprocess; d.provenance.timestamp = Clock::now();
  { net::Frame f; f.type = MsgType::Decision; serde::BinWriter w; serde::write_decision(w, d); f.payload = w.bytes();
    net::send_frame(client.socket(), f); net::Frame r; std::string e; net::recv_frame(client.socket(), r, e); }
  return 0;
}
// ---------------------------------------------------------------------------
// Observatory-only commands
// ---------------------------------------------------------------------------
static Observatory* load_trace(const std::string& trace) {
  static Observatory obs;
  if (!trace.empty()) obs.recover(trace);
  return &obs;
}

static int cmd_decisions(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto ds = obs->decisions();
  json::JsonArray arr; for (const auto& d : ds) arr.push_back(serde::to_json(d));
  std::printf("%s\n", json::to_string(json::JsonValue(std::move(arr)), true, 2).c_str());
  return 0;
}
static int cmd_candidates(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto cs = obs->candidates(PlacementDecisionId(au64(arg(a, "--decision"))));
  std::printf("%s\n", json::to_string(serde::to_json(cs), true, 2).c_str());
  return 0;
}
static int cmd_timeline(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto t = obs->timeline(WorkloadId(au64(arg(a, "--workload"))));
  std::printf("%s\n", json::to_string(serde::to_json(t), true, 2).c_str());
  return 0;
}
static int cmd_snapshot(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto s = obs->snapshot();
  std::printf("%s\n", json::to_string(serde::to_json(s), true, 2).c_str());
  return 0;
}
static int cmd_explain(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto e = obs->explain(PlacementDecisionId(au64(arg(a, "--decision"))));
  std::printf("%s\n", json::to_string(serde::to_json(e), true, 2).c_str());
  return 0;
}
static int cmd_compare(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto c = obs->compare(PlacementDecisionId(au64(arg(a, "--a"))), PlacementDecisionId(au64(arg(a, "--b"))));
  std::printf("%s\n", json::to_string(serde::to_json(c), true, 2).c_str());
  return 0;
}
static int cmd_replay(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto r = obs->replay(PlacementDecisionId(au64(arg(a, "--decision"))));
  std::printf("%s\n", json::to_string(serde::to_json(r), true, 2).c_str());
  return 0;
}
static int cmd_counterfactual(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  std::vector<CounterfactualChange> changes;
  const std::string field = arg(a, "--field");
  if (!field.empty()) { changes.push_back({field, Value(au64(arg(a, "--value", "0"))), flag(a, "--relative")}); }
  auto cs = obs->counterfactual(PlacementDecisionId(au64(arg(a, "--decision"))), changes);
  json::JsonArray arr; for (const auto& c : cs) arr.push_back(serde::to_json(c));
  std::printf("%s\n", json::to_string(json::JsonValue(std::move(arr)), true, 2).c_str());
  return 0;
}
static int cmd_sources(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  json::JsonArray arr; for (const auto& s : obs->sources()) arr.push_back(serde::to_json(s));
  std::printf("%s\n", json::to_string(json::JsonValue(std::move(arr)), true, 2).c_str());
  return 0;
}
static int cmd_health(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  const auto id = SourceId(au64(arg(a, "--source")));
  json::JsonObject o; o.items.emplace_back("source_id", json::JsonValue(id.value()));
  o.items.emplace_back("health", json::JsonValue(std::string(to_string(obs->source_health(id)))));
  o.items.emplace_back("current_generation", json::JsonValue(obs->current_source_generation(id)));
  std::printf("%s\n", json::to_string(json::JsonValue(std::move(o)), true, 2).c_str());
  return 0;
}
static int cmd_stats(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  auto s = obs->stats();
  json::JsonObject o; o.items.emplace_back("observation_count", json::JsonValue(s.observation_count));
  o.items.emplace_back("decision_count", json::JsonValue(s.decision_count));
  o.items.emplace_back("outcome_count", json::JsonValue(s.outcome_count));
  o.items.emplace_back("source_count", json::JsonValue(s.source_count));
  o.items.emplace_back("superseded_count", json::JsonValue(s.superseded_count));
  o.items.emplace_back("rejected_count", json::JsonValue(s.rejected_count));
  o.items.emplace_back("event_count", json::JsonValue(s.event_count));
  std::printf("%s\n", json::to_string(json::JsonValue(std::move(o)), true, 2).c_str());
  return 0;
}
static int cmd_recover(const std::vector<std::string>& a) {
  const std::string trace = arg(a, "--trace");
  Observatory obs;
  const std::size_t n = obs.recover(trace);
  std::printf("{\"records_loaded\":%llu}\n", (unsigned long long)n);
  return 0;
}
static int cmd_observe(const std::vector<std::string>& a) {
  auto* obs = load_trace(arg(a, "--trace"));
  SourceDescriptor sd; sd.source_id = SourceId(au64(arg(a, "--source", "5"))); sd.generation = 1;
  sd.type = SourceType::System; sd.name = "windows-host";
  obs->register_source(sd);
  ProviderContext ctx; ctx.source_id = sd.source_id; ctx.source_generation = 1; ctx.source_type = SourceType::System;
  ctx.workload_id = WorkloadId(au64(arg(a, "--workload", "1")));
  WindowsHostProvider prov;
  auto obslist = prov.collect(ctx);
  for (auto& o : obslist) obs->ingest(o);
  const std::string trace = arg(a, "--trace");
  if (!trace.empty()) obs->persist(trace);
  std::printf("{\"observation_count\":%llu}\n", (unsigned long long)obs->observations().size());
  return 0;
}
// deterministic built-in scenario (forward decl for bench)
static placement_observatory::PlacementDecision po_scenario_memory_decision(placement_observatory::PlacementDecisionId id, placement_observatory::WorkloadId wl);

static int cmd_bench(const std::vector<std::string>& a) {
  const std::uint64_t n = au64(arg(a, "--iterations", "10000"));
  Observatory obs;
  const auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    auto d = po_scenario_memory_decision(PlacementDecisionId(100000 + i), WorkloadId(100000 + i));
    obs.ingest_decision(d);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
  std::printf("decision_ingest: %llu in %.6f ms -> %.0f decisions/s\n", (unsigned long long)n, ns / 1e6, n / (ns / 1e9));
  return 0;
}
static int cmd_inspect(const std::vector<std::string>& a) {
  const std::string f = arg(a, "--file");
  std::ifstream in(f, std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try {
    auto v = json::parse(body);
    std::printf("%s\n", json::to_string(v, true, 2).c_str());
  } catch (const json::JsonParseError& e) {
    std::printf("{\"error\":\"json parse failed\",\"pos\":%zu}\n", e.position()); return 1;
  }
  return 0;
}

// deterministic built-in scenario (used by bench)
static placement_observatory::PlacementDecision po_scenario_memory_decision(placement_observatory::PlacementDecisionId id, placement_observatory::WorkloadId wl) {
  placement_observatory::PlacementDecision d;
  d.decision_id = id; d.attempt_id = placement_observatory::PlacementAttemptId(id.value());
  d.placement_generation = 1; d.observation_generation = 1; d.epoch = 1; d.policy_generation = 1;
  d.workload_id = wl; d.request_id = placement_observatory::RequestId(id.value());
  d.tenant_id = placement_observatory::TenantId(1); d.namespace_id = placement_observatory::NamespaceId(1);
  auto add = [&](placement_observatory::CandidateId cid, placement_observatory::DeviceId dev, std::uint64_t free, double cost) {
    placement_observatory::PlacementCandidate c;
    c.candidate_id = cid; c.device_id = dev; c.node_id = placement_observatory::NodeId(1);
    c.architecture = "sm_120"; c.health = placement_observatory::HealthState::Healthy;
    c.memory.total_bytes = 32ull<<30; c.memory.free_bytes = free; c.memory.used_bytes = (32ull<<30)-free;
    c.memory.pressure_ratio = ((32ull<<30)-free)/(32.0*1024*1024*1024);
    c.queue.depth = 0; c.total_cost = cost;
    placement_observatory::PlacementCostComponent cc;
    cc.kind = placement_observatory::CostComponentKind::MemoryHeadroom; cc.cost = cost; cc.policy_weight = 1.0;
    cc.classification = placement_observatory::Classification::Measured; cc.label="headroom";
    c.costs.push_back(cc);
    d.candidate_set.candidates.push_back(c);
  };
  add(placement_observatory::CandidateId(1), placement_observatory::DeviceId(1), 8ull<<30, 2.0);
  add(placement_observatory::CandidateId(2), placement_observatory::DeviceId(2), 24ull<<30, 0.5);
  d.candidate_set.complete = true; d.candidate_set.reconstructed = false;
  d.selected_candidate = placement_observatory::CandidateId(2);
  d.tie_break = placement_observatory::TieBreakReason::LowestCost; d.determinism = placement_observatory::DeterminismClass::Deterministic;
  d.provenance.source_id = placement_observatory::SourceId(1); d.provenance.source_generation = 1; d.provenance.timestamp = placement_observatory::Clock::now();
  return d;
}

// real ingest: reads a JSON file describing an observation/decision/outcome.
static int cmd_ingest(const std::vector<std::string>& a) {
  const std::string f = arg(a, "--file");
  const std::string trace = arg(a, "--trace");
  if (f.empty()) { std::printf("{\"error\":\"--file required\"}\n"); return 1; }
  std::ifstream in(f, std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  json::JsonValue v;
  try { v = json::parse(body); }
  catch (const json::JsonParseError& e) { std::printf("{\"error\":\"json\",\"pos\":%zu}\n", e.position()); return 1; }
  auto* obs = load_trace(trace);
  const std::string kind = v.is_object() && v.find("kind") ? v.find("kind")->as_string() : "decision";
  auto num = [&](const char* k) -> std::uint64_t { auto p = v.find(k); return p ? p->as_uint() : 0; };
  auto str = [&](const char* k) -> std::string { auto p = v.find(k); return (p && p->is_string()) ? p->as_string() : ""; };
  if (kind == "observation") {
    PlacementObservation o;
    o.observation_id = PlacementObservationId(num("observation_id")); o.observation_generation = num("observation_generation");
    o.source_id = SourceId(num("source_id")); o.source_generation = num("source_generation");
    o.worker_boot = num("worker_boot"); o.coordinator_epoch = num("coordinator_epoch");
    o.workload_id = WorkloadId(num("workload_id")); o.timestamp = Clock::now();
    if (auto p = v.find("fields"); p && p->is_array()) {
      for (const auto& fld : p->as_array()) {
        if (!fld.is_object()) continue;
        Measurement m; auto fp = fld.find("field"); if (fp) m.normalized_field = fp->as_string();
        auto vp = fld.find("value"); if (vp) { if (vp->is_double()) m.value = Value(vp->as_double()); else if (vp->is_int()) m.value = Value(vp->as_int()); else if (vp->is_string()) m.value = Value(vp->as_string()); }
        o.fields.push_back(std::move(m));
      }
    }
    auto r = obs->ingest(o);
    if (!r.accepted) { std::printf("{\"accepted\":false,\"error\":\"%s\"}\n", r.error.c_str()); return 1; }
  } else if (kind == "decision") {
    auto d = po_scenario_memory_decision(PlacementDecisionId(num("decision_id")), WorkloadId(num("workload_id")));
    auto sc = v.find("selected_candidate"); if (sc) d.selected_candidate = CandidateId(sc->as_uint());
    auto rs = v.find("selected_reason"); if (rs) d.selected_reason = rs->as_string();
    auto r = obs->ingest_decision(d);
    if (!r.accepted) { std::printf("{\"accepted\":false,\"error\":\"%s\"}\n", r.error.c_str()); return 1; }
  } else if (kind == "outcome") {
    PlacementOutcome o; o.decision_id = PlacementDecisionId(num("decision_id")); o.attempt_id = PlacementAttemptId(num("attempt_id"));
    o.disposition = OutcomeDisposition::Succeeded; o.duration_ns = (std::int64_t)num("duration_ns");
    o.provenance.timestamp = Clock::now();
    auto r = obs->ingest_outcome(o);
    if (!r.accepted) { std::printf("{\"accepted\":false,\"error\":\"%s\"}\n", r.error.c_str()); return 1; }
  } else { std::printf("{\"error\":\"unknown kind\"}\n"); return 1; }
  if (!trace.empty()) obs->persist(trace);
  std::printf("{\"accepted\":true}\n");
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { print_help(); return 1; }
  std::vector<std::string> a;
  for (int i = 2; i < argc; ++i) a.push_back(argv[i]);
  const std::string cmd = argv[1];
  if (cmd == "help") { print_help(); return 0; }
  if (cmd == "decisions") return cmd_decisions(a);
  if (cmd == "candidates") return cmd_candidates(a);
  if (cmd == "timeline") return cmd_timeline(a);
  if (cmd == "snapshot") return cmd_snapshot(a);
  if (cmd == "explain") return cmd_explain(a);
  if (cmd == "compare") return cmd_compare(a);
  if (cmd == "replay") return cmd_replay(a);
  if (cmd == "counterfactual") return cmd_counterfactual(a);
  if (cmd == "sources") return cmd_sources(a);
  if (cmd == "health") return cmd_health(a);
  if (cmd == "stats") return cmd_stats(a);
  if (cmd == "recover") return cmd_recover(a);
  if (cmd == "observe") return cmd_observe(a);
  if (cmd == "bench") return cmd_bench(a);
  if (cmd == "inspect") return cmd_inspect(a);
  if (cmd == "serve") return cmd_serve(a);
  if (cmd == "worker") return cmd_worker(a);
  if (cmd == "client") return cmd_client(a);
  if (cmd == "ingest") return cmd_ingest(a);
  std::printf("unknown command '%s'\n", cmd.c_str()); return 1;
}