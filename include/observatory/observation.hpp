#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"
#include "observatory/capability.hpp"

namespace iobs {

// Potential confounders that must be modeled explicitly. A comparison with unresolved major
// confounders must lower confidence or become INVALID/UNKNOWN.
enum class Confounder : std::uint8_t {
  ThermalThrottling,
  PowerThrottling,
  ClockChange,
  BackgroundOSLoad,
  DifferentWorkloadInput,
  DifferentKernelVersion,
  DifferentDriverRuntimeState,
  ColdStart,
  CacheWarmup,
  AllocationWarmup,
  FrequencyScaling,
  DeviceTemperature,
  MemoryPressure,
  RetryRecovery,
  WorkerRestart,
  DifferentPlacement,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(Confounder c) noexcept {
  using enum Confounder;
  switch (c) {
    case ThermalThrottling: return "THERMAL_THROTTLING";
    case PowerThrottling: return "POWER_THROTTLING";
    case ClockChange: return "CLOCK_CHANGE";
    case BackgroundOSLoad: return "BACKGROUND_OS_LOAD";
    case DifferentWorkloadInput: return "DIFFERENT_WORKLOAD_INPUT";
    case DifferentKernelVersion: return "DIFFERENT_KERNEL_VERSION";
    case DifferentDriverRuntimeState: return "DIFFERENT_DRIVER_RUNTIME_STATE";
    case ColdStart: return "COLD_START";
    case CacheWarmup: return "CACHE_WARMUP";
    case AllocationWarmup: return "ALLOCATION_WARMUP";
    case FrequencyScaling: return "FREQUENCY_SCALING";
    case DeviceTemperature: return "DEVICE_TEMPERATURE";
    case MemoryPressure: return "MEMORY_PRESSURE";
    case RetryRecovery: return "RETRY_RECOVERY";
    case WorkerRestart: return "WORKER_RESTART";
    case DifferentPlacement: return "DIFFERENT_PLACEMENT";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_CONFOUNDER";
}

// A single telemetry observation of a workload at a point in time, fully identified.
struct Observation {
  ObservationId id;
  ObservationGeneration generation;
  SourceId source_id;
  SourceGeneration source_generation;
  WorkerId worker_id;
  WorkerBootId worker_boot;
  WorkloadId workload_id;
  WorkloadGeneration workload_generation;
  DeviceId device_id;
  DeviceGeneration device_generation;
  std::int64_t timestamp_ms = 0;
  Measurement measurement;
  Provenance provenance = Provenance::Unknown;
  Freshness freshness = Freshness::Current;
  SourceHealth source_health = SourceHealth::Unknown;
  BackendCapabilities capabilities;
  std::vector<WorkloadId> co_run_peers;  // workloads observed overlapping this observation
  std::vector<Confounder> confounders;
};

}  // namespace iobs
