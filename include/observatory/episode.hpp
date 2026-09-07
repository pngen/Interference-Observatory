#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"

namespace iobs {

// A single piece of evidence supporting an episode or attribution decision.
struct InterferenceEvidence {
  EvidenceId id;
  EvidenceGeneration generation;
  Provenance provenance = Provenance::Unknown;
  AttributionStrength strength = AttributionStrength::Unknown;
  Outcome outcome = Outcome::Unknown;
  std::string summary;
};

// A time-bounded interference episode. Deterministic and replayable.
struct InterferenceEpisode {
  EpisodeId id;
  EpisodeGeneration generation;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;
  bool open = false;  // open episode has end_ms == start_ms (still ongoing)
  WorkloadId affected;
  std::vector<WorkloadId> co_running;
  std::vector<InterferenceDomain> domains;
  BaselineId baseline_id;
  Degradation degradation;
  std::vector<InterferenceEvidence> evidence;
  double confidence = 0.0;
  Outcome classification = Outcome::Unknown;
  Provenance provenance = Provenance::Unknown;
  Freshness freshness = Freshness::Unknown;
  CoordinatorEpoch epoch;  // epoch under which this episode was recorded
};

}  // namespace iobs
