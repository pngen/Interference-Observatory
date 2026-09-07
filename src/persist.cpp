#include "observatory/persist.hpp"
#include <array>

namespace iobs {
namespace persist {

// CRC-32C (Castagnoli), polynomial 0x1EDC6F41, reflected 0x82F63B78.
std::uint32_t crc32c(const void* data, std::size_t len) noexcept {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1u) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
      }
      t[i] = c;
    }
    return t;
  }();
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxPayload) {
    throw Error(Status::ResourceExhausted, "payload exceeds persistence bound");
  }
  const auto plen = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + payload.size() + kCrcSize);
  out.push_back(static_cast<std::uint8_t>(kMagic & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((kMagic >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((kMagic >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((kMagic >> 24) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(kVersion & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((kVersion >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(plen & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((plen >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((plen >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((plen >> 24) & 0xFFu));
  out.insert(out.end(), payload.begin(), payload.end());
  const auto crc = crc32c(out.data(), out.size());
  out.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((crc >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((crc >> 24) & 0xFFu));
  return out;
}

bool decode(const std::vector<std::uint8_t>& encoded, std::vector<std::uint8_t>& payload, std::string& reason) {
  payload.clear();
  reason.clear();
  if (encoded.size() < kHeaderSize + kCrcSize) {
    reason = "truncated: envelope too short";
    return false;
  }
  const std::uint32_t magic = static_cast<std::uint32_t>(encoded[0]) |
                              (static_cast<std::uint32_t>(encoded[1]) << 8) |
                              (static_cast<std::uint32_t>(encoded[2]) << 16) |
                              (static_cast<std::uint32_t>(encoded[3]) << 24);
  if (magic != kMagic) {
    reason = "bad magic";
    return false;
  }
  const std::uint16_t version = static_cast<std::uint16_t>(encoded[4]) |
                                (static_cast<std::uint16_t>(encoded[5]) << 8);
  if (version != kVersion) {
    reason = "unknown version " + std::to_string(version);
    return false;
  }
  const std::uint32_t plen = static_cast<std::uint32_t>(encoded[6]) |
                             (static_cast<std::uint32_t>(encoded[7]) << 8) |
                             (static_cast<std::uint32_t>(encoded[8]) << 16) |
                             (static_cast<std::uint32_t>(encoded[9]) << 24);
  if (plen > kMaxPayload) {
    reason = "payload exceeds bound";
    return false;
  }
  const std::size_t expected = kHeaderSize + static_cast<std::size_t>(plen) + kCrcSize;
  if (encoded.size() < expected) {
    reason = "truncated: payload incomplete";
    return false;
  }
  if (encoded.size() != expected) {
    reason = "trailing garbage";
    return false;
  }
  const std::uint32_t stored_crc = static_cast<std::uint32_t>(encoded[expected - 4]) |
                                   (static_cast<std::uint32_t>(encoded[expected - 3]) << 8) |
                                   (static_cast<std::uint32_t>(encoded[expected - 2]) << 16) |
                                   (static_cast<std::uint32_t>(encoded[expected - 1]) << 24);
  const auto computed = crc32c(encoded.data(), expected - kCrcSize);
  if (computed != stored_crc) {
    reason = "checksum mismatch";
    return false;
  }
  payload.assign(encoded.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                 encoded.begin() + static_cast<std::ptrdiff_t>(expected - kCrcSize));
  return true;
}

}  // namespace persist
}  // namespace iobs
