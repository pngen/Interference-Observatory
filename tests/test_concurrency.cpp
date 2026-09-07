#include "test_framework.hpp"
#include "helpers.hpp"
#include "observatory/observatory.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <filesystem>

using namespace iobs;
using testutil::make_workload, testutil::make_baseline, testutil::make_obs;

TEST(concurrent_ingest_deterministic_totals) {
  using namespace std::chrono;
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));

  const int kThreads = 8;
  const int kPerThread = 500;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        ObservationId id(static_cast<std::uint64_t>(t * kPerThread + i + 1));
        auto obs = make_obs(id, WorkloadId(10), 130.0, 100001 + t * kPerThread + i, {WorkloadId(20)});
        o.record_observation(obs);
      }
    });
  }
  for (auto& th : threads) th.join();
  // Exact deterministic total.
  CHECK_EQ(o.observation_count(), static_cast<std::size_t>(kThreads * kPerThread));

  // A concurrent comparison over the full set yields a deterministic outcome.
  Policy policy;
  std::vector<Observation> all = o.observations_for(WorkloadId(10));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, all, policy);
  CHECK_EQ(p.outcome, Outcome::InterferenceDetected);
  CHECK_EQ(p.sample_count, static_cast<std::uint64_t>(kThreads * kPerThread));
}

TEST(concurrent_baseline_idempotent) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  o.register_workload(wa);
  const int kThreads = 8;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] { o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0)); });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(o.baseline_count(), (std::size_t)1);  // duplicate idempotent, never double-counted
}

TEST(concurrent_save_load) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  auto path = (std::filesystem::temp_directory_path() / "iobs_conc_save.bin").string();
  std::filesystem::remove(path);

  const int kThreads = 8;
  const int kPerThread = 400;
  std::vector<std::thread> threads;
  std::atomic<bool> done{false};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        ObservationId id(static_cast<std::uint64_t>(100000 + t * kPerThread + i));
        auto obs = make_obs(id, WorkloadId(10), 130.0, 100001 + t * kPerThread + i, {WorkloadId(20)});
        o.record_observation(obs);
      }
      if (t == 0) {
        // A concurrent save while other threads are still ingesting.
        o.save(path);
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(o.observation_count(), static_cast<std::size_t>(kThreads * kPerThread));

  InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
  CHECK_EQ(o2.load(path), Status::Ok);
  CHECK(o2.observation_count() >= static_cast<std::size_t>(kPerThread));
  std::filesystem::remove(path);
}
