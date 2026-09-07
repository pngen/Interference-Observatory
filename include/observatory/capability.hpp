#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace iobs {

// Which measurements are actually available on the current platform/backend. Unsupported metrics
// remain UNSUPPORTED; the engine never fabricates a counter it does not have.
struct BackendCapabilities {
  bool cuda_device_discovery = false;
  bool cuda_memory_alloc = false;
  bool cuda_h2d = false;
  bool cuda_d2h = false;
  bool cuda_kernels = false;
  bool cuda_events = false;            // CUDA events / synchronization timing
  bool cuda_memory_accounting = false; // device memory accounting back to baseline
  bool nvml_utilization = false;
  bool pcie_counters = false;
  bool cache_counters = false;
  bool memory_bandwidth_counters = false;
  bool nvlink_counters = false;
  bool collective_metrics = false;
  bool numa_topology = false;
  bool storage_metrics = false;
  bool power_clock_telemetry = false;
  bool host_cpu_metrics = false;

  [[nodiscard]] bool has_cuda() const noexcept {
    return cuda_device_discovery && cuda_memory_alloc && cuda_h2d && cuda_d2h &&
           cuda_kernels && cuda_events;
  }

  // A canonical, deterministic capability list string.
  [[nodiscard]] std::vector<std::string> available() const;
};

}  // namespace iobs
