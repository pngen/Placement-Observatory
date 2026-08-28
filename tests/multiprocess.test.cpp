#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include "placement_observatory/protocol.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

using namespace placement_observatory;
using namespace placement_observatory::serde;
using namespace placement_observatory::net;
namespace fs = std::filesystem;

static std::string q(const std::string& s) { return std::string(1, '"') + s + std::string(1, '"'); }
static std::string cli_path() {
  char exe[MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, exe, static_cast<DWORD>(MAX_PATH));
  return (fs::path(exe).parent_path().parent_path() / "cli" / "po.exe").string();
}
static std::string cfgdir() {
  static std::atomic<unsigned> c{0};
  auto p = fs::temp_directory_path() / ("po_mp_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(c.fetch_add(1)));
  fs::remove_all(p);
  fs::create_directories(p);
  return p.string();
}
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Proc { HANDLE h = nullptr; bool valid = false; };
// Spawn a child OS process. If logfile is non-empty, redirect stdout/stderr there.
static Proc spawn(const std::string& cmdline, const std::string& logfile = "") {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  HANDLE logh = INVALID_HANDLE_VALUE;
  if (!logfile.empty()) {
    logh = CreateFileA(logfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = logh;
    si.hStdError = logh;
  }
  std::string cmd = cmdline;
  if (CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    if (logh != INVALID_HANDLE_VALUE) CloseHandle(logh);
    Proc p; p.h = pi.hProcess; p.valid = true; CloseHandle(pi.hThread); return p;
  }
  if (logh != INVALID_HANDLE_VALUE) CloseHandle(logh);
  return Proc{};
}
static bool wait_proc(const Proc& p, DWORD ms) { return p.valid && WaitForSingleObject(p.h, ms) == WAIT_OBJECT_0; }
static void term(const Proc& p) { if (p.valid) TerminateProcess(p.h, 1); }
static void close_proc(const Proc& p) { if (p.valid) CloseHandle(p.h); }

// --- authoritative readiness via the Status protocol query ---
static bool connect_port(const std::string& host, std::uint16_t port, TcpClient& out) {
  for (int k = 0; k < 200; ++k) { if (out.connect(host, port)) return true; sleep_ms(50); }
  return false;
}
// Query coordinator status for a source: returns (coordinator_epoch, source_generation).
static std::pair<std::uint64_t, std::uint64_t> query_status(const std::string& host, std::uint16_t port, SourceId sid) {
  TcpClient c;
  if (!connect_port(host, port, c)) return {0, 0};
  BinWriter w; w.u64(sid.value());
  Frame f; f.type = MsgType::Status; f.payload = w.bytes();
  if (!send_frame(c.socket(), f)) { c.close(); return {0, 0}; }
  Frame resp; std::string err;
  if (!recv_frame(c.socket(), resp, err)) { c.close(); return {0, 0}; }
  if (resp.type != MsgType::Status) { c.close(); return {0, 0}; }
  BinReader r(resp.payload.data(), resp.payload.size());
  const std::uint64_t epoch = r.u64();
  const std::uint64_t gen = r.u64();
  c.close();
  return {epoch, gen};
}
// Poll (authoritative) until the coordinator reports gen for sid.
static bool wait_gen(const std::string& host, std::uint16_t port, SourceId sid, std::uint64_t want) {
  for (int k = 0; k < 400; ++k) {
    const auto st = query_status(host, port, sid);
    if (st.second == want && st.second >= 1) return true;
    sleep_ms(25);
  }
  return false;
}
static std::string send_obs(const std::string& host, std::uint16_t port, PlacementObservationId id, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e) {
  TcpClient c;
  if (!connect_port(host, port, c)) return "connectfail";
  PlacementObservation o; o.observation_id = id; o.observation_generation = g; o.source_id = sid; o.source_generation = g;
  o.source_type = SourceType::Multiprocess; o.worker_boot = b; o.coordinator_epoch = e; o.workload_id = WorkloadId(7777);
  o.timestamp = Clock::now(); o.lifecycle = LifecycleState::Collected; o.fields.push_back({"f","",Value(1),Classification::Measured,Provenance{}});
  BinWriter w; write_observation(w, o); Frame f; f.type = MsgType::Observation; f.payload = w.bytes();
  if (!send_frame(c.socket(), f)) { c.close(); return "sendfail"; }
  Frame ack; std::string err;
  if (!recv_frame(c.socket(), ack, err)) { c.close(); return "recvfail:" + err; }
  BinReader r(ack.payload.data(), ack.payload.size()); std::string a = r.str(); c.close(); return a;
}
static std::string send_outcome(const std::string& host, std::uint16_t port, const PlacementOutcome& o) {
  TcpClient c;
  if (!connect_port(host, port, c)) return "connectfail";
  BinWriter w; write_outcome(w, o); Frame f; f.type = MsgType::Outcome; f.payload = w.bytes();
  if (!send_frame(c.socket(), f)) { c.close(); return "sendfail"; }
  Frame ack; std::string err;
  if (!recv_frame(c.socket(), ack, err)) { c.close(); return "recvfail:" + err; }
  BinReader r(ack.payload.data(), ack.payload.size()); std::string a = r.str(); c.close(); return a;
}
static std::string roll_epoch(const std::string& host, std::uint16_t port, std::uint64_t e) {
  TcpClient c;
  if (!connect_port(host, port, c)) return "connectfail";
  BinWriter w; w.u64(e); Frame f; f.type = MsgType::CoordinatorEpoch; f.payload = w.bytes();
  if (!send_frame(c.socket(), f)) { c.close(); return "sendfail"; }
  Frame ack; std::string err;
  if (!recv_frame(c.socket(), ack, err)) { c.close(); return "recvfail:" + err; }
  BinReader r(ack.payload.data(), ack.payload.size()); std::string a = r.str(); c.close(); return a;
}
static void shutdown_coord(const std::string& host, std::uint16_t port) {
  TcpClient c; if (connect_port(host, port, c)) { Frame f; f.type = MsgType::Shutdown; send_frame(c.socket(), f); } c.close();
}

PO_TEST(multiprocess_atomic_scenario) {
  const std::string dir = cfgdir();
  const auto portfile = (fs::path(dir) / "port.txt").string();
  const auto trace = (fs::path(dir) / "coord.bin").string();
  const std::string po = cli_path();
  const std::string host = "127.0.0.1";

  // 1. launch the real coordinator/collector OS process (stdio redirected to log)
  Proc coord = spawn(q(po) + " serve --port 0 --portfile " + q(portfile) + " --trace " + q(trace) + " --epoch 100", "");
  PO_CHECK(coord.valid);
  std::uint16_t port = 0;
  for (int i = 0; i < 80 && port == 0; ++i) { std::ifstream pf(portfile); if (pf) { pf >> port; } if (port == 0) sleep_ms(100); }
  PO_CHECK(port != 0);

  // 2. launch two real source/worker OS processes (worker A and worker B)
  Proc wa = spawn(q(po) + " worker --coord " + host + " --port " + std::to_string(port) + " --source 1 --gen 1 --boot 11 --epoch 100 --workload 100 --role srcA", "");
  Proc wb = spawn(q(po) + " worker --coord " + host + " --port " + std::to_string(port) + " --source 2 --gen 1 --boot 12 --epoch 100 --workload 200 --role srcB", "");
  // authoritative registration readiness: coordinator must report source 1 and 2 at gen 1.
  PO_CHECK(wait_gen(host, port, SourceId(1), 1));
  PO_CHECK(wait_gen(host, port, SourceId(2), 1));
  PO_CHECK(wait_proc(wa, 20000));
  PO_CHECK(wait_proc(wb, 20000));

  // 3. terminate worker A as a real OS process, then restart as a NEW process with NEW boot/gen
  term(wa); close_proc(wa);
  Proc wa2 = spawn(q(po) + " worker --coord " + host + " --port " + std::to_string(port) + " --source 1 --gen 2 --boot 22 --epoch 100 --workload 150 --role srcA-restart", "");
  PO_CHECK(wa2.valid);
  // authoritative readiness: coordinator reports source 1 at gen 2 (restart registered and usable)
  PO_CHECK(wait_gen(host, port, SourceId(1), 2));
  PO_CHECK(wait_proc(wa2, 20000));
  close_proc(wa2);

  // 4. roll the coordinator epoch
  PO_CHECK(roll_epoch(host, port, 101) == "epoch");

  // 5. stale OLD epoch evidence over real framed TCP -> deterministic stale_epoch rejection
  auto stale_epoch = send_obs(host, port, PlacementObservationId(700001), SourceId(9), 1, 9, 100);
  PO_CHECK(stale_epoch.find("stale coordinator epoch") != std::string::npos);

  // 6. current epoch with OLD WorkerBootId -> stale_worker_boot rejection
  auto stale_boot = send_obs(host, port, PlacementObservationId(700002), SourceId(1), 2, 11, 101);
  PO_CHECK(stale_boot.find("stale worker boot") != std::string::npos);

  // 7. obsolete SourceGeneration -> stale_generation rejection
  auto stale_gen = send_obs(host, port, PlacementObservationId(700003), SourceId(1), 1, 11, 101);
  PO_CHECK(stale_gen.find("stale source generation") != std::string::npos);

  // 8. fresh post-restart evidence under current authority -> HARD assert accepted
  auto fresh = send_obs(host, port, PlacementObservationId(700004), SourceId(1), 2, 22, 101);
  PO_CHECK(fresh == "accepted");

  // 9. complete a fresh placement/outcome link for the restarted worker
  PlacementOutcome out; out.decision_id = PlacementDecisionId(15001); out.attempt_id = PlacementAttemptId(15001);
  out.disposition = OutcomeDisposition::Succeeded; out.duration_ns = 555; out.provenance.timestamp = Clock::now();
  out.provenance.source_id = SourceId(1); out.provenance.source_generation = 2; out.provenance.source_type = SourceType::Multiprocess;
  out.provenance.coordinator_epoch = 101;
  auto ol = send_outcome(host, port, out);
  PO_CHECK(ol == "accepted");

  // 10. stale evidence must not have mutated current authority: source 1 remains gen 2
  PO_CHECK_EQ(query_status(host, port, SourceId(1)).second, 2u);

  // 11. shut down the coordinator (persists trace) and terminate
  shutdown_coord(host, port);
  sleep_ms(300);
  term(coord); close_proc(coord);
  sleep_ms(200);
  PO_CHECK(!wait_proc(coord, 1)); // process terminated

  // 12. reload persisted trace: deterministic replay before/after restart, only valid evidence present
  PO_CHECK(fs::exists(trace));
  Observatory obs; obs.recover(trace);
  PO_CHECK(obs.replay(PlacementDecisionId(10001)).reproduced);
  PO_CHECK(obs.replay(PlacementDecisionId(20001)).reproduced);
  PO_CHECK(obs.replay(PlacementDecisionId(15001)).reproduced);
  PO_CHECK_EQ(obs.current_source_generation(SourceId(1)), 2u);
  // the three stale observations were never admitted (only worker evidence + fresh probe)
  PO_CHECK_EQ(obs.stats().observation_count, 7u);  // 2+2+2 worker obs + 1 fresh probe
  // stale evidence did not mutate the pre-restart decision's explanation
  auto ex = obs.explain(PlacementDecisionId(10001));
  bool still_one = false;
  for (const auto& l : ex.lines) if (l.question == "why_candidate" && l.answer.find("candidate 1") != std::string::npos) still_one = true;
  PO_CHECK(still_one);
  // no orphan temp traces
  PO_CHECK(!fs::exists(trace + ".tmp"));
  PO_CHECK(!fs::exists(portfile + ".tmp"));
  fs::remove_all(dir);
}
PO_MAIN