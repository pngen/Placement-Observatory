// Coordinator regression scenarios against a real `po serve` process over
// framed TCP: malformed payloads are rejected without taking the coordinator
// down, the coordinator epoch never regresses over the protocol or across a
// restart, and shutdown with an idle connection still exits and persists.
// Pass a case name to run only that case.
#include "test_fw.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/serialize.hpp"
#include "placement_observatory/protocol.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace placement_observatory;
using namespace placement_observatory::serde;
using namespace placement_observatory::net;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& tag) {
  static std::atomic<unsigned> c{0};
  const fs::path p = fs::path(PO_TEST_SCRATCH) / (tag + "_" + std::to_string(c.fetch_add(1)));
  fs::create_directories(p);
  return p;
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

std::string cli_path() {
  char exe[MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, exe, static_cast<DWORD>(MAX_PATH));
  return (fs::path(exe).parent_path().parent_path() / "cli" / "po.exe").string();
}

// A coordinator process; terminated on scope exit if a failed check left it running.
struct Coord {
  HANDLE h = nullptr;
  std::uint16_t port = 0;
  Coord() = default;
  Coord(const Coord&) = delete;
  Coord& operator=(const Coord&) = delete;
  ~Coord() {
    if (!h) return;
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) TerminateProcess(h, 1);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }
};

// Starts `po serve --port 0` and waits until it has written its port.
void start(Coord& c, const std::string& extra) {
  const fs::path portfile = scratch("port") / "port.txt";
  std::string cmd = q(cli_path()) + " serve --port 0 --portfile " + q(portfile.string()) + " " + extra;
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return;
  CloseHandle(pi.hThread);
  c.h = pi.hProcess;
  for (;;) {
    { std::ifstream pf(portfile); unsigned p = 0; if (pf >> p && p != 0) { c.port = static_cast<std::uint16_t>(p); return; } }
    if (WaitForSingleObject(c.h, 0) == WAIT_OBJECT_0) return;   // exited before listening
    Sleep(10);
  }
}

// Sends one frame on a fresh connection and returns the Ack text.
std::string roundtrip(std::uint16_t port, const Frame& f) {
  TcpClient c;
  if (!c.connect("127.0.0.1", port)) return "connectfail";
  if (!send_frame(c.socket(), f)) return "sendfail";
  Frame ack; std::string err;
  if (!recv_frame(c.socket(), ack, err)) return "recvfail:" + err;
  if (ack.type != MsgType::Ack) return "not-an-ack";
  BinReader r(ack.payload.data(), ack.payload.size());
  return r.str();
}

PlacementObservation mk(std::uint64_t id, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e) {
  PlacementObservation o; o.observation_id = PlacementObservationId(id); o.observation_generation = 1;
  o.source_id = sid; o.source_generation = g; o.source_type = SourceType::Multiprocess;
  o.worker_boot = b; o.coordinator_epoch = e; o.timestamp = Clock::now();
  o.lifecycle = LifecycleState::Collected; o.workload_id = WorkloadId(1);
  o.fields.push_back({"f", "", Value(1), Classification::Measured, Provenance{}});
  return o;
}

std::string send_obs(std::uint16_t port, const PlacementObservation& o) {
  BinWriter w; write_observation(w, o);
  Frame f; f.type = MsgType::Observation; f.payload = w.bytes();
  return roundtrip(port, f);
}

std::string roll(std::uint16_t port, std::uint64_t epoch) {
  BinWriter w; w.u64(epoch);
  Frame f; f.type = MsgType::CoordinatorEpoch; f.payload = w.bytes();
  return roundtrip(port, f);
}

// (coordinator epoch, source generation) as reported by the Status query.
std::pair<std::uint64_t, std::uint64_t> status(TcpSocket& s, SourceId sid) {
  BinWriter w; w.u64(sid.value());
  Frame f; f.type = MsgType::Status; f.payload = w.bytes();
  if (!send_frame(s, f)) return {0, 0};
  Frame resp; std::string err;
  if (!recv_frame(s, resp, err) || resp.type != MsgType::Status) return {0, 0};
  BinReader r(resp.payload.data(), resp.payload.size());
  const std::uint64_t e = r.u64();
  return {e, r.u64()};
}
std::pair<std::uint64_t, std::uint64_t> status(std::uint16_t port, SourceId sid) {
  TcpClient c;
  if (!c.connect("127.0.0.1", port)) return {0, 0};
  return status(c.socket(), sid);
}

