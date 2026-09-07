#pragma once
#include <cstdint>
#include "observatory/status.hpp"

namespace iobs {

// Thresholds are policy-defined, never engine-invented. This struct bundles the significance and
// freshness gates used by the observatory when classifying and scoring comparisons.
struct Policy {
  // Relative degradation gates for classification.
  double potential_threshold = 0.02;   // >= +2% => POTENTIAL_INTERFERENCE
  double detected_threshold = 0.10;    // >= +10% => INTERFERENCE_DETECTED
  double severe_threshold = 0.50;      // >= +50% => SEVERE_INTERFERENCE

  // Sample discipline.
  std::uint64_t min_samples = 3;        // fewer => INSUFFICIENT_EVIDENCE
  double min_confidence = 0.60;         // below => degraded confidence / revalidation

  // Freshness gates (ms).
  std::int64_t stale_after_ms = 15 * 60 * 1000;   // 15 minutes -> STALE
  std::int64_t max_age_ms = 60 * 60 * 1000;       // 1 hour -> EXPIRED / REVALIDATION_REQUIRED

  // Confounder discipline: any unresolved major confounder invalidates or strongly degrades.
  bool allow_no_evidence_to_be_unknown = true;

  [[nodiscard]] bool isValid() const noexcept {
    return potential_threshold >= 0.0 && detected_threshold >= potential_threshold &&
           severe_threshold >= detected_threshold && min_samples >= 1 &&
           min_confidence >= 0.0 && min_confidence <= 1.0;
  }
};

}  // namespace iobs
