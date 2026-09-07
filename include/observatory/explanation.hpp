#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/metric.hpp"
#include "observatory/observation.hpp"

namespace iobs {

// A structured explanation of one interference conclusion. Never free-form, never authority.
struct InterferenceExplanation {
  WorkloadId affected;
  std::vector<WorkloadId> associated;
  InterferenceDomain domain = InterferenceDomain::Unknown;
  BaselineId baseline_id;
  bool has_baseline = false;
  Degradation degradation;
  std::uint64_t sample_count = 0;
  double variance = 0.0;
  double timeline_overlap_fraction = 0.0;  // [0,1] overlap of the affected/associated intervals
  std::vector<Confounder> confounders_checked;
  std::vector<Confounder> confounders_unresolved;
  Provenance provenance = Provenance::Unknown;
  Freshness freshness = Freshness::Unknown;
  double confidence = 0.0;
  AttributionStrength attribution = AttributionStrength::Unknown;
  Outcome outcome = Outcome::Unknown;
  std::vector<ObservationId> supporting_observations;
  std::vector<std::string> unknown_components;
  std::vector<std::string> what_would_strengthen_evidence;
  bool authority_only = true;  // read-only recommendation; never a control action
};

}  // namespace iobs
