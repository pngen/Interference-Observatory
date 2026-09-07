#include "observatory/observatory.hpp"
#include "observatory/digest.hpp"
#include "observatory/persist.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <tuple>
#include <filesystem>

namespace iobs {

namespace {

// Minimal bounded binary codec used for the deterministic state payload.
struct Writer {
  std::vector<std::uint8_t> buf;
  void u8(std::uint8_t v) { buf.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) { buf.push_back(static_cast<std::uint8_t>(v & 0xFFu)); v >>= 8; } }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) { buf.push_back(static_cast<std::uint8_t>(v & 0xFFu)); v >>= 8; } }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) { std::uint64_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); u64(bits); }
  void flag(bool v) { u8(v ? 1 : 0); }
  void str(const std::string& s) { u64(static_cast<std::uint64_t>(s.size())); for (char c : s) u8(static_cast<std::uint8_t>(c)); }
  template <typename Id> void id(const Id& v) { u64(v.value()); }
  template <typename E> void en(E e) { u8(static_cast<std::uint8_t>(e)); }
};

struct Reader {
  const std::uint8_t* p = nullptr;
  std::size_t n = 0;
  std::size_t pos = 0;
  bool ok = true;

  [[nodiscard]] bool need(std::size_t k) const { return pos + k <= n; }
  std::uint8_t u8() {
    if (!need(1)) { ok = false; return 0; }
    return p[pos++];
  }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) { if (!need(1)) { ok = false; return v; } v |= static_cast<std::uint32_t>(p[pos++]) << (i * 8); }
    return v;
  }
  std::uint64_t u64() {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) { if (!need(1)) { ok = false; return v; } v |= static_cast<std::uint64_t>(p[pos++]) << (i * 8); }
    return v;
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  double f64() { std::uint64_t bits = u64(); double v = 0.0; std::memcpy(&v, &bits, sizeof(v)); return v; }
  bool flag() { return u8() != 0; }
  std::string str() {
    std::uint64_t len = u64();
    if (!ok || len > 16u * 1024u * 1024u) { ok = false; return {}; }
    if (!need(static_cast<std::size_t>(len))) { ok = false; return {}; }
    std::string s(reinterpret_cast<const char*>(p + pos), static_cast<std::size_t>(len));
    pos += static_cast<std::size_t>(len);
    return s;
  }
  template <typename Id> Id id() { return Id(u64()); }
};

template <typename E>
bool valid_enum_value(E e, std::uint8_t max) { return static_cast<std::uint8_t>(e) < max; }

// Confounders that inherently invalidate attribution (a different workload input, kernel
// version, driver/runtime state, worker restart, placement change, or power/thermal/clock change
// present an alternative explanation for any observed slowdown).
bool is_hard_confounder(Confounder c) noexcept {
  switch (c) {
    case Confounder::ThermalThrottling:
    case Confounder::PowerThrottling:
    case Confounder::ClockChange:
    case Confounder::DifferentWorkloadInput:
    case Confounder::DifferentKernelVersion:
    case Confounder::DifferentDriverRuntimeState:
    case Confounder::WorkerRestart:
    case Confounder::DifferentPlacement:
      return true;
    default:
      return false;
  }
}

}  // namespace

// ---- free helpers ----------------------------------------------------------

bool metric_is_worse(MetricKind kind, double relative) noexcept {
  switch (kind) {
    case MetricKind::Latency: case MetricKind::DeviceTime: case MetricKind::TransferTime:
    case MetricKind::CollectiveTime: case MetricKind::StallTime: case MetricKind::TailLatency:
      return relative > 0.0;
    case MetricKind::Throughput: case MetricKind::CompletionRate: case MetricKind::MemoryBandwidth:
      return relative < 0.0;
  }
  return false;
}

double degradation_magnitude(MetricKind kind, double relative) noexcept {
  return metric_is_worse(kind, relative) ? std::fabs(relative) : 0.0;
}

double relative_degradation(const Baseline& baseline, const Measurement& measurement) noexcept {
  if (baseline.mean_value <= 0.0) return 0.0;
  return (measurement.value - baseline.mean_value) / baseline.mean_value;
}

Outcome classify_outcome(double worst_relative_degradation, const Policy& policy) noexcept {
  if (worst_relative_degradation < policy.potential_threshold) return Outcome::NoInterferenceDetected;
  if (worst_relative_degradation < policy.detected_threshold) return Outcome::PotentialInterference;
  if (worst_relative_degradation < policy.severe_threshold) return Outcome::InterferenceDetected;
  return Outcome::SevereInterference;
}

struct InterferenceObservatory::Impl {
  struct SourceRec {
    SourceGeneration generation;
    std::string name;
    BackendCapabilities caps;
    Freshness freshness = Freshness::Unknown;
  };
  struct WorkerRec {
    WorkerBootId boot;
    std::string name;
    bool connected = false;
  };

  ObservatoryId id;
  ObservatoryGeneration generation;
  CoordinatorEpoch epoch{1};

  std::map<SourceId, SourceRec> sources;
  std::map<WorkerId, WorkerRec> workers;
  std::map<WorkloadId, WorkloadDescriptor> workloads;
  std::map<BaselineId, Baseline> baselines;
  std::map<ObservationId, Observation> observations;
  std::map<EpisodeId, InterferenceEpisode> episodes;
  std::map<ComparisonId, PairwiseInterference> comparisons;
  InterferenceMatrix matrix;
  std::set<std::tuple<WorkloadId, WorkloadId, InterferenceDomain>> recoveries;
};

InterferenceObservatory::InterferenceObservatory(ObservatoryId id, ObservatoryGeneration generation)
    : impl_(std::make_unique<Impl>()) {
  impl_->id = id;
  impl_->generation = generation;
}

InterferenceObservatory::~InterferenceObservatory() = default;

