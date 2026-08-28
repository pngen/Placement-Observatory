#pragma once
// Real framed TCP transport for distributed placement observations.
//
// Frame layout (little-endian, fixed header, hard size cap, checksummed):
//   [magic u32][protocol_version u32][message_type u8][payload_len u32] [payload] [crc32 u32]
// The strict decoder rejects oversized frames, truncated frames, unknown
// protocol versions, unknown message types and checksum mismatches. Lossless
// 64-bit identities travel in the payload as integers (never floating point).
// Semantic authority validation (coordinator epoch, source generation, worker
// boot) is enforced by the collecting Observatory, not by the framer.
#include <cstdint>
#include <string>
#include <vector>

namespace placement_observatory::net {

constexpr std::uint32_t kFrameMagic = 0x504F5046u;     // "POPF"
constexpr std::uint32_t kProtocolVersion = 1u;
constexpr std::uint32_t kMaxFramePayload = 16u * 1024 * 1024; // hard cap (16 MiB)

enum class MsgType : std::uint8_t {
  Hello = 0,
  RegisterSource = 1,
  Observation = 2,
  Decision = 3,
  Outcome = 4,
  Ack = 5,
  CoordinatorEpoch = 6,
  Shutdown = 7,
  Ping = 8,
  Probe = 9,
};

struct Frame {
  MsgType type = MsgType::Hello;
  std::vector<std::uint8_t> payload;
};

// CRC-32 (IEEE 802.3) used for frame integrity.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t n) noexcept;

// Encode a frame. Throws std::runtime_error if payload exceeds the cap.
[[nodiscard]] std::vector<std::uint8_t> encode_frame(MsgType type, const std::vector<std::uint8_t>& payload,
                                                     std::uint32_t version = kProtocolVersion);
// Decode exactly one frame at the front of a buffer. Sets 'consumed' to the number
// of bytes this frame occupies. On any validity failure returns false and sets err.
[[nodiscard]] bool decode_frame(const std::uint8_t* data, std::size_t n, std::size_t& consumed, Frame& out, std::string& err);

// --- real TCP transport (Winsock2) ---
class TcpSocket {
 public:
  TcpSocket();
  ~TcpSocket();
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  [[nodiscard]] bool assign_socket(std::uintptr_t raw) noexcept;
  bool send_all(const std::uint8_t* data, std::size_t n) const;
  // Receives exactly n bytes; returns false on close/error.
  bool recv_exact(std::uint8_t* data, std::size_t n) const;
  void close() noexcept;
  [[nodiscard]] bool valid() const noexcept;
 private:
  std::uintptr_t sock_ = 0;   // SOCKET type stored as uintptr_t
  bool ok_ = false;
};

class TcpServer {
 public:
  TcpServer();
  ~TcpServer();
  [[nodiscard]] bool listen(std::uint16_t port);   // binds and listens on 0.0.0.0
  [[nodiscard]] bool accept(TcpSocket& out);       // blocking accept
  [[nodiscard]] std::uint16_t port() const;
  void close() noexcept;
 private:
  std::uintptr_t listen_sock_ = 0;
  bool ok_ = false;
  std::uint16_t port_ = 0;
};

class TcpClient {
 public:
  TcpClient();
  ~TcpClient();
  [[nodiscard]] bool connect(const std::string& host, std::uint16_t port);
  [[nodiscard]] TcpSocket& socket() noexcept { return sock_; }
  void close() noexcept;
 private:
  TcpSocket sock_;
};

// Send a frame over a socket. Returns false on write error/close.
bool send_frame(const TcpSocket& s, const Frame& f);
// Receive a frame over a socket (handles partial reads). Returns false if the peer
// closes or a protocol violation occurs (err set).
bool recv_frame(const TcpSocket& s, Frame& f, std::string& err);

} // namespace placement_observatory::net