// Sends Shutdown and waits for the coordinator process to exit; returns its exit code.
DWORD shutdown_and_wait(Coord& c) {
  { TcpClient s; if (s.connect("127.0.0.1", c.port)) { Frame f; f.type = MsgType::Shutdown; send_frame(s.socket(), f); } }
  WaitForSingleObject(c.h, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(c.h, &code);
  return code;
}

bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

}  // namespace

// Malformed payloads are rejected and the coordinator keeps serving.
PO_TEST(malformed_frames_rejected_coordinator_survives) {
  Coord c; start(c, "--epoch 100");
  PO_CHECK(c.port != 0);
  Frame truncated; truncated.type = MsgType::Observation; truncated.payload = {1, 2, 3};
  PO_CHECK(roundtrip(c.port, truncated).rfind("REJECT:malformed", 0) == 0);
  {
    PlacementObservation o = mk(1, SourceId(1), 1, 11, 100); o.fields.clear();
    BinWriter w; write_observation(w, o);
    Frame huge; huge.type = MsgType::Observation; huge.payload = w.bytes();
    // an element count far beyond the frame (just before the two trailing bytes)
    for (std::size_t i = 0; i < 4; ++i) huge.payload[huge.payload.size() - 6 + i] = 0xFF;
    PO_CHECK(roundtrip(c.port, huge).rfind("REJECT:malformed", 0) == 0);
  }
  Frame empty_status; empty_status.type = MsgType::Status;
  PO_CHECK(roundtrip(c.port, empty_status).rfind("REJECT:malformed", 0) == 0);
  PO_CHECK(send_obs(c.port, mk(2, SourceId(1), 1, 11, 100)) == "accepted");
  PO_CHECK_EQ(shutdown_and_wait(c), 0u);
}

// The coordinator epoch never regresses over the protocol or across a restart
// on the persisted trace.
PO_TEST(epoch_never_regresses_over_protocol_or_restart) {
  const std::string trace = (scratch("epoch") / "coord.bin").string();
  {
    Coord c; start(c, "--epoch 100 --trace " + q(trace));
    PO_CHECK(c.port != 0);
    PO_CHECK(roll(c.port, 101) == "epoch");
    PO_CHECK(has(roll(c.port, 50), "stale coordinator epoch"));
    PO_CHECK_EQ(status(c.port, SourceId(1)).first, 101u);
    PO_CHECK(has(send_obs(c.port, mk(1, SourceId(1), 1, 11, 100)), "stale coordinator epoch"));
    PO_CHECK_EQ(shutdown_and_wait(c), 0u);
  }
  {
    Coord c; start(c, "--trace " + q(trace));   // restarted without --epoch
    PO_CHECK(c.port != 0);
    PO_CHECK_EQ(status(c.port, SourceId(1)).first, 101u);
    PO_CHECK(has(send_obs(c.port, mk(2, SourceId(1), 1, 11, 100)), "stale coordinator epoch"));
    PO_CHECK_EQ(shutdown_and_wait(c), 0u);
  }
}

// Shutdown completes and persists while another connection is open and idle.
PO_TEST(shutdown_with_idle_connection_persists) {
  const std::string trace = (scratch("idle") / "coord.bin").string();
  Coord c; start(c, "--epoch 100 --trace " + q(trace));
  PO_CHECK(c.port != 0);
  PO_CHECK(send_obs(c.port, mk(1, SourceId(1), 1, 11, 100)) == "accepted");
  TcpClient idle;
  PO_CHECK(idle.connect("127.0.0.1", c.port));
  // one round trip, so its handler is running and now waits for the next frame
  PO_CHECK_EQ(status(idle.socket(), SourceId(1)).second, 1u);
  PO_CHECK_EQ(shutdown_and_wait(c), 0u);
  Observatory r;
  r.recover(trace);
  PO_CHECK_EQ(r.stats().observation_count, 1u);
}

int main(int argc, char** argv) {
  // No crash dialogs from this process or the coordinators it starts.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  auto& reg = po_test::Registry::instance();
  if (argc > 1) std::erase_if(reg.cases, [&](const po_test::Case& c) { return c.name != argv[1]; });
  if (reg.cases.empty()) { std::printf("unknown case\n"); return 2; }
  const int rc = reg.run();
  std::error_code ec;
  fs::remove_all(PO_TEST_SCRATCH, ec);
  return rc;
}