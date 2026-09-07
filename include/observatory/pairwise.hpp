#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"
#include "observatory/observation.hpp"

namespace iobs {

// A directional pairwise interference result. A -> B means "B is the victim, A is the associated
// peer"; it never implies the reverse (B -> A).
struct PairwiseInterference {
  WorkloadId victim;
  WorkloadId neighbor;
  InterferenceDomain domain = InterferenceDomain::Unknown;
  BaselineId baseline_id;
  Degradation degradation;
  double confidence = 0.0;           // [0,1]
  std::uint64_t sample_count = 0;    // trials backing this comparison
  Freshness freshness = Freshness::Unknown;
  Outcome outcome = Outcome::Unknown;
  AttributionStrength attribution = AttributionStrength::Unknown;
  EpisodeId episode_id;
  ComparisonId comparison_id;
  ComparisonGeneration comparison_generation;
  CoordinatorEpoch epoch;
  std::vector<Provenance> evidence_sources;
  bool confounded = false;
  std::vector<Confounder> unresolved_confounders;
};

// A directed matrix over (victim, neighbor, resource domain). Cells are independent and are
// never symmetrized or averaged across directions.
struct CellKey {
  WorkloadId victim;
  WorkloadId neighbor;
  InterferenceDomain domain;

  friend bool operator<(const CellKey& a, const CellKey& b) {
    if (a.victim != b.victim) return a.victim < b.victim;
    if (a.neighbor != b.neighbor) return a.neighbor < b.neighbor;
    return a.domain < b.domain;
  }
};

class InterferenceMatrix {
 public:
  void set(CellKey key, PairwiseInterference value) { cells_[key] = std::move(value); }
  [[nodiscard]] std::optional<PairwiseInterference> get(const CellKey& key) const {
    auto it = cells_.find(key);
    if (it == cells_.end()) return std::nullopt;
    return it->second;
  }
  [[nodiscard]] const std::map<CellKey, PairwiseInterference>& cells() const noexcept { return cells_; }
  [[nodiscard]] std::size_t size() const noexcept { return cells_.size(); }
  [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }

 private:
  std::map<CellKey, PairwiseInterference> cells_;
};

}  // namespace iobs
