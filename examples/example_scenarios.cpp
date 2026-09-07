#include "observatory/observatory.hpp"
#include <cstdio>
#include <string>

using namespace iobs;

// Run a set of documented interference scenarios and print each conclusion.

static WorkloadDescriptor workload(WorkloadId id) {
  WorkloadDescriptor w;
  w.id = id; w.generation = WorkloadGeneration(id.value() + 1000); w.name = "wl-" + std::to_string(id.value());
  w.config_fingerprint = "cfg-v1"; w.problem_size_fingerprint = "size-2048";
  w.device_id = DeviceId(11); w.device_generation = DeviceGeneration(33); w.worker_id = WorkerId(22);
  w.worker_boot = WorkerBootId(1); w.policy_generation = "pol-v1"; w.runtime_generation = "rt-v1";
  w.kernel_generation = "kernel-v1"; w.topology_generation = TopologyGeneration(44);
  w.attempt_id = 1; w.priority_class = "io"; return w;
}

static Baseline baseline(BaselineId id, WorkloadId w, double mean) {
  Baseline b;
  b.id = id; b.generation = BaselineGeneration(id.value() + 2000); b.type = BaselineType::Isolated;
  b.workload_id = w; b.workload_generation = WorkloadGeneration(w.value() + 1000);
  b.device_id = DeviceId(11); b.device_generation = DeviceGeneration(33); b.worker_id = WorkerId(22);
  b.worker_boot = WorkerBootId(1); b.config_fingerprint = "cfg-v1"; b.problem_size_fingerprint = "size-2048";
  b.runtime_generation = "rt-v1"; b.kernel_generation = "kernel-v1"; b.policy_generation = "pol-v1";
  b.topology_generation = TopologyGeneration(44); b.source_generation = SourceGeneration(55);
  b.evidence_generation = EvidenceGeneration(66); b.timestamp_ms = 100000; b.interval_ms = 1000;
  b.provenance = Provenance::Controlled; b.freshness = Freshness::Current; b.sample_count = 10;
  b.variance = 2.0; b.confidence = 0.9; b.mean_value = mean; b.metric_kind = MetricKind::Latency; b.valid = true;
  return b;
}

static Observation observation(ObservationId id, WorkloadId w, double value, std::uint64_t ts,
                               std::vector<WorkloadId> peers, std::vector<Confounder> confs) {
  Observation o;
  o.id = id; o.generation = ObservationGeneration(id.value() + 3000); o.source_id = SourceId(77);
  o.source_generation = SourceGeneration(55); o.worker_id = WorkerId(22); o.worker_boot = WorkerBootId(1);
  o.workload_id = w; o.workload_generation = WorkloadGeneration(w.value() + 1000);
  o.device_id = DeviceId(11); o.device_generation = DeviceGeneration(33); o.timestamp_ms = static_cast<std::int64_t>(ts);
  o.measurement.kind = MetricKind::Latency; o.measurement.value = value; o.measurement.unit = "ms";
  o.measurement.sample_count = 1; o.measurement.variance = 0.0; o.provenance = Provenance::Measured;
  o.freshness = Freshness::Current; o.source_health = SourceHealth::Healthy;
  o.capabilities.cuda_kernels = true; o.co_run_peers = std::move(peers); o.confounders = std::move(confs);
  return o;
}

