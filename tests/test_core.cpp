#include "test_framework.hpp"
#include "helpers.hpp"
#include "observatory/observatory.hpp"

using namespace iobs;
using testutil::make_workload, testutil::make_baseline, testutil::make_obs;

static InterferenceObservatory make_obsy() {
  return InterferenceObservatory(ObservatoryId(1), ObservatoryGeneration(1));
}

TEST(baseline_publish_and_lookup) {
  auto o = make_obsy();
  auto w = make_workload(WorkloadId(10));
  CHECK_EQ(o.register_workload(w), Status::Ok);
  auto b = make_baseline(BaselineId(5), WorkloadId(10), 100.0);
  CHECK_EQ(o.publish_baseline(b), Status::Ok);
  auto got = o.lookup_baseline(BaselineId(5));
  CHECK(got.has_value());
  CHECK_NEAR(got->mean_value, 100.0, 1e-9);
  CHECK_EQ(o.baseline_count(), (std::size_t)1);
}

TEST(compatible_baseline_compare) {
  auto o = make_obsy();
  auto w = make_workload(WorkloadId(10));
  auto n = make_workload(WorkloadId(20));
  o.register_workload(w);
  o.register_workload(n);
  o.publish_baseline(make_baseline(BaselineId(5), WorkloadId(10), 100.0));

  Policy policy;
  std::vector<Observation> obs;
  for (int i = 0; i < 5; ++i) {
    obs.push_back(make_obs(ObservationId(100 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  }
  auto p = o.compare(w, n, InterferenceDomain::MemoryBandwidth, obs, policy);
  CHECK_EQ(p.outcome, Outcome::InterferenceDetected);
  CHECK_EQ(p.attribution, AttributionStrength::ControlledComparison);
  CHECK(p.sample_count >= 3);
  CHECK(p.degradation.find(MetricKind::Latency).has_value());
  CHECK(p.confidence > 0.0);
}

TEST(incompatible_baseline_rejected) {
  auto o = make_obsy();
  auto w = make_workload(WorkloadId(10));
  auto n = make_workload(WorkloadId(20));
  o.register_workload(w);
  o.register_workload(n);
  auto b = make_baseline(BaselineId(5), WorkloadId(10), 100.0);
  b.workload_generation = WorkloadGeneration(999999);  // different generation -> incompatible
  b.config_fingerprint = "cfg-different";
  o.publish_baseline(b);

  Policy policy;
  std::vector<Observation> obs;
  for (int i = 0; i < 5; ++i) obs.push_back(make_obs(ObservationId(100 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(w, n, InterferenceDomain::MemoryBandwidth, obs, policy);
  CHECK_EQ(p.outcome, Outcome::BaselineInvalid);
}

TEST(stale_baseline_revalidation) {
  auto o = make_obsy();
  auto w = make_workload(WorkloadId(10));
  auto n = make_workload(WorkloadId(20));
  o.register_workload(w);
  o.register_workload(n);
  // baseline at ts 100000, but observations are ~1 hour+ later -> expired
  o.publish_baseline(make_baseline(BaselineId(5), WorkloadId(10), 100.0, 10, 100000));
  Policy policy;
  std::vector<Observation> obs;
  for (int i = 0; i < 5; ++i) obs.push_back(make_obs(ObservationId(100 + i), WorkloadId(10), 130.0, 100000 + policy.max_age_ms + 5000, {WorkloadId(20)}));
  auto p = o.compare(w, n, InterferenceDomain::MemoryBandwidth, obs, policy);
  CHECK_EQ(p.outcome, Outcome::RevalidationRequired);
}

TEST(missing_baseline_insufficient) {
  auto o = make_obsy();
  auto w = make_workload(WorkloadId(10));
  auto n = make_workload(WorkloadId(20));
  o.register_workload(w);
  o.register_workload(n);
  Policy policy;
  std::vector<Observation> obs;
  for (int i = 0; i < 5; ++i) obs.push_back(make_obs(ObservationId(100 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(w, n, InterferenceDomain::MemoryBandwidth, obs, policy);
  CHECK_EQ(p.outcome, Outcome::InsufficientEvidence);
  CHECK_EQ(p.attribution, AttributionStrength::Unknown);
}

TEST(directional_asymmetry) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  o.publish_baseline(make_baseline(BaselineId(2), WorkloadId(20), 100.0));

  Policy policy;
  std::vector<Observation> a_obs, b_obs;
  for (int i = 0; i < 5; ++i) {
    a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
    b_obs.push_back(make_obs(ObservationId(20 + i), WorkloadId(20), 105.0, 100001 + i, {WorkloadId(10)}));
  }
  auto a_to_b = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);  // A affected by B
  auto b_to_a = o.compare(wb, wa, InterferenceDomain::MemoryBandwidth, b_obs, policy);  // B affected by A
  CHECK_EQ(a_to_b.outcome, Outcome::InterferenceDetected);
  CHECK_EQ(b_to_a.outcome, Outcome::PotentialInterference);
  CHECK(a_to_b.victim == WorkloadId(10));
  CHECK(b_to_a.victim == WorkloadId(20));
  // Directional cells must remain distinct.
  auto m = o.pairwise_matrix();
  CHECK(m.get(CellKey{WorkloadId(10), WorkloadId(20), InterferenceDomain::MemoryBandwidth}).has_value());
  CHECK(m.get(CellKey{WorkloadId(20), WorkloadId(10), InterferenceDomain::MemoryBandwidth}).has_value());
}

TEST(no_interference_case) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 101.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::NoInterferenceDetected);
}

TEST(insufficient_evidence_few_samples) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 2; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::InsufficientEvidence);
}

TEST(confounded_comparison) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) {
    a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i,
                             {WorkloadId(20)}, {Confounder::ThermalThrottling}));
  }
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK(p.confounded);
  CHECK_EQ(p.attribution, AttributionStrength::Correlated);
  CHECK(p.confidence < 0.5);
}

