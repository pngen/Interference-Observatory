#pragma once
#include <cstdint>
#include <string_view>
#include "observatory/checked.hpp"
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"

namespace iobs {

enum class BaselineType : std::uint8_t {
  Isolated,
  HistoricalIsolated,
  ControlRun,
  RecentHealthy,
  ConfiguredReference,
  SyntheticReference
};

[[nodiscard]] constexpr std::string_view to_string(BaselineType t) noexcept {
  using enum BaselineType;
  switch (t) {
    case Isolated: return "ISOLATED";
    case HistoricalIsolated: return "HISTORICAL_ISOLATED";
    case ControlRun: return "CONTROL_RUN";
    case RecentHealthy: return "RECENT_HEALTHY";
    case ConfiguredReference: return "CONFIGURED_REFERENCE";
    case SyntheticReference: return "SYNTHETIC_REFERENCE";
  }
  return "UNKNOWN_BASELINE";
}

// Outcome of evaluating a baseline's fitness for a current comparison.
enum class BaselineFit : std::uint8_t {
  Compatible,
  Incompatible,      // wrong generation/shape
  Stale,             // compatible identity but not fresh
  Expired,
  Missing,
  Invalid            // malformed baseline
};

[[nodiscard]] constexpr std::string_view to_string(BaselineFit f) noexcept {
  using enum BaselineFit;
  switch (f) {
    case Compatible: return "COMPATIBLE";
    case Incompatible: return "INCOMPATIBLE";
    case Stale: return "STALE";
    case Expired: return "EXPIRED";
    case Missing: return "MISSING";
    case Invalid: return "INVALID";
  }
  return "UNKNOWN_FIT";
}

// A baseline is a first-class proof obligation binding a reference measurement to the exact
// identity of the workload, device, worker boot, and configuration it was taken under.
struct Baseline {
  BaselineId id;
  BaselineGeneration generation;
  BaselineType type = BaselineType::Isolated;
  WorkloadId workload_id;
  WorkloadGeneration workload_generation;
  DeviceId device_id;
  DeviceGeneration device_generation;
  WorkerId worker_id;
  WorkerBootId worker_boot;
  std::string config_fingerprint;
  std::string problem_size_fingerprint;
  std::string runtime_generation;
  std::string kernel_generation;
  std::string policy_generation;
  TopologyGeneration topology_generation;
  SourceGeneration source_generation;
  EvidenceGeneration evidence_generation;
  std::int64_t timestamp_ms = 0;   // reference time (end of measurement)
  std::int64_t interval_ms = 0;
  Provenance provenance = Provenance::Controlled;
  Freshness freshness = Freshness::Current;
  std::uint64_t sample_count = 0;
  double variance = 0.0;           // dispersion of the reference samples
  double confidence = 1.0;         // [0,1] confidence in the reference measurement
  double mean_value = 0.0;         // central reference value (natural unit)
  MetricKind metric_kind = MetricKind::Latency;
  bool valid = true;

  // Identity-binding fields that must match a candidate observation for the baseline to be usable.
  struct Binding {
    WorkloadGeneration workload_generation;
    DeviceGeneration device_generation;
    WorkerBootId worker_boot;
    std::string config_fingerprint;
    std::string problem_size_fingerprint;
    std::string runtime_generation;
    std::string kernel_generation;
    std::string policy_generation;
    TopologyGeneration topology_generation;
  };

  [[nodiscard]] Binding binding() const noexcept {
    return Binding{workload_generation, device_generation, worker_boot, config_fingerprint,
                   problem_size_fingerprint, runtime_generation, kernel_generation,
                   policy_generation, topology_generation};
  }

  [[nodiscard]] bool binding_matches(const Binding& b) const noexcept {
    return workload_generation == b.workload_generation &&
           device_generation == b.device_generation &&
           worker_boot == b.worker_boot &&
           config_fingerprint == b.config_fingerprint &&
           problem_size_fingerprint == b.problem_size_fingerprint &&
           runtime_generation == b.runtime_generation &&
           kernel_generation == b.kernel_generation &&
           policy_generation == b.policy_generation &&
           topology_generation == b.topology_generation;
  }

  [[nodiscard]] bool is_usable() const noexcept {
    return valid && sample_count > 0 && confidence > 0.0 && checked::is_finite_nonneg(mean_value);
  }
};

}  // namespace iobs
