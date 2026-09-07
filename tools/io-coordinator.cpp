#include "messages.hpp"
#include "observatory/protocol.hpp"
#include "observatory/observatory.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <signal.h>
#endif

using namespace iobs;

struct WorkerProc {
#ifdef _WIN32
  HANDLE h = nullptr;
  PROCESS_INFORMATION pi{};
#else
  pid_t pid = -1;
#endif
  std::int64_t sock = -1;
  std::uint64_t id = 0;
  std::uint64_t boot = 0;
  bool connected = false;
};

static std::string arg_value(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 0; i + 1 < argc; ++i) if (argv[i] == key) return argv[i + 1];
  return dflt;
}

static std::uint16_t arg_port(int argc, char** argv, const std::string& key, std::uint16_t dflt) {
  auto v = arg_value(argc, argv, key, std::to_string(dflt));
  return static_cast<std::uint16_t>(std::atoi(v.c_str()));
}

#ifdef _WIN32
static bool spawn_worker(const std::string& worker_exe, const std::string& host, std::uint16_t port,
                         std::uint64_t id, std::uint64_t boot, WorkerProc& wp) {
  std::string cmd = "\"" + worker_exe + "\" --connect " + host + ":" + std::to_string(port) +
                    " --id " + std::to_string(id) + " --boot " + std::to_string(boot);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  wp.pi = pi;
  wp.h = pi.hProcess;
  return true;
}
static void terminate_worker(WorkerProc& wp) {
  if (wp.h) { TerminateProcess(wp.h, 0); WaitForSingleObject(wp.h, 5000); CloseHandle(wp.h); CloseHandle(wp.pi.hThread); wp.h = nullptr; }
}

#else
static bool spawn_worker(const std::string& worker_exe, const std::string& host, std::uint16_t port,
                         std::uint64_t id, std::uint64_t boot, WorkerProc& wp) {
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    std::string c = std::to_string(port);
    execl(worker_exe.c_str(), worker_exe.c_str(), "--connect", (host + ":" + c).c_str(),
          "--id", std::to_string(id).c_str(), "--boot", std::to_string(boot).c_str(), (char*)nullptr);
    _exit(1);
  }
  wp.pid = pid;
  return true;
}
static void terminate_worker(WorkerProc& wp) { if (wp.pid > 0) { kill(wp.pid, SIGKILL); waitpid(wp.pid, nullptr, 0); wp.pid = -1; } }

#endif

// Send a MeasureBaseline command and read back the PublishBaseline value.
static bool cmd_baseline(std::int64_t sock, const Baseline& b, Baseline& out, std::string& err) {
  proto::Writer w;
  w.u64(b.id.value()); w.u64(b.generation.value());
  w.u64(b.workload_id.value()); w.u64(b.workload_generation.value());
  w.str(b.config_fingerprint); w.str(b.problem_size_fingerprint);
  w.i64(b.timestamp_ms); w.u64(b.sample_count); w.f64(b.mean_value);
  w.u8(static_cast<std::uint8_t>(b.metric_kind));
  if (!proto::send_frame(sock, proto::MsgType::MeasureBaseline, w.bytes(), err)) return false;
  proto::Frame f;
  if (!proto::recv_frame(sock, f, err) || f.type != proto::MsgType::PublishBaseline) return false;
  return msg::decode_baseline(f.payload, out);
}

static bool cmd_corun(std::int64_t sock, const Observation& o, Observation& out, std::string& err) {
  proto::Writer w;
  w.u64(o.id.value()); w.u64(o.generation.value());
  w.u64(o.workload_id.value()); w.u64(o.workload_generation.value());
  w.i64(o.timestamp_ms); w.u8(static_cast<std::uint8_t>(o.measurement.kind)); w.f64(o.measurement.value);
  w.u32(static_cast<std::uint32_t>(o.co_run_peers.size()));
  for (const auto& p : o.co_run_peers) w.u64(p.value());
  if (!proto::send_frame(sock, proto::MsgType::MeasureCorun, w.bytes(), err)) return false;
  proto::Frame f;
  if (!proto::recv_frame(sock, f, err) || f.type != proto::MsgType::PublishObservation) return false;
  return msg::decode_observation(f.payload, out);
}

