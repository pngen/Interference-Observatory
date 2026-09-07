#include "observatory/observatory.hpp"
#include "observatory/checked.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace iobs;
using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static WorkloadDescriptor wl(WorkloadId id) {
  WorkloadDescriptor w;
  w.id = id; w.generation = WorkloadGeneration(id.value() + 1000);
  w.config_fingerprint = "cfg"; w.problem_size_fingerprint = "size";
  w.device_id = DeviceId(11); w.device_generation = DeviceGeneration(33); w.worker_id = WorkerId(22);
  w.worker_boot = WorkerBootId(1); w.policy_generation = "pol"; w.runtime_generation = "rt";
  w.kernel_generation = "k"; w.topology_generation = TopologyGeneration(44); w.attempt_id = 1;
  return w;
}
static Observation obs(ObservationId id) {
  Observation o;
  o.id = id; o.generation = ObservationGeneration(id.value() + 3000); o.source_id = SourceId(77);
  o.source_generation = SourceGeneration(55); o.worker_id = WorkerId(22); o.worker_boot = WorkerBootId(1);
  o.workload_id = WorkloadId(10); o.workload_generation = WorkloadGeneration(1010);
  o.device_id = DeviceId(11); o.device_generation = DeviceGeneration(33); o.timestamp_ms = 100001;
  o.measurement.kind = MetricKind::Latency; o.measurement.value = 130.0; o.measurement.unit = "ms";
  o.measurement.sample_count = 1; o.provenance = Provenance::Measured; o.freshness = Freshness::Current;
  o.source_health = SourceHealth::Healthy; o.co_run_peers.push_back(WorkloadId(20));
  return o;
}

int main() {
  const std::size_t sizes[] = {100, 1000, 10000, 100000};
  std::printf("%-12s %-12s %-12s %-12s %-12s\n", "obs", "ingest_ms", "ingest/s", "save_ms", "load_ms");
  double cuda_first = -1;
  for (std::size_t n : sizes) {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = wl(WorkloadId(10)); auto wb = wl(WorkloadId(20));
    o.register_workload(wa); o.register_workload(wb);
    o.publish_baseline(Baseline{BaselineId(1), BaselineGeneration(2100), BaselineType::Isolated, WorkloadId(10),
      WorkloadGeneration(1010), DeviceId(11), DeviceGeneration(33), WorkerId(22), WorkerBootId(1),
      "cfg", "size", "rt", "k", "pol", TopologyGeneration(44), SourceGeneration(55), EvidenceGeneration(66),
      100000, 1000, Provenance::Controlled, Freshness::Current, 10, 2.0, 0.9, 100.0, MetricKind::Latency, true});

    auto t0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) o.record_observation(obs(ObservationId(i + 1)));
    auto ingest_ms = ms_since(t0);

    // Comparison over the whole set.
    auto tc = Clock::now();
    Policy policy;
    std::vector<Observation> all(n);
    // observations_for returns all for workload 10; reuse to drive comparison.
    all = o.observations_for(WorkloadId(10));
    o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, all, policy);
    auto compare_ms = ms_since(tc);

    // Persistence save/load.
    auto ts = Clock::now();
    std::string path = "bench_" + std::to_string(n) + ".bin";
    o.save(path);
    auto save_ms = ms_since(ts);
    InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
    auto tl = Clock::now();
    o2.load(path);
    auto load_ms = ms_since(tl);

    // Replay / pairwise matrix.
    auto m = o2.pairwise_matrix();
    auto digest = o2.canonical_digest();

    std::printf("%-12zu %-12.2f %-12.0f %-12.2f %-12.2f\n", n, ingest_ms, n / (ingest_ms / 1000.0), save_ms, load_ms);
    (void)cuda_first; (void)compare_ms; (void)m; (void)digest;
  }
  std::printf("Benchmark completed (completed work = observation ingest/s, save/load timing).\n");
  return 0;
}