ObservatoryId InterferenceObservatory::observatory_id() const noexcept { return impl_->id; }
ObservatoryGeneration InterferenceObservatory::observatory_generation() const noexcept { return impl_->generation; }
CoordinatorEpoch InterferenceObservatory::coordinator_epoch() const noexcept { return impl_->epoch; }

void InterferenceObservatory::set_coordinator_epoch(CoordinatorEpoch epoch) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (epoch == impl_->epoch) return;
  impl_->epoch = epoch;
  // Advancing the epoch marks dynamic baselines/sources/workers as requiring revalidation.
  for (auto& [id, b] : impl_->baselines) {
    if (b.type == BaselineType::HistoricalIsolated || b.freshness == Freshness::Historical) continue;
    b.freshness = Freshness::RevalidationRequired;
  }
  for (auto& [id, s] : impl_->sources) {
    if (s.freshness != Freshness::Historical) s.freshness = Freshness::RevalidationRequired;
  }
  for (auto& [id, w] : impl_->workers) {
    w.connected = false;
  }
}

Status InterferenceObservatory::register_source(SourceId id, SourceGeneration gen, std::string name, BackendCapabilities caps) {
  if (id.is_nil() || gen.is_nil()) return Status::InvalidInput;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->sources.find(id);
  if (it != impl_->sources.end()) {
    if (it->second.generation != gen) {
      it->second.generation = gen;
      it->second.name = std::move(name);
      it->second.caps = caps;
      it->second.freshness = Freshness::RevalidationRequired;
      return Status::StaleAuthority;
    }
    it->second.name = std::move(name);
    it->second.caps = caps;
    it->second.freshness = Freshness::Current;
    return Status::Ok;
  }
  impl_->sources.emplace(id, Impl::SourceRec{gen, std::move(name), caps, Freshness::Current});
  return Status::Ok;
}

Status InterferenceObservatory::register_worker(WorkerId id, WorkerBootId boot, std::string name, bool connected) {
  if (id.is_nil() || boot.is_nil()) return Status::InvalidInput;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->workers.find(id);
  if (it != impl_->workers.end()) {
    // A worker reconnecting with a new boot id is a fresh incarnation; old-boot evidence is stale.
    if (it->second.boot != boot) {
      it->second.boot = boot;
      it->second.name = std::move(name);
      it->second.connected = connected;
      return Status::StaleAuthority;  // signal caller: boot changed, prior evidence invalid
    }
    it->second.name = std::move(name);
    it->second.connected = connected;
    return Status::Ok;
  }
  impl_->workers.emplace(id, Impl::WorkerRec{boot, std::move(name), connected});
  return Status::Ok;
}

Status InterferenceObservatory::register_workload(WorkloadDescriptor workload) {
  if (workload.id.is_nil() || workload.generation.is_nil()) return Status::InvalidInput;
  std::lock_guard<std::mutex> lock(mutex_);
  impl_->workloads[workload.id] = std::move(workload);
  return Status::Ok;
}

Status InterferenceObservatory::publish_baseline(Baseline baseline) {
  if (baseline.id.is_nil() || baseline.generation.is_nil()) return Status::InvalidInput;
  if (!baseline.is_usable()) return Status::InsufficientEvidence;
  if (!checked::is_finite(baseline.confidence) || baseline.confidence < 0.0 || baseline.confidence > 1.0) return Status::InvalidInput;
  if (!checked::is_finite(baseline.variance) || baseline.variance < 0.0) return Status::InvalidInput;
  if (baseline.sample_count == 0) return Status::InsufficientEvidence;
  if (baseline.metric_kind != MetricKind::Latency && baseline.metric_kind != MetricKind::Throughput &&
      baseline.metric_kind != MetricKind::DeviceTime && baseline.metric_kind != MetricKind::TransferTime &&
      baseline.metric_kind != MetricKind::CollectiveTime && baseline.metric_kind != MetricKind::StallTime &&
      baseline.metric_kind != MetricKind::TailLatency && baseline.metric_kind != MetricKind::CompletionRate &&
      baseline.metric_kind != MetricKind::MemoryBandwidth) return Status::InvalidInput;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->baselines.find(baseline.id);
  if (it != impl_->baselines.end()) {
    // Idempotent duplicate: identical content is a no-op; conflicting content is rejected.
    const Baseline& b = it->second;
    if (b.generation == baseline.generation && b.workload_id == baseline.workload_id &&
        b.device_id == baseline.device_id && b.mean_value == baseline.mean_value &&
        b.sample_count == baseline.sample_count) {
      return Status::Ok;  // idempotent
    }
    return Status::InvalidInput;  // conflicting duplicate id
  }
  if (baseline.freshness == Freshness::Unknown) baseline.freshness = Freshness::Current;
  impl_->baselines.emplace(baseline.id, std::move(baseline));
  return Status::Ok;
}

std::optional<Baseline> InterferenceObservatory::lookup_baseline(BaselineId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->baselines.find(id);
  if (it == impl_->baselines.end()) return std::nullopt;
  return it->second;
}

Status InterferenceObservatory::record_observation(Observation observation) {
  if (observation.id.is_nil() || observation.generation.is_nil()) return Status::InvalidInput;
  if (observation.workload_id.is_nil()) return Status::InvalidInput;
  if (!observation.measurement.is_valid()) return Status::InvalidInput;
  if (!checked::is_finite(static_cast<double>(observation.timestamp_ms))) return Status::InvalidInput;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->observations.find(observation.id);
  if (it != impl_->observations.end()) {
    // Idempotent duplicate: identical content is a no-op, never double-counted.
    const Observation& o = it->second;
    if (o.timestamp_ms == observation.timestamp_ms &&
        o.measurement.value == observation.measurement.value &&
        o.workload_id == observation.workload_id) {
      return Status::Ok;
    }
    return Status::InvalidInput;  // conflicting duplicate id
  }
  impl_->observations.emplace(observation.id, std::move(observation));
  return Status::Ok;
}

