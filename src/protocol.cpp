#include "observatory/protocol.hpp"
#include "observatory/persist.hpp"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
  #include <cerrno>
#endif

#include <cstring>
#include <string>

namespace iobs {
namespace proto {

namespace {
#ifdef _WIN32
  using sock_t = SOCKET;
  inline bool is_valid(sock_t s) { return s != INVALID_SOCKET; }
#else
  using sock_t = int;
  inline bool is_valid(sock_t s) { return s >= 0; }
#endif

constexpr std::uint32_t kMagicHost = 0x494F4246u;  // "IOBF"
}  // namespace

std::int64_t socket_connect(const std::string& host, std::uint16_t port, std::string& err) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { err = "WSAStartup failed"; return -1; }
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!is_valid(s)) { err = "socket failed"; return -1; }
  if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    err = "connect failed: " + std::to_string(WSAGetLastError()); closesocket(s); return -1;
  }
  return static_cast<std::int64_t>(s);
#else
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { err = "socket failed"; return -1; }
  if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) { err = "connect failed"; close(s); return -1; }
  return static_cast<std::int64_t>(s);
#endif
}

std::int64_t socket_listen(std::uint16_t port, std::string& err) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { err = "WSAStartup failed"; return -1; }
  sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!is_valid(s)) { err = "socket failed"; return -1; }
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) { err = "bind failed"; closesocket(s); return -1; }
  if (listen(s, 16) != 0) { err = "listen failed"; closesocket(s); return -1; }
  return static_cast<std::int64_t>(s);
#else
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { err = "socket failed"; return -1; }
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) { err = "bind failed"; close(s); return -1; }
  if (listen(s, 16) != 0) { err = "listen failed"; close(s); return -1; }
  return static_cast<std::int64_t>(s);
#endif
}

std::int64_t socket_accept(std::int64_t listener, std::string& err) {
#ifdef _WIN32
  sock_t ls = static_cast<sock_t>(listener);
  sockaddr_in addr{};
  int len = sizeof(addr);
  sock_t c = accept(ls, reinterpret_cast<struct sockaddr*>(&addr), &len);
  if (!is_valid(c)) { err = "accept failed"; return -1; }
  return static_cast<std::int64_t>(c);
#else
  int l = static_cast<int>(listener);
  struct sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  int c = accept(l, reinterpret_cast<struct sockaddr*>(&addr), &len);
  if (c < 0) { err = "accept failed"; return -1; }
  return static_cast<std::int64_t>(c);
#endif
}

void socket_close(std::int64_t handle) {
#ifdef _WIN32
  closesocket(static_cast<sock_t>(handle));
#else
  close(static_cast<int>(handle));
#endif
}

namespace {
bool write_all(std::int64_t sock, const std::uint8_t* data, std::size_t n, std::string& err) {
  std::size_t off = 0;
  while (off < n) {
#ifdef _WIN32
    int r = send(static_cast<sock_t>(sock), reinterpret_cast<const char*>(data + off), static_cast<int>(n - off), 0);
#else
    ssize_t r = send(static_cast<int>(sock), data + off, n - off, 0);
#endif
    if (r <= 0) { err = "send failed"; return false; }
    off += static_cast<std::size_t>(r);
  }
  return true;
}

bool read_exact(std::int64_t sock, std::uint8_t* data, std::size_t n, std::string& err) {
  std::size_t off = 0;
  while (off < n) {
#ifdef _WIN32
    int r = recv(static_cast<sock_t>(sock), reinterpret_cast<char*>(data + off), static_cast<int>(n - off), 0);
#else
    ssize_t r = recv(static_cast<int>(sock), data + off, n - off, 0);
#endif
    if (r == 0) { err = "connection closed"; return false; }
    if (r < 0) { err = "recv failed"; return false; }
    off += static_cast<std::size_t>(r);
  }
  return true;
}
}  // namespace

bool send_frame(std::int64_t sock, MsgType type, const std::vector<std::uint8_t>& payload, std::string& err) {
  if (payload.size() > kMaxFrame) { err = "frame too large"; return false; }
  const auto plen = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> frame;
  frame.reserve(kHeaderSize + payload.size());
  // fixed header fields: magic(4) + version(2) + type(2) + payload length(4) = 12 bytes
  const auto magic = kMagicHost;
  for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>((magic >> (i * 8)) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(kVersion & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((kVersion >> 8) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(type) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((static_cast<std::uint16_t>(type) >> 8) & 0xFFu));
  for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>((plen >> (i * 8)) & 0xFFu));
  // CRC-32C over the 12 fixed bytes + payload.
  std::vector<std::uint8_t> crc_window(frame);
  crc_window.insert(crc_window.end(), payload.begin(), payload.end());
  const auto crc = persist::crc32c(crc_window.data(), crc_window.size());
  for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>((crc >> (i * 8)) & 0xFFu));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return write_all(sock, frame.data(), frame.size(), err);
}

bool recv_frame(std::int64_t sock, Frame& out, std::string& err) {
  std::uint8_t header[kHeaderSize];
  if (!read_exact(sock, header, kHeaderSize, err)) {
    if (err == "connection closed" || err == "EOF") { err = "EOF"; return false; }
    return false;
  }
  std::uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= static_cast<std::uint32_t>(header[i]) << (i * 8);
  if (magic != kMagicHost) { err = "bad frame magic"; return false; }
  const std::uint16_t version = static_cast<std::uint16_t>(header[4]) | (static_cast<std::uint16_t>(header[5]) << 8);
  if (version != kVersion) { err = "frame version mismatch"; return false; }
  const std::uint16_t type = static_cast<std::uint16_t>(header[6]) | (static_cast<std::uint16_t>(header[7]) << 8);
  std::uint32_t plen = 0;
  for (int i = 0; i < 4; ++i) plen |= static_cast<std::uint32_t>(header[8 + i]) << (i * 8);
  if (plen > kMaxFrame) { err = "frame too large"; return false; }

  std::vector<std::uint8_t> payload(plen);
  if (!read_exact(sock, payload.data(), payload.size(), err)) return false;

  std::vector<std::uint8_t> crc_window(header, header + 12);
  crc_window.insert(crc_window.end(), payload.begin(), payload.end());
  std::uint32_t stored = 0;
  for (int i = 0; i < 4; ++i) stored |= static_cast<std::uint32_t>(header[12 + i]) << (i * 8);
  const auto computed = persist::crc32c(crc_window.data(), crc_window.size());
  if (computed != stored) { err = "frame checksum mismatch"; return false; }

  out.type = static_cast<MsgType>(type);
  out.payload = std::move(payload);
  return true;
}

}  // namespace proto
}  // namespace iobs
