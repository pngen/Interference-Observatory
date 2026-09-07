#include "test_framework.hpp"
#include "helpers.hpp"
#include "observatory/observatory.hpp"
#include "observatory/checked.hpp"
#include "observatory/domain.hpp"
#include <limits>
#include <cmath>

using namespace iobs;
using testutil::make_workload, testutil::make_baseline, testutil::make_obs;

TEST(malformed_baseline_rejected) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto b = make_baseline(BaselineId(1), WorkloadId(10), 100.0);
  b.metric_kind = static_cast<MetricKind>(200);  // invalid enum
  CHECK_EQ(o.publish_baseline(b), Status::InvalidInput);
}

TEST(malformed_observation_rejected) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto obs = make_obs(ObservationId(1), WorkloadId(10), std::numeric_limits<double>::quiet_NaN(), 100);
  CHECK_EQ(o.record_observation(obs), Status::InvalidInput);
}

TEST(duplicate_observation_idempotent) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto obs = make_obs(ObservationId(1), WorkloadId(10), 130.0, 100001, {WorkloadId(20)});
  CHECK_EQ(o.record_observation(obs), Status::Ok);
  CHECK_EQ(o.record_observation(obs), Status::Ok);  // idempotent, no double count
  CHECK_EQ(o.observation_count(), (std::size_t)1);
}

TEST(conflicting_duplicate_baseline_rejected) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto b1 = make_baseline(BaselineId(5), WorkloadId(10), 100.0);
  auto b2 = make_baseline(BaselineId(5), WorkloadId(10), 200.0);  // same id, different content
  CHECK_EQ(o.publish_baseline(b1), Status::Ok);
  CHECK_EQ(o.publish_baseline(b2), Status::InvalidInput);
}

TEST(checked_arithmetic_overflow) {
  std::uint64_t out = 0;
  CHECK(!checked::add_u64(std::numeric_limits<std::uint64_t>::max(), 1, out));
  CHECK(!checked::mul_u64(std::numeric_limits<std::uint64_t>::max(), 2, out));
  CHECK(!checked::sub_u64(1, 2, out));
  CHECK(checked::add_u64(5, 3, out) && out == 8);
  CHECK(checked::mul_u64(7, 6, out) && out == 42);
  CHECK(!checked::is_finite_nonneg(std::numeric_limits<double>::infinity()));
  CHECK(!checked::is_finite_nonneg(-1.0));
}

TEST(coordinator_restart_revalidation) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  CHECK_EQ(o.coordinator_epoch(), CoordinatorEpoch(1));
  // Restart: advance to a new epoch.
  o.set_coordinator_epoch(CoordinatorEpoch(2));
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  // Dynamic baseline from the prior epoch requires revalidation.
  CHECK_EQ(p.outcome, Outcome::RevalidationRequired);
}

TEST(old_epoch_baseline_not_authoritative) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  // Baseline published under epoch 1 is marked revalidation-required on epoch advance.
  o.set_coordinator_epoch(CoordinatorEpoch(2));
  auto got = o.lookup_baseline(BaselineId(1));
  CHECK(got.has_value());
  CHECK_EQ(got->freshness, Freshness::RevalidationRequired);
}

TEST(triple_workload_group_not_split) {
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  auto wc = make_workload(WorkloadId(30));
  for (auto* w : {&wa, &wb, &wc}) o.register_workload(*w);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 140.0, 100001 + i, {WorkloadId(20), WorkloadId(30)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::InterferenceDetected);
  // The group effect is NOT silently split among neighbors: the matrix holds this cell only.
  auto m = o.pairwise_matrix();
  CHECK(m.get(CellKey{WorkloadId(10), WorkloadId(20), InterferenceDomain::MemoryBandwidth}).has_value());
  // No fabricated cell for the other neighbor automatically.
  CHECK(!m.get(CellKey{WorkloadId(10), WorkloadId(30), InterferenceDomain::MemoryBandwidth}).has_value());
}

TEST(invalid_enum_parsing) {
  CHECK(!parse_interference_domain("NOT_A_DOMAIN").has_value());
  CHECK(parse_interference_domain("MEMORY_BANDWIDTH").has_value());
  CHECK(parse_interference_domain("COMPUTE_EXECUTION").has_value());
}

TEST(forged_numeric_identity_rejected) {
  CHECK(!parse_id_value("").has_value());
  CHECK(!parse_id_value("12a").has_value());
  CHECK(!parse_id_value("-5").has_value());
  CHECK(parse_id_value("123456").has_value());
}

TEST(unsupported_metric_stays_unsupported) {
  // Capabilities do not claim cache counters by default; BackendCapabilities.available() honesty.
  BackendCapabilities caps;
  CHECK(!caps.cache_counters);
  CHECK(!caps.pcie_counters);
  auto avail = caps.available();
  CHECK(avail.empty());
  caps.cuda_kernels = true;
  auto avail2 = caps.available();
  CHECK_EQ(avail2.size(), (std::size_t)1);
  CHECK(avail2[0].find("CUDA_KERNELS") != std::string::npos);
}

TEST(measured_physical_work_preserved) {
  // A measurement recorded as MEASURED keeps its provenance even if usefulness is later questioned.
  auto o = InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
  auto obs = make_obs(ObservationId(7), WorkloadId(10), 99.0, 100001);
  CHECK_EQ(o.record_observation(obs), Status::Ok);
  auto found = o.observations_for(WorkloadId(10));
  CHECK_EQ(found.size(), (std::size_t)1);
  CHECK_EQ(found[0].provenance, Provenance::Measured);
}