Status InterferenceObservatory::record_recovery(WorkloadId victim, WorkloadId neighbor, InterferenceDomain domain) {
  if (victim.is_nil() || neighbor.is_nil()) return Status::InvalidInput;
  std::lock_guard<std::mutex> lock(mutex_);
  impl_->recoveries.insert(std::make_tuple(victim, neighbor, domain));
  return Status::Ok;
}

PairwiseInterference InterferenceObservatory::compare(const WorkloadDescriptor& affected,
                                                      const WorkloadDescriptor& associated,
                                                      InterferenceDomain domain,
                                                      const std::vector<Observation>& co_run_observations,
                                                      const Policy& policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  PairwiseInterference result;
  result.victim = affected.id;
  result.neighbor = associated.id;
  result.domain = domain;
  result.comparison_generation = ComparisonGeneration(1);
  result.epoch = impl_->epoch;
  result.freshness = Freshness::Unknown;

  if (!policy.isValid()) {
    result.outcome = Outcome::Unknown;
    result.attribution = AttributionStrength::Unknown;
    return result;
  }

  // Select the most recent binding-compatible, usable baseline for the affected workload/device.
  const Baseline* usable = nullptr;
  const Baseline* any_match = nullptr;
  for (const auto& [bid, b] : impl_->baselines) {
    if (b.workload_id == affected.id && b.device_id == affected.device_id) {
      if (!any_match) any_match = &b;
      if (b.is_usable() && b.binding_matches(affected.binding())) {
        if (!usable || b.timestamp_ms > usable->timestamp_ms ||
            (b.timestamp_ms == usable->timestamp_ms && b.id.value() > usable->id.value())) {
          usable = &b;
        }
      }
    }
  }

  // Validate freshness window.
  const std::int64_t now = co_run_observations.empty() ? 0 : co_run_observations.back().timestamp_ms;
  Freshness baseline_fresh = Freshness::Unknown;
  bool fresh_ok = false;
  if (usable != nullptr) {
    baseline_fresh = usable->freshness;
    if (baseline_fresh == Freshness::Current) {
      const std::int64_t age = now - usable->timestamp_ms;
      if (age <= policy.stale_after_ms) { fresh_ok = true; }
      else if (age <= policy.max_age_ms) { baseline_fresh = Freshness::Stale; }
      else { baseline_fresh = Freshness::Expired; }
    }
    // Historical / RevalidationRequired baselines are never authoritative for current decisions.
  }

  if (!any_match) {
    // No baseline exists at all for this workload + device.
    result.outcome = Outcome::InsufficientEvidence;
    result.freshness = Freshness::Unknown;
    return result;
  }
  if (!usable) {
    // A baseline exists but none matches the current binding (generation/shape/placement mismatch).
    result.outcome = Outcome::BaselineInvalid;
    result.freshness = Freshness::Unknown;
    return result;
  }
  if (!fresh_ok) {
    result.outcome = Outcome::RevalidationRequired;
    result.freshness = baseline_fresh;
    return result;
  }

  // Aggregate the affected workload's co-run measurements.
  double mean = 0.0, variance = 0.0, p95 = 0.0;
  std::uint64_t n = 0;
  bool has_p95 = false;
  bool overlap = false;
  std::vector<double> vals;
  for (const auto& o : co_run_observations) {
    if (o.workload_id != affected.id) continue;
    if (o.measurement.kind != usable->metric_kind) continue;
    if (!o.measurement.is_valid()) continue;
    vals.push_back(o.measurement.value);
    if (o.measurement.has_p95 && o.measurement.p95 > p95) { p95 = o.measurement.p95; has_p95 = true; }
    for (const auto& peer : o.co_run_peers) {
      if (peer == associated.id) { overlap = true; }
    }
    ++n;
  }

  result.baseline_id = usable->id;
  result.sample_count = n;
  result.freshness = baseline_fresh;

  if (n == 0) {
    result.outcome = Outcome::InsufficientEvidence;
    return result;
  }
  if (n < policy.min_samples) {
    result.outcome = Outcome::InsufficientEvidence;
    result.confidence = 0.0;
    return result;
  }
  for (double v : vals) mean += v;
  mean /= static_cast<double>(n);
  if (n > 1) {
    double s = 0.0;
    for (double v : vals) { double d = v - mean; s += d * d; }
    variance = s / static_cast<double>(n - 1);
  }

  const double relative = relative_degradation(*usable, Measurement{usable->metric_kind, mean});
  const double magnitude = degradation_magnitude(usable->metric_kind, relative);

  // Confounder analysis.
  std::vector<Confounder> unresolved_hard;
  std::vector<Confounder> unresolved_soft;
  for (const auto& o : co_run_observations) {
    if (o.workload_id != affected.id) continue;
    for (Confounder c : o.confounders) {
      if (std::find(unresolved_hard.begin(), unresolved_hard.end(), c) == unresolved_hard.end() &&
          std::find(unresolved_soft.begin(), unresolved_soft.end(), c) == unresolved_soft.end()) {
        if (is_hard_confounder(c)) unresolved_hard.push_back(c);
        else unresolved_soft.push_back(c);
      }
    }
  }
  result.confounded = !unresolved_hard.empty();
  result.unresolved_confounders = unresolved_hard;
  result.unresolved_confounders.insert(result.unresolved_confounders.end(),
                                       unresolved_soft.begin(), unresolved_soft.end());

  Degradation deg;
  deg.sample_count = n;
  MetricDelta delta;
  delta.kind = usable->metric_kind;
  delta.absolute = mean - usable->mean_value;
  delta.relative = relative;
  delta.sample_count = n;
  delta.variance = variance;
  deg.deltas.push_back(delta);
  deg.worst_relative = magnitude;
  result.degradation = deg;

  // Classification.
  Outcome outcome = classify_outcome(magnitude, policy);
  result.outcome = outcome;

  // Confidence: start from baseline confidence, discount for confounders and sample discipline.
  double conf = usable->confidence;
  if (fresh_ok && (baseline_fresh == Freshness::Stale)) conf *= 0.5;
  conf *= std::min(1.0, static_cast<double>(n) / static_cast<double>(policy.min_samples * 2));
  if (!result.confounded) {
    // soft confounders each reduce confidence 15%
    for (std::size_t i = 0; i < unresolved_soft.size(); ++i) conf *= 0.85;
  } else {
    conf *= 0.2;
  }
  result.confidence = std::clamp(conf, 0.0, 1.0);

  // Attribution strength ladder.
  AttributionStrength strength = AttributionStrength::Unknown;
  if (!overlap) {
    outcome = Outcome::NoInterferenceDetected;
    result.outcome = outcome;
    strength = AttributionStrength::Unknown;
  } else if (result.confounded) {
    strength = AttributionStrength::Correlated;
    if (unresolved_hard.size() >= 2) strength = AttributionStrength::TemporallyAssociated;
  } else {
    // A recovery record for this (victim, neighbor, domain) upgrades to counterfactual evidence.
    const bool has_recovery =
        impl_->recoveries.count(std::make_tuple(affected.id, associated.id, domain)) != 0;
    if (has_recovery) {
      strength = AttributionStrength::StrongCounterfactualEvidence;
    } else {
      strength = AttributionStrength::ControlledComparison;
    }
  }
  result.attribution = strength;

  // Evidence provenance: measured if every underlying observation is MEASURED or CONTROLLED.
  bool all_measured = true;
  for (const auto& o : co_run_observations) {
    if (o.workload_id != affected.id) continue;
    if (o.provenance != Provenance::Measured && o.provenance != Provenance::Controlled) all_measured = false;
  }
  result.evidence_sources.push_back(all_measured ? Provenance::Measured : Provenance::Controlled);

  // Stable content-derived identity so duplicate comparisons never double-count.
  {
    DigestBuilder db;
    db.add_u8(0xA1);
    db.add_id(affected.id);
    db.add_id(associated.id);
    db.add_enum(domain);
    db.add_id(usable->id);
    db.add_u64(n);
    std::uint64_t v = db.finish_u64();
    if (v == 0) v = 1;
    result.comparison_id = ComparisonId(v);
  }

  impl_->comparisons[result.comparison_id] = result;
  impl_->matrix.set(CellKey{affected.id, associated.id, domain}, result);

  // Build/update an episode when interference is indicated.
  if (outcome == Outcome::PotentialInterference || outcome == Outcome::InterferenceDetected ||
      outcome == Outcome::SevereInterference) {
    InterferenceEpisode ep;
    ep.affected = affected.id;
    ep.co_running = {associated.id};
    ep.domains = {domain};
    ep.baseline_id = usable->id;
    ep.degradation = deg;
    ep.confidence = result.confidence;
    ep.classification = outcome;
    ep.provenance = all_measured ? Provenance::Measured : Provenance::Controlled;
    ep.freshness = baseline_fresh;
    ep.epoch = impl_->epoch;
    ep.evidence.push_back(InterferenceEvidence{EvidenceId(1), EvidenceGeneration(1),
                                               ep.provenance, strength, outcome,
                                               "controlled co-run degradation"});
    ep.start_ms = co_run_observations.empty() ? 0 : co_run_observations.front().timestamp_ms;
    ep.end_ms = co_run_observations.empty() ? 0 : co_run_observations.back().timestamp_ms;

    DigestBuilder db;
    db.add_u8(0xB2);
    db.add_id(affected.id);
    db.add_enum(domain);
    db.add_id(usable->id);
    std::uint64_t v = db.finish_u64();
    if (v == 0) v = 1;
    ep.id = EpisodeId(v);
    ep.generation = EpisodeGeneration(1);
    impl_->episodes[ep.id] = std::move(ep);
    result.episode_id = EpisodeId(v);
  }

  return result;
}

