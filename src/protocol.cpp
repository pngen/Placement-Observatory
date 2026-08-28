#include "placement_observatory/protocol.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <stdexcept>

namespace placement_observatory::net {

// ---------------- CRC-32 ----------------
std::uint32_t crc32(const std::uint8_t* data, std::size_t n) noexcept {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

namespace {
std::uint32_t read_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
       | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  v.push_back(static_cast<std::uint8_t>(x & 0xff)); v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
  v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xff)); v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xff));
}
void put_u8(std::vector<std::uint8_t>& v, std::uint8_t x) { v.push_back(x); }
bool valid_type(std::uint8_t t) { return t <= static_cast<std::uint8_t>(MsgType::Confirm); }
constexpr std::size_t kHeaderLen = 4 + 4 + 1 + 4; // magic+version+type+payload_len = 13
} // namespace

// ---------------- frame encode/decode ----------------
std::vector<std::uint8_t> encode_frame(MsgType type, const std::vector<std::uint8_t>& payload, std::uint32_t version) {
  if (payload.size() > kMaxFramePayload) throw std::runtime_error("frame payload exceeds cap");
  if (!valid_type(static_cast<std::uint8_t>(type))) throw std::runtime_error("invalid message type");
  std::vector<std::uint8_t> out;
  put_u32(out, kFrameMagic);
  put_u32(out, version);
  put_u8(out, static_cast<std::uint8_t>(type));
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  const std::uint32_t crc = crc32(out.data(), out.size());
  put_u32(out, crc);
  return out;
}

bool decode_frame(const std::uint8_t* data, std::size_t n, std::size_t& consumed, Frame& out, std::string& err) {
  if (n < kHeaderLen + 4) { err = "truncated frame header"; return false; }
  const std::uint32_t magic = read_u32(data);
  if (magic != kFrameMagic) { err = "bad frame magic"; return false; }
  const std::uint32_t version = read_u32(data + 4);
  if (version != kProtocolVersion) { err = "unknown protocol version"; return false; }
  const std::uint8_t type = data[8];
  if (!valid_type(type)) { err = "unknown message type"; return false; }
  const std::uint32_t plen = read_u32(data + 9);
  if (plen > kMaxFramePayload) { err = "oversized frame"; return false; }
  const std::size_t total = kHeaderLen + static_cast<std::size_t>(plen) + 4;
  if (n < total) { err = "truncated frame"; return false; }
  const std::uint32_t stored = read_u32(data + kHeaderLen + static_cast<std::size_t>(plen));
  const std::uint32_t computed = crc32(data, kHeaderLen + static_cast<std::size_t>(plen));
  if (stored != computed) { err = "frame checksum mismatch"; return false; }
  consumed = total;
  out.type = static_cast<MsgType>(type);
  out.payload.assign(data + kHeaderLen, data + kHeaderLen + static_cast<std::size_t>(plen));
  return true;
}

// ---------------- Winsock init ----------------
namespace {
bool winsock_init() {
  static WSADATA wsa;
  static bool ok = []() { return WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }();
  return ok;
}
} // namespace

