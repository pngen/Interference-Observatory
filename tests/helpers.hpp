#pragma once
#include "observatory/observatory.hpp"

namespace iobs {
namespace testutil {

inline WorkloadDescriptor make_workload(WorkloadId id, std::uint64_t boot = 1, std::string cfg = "cfg-v1",
                                        std::string size = "size-v1") {
  WorkloadDescriptor w;
  w.id = id;
  w.generation = WorkloadGeneration(id.value() + 1000);
  w.name = "wl-" + std::to_string(id.value());
  w.config_fingerprint = cfg;
  w.problem_size_fingerprint = size;
  w.device_id = DeviceId(11);
  w.device_generation = DeviceGeneration(33);
  w.worker_id = WorkerId(22);
  w.worker_boot = WorkerBootId(boot);
  w.policy_generation = "pol-v1";
  w.runtime_generation = "rt-v1";
  w.kernel_generation = "kernel-v1";
  w.topology_generation = TopologyGeneration(44);
  w.start_ms = 0;
  w.end_ms = 0;
  w.attempt_id = 1;
  w.priority_class = "normal";
  return w;
}

inline Baseline make_baseline(BaselineId id, WorkloadId w, double mean, std::uint64_t samples = 10,
                              std::int64_t ts = 100000, MetricKind kind = MetricKind::Latency,
                              BaselineType type = BaselineType::Isolated) {
  Baseline b;
  b.id = id;
  b.generation = BaselineGeneration(id.value() + 2000);
  b.type = type;
  b.workload_id = w;
  b.workload_generation = WorkloadGeneration(w.value() + 1000);
  b.device_id = DeviceId(11);
  b.device_generation = DeviceGeneration(33);
  b.worker_id = WorkerId(22);
  b.worker_boot = WorkerBootId(1);
  b.config_fingerprint = "cfg-v1";
  b.problem_size_fingerprint = "size-v1";
  b.runtime_generation = "rt-v1";
  b.kernel_generation = "kernel-v1";
  b.policy_generation = "pol-v1";
  b.topology_generation = TopologyGeneration(44);
  b.source_generation = SourceGeneration(55);
  b.evidence_generation = EvidenceGeneration(66);
  b.timestamp_ms = ts;
  b.interval_ms = 1000;
  b.provenance = Provenance::Controlled;
  b.freshness = Freshness::Current;
  b.sample_count = samples;
  b.variance = 1.0;
  b.confidence = 0.95;
  b.mean_value = mean;
  b.metric_kind = kind;
  b.valid = true;
  return b;
}

inline Observation make_obs(ObservationId id, WorkloadId w, double value, std::int64_t ts,
                            std::vector<WorkloadId> peers = {}, std::vector<Confounder> confs = {},
                            MetricKind kind = MetricKind::Latency) {
  Observation o;
  o.id = id;
  o.generation = ObservationGeneration(id.value() + 3000);
  o.source_id = SourceId(77);
  o.source_generation = SourceGeneration(55);
  o.worker_id = WorkerId(22);
  o.worker_boot = WorkerBootId(1);
  o.workload_id = w;
  o.workload_generation = WorkloadGeneration(w.value() + 1000);
  o.device_id = DeviceId(11);
  o.device_generation = DeviceGeneration(33);
  o.timestamp_ms = ts;
  o.measurement.kind = kind;
  o.measurement.value = value;
  o.measurement.unit = "ms";
  o.measurement.sample_count = 1;
  o.measurement.variance = 0.0;
  o.provenance = Provenance::Measured;
  o.freshness = Freshness::Current;
  o.source_health = SourceHealth::Healthy;
  o.capabilities.cuda_kernels = true;
  o.co_run_peers = std::move(peers);
  o.confounders = std::move(confs);
  return o;
}

}  // namespace testutil
}  // namespace iobs