ExperimentResult InterferenceObservatory::run_experiment(const ExperimentPlan& plan, const Policy& policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  ExperimentResult res;
  res.experiment_id = plan.id;
  res.generation = plan.generation;
  if (plan.id.is_nil()) return res;
  auto itP = impl_->workloads.find(plan.primary);
  auto itN = impl_->workloads.find(plan.neighbor);
  if (itP == impl_->workloads.end() || itN == impl_->workloads.end()) return res;

  std::vector<Observation> primary_obs, neighbor_obs;
  for (const auto& [oid, o] : impl_->observations) {
    if (o.workload_id == plan.primary) {
      for (const auto& peer : o.co_run_peers) {
        if (peer == plan.neighbor) { primary_obs.push_back(o); break; }
      }
    } else if (o.workload_id == plan.neighbor) {
      for (const auto& peer : o.co_run_peers) {
        if (peer == plan.primary) { neighbor_obs.push_back(o); break; }
      }
    }
  }
  if (!primary_obs.empty()) {
    res.primary_as_victim = compare(itP->second, itN->second, plan.domain, primary_obs, policy);
  }
  if (!neighbor_obs.empty()) {
    res.neighbor_as_victim = compare(itN->second, itP->second, plan.domain, neighbor_obs, policy);
  }
  res.repetitions_executed = plan.repetitions;
  res.complete = (res.primary_as_victim.has_value() && res.neighbor_as_victim.has_value());
  return res;
}

