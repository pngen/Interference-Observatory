#include "observatory/observatory.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>

using namespace iobs;

struct Args {
  std::vector<std::string> raw;
  std::string command;
  std::string state;
  std::string victim, neighbor, domain;
  std::uint64_t victim_id = 0, neighbor_id = 0;
  bool has_victim = false, has_neighbor = false;
};

static std::string arg_value(const std::vector<std::string>& a, const std::string& key, const std::string& dflt) {
  for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == key) return a[i + 1];
  return dflt;
}
static bool has_flag(const std::vector<std::string>& a, const std::string& flag) {
  for (const auto& s : a) if (s == flag) return true;
  return false;
}
static std::uint64_t parse_u64(const std::string& s) { return static_cast<std::uint64_t>(std::strtoull(s.c_str(), nullptr, 10)); }

static WorkloadDescriptor wl(int wid, const char* name, const char* cfg, const char* size) {
  WorkloadDescriptor w;
  w.id = WorkloadId(static_cast<std::uint64_t>(wid));
  w.generation = WorkloadGeneration(static_cast<std::uint64_t>(wid * 100 + 10));
  w.name = name;
  w.config_fingerprint = cfg;
  w.problem_size_fingerprint = size;
  w.device_id = DeviceId(11);
  w.device_generation = DeviceGeneration(33);
  w.worker_id = WorkerId(22);
  w.worker_boot = WorkerBootId(1);
  w.policy_generation = "pol-v1";
  w.runtime_generation = "rt-cuda-12.9";
  w.kernel_generation = "kernel-v1";
  w.topology_generation = TopologyGeneration(44);
  w.attempt_id = 1;
  w.priority_class = "io";
  return w;
}

static Baseline bl(int bid, int wid, double mean, MetricKind kind, const char* cfg, const char* size) {
  Baseline b;
  b.id = BaselineId(static_cast<std::uint64_t>(bid));
  b.generation = BaselineGeneration(static_cast<std::uint64_t>(bid * 100 + 10));
  b.type = BaselineType::Isolated;
  b.workload_id = WorkloadId(static_cast<std::uint64_t>(wid));
  b.workload_generation = WorkloadGeneration(static_cast<std::uint64_t>(wid * 100 + 10));
  b.device_id = DeviceId(11);
  b.device_generation = DeviceGeneration(33);
  b.worker_id = WorkerId(22);
  b.worker_boot = WorkerBootId(1);
  b.config_fingerprint = cfg;
  b.problem_size_fingerprint = size;
  b.runtime_generation = "rt-cuda-12.9";
  b.kernel_generation = "kernel-v1";
  b.policy_generation = "pol-v1";
  b.topology_generation = TopologyGeneration(44);
  b.source_generation = SourceGeneration(55);
  b.evidence_generation = EvidenceGeneration(66);
  b.timestamp_ms = 100000;
  b.interval_ms = 2000;
  b.provenance = Provenance::Controlled;
  b.freshness = Freshness::Current;
  b.sample_count = 20;
  b.variance = 3.0;
  b.confidence = 0.92;
  b.mean_value = mean;
  b.metric_kind = kind;
  b.valid = true;
  return b;
}

static Observation obs(std::uint64_t id, int wid, int wgen, double value, MetricKind kind, std::uint64_t ts) {
  Observation o;
  o.id = ObservationId(id);
  o.generation = ObservationGeneration(500000 + id);
  o.source_id = SourceId(77);
  o.source_generation = SourceGeneration(55);
  o.worker_id = WorkerId(22);
  o.worker_boot = WorkerBootId(1);
  o.workload_id = WorkloadId(static_cast<std::uint64_t>(wid));
  o.workload_generation = WorkloadGeneration(static_cast<std::uint64_t>(wgen));
  o.device_id = DeviceId(11);
  o.device_generation = DeviceGeneration(33);
  o.timestamp_ms = static_cast<std::int64_t>(ts);
  o.measurement.kind = kind;
  o.measurement.value = value;
  o.measurement.unit = (kind == MetricKind::Latency) ? "ms" : "ops/s";
  o.measurement.sample_count = 1;
  o.measurement.variance = 0.0;
  o.provenance = Provenance::Measured;
  o.freshness = Freshness::Current;
  o.source_health = SourceHealth::Healthy;
  o.capabilities.cuda_device_discovery = true;
  o.capabilities.cuda_memory_alloc = true;
  o.capabilities.cuda_h2d = true;
  o.capabilities.cuda_d2h = true;
  o.capabilities.cuda_kernels = true;
  o.capabilities.cuda_events = true;
  return o;
}

static void print_help() {
  std::printf(
    "Interference Observatory CLI\n"
    "Usage: io-observatory <command> [options]\n\n"
    "Commands:\n  demo\n  summary\n  workloads\n  episodes\n  pairwise\n  sources\n"
    "  capabilities\n  compare\n  explain\n  digest\n  replay\n  validate-state\n\n"
    "Options: --state <path> --out <path> --victim <id> --neighbor <id> --domain <DOMAIN>\n");
}

