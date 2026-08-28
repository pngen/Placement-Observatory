#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
#include "placement_observatory/protocol.hpp"
#include <thread>
int main() {
  using namespace placement_observatory::net;
  TcpServer srv;
  if (!srv.listen(0)) { std::printf("listen failed\n"); return 1; }
  const auto port = srv.port();
  std::thread thr([&](){ TcpSocket conn; if (srv.accept(conn)) { Frame f; std::string err; if (recv_frame(conn, f, err)) { Frame a; a.type = MsgType::Ack; a.payload = {1,2,3}; send_frame(conn, a); } } });
  TcpClient cli;
  if (!cli.connect("127.0.0.1", port)) { thr.join(); std::printf("connect failed\n"); return 1; }
  Frame req; req.type = MsgType::Probe; req.payload = {9,8,7};
  if (!send_frame(cli.socket(), req)) { thr.join(); std::printf("send failed\n"); return 1; }
  Frame resp; std::string err;
  const bool ok = recv_frame(cli.socket(), resp, err);
  thr.join();
  std::printf("distributed loopback ok=%d ack_type=%d payload=%zu\n", ok?1:0, (int)resp.type, resp.payload.size());
  return ok ? 0 : 1;
}