#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include "observatory/status.hpp"

namespace iobs {
namespace persist {

// Versioned, integrity-checked envelope. Layout:
//   [0..3)   magic 'IOB' = 0x495f4f42
//   [4..5)   version u16
//   [6..9)   payload length u32 (bounded)
//   [9..9+len) payload
//   [9+len..9+len+4) CRC-32C over the previous bytes
inline constexpr std::uint32_t kMagic = 0x495f4f42u;   // "IOB"
inline constexpr std::uint16_t kVersion = 1u;
inline constexpr std::uint32_t kMaxPayload = 256u * 1024u * 1024u;  // 256 MiB bound
inline constexpr std::size_t kHeaderSize = 4 + 2 + 4;  // magic(4) + version(2) + payload length(4)
inline constexpr std::size_t kCrcSize = 4;

// CRC-32C (Castagnoli).
std::uint32_t crc32c(const void* data, std::size_t len) noexcept;

// Encode payload into the versioned envelope. Throws Error on bound violation.
std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& payload);

// Validate and decode. Returns false (and clears payload) if the envelope is corrupt, truncated,
// has trailing garbage, or uses an unknown version.
bool decode(const std::vector<std::uint8_t>& encoded, std::vector<std::uint8_t>& payload, std::string& reason);

}  // namespace persist
}  // namespace iobs
