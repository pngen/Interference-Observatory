#pragma once
#include "observatory/protocol.hpp"
#include "observatory/observatory.hpp"

namespace iobs {
namespace msg {

inline std::vector<std::uint8_t> encode_baseline(const Baseline& b) {
  proto::Writer w;
  w.u64(b.id.value()); w.u64(b.generation.value());
  w.u8(static_cast<std::uint8_t>(b.type));
  w.u64(b.workload_id.value()); w.u64(b.workload_generation.value());
  w.u64(b.device_id.value()); w.u64(b.device_generation.value());
  w.u64(b.worker_id.value()); w.u64(b.worker_boot.value());
  w.str(b.config_fingerprint); w.str(b.problem_size_fingerprint);
  w.str(b.runtime_generation); w.str(b.kernel_generation); w.str(b.policy_generation);
  w.u64(b.topology_generation.value()); w.u64(b.source_generation.value()); w.u64(b.evidence_generation.value());
  w.i64(b.timestamp_ms); w.i64(b.interval_ms);
  w.u8(static_cast<std::uint8_t>(b.provenance)); w.u8(static_cast<std::uint8_t>(b.freshness));
  w.u64(b.sample_count); w.f64(b.variance); w.f64(b.confidence); w.f64(b.mean_value);
  w.u8(static_cast<std::uint8_t>(b.metric_kind)); w.u8(b.valid ? 1 : 0);
  return w.bytes();
}

inline bool decode_baseline(const std::vector<std::uint8_t>& p, Baseline& b) {
  proto::Reader r(p.data(), p.size());
  b.id = BaselineId(r.u64()); b.generation = BaselineGeneration(r.u64());
  b.type = static_cast<BaselineType>(r.u8());
  b.workload_id = WorkloadId(r.u64()); b.workload_generation = WorkloadGeneration(r.u64());
  b.device_id = DeviceId(r.u64()); b.device_generation = DeviceGeneration(r.u64());
  b.worker_id = WorkerId(r.u64()); b.worker_boot = WorkerBootId(r.u64());
  b.config_fingerprint = r.str(); b.problem_size_fingerprint = r.str();
  b.runtime_generation = r.str(); b.kernel_generation = r.str(); b.policy_generation = r.str();
  b.topology_generation = TopologyGeneration(r.u64()); b.source_generation = SourceGeneration(r.u64());
  b.evidence_generation = EvidenceGeneration(r.u64());
  b.timestamp_ms = r.i64(); b.interval_ms = r.i64();
  b.provenance = static_cast<Provenance>(r.u8()); b.freshness = static_cast<Freshness>(r.u8());
  b.sample_count = r.u64(); b.variance = r.f64(); b.confidence = r.f64(); b.mean_value = r.f64();
  b.metric_kind = static_cast<MetricKind>(r.u8()); b.valid = r.u8() != 0;
  r.finish();
  return r.ok();
}

inline std::vector<std::uint8_t> encode_observation(const Observation& o) {
  proto::Writer w;
  w.u64(o.id.value()); w.u64(o.generation.value());
  w.u64(o.source_id.value()); w.u64(o.source_generation.value());
  w.u64(o.worker_id.value()); w.u64(o.worker_boot.value());
  w.u64(o.workload_id.value()); w.u64(o.workload_generation.value());
  w.u64(o.device_id.value()); w.u64(o.device_generation.value());
  w.i64(o.timestamp_ms);
  w.u8(static_cast<std::uint8_t>(o.measurement.kind)); w.f64(o.measurement.value);
  w.str(o.measurement.unit); w.u64(o.measurement.sample_count); w.f64(o.measurement.variance);
  w.u8(static_cast<std::uint8_t>(o.provenance)); w.u8(static_cast<std::uint8_t>(o.freshness));
  w.u8(static_cast<std::uint8_t>(o.source_health));
  w.u32(static_cast<std::uint32_t>(o.co_run_peers.size()));
  for (const auto& peer : o.co_run_peers) w.u64(peer.value());
  return w.bytes();
}

inline bool decode_observation(const std::vector<std::uint8_t>& p, Observation& o) {
  proto::Reader r(p.data(), p.size());
  o.id = ObservationId(r.u64()); o.generation = ObservationGeneration(r.u64());
  o.source_id = SourceId(r.u64()); o.source_generation = SourceGeneration(r.u64());
  o.worker_id = WorkerId(r.u64()); o.worker_boot = WorkerBootId(r.u64());
  o.workload_id = WorkloadId(r.u64()); o.workload_generation = WorkloadGeneration(r.u64());
  o.device_id = DeviceId(r.u64()); o.device_generation = DeviceGeneration(r.u64());
  o.timestamp_ms = r.i64();
  o.measurement.kind = static_cast<MetricKind>(r.u8()); o.measurement.value = r.f64();
  o.measurement.unit = r.str(); o.measurement.sample_count = r.u64(); o.measurement.variance = r.f64();
  o.provenance = static_cast<Provenance>(r.u8()); o.freshness = static_cast<Freshness>(r.u8());
  o.source_health = static_cast<SourceHealth>(r.u8());
  std::uint32_t peers = r.u32();
  for (std::uint32_t i = 0; i < peers && r.ok(); ++i) o.co_run_peers.push_back(WorkloadId(r.u64()));
  r.finish();
  return r.ok();
}

}  // namespace msg
}  // namespace iobs
