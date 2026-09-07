#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace iobs {

// Incremental canonical-digest builder. Excludes nondeterministic state (addresses, unordered
// iteration, ephemeral sockets, wall-clock noise). Same semantic byte stream -> same digest.
// Uses FNV-1a with length-delimited encoding so concatenations cannot be ambiguous.
class DigestBuilder {
 public:
  DigestBuilder() = default;

  void add_u8(std::uint8_t v) { hash_ ^= v; hash_ *= kPrime; }
  void add_u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) { hash_ ^= static_cast<std::uint8_t>(v & 0xFFu); hash_ *= kPrime; v >>= 8; }
  }
  void add_i64(std::int64_t v) { add_u64(static_cast<std::uint64_t>(v)); }
  void add_bool(bool v) { add_u8(v ? 1 : 0); }

  void add_double(double v) {
    std::uint64_t bits = 0;
    static_assert(sizeof(v) == sizeof(bits), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    add_u64(bits);
  }

  void add_str(std::string_view s) {
    add_u64(static_cast<std::uint64_t>(s.size()));
    for (char c : s) add_u8(static_cast<std::uint8_t>(c));
  }

  template <typename IdType>
  void add_id(const IdType& id) { add_u64(id.value()); }

  template <typename Enum>
  void add_enum(Enum e) { add_u8(static_cast<std::uint8_t>(e)); }

  void add_bytes(const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    add_u64(static_cast<std::uint64_t>(n));
    for (std::size_t i = 0; i < n; ++i) add_u8(p[i]);
  }

  // Raw 64-bit digest value (for constructing content-derived identities).
  [[nodiscard]] std::uint64_t finish_u64() const noexcept { return hash_; }

  // Hex-encoded 64-bit digest.
  [[nodiscard]] std::string finish() const {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (int i = 7; i >= 0; --i) {
      const auto b = static_cast<std::uint8_t>((hash_ >> (i * 8)) & 0xFFu);
      out.push_back(hex[b >> 4]);
      out.push_back(hex[b & 0xFu]);
    }
    return out;
  }

 private:
  static constexpr std::uint64_t kPrime = 0x100000001b3ULL;
  std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

}  // namespace iobs
