#pragma once
#include <cstdint>
#include <string>
#include "observatory/ids.hpp"
#include "observatory/baseline.hpp"

namespace iobs {

// A workload carries enough identity to establish comparability across runs. Two semantic
// incarnations (e.g. changed config or problem size) are never silently compared.
struct WorkloadDescriptor {
  WorkloadId id;
  WorkloadGeneration generation;
  std::string name;
  std::string config_fingerprint;       // deterministic configuration hash
  std::string problem_size_fingerprint; // deterministic input/problem-size hash
  DeviceId device_id;
  DeviceGeneration device_generation;
  WorkerId worker_id;
  WorkerBootId worker_boot;
  std::string policy_generation;
  std::string runtime_generation;
  std::string kernel_generation;
  TopologyGeneration topology_generation;
  std::int64_t start_ms = 0;            // interval of this incarnation
  std::int64_t end_ms = 0;
  std::uint64_t attempt_id = 0;
  std::string priority_class;

  // Compatibility: two workloads are comparable iff their semantic fingerprints and platform
  // bindings match, irrespective of id/attempt/interval.
  [[nodiscard]] bool compatible(const WorkloadDescriptor& other) const noexcept {
    return generation == other.generation &&
           config_fingerprint == other.config_fingerprint &&
           problem_size_fingerprint == other.problem_size_fingerprint &&
           device_id == other.device_id &&
           device_generation == other.device_generation &&
           worker_boot == other.worker_boot &&
           policy_generation == other.policy_generation &&
           runtime_generation == other.runtime_generation &&
           kernel_generation == other.kernel_generation &&
           topology_generation == other.topology_generation;
  }

  // The identity binding a baseline must match for a comparison to be valid.
  [[nodiscard]] Baseline::Binding binding() const noexcept {
    return Baseline::Binding{generation, device_generation, worker_boot, config_fingerprint,
                             problem_size_fingerprint, runtime_generation, kernel_generation,
                             policy_generation, topology_generation};
  }
};

}  // namespace iobs
