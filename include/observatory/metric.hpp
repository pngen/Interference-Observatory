#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <cmath>
#include "observatory/checked.hpp"
#include "observatory/domain.hpp"
#include "observatory/status.hpp"

namespace iobs {

// Which scalar quantity a measurement describes.
enum class MetricKind : std::uint8_t {
  Latency,
  Throughput,
  DeviceTime,
  TransferTime,
  CollectiveTime,
  StallTime,
  MemoryBandwidth,
  TailLatency,
  CompletionRate
};

[[nodiscard]] constexpr std::string_view to_string(MetricKind k) noexcept {
  using enum MetricKind;
  switch (k) {
    case Latency: return "LATENCY";
    case Throughput: return "THROUGHPUT";
    case DeviceTime: return "DEVICE_TIME";
    case TransferTime: return "TRANSFER_TIME";
    case CollectiveTime: return "COLLECTIVE_TIME";
    case StallTime: return "STALL_TIME";
    case MemoryBandwidth: return "MEMORY_BANDWIDTH";
    case TailLatency: return "TAIL_LATENCY";
    case CompletionRate: return "COMPLETION_RATE";
  }
  return "UNKNOWN_METRIC";
}

// A single validated measurement. All fields are bounded; invalid values are rejected at input.
struct Measurement {
  MetricKind kind = MetricKind::Latency;
  double value = 0.0;                 // in the metric's natural unit
  std::string unit;                   // e.g. "ms", "ops/s", "MiB/s"
  std::uint64_t sample_count = 1;     // number of samples aggregated into this measurement
  double variance = 0.0;              // sample variance across the aggregated samples
  double p95 = 0.0;                   // optional p95; 0 means not supplied
  bool has_p95 = false;

  [[nodiscard]] bool is_valid() const noexcept {
    if (!checked::is_finite_nonneg(value)) return false;
    if (sample_count == 0) return false;
    if (!checked::is_finite_nonneg(variance)) return false;
    return true;
  }
};

constexpr std::string_view metric_natural_unit(MetricKind k) noexcept {
  using enum MetricKind;
  switch (k) {
    case Latency: case DeviceTime: case TransferTime: case CollectiveTime:
    case StallTime: case TailLatency: return "ms";
    case Throughput: return "ops/s";
    case MemoryBandwidth: return "MiB/s";
    case CompletionRate: return "work/s";
  }
  return "";
}

// A typed displacement for one metric, expressed both in absolute terms (natural unit)
// and as a signed relative ratio (0.31 means +31%).
struct MetricDelta {
  MetricKind kind;
  double absolute = 0.0;   // change in natural unit
  double relative = 0.0;   // signed ratio delta (negative for "loss" of a higher-is-better metric)
  std::uint64_t sample_count = 0;
  double variance = 0.0;

  friend bool operator==(const MetricDelta& a, const MetricDelta& b) {
    return a.kind == b.kind && a.absolute == b.absolute && a.relative == b.relative &&
           a.sample_count == b.sample_count;
  }
};

// The observed degradation of one workload relative to a baseline. Never a single opaque score;
// it is a decomposable vector of typed deltas plus the sample discipline needed to trust them.
struct Degradation {
  std::vector<MetricDelta> deltas;
  std::uint64_t sample_count = 0;
  double worst_relative = 0.0;  // signed magnitude of the worst (most negative) delta
  bool empty() const noexcept { return deltas.empty(); }

  [[nodiscard]] std::optional<MetricDelta> find(MetricKind k) const noexcept {
    for (const auto& d : deltas) {
      if (d.kind == k) return d;
    }
    return std::nullopt;
  }
};

}  // namespace iobs