int main() {
  Policy policy;

  // Example 1: isolated baseline + pairwise directional slowdown (asymmetric).
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    o.publish_baseline(baseline(BaselineId(2), WorkloadId(20), 100.0));
    std::vector<Observation> a, b;
    for (int i = 0; i < 5; ++i) { a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100001 + i, {WorkloadId(20)}, {})); b.push_back(observation(ObservationId(20 + i), WorkloadId(20), 103.0, 100001 + i, {WorkloadId(10)}, {})); }
    auto ab = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    auto ba = o.compare(wb, wa, InterferenceDomain::MemoryBandwidth, b, policy);
    std::printf("\n[example 1] asymmetric pairwise\n  A->B: %s (%.1f%%)  B->A: %s (%.1f%%)\n",
                std::string(to_string(ab.outcome)).c_str(), ab.degradation.deltas.empty()?0.0:ab.degradation.deltas.front().relative*100,
                std::string(to_string(ba.outcome)).c_str(), ba.degradation.deltas.empty()?0.0:ba.degradation.deltas.front().relative*100);
  }

  // Example 2: no-interference case.
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    std::vector<Observation> a;
    for (int i = 0; i < 5; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 100.5, 100001 + i, {WorkloadId(20)}, {}));
    auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 2] no interference: outcome=%s (neighbor present, no measurable effect)\n", std::string(to_string(p.outcome)).c_str());
  }

  // Example 3: insufficient evidence (too few samples).
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    std::vector<Observation> a;
    for (int i = 0; i < 1; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100001 + i, {WorkloadId(20)}, {}));
    auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 3] insufficient evidence: outcome=%s (samples=%llu)\n", std::string(to_string(p.outcome)).c_str(), (unsigned long long)p.sample_count);
  }

  // Example 4: confounded comparison (thermal throttle).
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    std::vector<Observation> a;
    for (int i = 0; i < 5; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100001 + i, {WorkloadId(20)}, {Confounder::ThermalThrottling}));
    auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 4] confounded: outcome=%s attribution=%s confounded=%s confidence=%.2f (thermal throttle confounder)\n",
                std::string(to_string(p.outcome)).c_str(), std::string(to_string(p.attribution)).c_str(), p.confounded?"yes":"no", p.confidence);
  }

  // Example 5: stale baseline -> revalidation required.
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    auto bl = baseline(BaselineId(1), WorkloadId(10), 100.0);
    bl.timestamp_ms = 100000;
    o.publish_baseline(bl);
    std::vector<Observation> a;
    for (int i = 0; i < 5; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100000 + policy.max_age_ms + 5000, {WorkloadId(20)}, {}));
    auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 5] stale baseline: outcome=%s (baseline expired; current evidence requires revalidation)\n", std::string(to_string(p.outcome)).c_str());
  }

  // Example 6: historical replay + stable digest.
  {
    InterferenceObservatory o1(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o1.register_workload(wa); o1.register_workload(wb);
    o1.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    std::vector<Observation> a;
    for (int i = 0; i < 5; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100001 + i, {WorkloadId(20)}, {}));
    o1.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
    // Rebuild the same history deterministically.
    o2.register_workload(wa); o2.register_workload(wb);
    o2.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    o2.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 6] historical replay: digest=%s match=%s\n", o1.canonical_digest().c_str(), o1.canonical_digest()==o2.canonical_digest()?"YES":"NO");
  }

  // Example 7: worker restart -> stale boot authority.
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.register_worker(WorkerId(22), WorkerBootId(1), "w");
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));  // bound to boot 1
    auto wa2 = wa; wa2.worker_boot = WorkerBootId(999);  // fresh incarnation
    std::vector<Observation> a;
    for (int i = 0; i < 5; ++i) a.push_back(observation(ObservationId(10 + i), WorkloadId(10), 135.0, 100001 + i, {WorkloadId(20)}, {}));
    auto p = o.compare(wa2, wb, InterferenceDomain::MemoryBandwidth, a, policy);
    std::printf("\n[example 7] worker restart: outcome=%s (baseline bound to old boot; must reject)\n", std::string(to_string(p.outcome)).c_str());
  }

  // Example 8: coordinator restart -> dynamic revalidation.
  {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = workload(WorkloadId(10)); auto wb = workload(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(baseline(BaselineId(1), WorkloadId(10), 100.0));
    o.set_coordinator_epoch(CoordinatorEpoch(2));
    auto bl = o.lookup_baseline(BaselineId(1));
    std::printf("\n[example 8] coordinator restart: epoch=%llu baseline freshness=%s (requires revalidation)\n",
                (unsigned long long)o.coordinator_epoch().value(), std::string(to_string(bl->freshness)).c_str());
  }

  std::printf("\nAll examples completed.\n");
  return 0;
}
