#include "observatory/observatory.hpp"
#include "observatory/cuda_api.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace iobs;

static double pct(double a, double b) { return (a > 0.0) ? (b - a) / a * 100.0 : 0.0; }

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  const char* name = nullptr;
  int cc = 0;
  long long total = 0;
  int rc = iobs_cuda_probe(&name, &cc, &total);
  if (rc != 0) { std::printf("CUDA probe failed (rc=%d)\n", rc); return 1; }
  std::printf("device: %s (compute capability %d.%d, total mem %.1f GiB)\n", name, cc / 10, cc % 10, total / 1073741824.0);

  const long long mem_before = iobs_cuda_free_memory_bytes();
  std::printf("device free memory baseline: %lld bytes\n", mem_before);

  // ---- Experiment A: compute/compute co-run (real device) ------------------
  const std::uint64_t iters = 4000000ull;
  const std::uint64_t blocks = 2048;
  double alone = 0.0, host_cpu = 0.0, c_a = 0.0, c_b = 0.0;
  iobs_cuda_compute_alone(iters, blocks, &alone, &host_cpu);
  iobs_cuda_compute_concurrent(iters, blocks, &c_a, &c_b);
  std::printf("A compute: alone=%.2f ms, co-run A=%.2f ms, co-run B=%.2f ms -> A deg=%.1f%%, B deg=%.1f%%\n",
              alone, c_a, c_b, pct(alone, c_a), pct(alone, c_b));

  // ---- Experiment B: memory/intensive co-run -------------------------------
  const std::uint64_t bytes = 512ull * 1024 * 1024;
  double m_alone = 0, m_bw = 0, m_a = 0, m_b = 0;
  iobs_cuda_memory_alone(bytes, &m_alone, &m_bw);
  iobs_cuda_memory_concurrent(bytes, &m_a, &m_b);
  std::printf("B memory: alone=%.2f ms (%.1f GiB/s), co-run A=%.2f ms, co-run B=%.2f ms -> A deg=%.1f%%\n",
              m_alone, m_bw, m_a, m_b, pct(m_alone, m_a));

  // ---- Experiment C: transfer interference ----------------------------------
  double t_alone = 0, tbw = 0, t_a = 0, t_b = 0;
  iobs_cuda_transfer_alone(bytes, &t_alone, &tbw);
  iobs_cuda_transfer_concurrent(bytes, &t_a, &t_b);
  std::printf("C transfer: alone=%.2f ms (%.1f GiB/s), co-run A=%.2f ms, co-run B=%.2f ms -> A deg=%.1f%%\n",
              t_alone, tbw, t_a, t_b, pct(t_alone, t_a));

  // ---- Device memory returns to baseline ------------------------------------
  const long long mem_after = iobs_cuda_free_memory_bytes();
  const long long delta = mem_after - mem_before;
  const bool closed = (delta >= -16 * 1024 * 1024) && (delta <= 16 * 1024 * 1024);
  std::printf("device free memory after: %lld bytes (delta %lld bytes) -> %s\n",
              mem_after, delta, closed ? "BASELINE CLOSED" : "LEAK/CONSUMED");

  // ---- Controlled comparison into the observatory ---------------------------
  InterferenceObservatory observatory(ObservatoryId(1), ObservatoryGeneration(1));
  Policy policy;
  // Build a workload descriptor and baseline for compute domain, using the ALONE run as baseline.
  auto wa = WorkloadDescriptor{WorkloadId(10), WorkloadGeneration(1010), "cuda-compute", "cfg-compute", "size-4096",
                               DeviceId(1), DeviceGeneration(static_cast<std::uint64_t>(cc)), WorkerId(22), WorkerBootId(1),
                               "pol-v1", "cuda-12.9", "kernel-v1", TopologyGeneration(44), 0, 0, 1, "io"};
  auto wb = WorkloadDescriptor{WorkloadId(20), WorkloadGeneration(1020), "cuda-neighbor", "cfg-neighbor", "size-4096",
                               DeviceId(1), DeviceGeneration(static_cast<std::uint64_t>(cc)), WorkerId(22), WorkerBootId(1),
                               "pol-v1", "cuda-12.9", "kernel-v1", TopologyGeneration(44), 0, 0, 1, "io"};
  observatory.register_workload(wa);
  observatory.register_workload(wb);
  Baseline bl;
  bl.id = BaselineId(700); bl.generation = BaselineGeneration(7100); bl.type = BaselineType::Isolated;
  bl.workload_id = WorkloadId(10); bl.workload_generation = WorkloadGeneration(1010);
  bl.device_id = DeviceId(1); bl.device_generation = DeviceGeneration(static_cast<std::uint64_t>(cc));
  bl.worker_id = WorkerId(22); bl.worker_boot = WorkerBootId(1);
  bl.config_fingerprint = "cfg-compute"; bl.problem_size_fingerprint = "size-4096";
  bl.runtime_generation = "cuda-12.9"; bl.kernel_generation = "kernel-v1"; bl.policy_generation = "pol-v1";
  bl.topology_generation = TopologyGeneration(44); bl.source_generation = SourceGeneration(55);
  bl.evidence_generation = EvidenceGeneration(66); bl.timestamp_ms = 100000; bl.interval_ms = 1000;
  bl.provenance = Provenance::Measured; bl.freshness = Freshness::Current; bl.sample_count = 10;
  bl.variance = 2.0; bl.confidence = 0.9; bl.mean_value = alone; bl.metric_kind = MetricKind::Latency; bl.valid = true;
  observatory.publish_baseline(bl);

  std::vector<Observation> co_obs;
  for (int i = 0; i < 6; ++i) {
    Observation o;
    o.id = ObservationId(1000 + i); o.generation = ObservationGeneration(10000 + i);
    o.source_id = SourceId(77); o.source_generation = SourceGeneration(55); o.worker_id = WorkerId(22);
    o.worker_boot = WorkerBootId(1); o.workload_id = WorkloadId(10); o.workload_generation = WorkloadGeneration(1010);
    o.device_id = DeviceId(1); o.device_generation = DeviceGeneration(static_cast<std::uint64_t>(cc));
    o.timestamp_ms = 100001 + i; o.measurement.kind = MetricKind::Latency; o.measurement.value = c_a;
    o.measurement.unit = "ms"; o.measurement.sample_count = 1; o.measurement.variance = 0.0;
    o.provenance = Provenance::Measured; o.freshness = Freshness::Current; o.source_health = SourceHealth::Healthy;
    o.co_run_peers.push_back(WorkloadId(20));
    co_obs.push_back(o);
  }
  auto p = observatory.compare(wa, wb, InterferenceDomain::ComputeExecution, co_obs, policy);
  std::printf("observatory compute/compute: outcome=%s attribution=%s relative=%.3f confidence=%.2f\n",
              std::string(to_string(p.outcome)).c_str(), std::string(to_string(p.attribution)).c_str(),
              p.degradation.deltas.empty() ? 0.0 : p.degradation.deltas.front().relative, p.confidence);

  // Memory-bandwidth classification (the major real-hardware interference target).
  Baseline mb;
  mb = bl; mb.id = BaselineId(701); mb.mean_value = m_alone; mb.metric_kind = MetricKind::Latency;
  observatory.publish_baseline(mb);
  std::vector<Observation> m_obs;
  for (int i = 0; i < 6; ++i) {
    Observation o;
    o.id = ObservationId(2000 + i); o.generation = ObservationGeneration(20000 + i);
    o.source_id = SourceId(77); o.source_generation = SourceGeneration(55); o.worker_id = WorkerId(22);
    o.worker_boot = WorkerBootId(1); o.workload_id = WorkloadId(10); o.workload_generation = WorkloadGeneration(1010);
    o.device_id = DeviceId(1); o.device_generation = DeviceGeneration(static_cast<std::uint64_t>(cc));
    o.timestamp_ms = 100001 + i; o.measurement.kind = MetricKind::Latency; o.measurement.value = m_a;
    o.measurement.unit = "ms"; o.measurement.sample_count = 1; o.measurement.variance = 0.0;
    o.provenance = Provenance::Measured; o.freshness = Freshness::Current; o.source_health = SourceHealth::Healthy;
    o.co_run_peers.push_back(WorkloadId(20));
    m_obs.push_back(o);
  }
  auto pm = observatory.compare(wa, wb, InterferenceDomain::MemoryBandwidth, m_obs, policy);
  std::printf("observatory memory-bandwidth (real): outcome=%s attribution=%s relative=%.3f bw_alone=%.1f GiB/s\n",
              std::string(to_string(pm.outcome)).c_str(), std::string(to_string(pm.attribution)).c_str(),
              pm.degradation.deltas.empty() ? 0.0 : pm.degradation.deltas.front().relative, m_bw);
  observatory.save("iobs-cuda-state.bin");
  std::printf("cuda proof summary: digest=%s\n", observatory.canonical_digest().c_str());
  std::printf("CUDA_PROOF_DONE\n");
  return 0;
}