TEST(episode_and_explanation) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(o.episode_count(), (std::size_t)1);
  auto eps = o.episodes();
  CHECK_EQ(eps.size(), (std::size_t)1);
  CHECK(eps[0].affected == WorkloadId(10));
  auto ex = o.explain(WorkloadId(10), WorkloadId(20), InterferenceDomain::MemoryBandwidth, policy);
  CHECK(ex.affected == WorkloadId(10));
  CHECK(ex.has_baseline);
  CHECK_EQ(ex.outcome, Outcome::InterferenceDetected);
  CHECK(ex.authority_only);
}

TEST(pairwise_matrix) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
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
  auto m = o.pairwise_matrix();
  CHECK_EQ(m.size(), (std::size_t)2);
}

TEST(digest_determinism) {
  auto o1 = make_obsy();
  auto o2 = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  for (auto* o : {&o1, &o2}) {
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

TEST(workload_generation_mismatch_invalidates) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  // affected descriptor with a NEW generation + different config -> binding mismatch
  auto wa2 = wa;
  wa2.generation = WorkloadGeneration(5555);
  wa2.config_fingerprint = "cfg-v2";
  std::vector<Observation> a_obs;
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto p = o.compare(wa2, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  CHECK_EQ(p.outcome, Outcome::BaselineInvalid);
}

TEST(stale_worker_boot) {
  auto o = make_obsy();
  auto wa = make_workload(WorkloadId(10));
  auto wb = make_workload(WorkloadId(20));
  o.register_workload(wa);
  o.register_workload(wb);
  o.register_worker(WorkerId(22), WorkerBootId(1), "w");
  o.publish_baseline(make_baseline(BaselineId(1), WorkloadId(10), 100.0));
  Policy policy;
  std::vector<Observation> a_obs;
  // New boot id -> worker restart; baseline bound to boot 1 is now stale authority.
  for (int i = 0; i < 5; ++i) a_obs.push_back(make_obs(ObservationId(10 + i), WorkloadId(10), 130.0, 100001 + i, {WorkloadId(20)}));
  auto st = o.register_worker(WorkerId(22), WorkerBootId(999), "w", true);
  CHECK_EQ(st, Status::StaleAuthority);
  // A fresh worker incarnation: the affected workload is now bound to boot 999.
  wa.worker_boot = WorkerBootId(999);
  wa.worker_id = WorkerId(22);
  auto p = o.compare(wa, wb, InterferenceDomain::MemoryBandwidth, a_obs, policy);
  // The baseline is bound to old boot; a fresh-affinity comparison finds baseline incompatible.
  CHECK_EQ(p.outcome, Outcome::BaselineInvalid);
}