InterferenceExplanation InterferenceObservatory::explain(WorkloadId affected, WorkloadId associated,
                                                         InterferenceDomain domain,
                                                         const Policy& policy) const {
  std::lock_guard<std::mutex> lock(mutex_);
  InterferenceExplanation ex;
  ex.affected = affected;
  ex.associated.push_back(associated);
  ex.domain = domain;
  ex.authority_only = true;
  ex.attribution = AttributionStrength::Unknown;
  ex.outcome = Outcome::Unknown;

  auto cell = impl_->matrix.get(CellKey{affected, associated, domain});
  if (!cell) return ex;

  const PairwiseInterference& p = *cell;
  ex.degradation = p.degradation;
  ex.sample_count = p.sample_count;
  ex.confidence = p.confidence;
  ex.attribution = p.attribution;
  ex.outcome = p.outcome;
  ex.provenance = p.evidence_sources.empty() ? Provenance::Unknown : p.evidence_sources.front();
  ex.freshness = p.freshness;
  ex.baseline_id = p.baseline_id;
  ex.has_baseline = !p.baseline_id.is_nil();
  ex.confounders_unresolved = p.unresolved_confounders;

  if (!p.degradation.deltas.empty()) {
    ex.variance = p.degradation.deltas.front().variance;
  }

  // Structural "what would strengthen evidence" guidance (explanatory, never control).
  if (!ex.has_baseline) ex.what_would_strengthen_evidence.push_back("run isolated baseline");
  if (ex.sample_count < policy.min_samples) {
    ex.what_would_strengthen_evidence.push_back("increase repetitions");
    ex.outcome = Outcome::InsufficientEvidence;
  }
  if (ex.freshness != Freshness::Current) ex.what_would_strengthen_evidence.push_back("wait for source freshness");
  for (Confounder c : p.unresolved_confounders) {
    ex.what_would_strengthen_evidence.push_back("control " + std::string(to_string(c)));
  }
  if (ex.attribution == AttributionStrength::ControlledComparison) {
    ex.what_would_strengthen_evidence.push_back("remove neighbor workload and confirm recovery");
  }
  return ex;
}

InterferenceMatrix InterferenceObservatory::pairwise_matrix() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->matrix;
}

std::vector<InterferenceEpisode> InterferenceObservatory::episodes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<InterferenceEpisode> out;
  out.reserve(impl_->episodes.size());
  for (const auto& [id, e] : impl_->episodes) out.push_back(e);
  return out;
}

std::size_t InterferenceObservatory::observation_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->observations.size();
}
std::size_t InterferenceObservatory::baseline_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->baselines.size();
}
std::size_t InterferenceObservatory::source_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->sources.size();
}
std::size_t InterferenceObservatory::workload_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->workloads.size();
}
std::size_t InterferenceObservatory::worker_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->workers.size();
}
std::size_t InterferenceObservatory::episode_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->episodes.size();
}

std::vector<Observation> InterferenceObservatory::observations_for(WorkloadId w) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Observation> out;
  for (const auto& [id, o] : impl_->observations) {
    if (o.workload_id == w) out.push_back(o);
  }
  return out;
}

namespace {

std::uint32_t pack_caps(const BackendCapabilities& c) {
  std::uint32_t m = 0;
  if (c.cuda_device_discovery) m |= 1u << 0;
  if (c.cuda_memory_alloc) m |= 1u << 1;
  if (c.cuda_h2d) m |= 1u << 2;
  if (c.cuda_d2h) m |= 1u << 3;
  if (c.cuda_kernels) m |= 1u << 4;
  if (c.cuda_events) m |= 1u << 5;
  if (c.cuda_memory_accounting) m |= 1u << 6;
  if (c.nvml_utilization) m |= 1u << 7;
  if (c.pcie_counters) m |= 1u << 8;
  if (c.cache_counters) m |= 1u << 9;
  if (c.memory_bandwidth_counters) m |= 1u << 10;
  if (c.nvlink_counters) m |= 1u << 11;
  if (c.collective_metrics) m |= 1u << 12;
  if (c.numa_topology) m |= 1u << 13;
  if (c.storage_metrics) m |= 1u << 14;
  if (c.power_clock_telemetry) m |= 1u << 15;
  if (c.host_cpu_metrics) m |= 1u << 16;
  return m;
}

BackendCapabilities unpack_caps(std::uint32_t m) {
  BackendCapabilities c;
  c.cuda_device_discovery = (m & (1u << 0)) != 0;
  c.cuda_memory_alloc = (m & (1u << 1)) != 0;
  c.cuda_h2d = (m & (1u << 2)) != 0;
  c.cuda_d2h = (m & (1u << 3)) != 0;
  c.cuda_kernels = (m & (1u << 4)) != 0;
  c.cuda_events = (m & (1u << 5)) != 0;
  c.cuda_memory_accounting = (m & (1u << 6)) != 0;
  c.nvml_utilization = (m & (1u << 7)) != 0;
  c.pcie_counters = (m & (1u << 8)) != 0;
  c.cache_counters = (m & (1u << 9)) != 0;
  c.memory_bandwidth_counters = (m & (1u << 10)) != 0;
  c.nvlink_counters = (m & (1u << 11)) != 0;
  c.collective_metrics = (m & (1u << 12)) != 0;
  c.numa_topology = (m & (1u << 13)) != 0;
  c.storage_metrics = (m & (1u << 14)) != 0;
  c.power_clock_telemetry = (m & (1u << 15)) != 0;
  c.host_cpu_metrics = (m & (1u << 16)) != 0;
  return c;
}

constexpr std::uint32_t kCountBound = 50'000'000u;

}  // namespace