// ---------------- TcpSocket ----------------
TcpSocket::TcpSocket() { ok_ = false; sock_ = 0; }
TcpSocket::~TcpSocket() { close(); }
bool TcpSocket::assign_socket(std::uintptr_t raw) noexcept { sock_ = raw; ok_ = raw != 0; return ok_; }
bool TcpSocket::valid() const noexcept { return ok_ && sock_ != 0; }
void TcpSocket::close() noexcept {
  if (ok_ && sock_ != 0) { closesocket(static_cast<SOCKET>(sock_)); }
  sock_ = 0; ok_ = false;
}
bool TcpSocket::send_all(const std::uint8_t* data, std::size_t n) const {
  if (!ok_) return false;
  std::size_t sent = 0;
  while (sent < n) {
    const int r = static_cast<int>(::send(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(data + sent),
        static_cast<int>(n - sent), 0));
    if (r == SOCKET_ERROR) return false;
    if (r == 0) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}
bool TcpSocket::recv_exact(std::uint8_t* data, std::size_t n) const {
  if (!ok_) return false;
  std::size_t got = 0;
  while (got < n) {
    const int r = static_cast<int>(::recv(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(data + got),
        static_cast<int>(n - got), 0));
    if (r == SOCKET_ERROR) return false;
    if (r == 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

// ---------------- TcpServer ----------------
TcpServer::TcpServer() { ok_ = false; listen_sock_ = 0; port_ = 0; }
TcpServer::~TcpServer() { close(); }
bool TcpServer::listen(std::uint16_t port) {
  if (!winsock_init()) return false;
  const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr.s_addr = INADDR_ANY;
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
  if (::listen(s, SOMAXCONN) == SOCKET_ERROR) { closesocket(s); return false; }
  sockaddr_in bound{}; int len = sizeof(bound);
  ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
  port_ = ntohs(bound.sin_port);
  listen_sock_ = static_cast<std::uintptr_t>(s);
  ok_ = true;
  return true;
}
bool TcpServer::wait_accept(int ms) const {
  if (!ok_ || listen_sock_ == 0) return false;
  fd_set rfds; FD_ZERO(&rfds); FD_SET(static_cast<SOCKET>(listen_sock_), &rfds);
  timeval tv; tv.tv_sec = 0; tv.tv_usec = static_cast<long>(ms) * 1000;
  const int n = select(0, &rfds, nullptr, nullptr, &tv);
  return n > 0;
}
bool TcpServer::accept(TcpSocket& out) {
  if (!ok_) return false;
  const SOCKET client = ::accept(static_cast<SOCKET>(listen_sock_), nullptr, nullptr);
  if (client == INVALID_SOCKET) return false;
  return out.assign_socket(static_cast<std::uintptr_t>(client));
}
std::uint16_t TcpServer::port() const { return port_; }
void TcpServer::close() noexcept {
  if (ok_ && listen_sock_ != 0) closesocket(static_cast<SOCKET>(listen_sock_));
  listen_sock_ = 0; ok_ = false; port_ = 0;
}

// ---------------- TcpClient ----------------
TcpClient::TcpClient() = default;
TcpClient::~TcpClient() { close(); }
bool TcpClient::connect(const std::string& host, std::uint16_t port) {
  if (!winsock_init()) return false;
  const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
  if (!sock_.assign_socket(static_cast<std::uintptr_t>(s))) { closesocket(s); return false; }
  return true;
}
void TcpClient::close() noexcept { sock_.close(); }

// ---------------- frame I/O over a socket ----------------
bool send_frame(const TcpSocket& s, const Frame& f) {
  std::vector<std::uint8_t> bytes = encode_frame(f.type, f.payload);
  return s.send_all(bytes.data(), bytes.size());
}
bool recv_frame(const TcpSocket& s, Frame& f, std::string& err) {
  std::uint8_t hdr[kHeaderLen];
  if (!s.recv_exact(hdr, sizeof(hdr))) { err = "peer closed reading header"; return false; }
  const std::uint32_t magic = read_u32(hdr);
  const std::uint32_t version = read_u32(hdr + 4);
  const std::uint8_t type = hdr[8];
  const std::uint32_t plen = read_u32(hdr + 9);
  if (magic != kFrameMagic) { err = "bad frame magic"; return false; }
  if (version != kProtocolVersion) { err = "unknown protocol version"; return false; }
  if (!valid_type(type)) { err = "unknown message type"; return false; }
  if (plen > kMaxFramePayload) { err = "oversized frame"; return false; }
  Frame f2; f2.type = static_cast<MsgType>(type);
  f2.payload.resize(plen);
  if (plen > 0 && !s.recv_exact(f2.payload.data(), plen)) { err = "truncated frame payload"; return false; }
  std::uint8_t crcbuf[4];
  if (!s.recv_exact(crcbuf, 4)) { err = "truncated frame checksum"; return false; }
  std::vector<std::uint8_t> full; full.reserve(sizeof(hdr) + plen);
  full.insert(full.end(), hdr, hdr + sizeof(hdr));
  full.insert(full.end(), f2.payload.begin(), f2.payload.end());
  const std::uint32_t stored = read_u32(crcbuf);
  const std::uint32_t computed = crc32(full.data(), full.size());
  if (stored != computed) { err = "frame checksum mismatch"; return false; }
  f = std::move(f2);
  return true;
}

} // namespace placement_observatory::net