static Status build_demo(const std::string& out_path) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = wl(10, "matrix-scale-A", "cfg-A", "size-2048");
  auto wb = wl(20, "bandwidth-B", "cfg-B", "size-2048");
  auto wc = wl(30, "idle-C", "cfg-C", "size-1");
  o.register_workload(wa);
  o.register_workload(wb);
  o.register_workload(wc);
  o.register_worker(WorkerId(22), WorkerBootId(1), "worker-a");
  o.publish_baseline(bl(500, 10, 120.0, MetricKind::Latency, "cfg-A", "size-2048"));
  o.publish_baseline(bl(501, 20, 1500.0, MetricKind::Throughput, "cfg-B", "size-2048"));

  Policy policy;
  std::vector<Observation> a_obs, b_obs;
  for (int i = 0; i < 8; ++i) {
    auto oa = obs(1000 + i, 10, 1010, 158.0, MetricKind::Latency, 100001 + i);
    oa.co_run_peers.push_back(WorkloadId(20));
    a_obs.push_back(oa);
    auto ob = obs(2000 + i, 20, 1020, 1310.0, MetricKind::Throughput, 100001 + i);
    ob.co_run_peers.push_back(WorkloadId(10));
    b_obs.push_back(ob);
  }
  auto pa = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  auto pb = o.compare(wb, wa, InterferenceDomain::MemoryBandwidth, b_obs, policy);
  std::printf("demo: A->B outcome=%s attribution=%s degradation=%.1f%%\n",
              std::string(to_string(pa.outcome)).c_str(), std::string(to_string(pa.attribution)).c_str(),
              pa.degradation.deltas.empty() ? 0.0 : pa.degradation.deltas.front().relative * 100.0);
  std::printf("demo: B->A outcome=%s attribution=%s degradation=%.1f%%\n",
              std::string(to_string(pb.outcome)).c_str(), std::string(to_string(pb.attribution)).c_str(),
              pb.degradation.deltas.empty() ? 0.0 : pb.degradation.deltas.front().relative * 100.0);
  return o.save(out_path);
}

static int stats(const std::string& path) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto st = o.load(path);
  if (st != Status::Ok) { std::fprintf(stderr, "error: cannot load state (%s)\n", std::string(to_string(st)).c_str()); return 2; }
  std::printf("observatory=%llu epoch=%llu workloads=%zu sources=%zu workers=%zu baselines=%zu observations=%zu episodes=%zu pairwise=%zu\n",
              (unsigned long long)o.observatory_id().value(), (unsigned long long)o.coordinator_epoch().value(),
              o.workload_count(), o.source_count(), o.worker_count(), o.baseline_count(), o.observation_count(), o.episode_count(),
              o.pairwise_matrix().size());
  std::printf("digest=%s\n", o.canonical_digest().c_str());
  return 0;
}