std::vector<std::uint8_t> InterferenceObservatory::serialize_state() const {
  Writer w;
  w.u8(1);
  w.id(impl_->id);
  w.id(impl_->generation);
  w.id(impl_->epoch);

  w.u32(static_cast<std::uint32_t>(impl_->sources.size()));
  for (const auto& [id, s] : impl_->sources) {
    w.id(id); w.id(s.generation); w.str(s.name); w.u32(pack_caps(s.caps)); w.en(s.freshness);
  }
  w.u32(static_cast<std::uint32_t>(impl_->workers.size()));
  for (const auto& [id, s] : impl_->workers) {
    w.id(id); w.id(s.boot); w.str(s.name); w.flag(s.connected);
  }
  w.u32(static_cast<std::uint32_t>(impl_->workloads.size()));
  for (const auto& [id, wd] : impl_->workloads) {
    w.id(id); w.id(wd.generation); w.str(wd.name); w.str(wd.config_fingerprint);
    w.str(wd.problem_size_fingerprint); w.id(wd.device_id); w.id(wd.device_generation);
    w.id(wd.worker_id); w.id(wd.worker_boot); w.str(wd.policy_generation);
    w.str(wd.runtime_generation); w.str(wd.kernel_generation); w.id(wd.topology_generation);
    w.i64(wd.start_ms); w.i64(wd.end_ms); w.u64(wd.attempt_id); w.str(wd.priority_class);
  }
  w.u32(static_cast<std::uint32_t>(impl_->baselines.size()));
  for (const auto& [id, b] : impl_->baselines) {
    w.id(id); w.id(b.generation); w.en(b.type); w.id(b.workload_id); w.id(b.workload_generation);
    w.id(b.device_id); w.id(b.device_generation); w.id(b.worker_id); w.id(b.worker_boot);
    w.str(b.config_fingerprint); w.str(b.problem_size_fingerprint); w.str(b.runtime_generation);
    w.str(b.kernel_generation); w.str(b.policy_generation); w.id(b.topology_generation);
    w.id(b.source_generation); w.id(b.evidence_generation); w.i64(b.timestamp_ms); w.i64(b.interval_ms);
    w.en(b.provenance); w.en(b.freshness); w.u64(b.sample_count); w.f64(b.variance);
    w.f64(b.confidence); w.f64(b.mean_value); w.en(b.metric_kind); w.flag(b.valid);
  }
  w.u32(static_cast<std::uint32_t>(impl_->observations.size()));
  for (const auto& [id, o] : impl_->observations) {
    w.id(id); w.id(o.generation); w.id(o.source_id); w.id(o.source_generation);
    w.id(o.worker_id); w.id(o.worker_boot); w.id(o.workload_id); w.id(o.workload_generation);
    w.id(o.device_id); w.id(o.device_generation); w.i64(o.timestamp_ms);
    w.en(o.measurement.kind); w.f64(o.measurement.value); w.str(o.measurement.unit);
    w.u64(o.measurement.sample_count); w.f64(o.measurement.variance); w.f64(o.measurement.p95);
    w.flag(o.measurement.has_p95);
    w.en(o.provenance); w.en(o.freshness); w.en(o.source_health); w.u32(pack_caps(o.capabilities));
    w.u32(static_cast<std::uint32_t>(o.co_run_peers.size()));
    for (const auto& peer : o.co_run_peers) w.id(peer);
    w.u32(static_cast<std::uint32_t>(o.confounders.size()));
    for (auto c : o.confounders) w.en(c);
  }
  w.u32(static_cast<std::uint32_t>(impl_->episodes.size()));
  for (const auto& [id, e] : impl_->episodes) {
    w.id(id); w.id(e.generation); w.i64(e.start_ms); w.i64(e.end_ms); w.flag(e.open);
    w.id(e.affected);
    w.u32(static_cast<std::uint32_t>(e.co_running.size()));
    for (const auto& x : e.co_running) w.id(x);
    w.u32(static_cast<std::uint32_t>(e.domains.size()));
    for (auto d : e.domains) w.en(d);
    w.id(e.baseline_id);
    w.u64(e.degradation.sample_count);
    w.u32(static_cast<std::uint32_t>(e.degradation.deltas.size()));
    for (const auto& d : e.degradation.deltas) { w.en(d.kind); w.f64(d.absolute); w.f64(d.relative); w.u64(d.sample_count); w.f64(d.variance); }
    w.f64(e.degradation.worst_relative);
    w.f64(e.confidence); w.en(e.classification); w.en(e.provenance); w.en(e.freshness); w.id(e.epoch);
    w.u32(static_cast<std::uint32_t>(e.evidence.size()));
    for (const auto& ev : e.evidence) { w.id(ev.id); w.id(ev.generation); w.en(ev.provenance); w.en(ev.strength); w.en(ev.outcome); w.str(ev.summary); }
  }
  w.u32(static_cast<std::uint32_t>(impl_->comparisons.size()));
  for (const auto& [cid, p] : impl_->comparisons) {
    w.id(cid); w.id(p.victim); w.id(p.neighbor); w.en(p.domain); w.id(p.baseline_id);
    w.u64(p.degradation.sample_count);
    w.u32(static_cast<std::uint32_t>(p.degradation.deltas.size()));
    for (const auto& d : p.degradation.deltas) { w.en(d.kind); w.f64(d.absolute); w.f64(d.relative); w.u64(d.sample_count); w.f64(d.variance); }
    w.f64(p.degradation.worst_relative);
    w.f64(p.confidence); w.u64(p.sample_count); w.en(p.freshness); w.en(p.outcome); w.en(p.attribution);
    w.id(p.episode_id); w.id(p.comparison_id); w.id(p.comparison_generation); w.id(p.epoch);
    w.u32(static_cast<std::uint32_t>(p.evidence_sources.size()));
    for (auto ps : p.evidence_sources) w.en(ps);
    w.flag(p.confounded);
    w.u32(static_cast<std::uint32_t>(p.unresolved_confounders.size()));
    for (auto c : p.unresolved_confounders) w.en(c);
  }
  w.u32(static_cast<std::uint32_t>(impl_->recoveries.size()));
  for (const auto& t : impl_->recoveries) { w.id(std::get<0>(t)); w.id(std::get<1>(t)); w.en(std::get<2>(t)); }
  return std::move(w.buf);
}

void InterferenceObservatory::hash_state(DigestBuilder& b) const {
  auto bytes = serialize_state();
  b.add_bytes(bytes.data(), bytes.size());
}

std::string InterferenceObservatory::canonical_digest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  DigestBuilder b;
  hash_state(b);
  return b.finish();
}

