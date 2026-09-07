#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include "observatory/status.hpp"

namespace iobs {
namespace proto {

// Framed, versioned, bounded, checksummed TCP message protocol. Every frame carries magic,
// version, message type, bounded payload length, and a CRC-32C over the preceding bytes.
// Malformed frames (bad magic, wrong version, oversized, truncated, bad checksum, trailing
// garbage) are rejected, never silently accepted.

enum class MsgType : std::uint16_t {
  Hello = 1,
  PublishBaseline = 2,
  PublishObservation = 3,
  CompareRequest = 4,
  Snapshot = 5,
  Ack = 6,
  Error = 7,
  Shutdown = 8,
  Exit = 9,
  MeasureBaseline = 10,   // coordinator -> worker: produce/return a baseline
  MeasureCorun = 11,      // coordinator -> worker: produce/return a co-run observation
  ExitProcess = 12,       // coordinator -> worker: terminate the worker process (neighbor removal)
  Revalidate = 13         // coordinator -> worker: republish under a new epoch (coordinator restart)
};

inline constexpr std::uint32_t kMagic = 0x494F4246u;   // "IOBF"
inline constexpr std::uint16_t kVersion = 1u;
inline constexpr std::uint32_t kMaxFrame = 16u * 1024u * 1024u;
inline constexpr std::size_t kHeaderSize = 4 + 2 + 2 + 4 + 4;

struct Frame {
  MsgType type = MsgType::Hello;
  std::vector<std::uint8_t> payload;
};

// ---- payload codec (length-prefixed, bounded) -----------------------------
class Writer {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) { buf_.push_back(static_cast<std::uint8_t>(v & 0xFFu)); v >>= 8; } }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) { buf_.push_back(static_cast<std::uint8_t>(v & 0xFFu)); v >>= 8; } }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) { std::uint64_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); u64(bits); }
  void str(const std::string& s) { u64(static_cast<std::uint64_t>(s.size())); for (char c : s) u8(static_cast<std::uint8_t>(c)); }
  const std::vector<std::uint8_t>& bytes() const { return buf_; }
 private:
  std::vector<std::uint8_t> buf_;
};

class Reader {
 public:
  Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
  bool ok() const { return ok_; }
  std::uint8_t u8() { if (!need(1)) { ok_ = false; return 0; } return p_[pos_++]; }
  std::uint32_t u32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) { if (!need(1)) { ok_ = false; return v; } v |= static_cast<std::uint32_t>(p_[pos_++]) << (i * 8); } return v; }
  std::uint64_t u64() { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) { if (!need(1)) { ok_ = false; return v; } v |= static_cast<std::uint64_t>(p_[pos_++]) << (i * 8); } return v; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  double f64() { std::uint64_t bits = u64(); double v = 0.0; std::memcpy(&v, &bits, sizeof(v)); return v; }
  std::string str() { std::uint64_t len = u64(); if (!ok_ || len > kMaxFrame) { ok_ = false; return {}; } if (!need(static_cast<std::size_t>(len))) { ok_ = false; return {}; } std::string s(reinterpret_cast<const char*>(p_ + pos_), static_cast<std::size_t>(len)); pos_ += static_cast<std::size_t>(len); return s; }
  void finish() { if (pos_ != n_) ok_ = false; }
 private:
  bool need(std::size_t k) const { return pos_ + k <= n_; }
  const std::uint8_t* p_;
  std::size_t n_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

// ---- socket layer (Winsock on Windows, POSIX elsewhere) -------------------
// Returns a raw handle; on the current platform SOCKET is used.
std::int64_t socket_connect(const std::string& host, std::uint16_t port, std::string& err);
std::int64_t socket_listen(std::uint16_t port, std::string& err);
std::int64_t socket_accept(std::int64_t listener, std::string& err);
void socket_close(std::int64_t handle);

// Returns false on EOF/error. Full reads/writes (handles partial reads/writes).
bool send_frame(std::int64_t sock, MsgType type, const std::vector<std::uint8_t>& payload, std::string& err);
bool recv_frame(std::int64_t sock, Frame& out, std::string& err);

}  // namespace proto
}  // namespace iobs
