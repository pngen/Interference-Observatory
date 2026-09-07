#include "test_framework.hpp"
#include "helpers.hpp"
#include "observatory/observatory.hpp"
#include "observatory/persist.hpp"
#include <filesystem>
#include <fstream>

using namespace iobs;
using testutil::make_workload, testutil::make_baseline, testutil::make_obs;

using namespace std::filesystem;

static std::string tmp_path(const char* name) {
  return (path(temp_directory_path()) / std::string(name)).string();
}

static void populate(InterferenceObservatory& o) {
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.register_worker(WorkerId(22), WorkerBootId(1), "w");
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  o.publish_baseline(make_baseline(BaselineId(2), WorkloadId(20), 100.0));
  Policy policy;
  std::vector<Observation> a_obs, b_obs;
  for (int i = 0; i < 5; ++i) {
    a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
    b_obs.push_back(make_obs(ObservationId(20 + i), WorkloadId(20), 108.0, 100001 + i, {WorkloadId(10)}));
  }
  o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  o.compare(wb, wa, InterferenceDomain::MemoryBandwidth, b_obs, policy);
}

TEST(save_load_roundtrip) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  populate(o);
  auto path1 = tmp_path("iobs_persist.bin");
  remove(path1);
  CHECK_EQ(o.save(path1), Status::Ok);
  InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
  CHECK_EQ(o2.load(path1), Status::Ok);
  CHECK_EQ(o2.observation_count(), o.observation_count());
  CHECK_EQ(o2.baseline_count(), o.baseline_count());
  CHECK_EQ(o2.episode_count(), o.episode_count());
  CHECK_EQ(o2.workload_count(), o.workload_count());
  CHECK_EQ(o2.worker_count(), o.worker_count());
  CHECK_EQ(o.canonical_digest(), o2.canonical_digest());
  CHECK_EQ(o2.pairwise_matrix().size(), (std::size_t)2);
  remove(path1);
}

TEST(historical_episode_survives_restart) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  populate(o);
  auto path1 = tmp_path("iobs_hist.bin");
  remove(path1);
  o.save(path1);
  InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
  o2.load(path1);
  CHECK_EQ(o2.episode_count(), (std::size_t)2);  // A->B and B->A both produce an episode
  // Historical evidence remains historical; dynamic state must be revalidated on restart.
  o2.set_coordinator_epoch(CoordinatorEpoch(2));
  CHECK_EQ(o2.episode_count(), (std::size_t)2);  // historical episodes persist across epoch advance
  remove(path1);
}

TEST(corruption_rejected) {
  std::vector<std::uint8_t> payload(2000, 0x41);
  auto enc = persist::encode(payload);
  // Flip a byte in the middle of the payload.
  enc[enc.size() / 2] ^= 0xFF;
  std::vector<std::uint8_t> out;
  std::string reason;
  CHECK(!persist::decode(enc, out, reason));
  CHECK(reason.find("checksum") != std::string::npos);
}

TEST(truncation_rejected) {
  std::vector<std::uint8_t> payload(2000, 0x41);
  auto enc = persist::encode(payload);
  enc.resize(enc.size() - 10);  // truncate
  std::vector<std::uint8_t> out;
  std::string reason;
  CHECK(!persist::decode(enc, out, reason));
  CHECK(reason.find("truncated") != std::string::npos || reason.find("payload") != std::string::npos);
}

TEST(trailing_garbage_rejected) {
  std::vector<std::uint8_t> payload(100, 0x41);
  auto enc = persist::encode(payload);
  enc.push_back(0x00);
  enc.push_back(0x01);
  std::vector<std::uint8_t> out;
  std::string reason;
  CHECK(!persist::decode(enc, out, reason));
  CHECK(reason.find("trailing") != std::string::npos);
}

TEST(unknown_version_rejected) {
  std::vector<std::uint8_t> payload(100, 0x41);
  auto enc = persist::encode(payload);
  enc[4] = 0x7F;  // bump version
  enc[5] = 0x7F;
  std::vector<std::uint8_t> out;
  std::string reason;
  CHECK(!persist::decode(enc, out, reason));
  CHECK(reason.find("version") != std::string::npos);
}

TEST(bad_magic_rejected) {
  std::vector<std::uint8_t> payload(100, 0x41);
  auto enc = persist::encode(payload);
  enc[0] = 0x00;
  std::vector<std::uint8_t> out;
  std::string reason;
  CHECK(!persist::decode(enc, out, reason));
  CHECK(reason.find("magic") != std::string::npos);
}

TEST(oversized_payload_rejected) {
  std::vector<std::uint8_t> payload(static_cast<std::size_t>(persist::kMaxPayload) + 1, 0x41);
  CHECK_THROWS_AS(persist::encode(payload), Error);
}

TEST(digest_stable_across_reload) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  populate(o);
  auto path1 = tmp_path("iobs_digest.bin");
  remove(path1);
  o.save(path1);
  InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
  o2.load(path1);
  CHECK_EQ(o.canonical_digest(), o2.canonical_digest());
  // Reload again -> same digest.
  InterferenceObservatory o3(ObservatoryId(1), ObservatoryGeneration(1));
  o3.load(path1);
  CHECK_EQ(o.canonical_digest(), o3.canonical_digest());
  remove(path1);
}