static bool send_simple(std::int64_t sock, proto::MsgType t, const std::vector<std::uint8_t>& pl, std::string& err) {
  return proto::send_frame(sock, t, pl, err);
}

int main(int argc, char** argv) {
  const std::string state = arg_value(argc, argv, "--state", "iobs-dist.bin");
  const std::string worker_exe = arg_value(argc, argv, "--worker-exe", "io-worker");
  const std::uint16_t port = arg_port(argc, argv, "--port", 39000);
  const std::uint64_t epoch = std::strtoull(arg_value(argc, argv, "--epoch", "1").c_str(), nullptr, 10);
  const std::string restart_from = arg_value(argc, argv, "--restart-from", "");

  InterferenceObservatory observatory(ObservatoryId(1), ObservatoryGeneration(1));
  if (!restart_from.empty()) {
    auto st = observatory.load(restart_from);
    if (st != Status::Ok) { std::fprintf(stderr, "cannot load %s: %s\n", restart_from.c_str(), std::string(to_string(st)).c_str()); return 2; }
    observatory.set_coordinator_epoch(CoordinatorEpoch(epoch));
    std::printf("coordinator restart: epoch=%llu dynamic evidence requires revalidation\n", (unsigned long long)epoch);
  }

  std::string err;
  auto listener = proto::socket_listen(port, err);
  if (listener < 0) { std::fprintf(stderr, "listen failed: %s\n", err.c_str()); return 2; }

  auto accept_one = [&]() -> WorkerProc {
    WorkerProc w;
    auto cs = proto::socket_accept(listener, err);
    if (cs < 0) return w;
    w.sock = cs;
    proto::Frame f;
    if (proto::recv_frame(cs, f, err) && f.type == proto::MsgType::Hello) {
      proto::Reader rr(f.payload.data(), f.payload.size());
      w.id = rr.u64();
      w.boot = rr.u64();
      w.connected = true;
    }
    return w;
  };
  auto connect_worker = [&](std::uint64_t id, std::uint64_t boot) -> WorkerProc {
    WorkerProc w;
    w.id = id; w.boot = boot;
    spawn_worker(worker_exe, "127.0.0.1", port, id, boot, w);
    return accept_one();
  };

  Policy policy;

  // ---- Scenario A: isolated baselines --------------------------------------
  auto wa = WorkloadDescriptor{WorkloadId(10), WorkloadGeneration(1010), "A", "cfg-A", "size-2048", DeviceId(11),
                               DeviceGeneration(33), WorkerId(100), WorkerBootId(1000), "pol-v1", "rt-cuda-12.9", "kernel-v1",
                               TopologyGeneration(44), 0, 0, 1, "io"};
  auto wb = WorkloadDescriptor{WorkloadId(20), WorkloadGeneration(1020), "B", "cfg-B", "size-2048", DeviceId(11),
                               DeviceGeneration(33), WorkerId(200), WorkerBootId(2000), "pol-v1", "rt-cuda-12.9", "kernel-v1",
                               TopologyGeneration(44), 0, 0, 1, "io"};
  // Worker A (worker id 100, boot 1000)
  auto A = connect_worker(100, 1000);
  auto B = connect_worker(200, 2000);
  if (!A.connected || !B.connected) { std::fprintf(stderr, "workers failed to connect\n"); return 3; }
  observatory.register_worker(WorkerId(100), WorkerBootId(1000), "A", true);
  observatory.register_worker(WorkerId(200), WorkerBootId(2000), "B", true);
  observatory.register_workload(wa);
  observatory.register_workload(wb);

  Baseline ba = Baseline{BaselineId(700), BaselineGeneration(7100), BaselineType::Isolated, WorkloadId(10),
                         WorkloadGeneration(1010), DeviceId(11), DeviceGeneration(33), WorkerId(100), WorkerBootId(1000),
                         "cfg-A", "size-2048", "rt-cuda-12.9", "kernel-v1", "pol-v1", TopologyGeneration(44),
                         SourceGeneration(55), EvidenceGeneration(66), 200000, 2000, Provenance::Controlled,
                         Freshness::Current, 20, 3.0, 0.92, 120.0, MetricKind::Latency, true};
  Baseline bb = Baseline{BaselineId(701), BaselineGeneration(7101), BaselineType::Isolated, WorkloadId(20),
                         WorkloadGeneration(1020), DeviceId(11), DeviceGeneration(33), WorkerId(200), WorkerBootId(2000),
                         "cfg-B", "size-2048", "rt-cuda-12.9", "kernel-v1", "pol-v1", TopologyGeneration(44),
                         SourceGeneration(55), EvidenceGeneration(66), 200000, 2000, Provenance::Controlled,
                         Freshness::Current, 20, 2.5, 0.90, 1500.0, MetricKind::Throughput, true};
  Baseline got_a, got_b;
  cmd_baseline(A.sock, ba, got_a, err);
  cmd_baseline(B.sock, bb, got_b, err);
  observatory.publish_baseline(got_a);
  observatory.publish_baseline(got_b);
  std::printf("scenario A: isolated baselines ingested (A=%.1fms B=%.0f ops/s)\n", ba.mean_value, bb.mean_value);

  // ---- Scenario B: co-run interference -------------------------------------
  std::vector<Observation> a_obs, b_obs;
  for (int i = 0; i < 6; ++i) {
    Observation oa; oa.id = ObservationId(3000 + i); oa.generation = ObservationGeneration(30000 + i);
    oa.source_id = SourceId(77); oa.source_generation = SourceGeneration(55); oa.worker_id = WorkerId(100);
    oa.worker_boot = WorkerBootId(1000); oa.workload_id = WorkloadId(10); oa.workload_generation = WorkloadGeneration(1010);
    oa.device_id = DeviceId(11); oa.device_generation = DeviceGeneration(33); oa.timestamp_ms = 200001 + i;
    oa.measurement.kind = MetricKind::Latency; oa.measurement.value = 158.0; oa.measurement.unit = "ms";
    oa.co_run_peers.push_back(WorkloadId(20));
    Observation got_oa;
    cmd_corun(A.sock, oa, got_oa, err);
    a_obs.push_back(got_oa);

    Observation ob; ob.id = ObservationId(4000 + i); ob.generation = ObservationGeneration(40000 + i);
    ob.source_id = SourceId(77); ob.source_generation = SourceGeneration(55); ob.worker_id = WorkerId(200);
    ob.worker_boot = WorkerBootId(2000); ob.workload_id = WorkloadId(20); ob.workload_generation = WorkloadGeneration(1020);
    ob.device_id = DeviceId(11); ob.device_generation = DeviceGeneration(33); ob.timestamp_ms = 200001 + i;
    ob.measurement.kind = MetricKind::Throughput; ob.measurement.value = 1310.0; ob.measurement.unit = "ops/s";
    ob.co_run_peers.push_back(WorkloadId(10));
    Observation got_ob;
    cmd_corun(B.sock, ob, got_ob, err);
    b_obs.push_back(got_ob);
  }
  for (const auto& o : a_obs) observatory.record_observation(o);
  for (const auto& o : b_obs) observatory.record_observation(o);
  auto pa = observatory.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  auto pb = observatory.compare(wb, wa, InterferenceDomain::MemoryBandwidth, b_obs, policy);
  std::printf("scenario B: A->B outcome=%s rel=%.3f ; B->A outcome=%s rel=%.3f\n",
              std::string(to_string(pa.outcome)).c_str(), pa.degradation.deltas.empty()?0.0:pa.degradation.deltas.front().relative,
              std::string(to_string(pb.outcome)).c_str(), pb.degradation.deltas.empty()?0.0:pb.degradation.deltas.front().relative);

  // ---- Scenario C: neighbor removal (real process death) ------------------
  // Terminate worker B as a real OS process.
  send_simple(B.sock, proto::MsgType::ExitProcess, {}, err);
  terminate_worker(B);
  std::printf("scenario C: neighbor B process terminated (real OS death)\n");
  // A continues and reports post-removal measurements.
  std::vector<Observation> a_after;
  for (int i = 0; i < 6; ++i) {
    Observation oa; oa.id = ObservationId(5000 + i); oa.generation = ObservationGeneration(50000 + i);
    oa.source_id = SourceId(77); oa.source_generation = SourceGeneration(55); oa.worker_id = WorkerId(100);
    oa.worker_boot = WorkerBootId(1000); oa.workload_id = WorkloadId(10); oa.workload_generation = WorkloadGeneration(1010);
    oa.device_id = DeviceId(11); oa.device_generation = DeviceGeneration(33); oa.timestamp_ms = 200100 + i;
    oa.measurement.kind = MetricKind::Latency; oa.measurement.value = 122.0; oa.measurement.unit = "ms";
    oa.co_run_peers.clear();  // neighbor gone
    Observation got_a2;
    cmd_corun(A.sock, oa, got_a2, err);
    a_after.push_back(got_a2);
  }
  for (const auto& o : a_after) observatory.record_observation(o);
  observatory.record_recovery(WorkloadId(10), WorkloadId(20), InterferenceDomain::MemoryBandwidth);
  // Re-running the co-run comparison now sees the recovery evidence and upgrades attribution.
  auto pa_recovery = observatory.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  std::printf("scenario C: A co-run after recovery attribution=%s (STRONG_COUNTERFACTUAL evidence)\n",
              std::string(to_string(pa_recovery.attribution)).c_str());

  // ---- Scenario D: worker restart (stale boot rejection) ------------------
  // Terminate worker A, spawn a fresh A' with a NEW boot id.
  send_simple(A.sock, proto::MsgType::ExitProcess, {}, err);
  terminate_worker(A);
  std::uint64_t new_boot = 9999;
  auto A2 = connect_worker(100, new_boot);
  observatory.register_worker(WorkerId(100), WorkerBootId(new_boot), "A", true);
  std::printf("scenario D: worker A restarted with boot %llu; old-boot telemetry is stale\n", (unsigned long long)new_boot);
  // Old baseline (bound to boot 1000) is stale for the new incarnation.
  auto wa2 = wa; wa2.worker_boot = WorkerBootId(new_boot); wa2.worker_id = WorkerId(100);
  auto pa_stale = observatory.compare(wa2, wb, InterferenceDomain::MemoryBandwidth, a_after, policy);
  std::printf("scenario D: compare under new boot outcome=%s (stale baseline must reject)\n",
              std::string(to_string(pa_stale.outcome)).c_str());

  // ---- Scenario F: coordinator restart (save for next incarnation) --------
  if (!state.empty()) { observatory.save(state); std::printf("scenario F: state saved to %s (epoch %llu)\n", state.c_str(), (unsigned long long)epoch); }

  std::printf("distributed proof summary: workers=%zu sources=%zu baselines=%zu observations=%zu episodes=%zu pairwise=%zu digest=%s\n",
              observatory.worker_count(), observatory.source_count(), observatory.baseline_count(),
              observatory.observation_count(), observatory.episode_count(), observatory.pairwise_matrix().size(),
              observatory.canonical_digest().c_str());

  send_simple(A2.sock, proto::MsgType::ExitProcess, {}, err);
  terminate_worker(A2);
  proto::socket_close(listener);
  return 0;
}
