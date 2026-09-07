#pragma once
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace iobs {
namespace checked {

// Checked unsigned 64-bit arithmetic: returns false on overflow and does not write out.

[[nodiscard]] inline bool add_u64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
  out = a + b;
  return true;
}

[[nodiscard]] inline bool mul_u64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
  out = a * b;
  return true;
}

[[nodiscard]] inline bool sub_u64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b > a) return false;
  out = a - b;
  return true;
}

[[nodiscard]] inline bool is_finite(double v) noexcept { return std::isfinite(v); }
[[nodiscard]] inline bool is_finite_nonneg(double v) noexcept { return std::isfinite(v) && v >= 0.0; }
[[nodiscard]] inline bool is_finite_signed(double v) noexcept { return std::isfinite(v); }

// Convenience helpers that throw on overflow. Use only where an error is tolerable.
[[nodiscard]] inline std::uint64_t add_u64_throwing(std::uint64_t a, std::uint64_t b) {
  std::uint64_t out = 0;
  if (!add_u64(a, b, out)) throw std::overflow_error("iobs checked add overflow");
  return out;
}

[[nodiscard]] inline std::uint64_t mul_u64_throwing(std::uint64_t a, std::uint64_t b) {
  std::uint64_t out = 0;
  if (!mul_u64(a, b, out)) throw std::overflow_error("iobs checked mul overflow");
  return out;
}

}  // namespace checked
}  // namespace iobs
