#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"
#include "observatory/workload.hpp"
#include "observatory/observation.hpp"
#include "observatory/baseline.hpp"
#include "observatory/pairwise.hpp"
#include "observatory/episode.hpp"
#include "observatory/experiment.hpp"
#include "observatory/explanation.hpp"
#include "observatory/capability.hpp"
#include "observatory/policy.hpp"
#include "observatory/status.hpp"
#include "observatory/digest.hpp"

namespace iobs {

// The Interference Observatory. Owns cross-workload interference evidence, shared-resource
// observation, baseline construction, isolated-vs-co-run comparison, episodes, degradation
// measurement, classification, confidence, and persistence/replay. It never takes control
// actions (no throttle/preempt/migrate/reassign) — that is Contention Governor's scope.
class InterferenceObservatory {
 public:
  InterferenceObservatory(ObservatoryId id, ObservatoryGeneration generation);
  ~InterferenceObservatory();
  InterferenceObservatory(const InterferenceObservatory&) = delete;
  InterferenceObservatory& operator=(const InterferenceObservatory&) = delete;

  // Identity / epoch ---------------------------------------------------------
  [[nodiscard]] ObservatoryId observatory_id() const noexcept;
  [[nodiscard]] ObservatoryGeneration observatory_generation() const noexcept;
  [[nodiscard]] CoordinatorEpoch coordinator_epoch() const noexcept;
  // Advancing the epoch invalidates dynamic current evidence (-> REVALIDATION_REQUIRED) while
  // historical evidence remains historical. Workers must republish under the new epoch.
  void set_coordinator_epoch(CoordinatorEpoch epoch);

  // Registration --------------------------------------------------------------
  Status register_source(SourceId id, SourceGeneration gen, std::string name, BackendCapabilities caps);
  Status register_worker(WorkerId id, WorkerBootId boot, std::string name, bool connected = true);
  Status register_workload(WorkloadDescriptor workload);

  // Baselines ----------------------------------------------------------------
  Status publish_baseline(Baseline baseline);
  [[nodiscard]] std::optional<Baseline> lookup_baseline(BaselineId id) const;

  // Observations ---------------------------------------------------------------
  Status record_observation(Observation observation);

  // Comparison ----------------------------------------------------------------
  // Produce a directional interference result for affected against associated using the active
  // binding-compatible baseline for the affected workload. Records the comparison and, when
  // interference is indicated, an episode.
  PairwiseInterference compare(const WorkloadDescriptor& affected,
                               const WorkloadDescriptor& associated,
                               InterferenceDomain domain,
                               const std::vector<Observation>& co_run_observations,
                               const Policy& policy);

  // Recovery ----------------------------------------------------------------
  // Records that a victim measurably recovered after the neighbor was removed (neighbor removal),
  // which upgrades the relevant comparison to STRONG_COUNTERFACTUAL_EVIDENCE.
  Status record_recovery(WorkloadId victim, WorkloadId neighbor, InterferenceDomain domain);

  // Experiment ----------------------------------------------------------------
  ExperimentResult run_experiment(const ExperimentPlan& plan, const Policy& policy);

  // Explanation / matrix / static views --------------------------------------
  InterferenceExplanation explain(WorkloadId affected, WorkloadId associated,
                                  InterferenceDomain domain, const Policy& policy) const;
  InterferenceMatrix pairwise_matrix() const;  // ordered, deterministic
  std::vector<InterferenceEpisode> episodes() const;  // ordered by id
  [[nodiscard]] std::size_t observation_count() const noexcept;
  [[nodiscard]] std::size_t baseline_count() const noexcept;
  [[nodiscard]] std::size_t source_count() const noexcept;
  [[nodiscard]] std::size_t workload_count() const noexcept;
  [[nodiscard]] std::size_t worker_count() const noexcept;
  [[nodiscard]] size_t episode_count() const noexcept;
  [[nodiscard]] std::vector<Observation> observations_for(WorkloadId w) const;

  // Persistence / replay / digest ---------------------------------------------
  Status save(const std::string& path) const;
  Status load(const std::string& path);
  [[nodiscard]] std::string canonical_digest() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::mutex mutex_;

  std::vector<std::uint8_t> serialize_state() const;
  bool deserialize_state(const std::vector<std::uint8_t>& payload, std::string& reason);
  void hash_state(DigestBuilder& b) const;
};

// Free helpers.
[[nodiscard]] Outcome classify_outcome(double worst_relative_degradation, const Policy& policy) noexcept;
[[nodiscard]] double relative_degradation(const Baseline& baseline, const Measurement& measurement) noexcept;
[[nodiscard]] bool metric_is_worse(MetricKind kind, double relative) noexcept;
[[nodiscard]] double degradation_magnitude(MetricKind kind, double relative) noexcept;

}  // namespace iobs
