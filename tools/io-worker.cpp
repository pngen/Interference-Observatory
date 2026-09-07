#include "messages.hpp"
#include "observatory/protocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace iobs;

static std::string arg_value(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 0; i + 1 < argc; ++i) if (argv[i] == key) return argv[i + 1];
  return dflt;
}

// Parse host:port -> host, port.
static bool parse_endpoint(const std::string& ep, std::string& host, std::uint16_t& port) {
  auto pos = ep.find(':');
  if (pos == std::string::npos) return false;
  host = ep.substr(0, pos);
  port = static_cast<std::uint16_t>(std::atoi(ep.substr(pos + 1).c_str()));
  return !host.empty() && port > 0;
}

int main(int argc, char** argv) {
  std::string host, ep = arg_value(argc, argv, "--connect", "127.0.0.1:39000");
  std::uint16_t port = 0;
  if (!parse_endpoint(ep, host, port)) { std::fprintf(stderr, "bad --connect endpoint\n"); return 2; }
  std::uint64_t id = static_cast<std::uint64_t>(std::strtoull(arg_value(argc, argv, "--id", "1").c_str(), nullptr, 10));
  std::uint64_t boot = static_cast<std::uint64_t>(std::strtoull(arg_value(argc, argv, "--boot", "1").c_str(), nullptr, 10));

  std::string err;
  auto sock = proto::socket_connect(host, port, err);
  if (sock < 0) { std::fprintf(stderr, "worker connect failed: %s\n", err.c_str()); return 2; }

  // HELLO: worker_id, worker_boot, name.
  {
    proto::Writer w;
    w.u64(id);
    w.u64(boot);
    w.str("worker-" + std::to_string(id));
    proto::send_frame(sock, proto::MsgType::Hello, w.bytes(), err);
  }

  std::uint64_t current_boot = boot;
  bool running = true;
  while (running) {
    proto::Frame f;
    if (!proto::recv_frame(sock, f, err)) { std::fprintf(stderr, "worker recv: %s\n", err.c_str()); break; }
    switch (f.type) {
      case proto::MsgType::MeasureBaseline: {
        proto::Reader r(f.payload.data(), f.payload.size());
        Baseline b;
        b.id = BaselineId(r.u64());
        b.generation = BaselineGeneration(r.u64());
        b.workload_id = WorkloadId(r.u64());
        b.workload_generation = WorkloadGeneration(r.u64());
        b.device_id = DeviceId(11);
        b.device_generation = DeviceGeneration(33);
        b.worker_id = WorkerId(id);
        b.worker_boot = WorkerBootId(current_boot);
        b.config_fingerprint = r.str();
        b.problem_size_fingerprint = r.str();
        b.runtime_generation = "rt-cuda-12.9";
        b.kernel_generation = "kernel-v1";
        b.policy_generation = "pol-v1";
        b.topology_generation = TopologyGeneration(44);
        b.source_generation = SourceGeneration(55);
        b.evidence_generation = EvidenceGeneration(66);
        b.timestamp_ms = r.i64();
        b.interval_ms = 2000;
        b.provenance = Provenance::Controlled;
        b.freshness = Freshness::Current;
        b.sample_count = r.u64();
        b.variance = 2.0;
        b.confidence = 0.90;
        b.mean_value = r.f64();
        b.metric_kind = static_cast<MetricKind>(r.u8());
        b.valid = true;
        if (!r.ok()) { std::fprintf(stderr, "bad MeasureBaseline cmd\n"); break; }
        proto::send_frame(sock, proto::MsgType::PublishBaseline, msg::encode_baseline(b), err);
        break;
      }
      case proto::MsgType::MeasureCorun: {
        proto::Reader r(f.payload.data(), f.payload.size());
        Observation o;
        o.id = ObservationId(r.u64());
        o.generation = ObservationGeneration(r.u64());
        o.source_id = SourceId(77);
        o.source_generation = SourceGeneration(55);
        o.worker_id = WorkerId(id);
        o.worker_boot = WorkerBootId(current_boot);
        o.workload_id = WorkloadId(r.u64());
        o.workload_generation = WorkloadGeneration(r.u64());
        o.device_id = DeviceId(11);
        o.device_generation = DeviceGeneration(33);
        o.timestamp_ms = r.i64();
        o.measurement.kind = static_cast<MetricKind>(r.u8());
        o.measurement.value = r.f64();
        o.measurement.unit = (o.measurement.kind == MetricKind::Latency) ? "ms" : "ops/s";
        o.measurement.sample_count = 1;
        o.measurement.variance = 0.0;
        o.provenance = Provenance::Controlled;
        o.freshness = Freshness::Current;
        o.source_health = SourceHealth::Healthy;
        std::uint32_t peers = r.u32();
        for (std::uint32_t i = 0; i < peers && r.ok(); ++i) o.co_run_peers.push_back(WorkloadId(r.u64()));
        if (!r.ok()) { std::fprintf(stderr, "bad MeasureCorun cmd\n"); break; }
        proto::send_frame(sock, proto::MsgType::PublishObservation, msg::encode_observation(o), err);
        break;
      }
      case proto::MsgType::Revalidate: {
        proto::Reader r(f.payload.data(), f.payload.size());
        current_boot = r.u64();
        proto::send_frame(sock, proto::MsgType::Ack, {}, err);
        break;
      }
      case proto::MsgType::ExitProcess:
        running = false;
        break;
      default:
        proto::send_frame(sock, proto::MsgType::Error, {}, err);
        break;
    }
  }
  proto::socket_close(sock);
  std::printf("worker %llu exiting cleanly\n", (unsigned long long)id);
  return 0;
}
