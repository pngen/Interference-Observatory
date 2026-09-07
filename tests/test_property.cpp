#include "test_framework.hpp"
#include "helpers.hpp"
#include "observatory/observatory.hpp"

using namespace iobs;
using testutil::make_workload, testutil::make_baseline, testutil::make_obs;

TEST(identical_canonical_experiment_identical_result) {
  auto run = [] {
    InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
    auto wa = make_workload(WorkloadId(10));
    auto wb = make_workload(WorkloadId(20));
    o.register_workload(wa);
    o.register_workload(wb);
    o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
    Policy policy;
    std::vector<Observation> a_obs;
    for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
    return o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  };
  auto p1 = run();
  auto p2 = run();
  CHECK_EQ(p1.outcome, p2.outcome);
  CHECK_EQ(p1.attribution, p2.attribution);
  CHECK_EQ(p1.sample_count, p2.sample_count);
  CHECK_EQ(p1.comparison_id, p2.comparison_id);
  CHECK_EQ(p1.degradation.find(MetricKind::Latency)->relative, p2.degradation.find(MetricKind::Latency)->relative);
}

TEST(duplicate_evidence_never_changes_totals_twice) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  o.register_workload(wa);
  auto obs = make_obs(ObservationId(1), WorkloadId(10), 130.0, 100001);
  o.record_observation(obs);
  std::size_t before = o.observation_count();
  o.record_observation(obs);
  o.record_observation(obs);
  CHECK_EQ(o.observation_count(), before);  // idempotent: never double-counted
}

TEST(stale_evidence_never_mutates_current_state) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  // Advance epoch -> the baseline is dynamic and requires revalidation; it is NOT silently used.
  o.set_coordinator_epoch(CoordinatorEpoch(2));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  // Stale/dynamic authority must not yield a confident interference outcome.
  CHECK(p.outcome != Outcome::InterferenceDetected);
  CHECK(p.outcome != Outcome::SevereInterference);
}

TEST(no_baseline_no_confident_claim) {
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::InsufficientEvidence);
  CHECK_EQ(p.attribution, AttributionStrength::Unknown);
}

TEST(no_auto_symmetry) {
  // A->B with a large effect must not create B->A with the same value.
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 160.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::SevereInterference);
  // The reverse cell does not exist until separately measured.
  CHECK(!o.pairwise_matrix().get(CellKey{WorkloadId(20), WorkloadId(10), InterferenceDomain::MemoryBandwidth}).has_value());
}

TEST(same_history_same_digest) {
  InterferenceObservatory o1(ObservatoryId(1), ObservatoryGeneration(1));
  InterferenceObservatory o2(ObservatoryId(1), ObservatoryGeneration(1));
  for (auto* o : {&o1, &o2}) {
    auto wa = make_workload(WorkloadId(10));
    auto wb = make_workload(WorkloadId(20));
    o->register_workload(wa);
    o->register_workload(wb);
    o->publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
    Policy policy;
    std::vector<Observation> a_obs;
    for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
    o->compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  }
  CHECK_EQ(o1.canonical_digest(), o2.canonical_digest());
}

TEST(unknown_never_becomes_no_interference) {
  // An UNKNOWN outcome must never be coerced to NO_INTERFERENCE_DETECTED.
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  Policy policy;
  std::vector<Observation> a_obs;  // no co-run observations at all
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  // Missing baseline/samples yields InsufficientEvidence/Unknown, never a fabricated no-interference.
  CHECK(p.outcome == Outcome::InsufficientEvidence || p.outcome == Outcome::Unknown);
}

TEST(effect_direction_preserved) {
  // Degradation is recorded as a signed relative delta, preserving direction.
  InterferenceObservatory o(ObservatoryId(1), ObservatoryGeneration(1));
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  auto d = p.degradation.find(MetricKind::Latency);
  CHECK(d.has_value());
  CHECK(d->relative > 0.0);  // latency increased (higher-is-worse)
  CHECK(d->absolute > 0.0);
}