Status InterferenceObservatory::save(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    auto payload = serialize_state();
    auto encoded = persist::encode(payload);
    {
      std::ofstream f(path + ".tmp", std::ios::binary | std::ios::trunc);
      if (!f) return Status::PersistenceCorrupt;
      f.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
      f.flush();
      if (!f.good()) { f.close(); std::filesystem::remove(path + ".tmp"); return Status::PersistenceCorrupt; }
    }
    std::error_code ec;
    std::filesystem::rename(path + ".tmp", path, ec);
    if (ec) { std::filesystem::remove(path + ".tmp"); return Status::PersistenceCorrupt; }
    return Status::Ok;
  } catch (const Error& e) {
    return e.status();
  } catch (...) {
    return Status::ResourceExhausted;
  }
}

Status InterferenceObservatory::load(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ifstream f(path, std::ios::binary);
  if (!f) return Status::PersistenceCorrupt;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  f.close();
  if (bytes.empty()) return Status::PersistenceCorrupt;
  std::vector<std::uint8_t> payload;
  std::string reason;
  if (!persist::decode(bytes, payload, reason)) return Status::PersistenceCorrupt;
  if (!deserialize_state(payload, reason)) return Status::PersistenceCorrupt;
  return Status::Ok;
}

bool InterferenceObservatory::deserialize_state(const std::vector<std::uint8_t>& payload, std::string& reason) {
  reason.clear();
  if (payload.empty()) { reason = "empty payload"; return false; }
  Reader r{payload.data(), payload.size(), 0};
  const auto version = r.u8();
  if (version != 1) { reason = "unknown payload version"; return false; }

  impl_->sources.clear();
  impl_->workers.clear();
  impl_->workloads.clear();
  impl_->baselines.clear();
  impl_->observations.clear();
  impl_->episodes.clear();
  impl_->comparisons.clear();
  impl_->recoveries.clear();

  impl_->id = r.id<ObservatoryId>();
  impl_->generation = r.id<ObservatoryGeneration>();
  impl_->epoch = r.id<CoordinatorEpoch>();

  std::uint32_t count = r.u32();
  if (count > kCountBound) { reason = "source count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    Impl::SourceRec s;
    auto id = r.id<SourceId>();
    s.generation = r.id<SourceGeneration>();
    s.name = r.str();
    s.caps = unpack_caps(r.u32());
    s.freshness = static_cast<Freshness>(r.u8());
    if (!r.ok) break;
    impl_->sources.emplace(id, std::move(s));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "worker count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    Impl::WorkerRec wc;
    auto id = r.id<WorkerId>();
    wc.boot = r.id<WorkerBootId>();
    wc.name = r.str();
    wc.connected = r.flag();
    if (!r.ok) break;
    impl_->workers.emplace(id, std::move(wc));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "workload count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    WorkloadDescriptor wd;
    auto id = r.id<WorkloadId>();
    wd.generation = r.id<WorkloadGeneration>();
    wd.name = r.str();
    wd.config_fingerprint = r.str();
    wd.problem_size_fingerprint = r.str();
    wd.device_id = r.id<DeviceId>();
    wd.device_generation = r.id<DeviceGeneration>();
    wd.worker_id = r.id<WorkerId>();
    wd.worker_boot = r.id<WorkerBootId>();
    wd.policy_generation = r.str();
    wd.runtime_generation = r.str();
    wd.kernel_generation = r.str();
    wd.topology_generation = r.id<TopologyGeneration>();
    wd.start_ms = r.i64();
    wd.end_ms = r.i64();
    wd.attempt_id = r.u64();
    wd.priority_class = r.str();
    wd.id = id;
    if (!r.ok) break;
    impl_->workloads.emplace(id, std::move(wd));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "baseline count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    Baseline b;
    auto id = r.id<BaselineId>();
    b.generation = r.id<BaselineGeneration>();
    b.type = static_cast<BaselineType>(r.u8());
    b.workload_id = r.id<WorkloadId>();
    b.workload_generation = r.id<WorkloadGeneration>();
    b.device_id = r.id<DeviceId>();
    b.device_generation = r.id<DeviceGeneration>();
    b.worker_id = r.id<WorkerId>();
    b.worker_boot = r.id<WorkerBootId>();
    b.config_fingerprint = r.str();
    b.problem_size_fingerprint = r.str();
    b.runtime_generation = r.str();
    b.kernel_generation = r.str();
    b.policy_generation = r.str();
    b.topology_generation = r.id<TopologyGeneration>();
    b.source_generation = r.id<SourceGeneration>();
    b.evidence_generation = r.id<EvidenceGeneration>();
    b.timestamp_ms = r.i64();
    b.interval_ms = r.i64();
    b.provenance = static_cast<Provenance>(r.u8());
    b.freshness = static_cast<Freshness>(r.u8());
    b.sample_count = r.u64();
    b.variance = r.f64();
    b.confidence = r.f64();
    b.mean_value = r.f64();
    b.metric_kind = static_cast<MetricKind>(r.u8());
    b.valid = r.flag();
    b.id = id;
    if (!r.ok) break;
    impl_->baselines.emplace(id, std::move(b));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "observation count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    Observation o;
    auto id = r.id<ObservationId>();
    o.generation = r.id<ObservationGeneration>();
    o.source_id = r.id<SourceId>();
    o.source_generation = r.id<SourceGeneration>();
    o.worker_id = r.id<WorkerId>();
    o.worker_boot = r.id<WorkerBootId>();
    o.workload_id = r.id<WorkloadId>();
    o.workload_generation = r.id<WorkloadGeneration>();
    o.device_id = r.id<DeviceId>();
    o.device_generation = r.id<DeviceGeneration>();
    o.timestamp_ms = r.i64();
    o.measurement.kind = static_cast<MetricKind>(r.u8());
    o.measurement.value = r.f64();
    o.measurement.unit = r.str();
    o.measurement.sample_count = r.u64();
    o.measurement.variance = r.f64();
    o.measurement.p95 = r.f64();
    o.measurement.has_p95 = r.flag();
    o.provenance = static_cast<Provenance>(r.u8());
    o.freshness = static_cast<Freshness>(r.u8());
    o.source_health = static_cast<SourceHealth>(r.u8());
    o.capabilities = unpack_caps(r.u32());
    std::uint32_t peers = r.u32();
    if (peers > kCountBound) { reason = "peer count bound"; return false; }
    for (std::uint32_t j = 0; j < peers && r.ok; ++j) o.co_run_peers.push_back(r.id<WorkloadId>());
    std::uint32_t confs = r.u32();
    if (confs > kCountBound) { reason = "confounder count bound"; return false; }
    for (std::uint32_t j = 0; j < confs && r.ok; ++j) o.confounders.push_back(static_cast<Confounder>(r.u8()));
    o.id = id;
    if (!r.ok) break;
    impl_->observations.emplace(id, std::move(o));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "episode count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    InterferenceEpisode e;
    auto id = r.id<EpisodeId>();
    e.generation = r.id<EpisodeGeneration>();
    e.start_ms = r.i64();
    e.end_ms = r.i64();
    e.open = r.flag();
    e.affected = r.id<WorkloadId>();
    std::uint32_t cr = r.u32();
    if (cr > kCountBound) { reason = "co-running bound"; return false; }
    for (std::uint32_t j = 0; j < cr && r.ok; ++j) e.co_running.push_back(r.id<WorkloadId>());
    std::uint32_t dm = r.u32();
    if (dm > kCountBound) { reason = "domain bound"; return false; }
    for (std::uint32_t j = 0; j < dm && r.ok; ++j) e.domains.push_back(static_cast<InterferenceDomain>(r.u8()));
    e.baseline_id = r.id<BaselineId>();
    e.degradation.sample_count = r.u64();
    std::uint32_t dd = r.u32();
    if (dd > kCountBound) { reason = "delta bound"; return false; }
    for (std::uint32_t j = 0; j < dd && r.ok; ++j) {
      MetricDelta d; d.kind = static_cast<MetricKind>(r.u8()); d.absolute = r.f64(); d.relative = r.f64();
      d.sample_count = r.u64(); d.variance = r.f64(); e.degradation.deltas.push_back(d);
    }
    e.degradation.worst_relative = r.f64();
    e.confidence = r.f64();
    e.classification = static_cast<Outcome>(r.u8());
    e.provenance = static_cast<Provenance>(r.u8());
    e.freshness = static_cast<Freshness>(r.u8());
    e.epoch = r.id<CoordinatorEpoch>();
    std::uint32_t ev = r.u32();
    if (ev > kCountBound) { reason = "evidence bound"; return false; }
    for (std::uint32_t j = 0; j < ev && r.ok; ++j) {
      InterferenceEvidence x; x.id = r.id<EvidenceId>(); x.generation = r.id<EvidenceGeneration>();
      x.provenance = static_cast<Provenance>(r.u8()); x.strength = static_cast<AttributionStrength>(r.u8());
      x.outcome = static_cast<Outcome>(r.u8()); x.summary = r.str();
      e.evidence.push_back(std::move(x));
    }
    e.id = id;
    if (!r.ok) break;
    impl_->episodes.emplace(id, std::move(e));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "comparison count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    PairwiseInterference p;
    auto cid = r.id<ComparisonId>();
    p.victim = r.id<WorkloadId>();
    p.neighbor = r.id<WorkloadId>();
    p.domain = static_cast<InterferenceDomain>(r.u8());
    p.baseline_id = r.id<BaselineId>();
    p.degradation.sample_count = r.u64();
    std::uint32_t dd = r.u32();
    if (dd > kCountBound) { reason = "delta bound"; return false; }
    for (std::uint32_t j = 0; j < dd && r.ok; ++j) {
      MetricDelta d; d.kind = static_cast<MetricKind>(r.u8()); d.absolute = r.f64(); d.relative = r.f64();
      d.sample_count = r.u64(); d.variance = r.f64(); p.degradation.deltas.push_back(d);
    }
    p.degradation.worst_relative = r.f64();
    p.confidence = r.f64();
    p.sample_count = r.u64();
    p.freshness = static_cast<Freshness>(r.u8());
    p.outcome = static_cast<Outcome>(r.u8());
    p.attribution = static_cast<AttributionStrength>(r.u8());
    p.episode_id = r.id<EpisodeId>();
    p.comparison_id = r.id<ComparisonId>();
    p.comparison_generation = r.id<ComparisonGeneration>();
    p.epoch = r.id<CoordinatorEpoch>();
    std::uint32_t srcs = r.u32();
    if (srcs > kCountBound) { reason = "evidence source bound"; return false; }
    for (std::uint32_t j = 0; j < srcs && r.ok; ++j) p.evidence_sources.push_back(static_cast<Provenance>(r.u8()));
    p.confounded = r.flag();
    std::uint32_t us = r.u32();
    if (us > kCountBound) { reason = "unresolved bound"; return false; }
    for (std::uint32_t j = 0; j < us && r.ok; ++j) p.unresolved_confounders.push_back(static_cast<Confounder>(r.u8()));
    if (!r.ok) break;
    impl_->comparisons.emplace(cid, p);
    impl_->matrix.set(CellKey{p.victim, p.neighbor, p.domain}, std::move(p));
  }
  count = r.u32();
  if (count > kCountBound) { reason = "recovery count bound"; return false; }
  for (std::uint32_t i = 0; i < count && r.ok; ++i) {
    auto v = r.id<WorkloadId>();
    auto n = r.id<WorkloadId>();
    auto d = static_cast<InterferenceDomain>(r.u8());
    if (!r.ok) break;
    impl_->recoveries.insert(std::make_tuple(v, n, d));
  }
  if (!r.ok) { reason = "truncated payload"; return false; }
  return true;
}

}  // namespace iobs


