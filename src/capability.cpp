#include "observatory/capability.hpp"

namespace iobs {

std::vector<std::string> BackendCapabilities::available() const {
  std::vector<std::string> out;
  if (cuda_device_discovery) out.emplace_back("CUDA_DEVICE_DISCOVERY");
  if (cuda_memory_alloc) out.emplace_back("CUDA_MEMORY_ALLOC");
  if (cuda_h2d) out.emplace_back("CUDA_H2D");
  if (cuda_d2h) out.emplace_back("CUDA_D2H");
  if (cuda_kernels) out.emplace_back("CUDA_KERNELS");
  if (cuda_events) out.emplace_back("CUDA_EVENTS");
  if (cuda_memory_accounting) out.emplace_back("CUDA_MEMORY_ACCOUNTING");
  if (nvml_utilization) out.emplace_back("NVML_UTILIZATION");
  if (pcie_counters) out.emplace_back("PCIE_COUNTERS");
  if (cache_counters) out.emplace_back("CACHE_COUNTERS");
  if (memory_bandwidth_counters) out.emplace_back("MEMORY_BANDWIDTH_COUNTERS");
  if (nvlink_counters) out.emplace_back("NVLINK_COUNTERS");
  if (collective_metrics) out.emplace_back("COLLECTIVE_METRICS");
  if (numa_topology) out.emplace_back("NUMA_TOPOLOGY");
  if (storage_metrics) out.emplace_back("STORAGE_METRICS");
  if (power_clock_telemetry) out.emplace_back("POWER_CLOCK_TELEMETRY");
  if (host_cpu_metrics) out.emplace_back("HOST_CPU_METRICS");
  return out;
}

}  // namespace iobs