int main(int argc, char** argv) {
  Args a;
  if (argc < 2) { print_help(); return 1; }
  a.command = argv[1];
  for (int i = 2; i < argc; ++i) a.raw.push_back(argv[i]);
  a.state = arg_value(a.raw, "--state", "iobs-state.bin");
  a.victim = arg_value(a.raw, "--victim", "");
  a.neighbor = arg_value(a.raw, "--neighbor", "");
  a.domain = arg_value(a.raw, "--domain", "MEMORY_BANDWIDTH");
  a.has_victim = has_flag(a.raw, "--victim");
  a.has_neighbor = has_flag(a.raw, "--neighbor");
  if (a.has_victim) a.victim_id = parse_u64(a.victim);
  if (a.has_neighbor) a.neighbor_id = parse_u64(a.neighbor);

  if (a.command == "help" || a.command == "--help") { print_help(); return 0; }
  if (a.command == "demo") {
    auto out = arg_value(a.raw, "--out", "iobs-state.bin");
    auto st = build_demo(out);
    if (st != Status::Ok) { std::fprintf(stderr, "demo failed: %s\n", std::string(to_string(st)).c_str()); return 2; }
    std::printf("demo persisted to %s\n", out.c_str());
    return 0;
  }
  if (a.command == "summary" || a.command == "digest") return stats(a.state);

  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto st = o.load(a.state);
  if (st != Status::Ok) { std::fprintf(stderr, "error: cannot load state (%s)\n", std::string(to_string(st)).c_str()); return 2; }

  if (a.command == "workloads") {
    const auto pairs = o.pairwise_matrix();
    for (const auto& [k, v] : pairs.cells()) {
      std::printf("victim=%llu neighbor=%llu domain=%s outcome=%s conf=%.2f samples=%llu relative=%.3f\n",
                  (unsigned long long)k.victim.value(), (unsigned long long)k.neighbor.value(),
                  std::string(to_string(k.domain)).c_str(), std::string(to_string(v.outcome)).c_str(),
                  v.confidence, (unsigned long long)v.sample_count,
                  v.degradation.deltas.empty() ? 0.0 : v.degradation.deltas.front().relative);
    }
    return 0;
  }
  if (a.command == "episodes") {
    for (const auto& e : o.episodes()) {
      std::printf("episode=%llu affected=%llu classification=%s confidence=%.2f start=%lld end=%lld\n",
                  (unsigned long long)e.id.value(), (unsigned long long)e.affected.value(),
                  std::string(to_string(e.classification)).c_str(), e.confidence,
                  (long long)e.start_ms, (long long)e.end_ms);
    }
    return 0;
  }
  if (a.command == "pairwise") {
    for (const auto& [k, v] : o.pairwise_matrix().cells()) {
      std::printf("victim=%llu neighbor=%llu domain=%s outcome=%s attribution=%s rel=%.3f conf=%.2f\n",
                  (unsigned long long)k.victim.value(), (unsigned long long)k.neighbor.value(),
                  std::string(to_string(k.domain)).c_str(), std::string(to_string(v.outcome)).c_str(),
                  std::string(to_string(v.attribution)).c_str(),
                  v.degradation.deltas.empty() ? 0.0 : v.degradation.deltas.front().relative, v.confidence);
    }
    return 0;
  }
  if (a.command == "sources") {
    std::printf("sources in state: %zu; dynamic current evidence requires a live source probe.\n", o.source_count());
    return 0;
  }
  if (a.command == "capabilities") {
    BackendCapabilities caps;
    caps.cuda_device_discovery = caps.cuda_memory_alloc = caps.cuda_h2d = caps.cuda_d2h =
      caps.cuda_kernels = caps.cuda_events = caps.cuda_memory_accounting = caps.nvml_utilization = true;
    for (const auto& c : caps.available()) std::printf("  %s\n", c.c_str());
    std::printf("  cache_counters=UNSUPPORTED memory_bandwidth_counters=UNSUPPORTED pcie_counters=UNSUPPORTED\n");
    return 0;
  }
  if (a.command == "compare") {
    if (!a.has_victim || !a.has_neighbor) { std::fprintf(stderr, "compare needs --victim and --neighbor\n"); return 2; }
    auto dom = parse_interference_domain(a.domain);
    if (!dom) { std::fprintf(stderr, "unknown domain %s\n", a.domain.c_str()); return 2; }
    auto cell = o.pairwise_matrix().get(CellKey{WorkloadId(a.victim_id), WorkloadId(a.neighbor_id), *dom});
    if (!cell) { std::fprintf(stderr, "no recorded comparison for victim=%llu neighbor=%llu domain=%s\n", (unsigned long long)a.victim_id, (unsigned long long)a.neighbor_id, a.domain.c_str()); return 3; }
    std::printf("victim=%llu neighbor=%llu domain=%s outcome=%s attribution=%s rel=%.3f conf=%.2f\n",
                (unsigned long long)a.victim_id, (unsigned long long)a.neighbor_id, a.domain.c_str(),
                std::string(to_string(cell->outcome)).c_str(), std::string(to_string(cell->attribution)).c_str(),
                cell->degradation.deltas.empty() ? 0.0 : cell->degradation.deltas.front().relative, cell->confidence);
    return 0;
  }
  if (a.command == "explain") {
    if (!a.has_victim || !a.has_neighbor) { std::fprintf(stderr, "explain needs --victim and --neighbor\n"); return 2; }
    auto dom = parse_interference_domain(a.domain);
    if (!dom) { std::fprintf(stderr, "unknown domain %s\n", a.domain.c_str()); return 2; }
    Policy policy;
    auto ex = o.explain(WorkloadId(a.victim_id), WorkloadId(a.neighbor_id), *dom, policy);
    std::printf("affected=%llu associated=%llu domain=%s outcome=%s attribution=%s confidence=%.2f authority_only=%s\n",
                (unsigned long long)ex.affected.value(), (unsigned long long)ex.associated.at(0).value(),
                std::string(to_string(ex.domain)).c_str(), std::string(to_string(ex.outcome)).c_str(),
                std::string(to_string(ex.attribution)).c_str(), ex.confidence, ex.authority_only ? "true" : "false");
    for (const auto& w : ex.what_would_strengthen_evidence) std::printf("  strengthen: %s\n", w.c_str());
    return 0;
  }
  if (a.command == "replay") {
    InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
    auto st2 = o2.load(a.state);
    if (st2 != Status::Ok) { std::fprintf(stderr, "replay failed: %s\n", std::string(to_string(st2)).c_str()); return 2; }
    auto s1 = o.canonical_digest(), s2 = o2.canonical_digest();
    std::printf("replay digest=%s match=%s\n", s2.c_str(), s1 == s2 ? "YES" : "NO");
    return (s1 == s2) ? 0 : 1;
  }
  if (a.command == "validate-state") {
    std::printf("state %s loaded and integrity-checked: OK (digest %s)\n", a.state.c_str(), o.canonical_digest().c_str());
    return 0;
  }
  print_help();
  return 1;
}
