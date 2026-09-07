#pragma once
#include <cstdint>
#include <optional>
#include "observatory/ids.hpp"
#include "observatory/domain.hpp"
#include "observatory/pairwise.hpp"

namespace iobs {

enum class LaunchOrder : std::uint8_t {
  PrimaryFirst,
  NeighborFirst,
  Simultaneous
};

[[nodiscard]] constexpr std::string_view to_string(LaunchOrder o) noexcept {
  using enum LaunchOrder;
  switch (o) {
    case PrimaryFirst: return "PRIMARY_FIRST";
    case NeighborFirst: return "NEIGHBOR_FIRST";
    case Simultaneous: return "SIMULTANEOUS";
  }
  return "UNKNOWN_ORDER";
}

// An explicit, deterministic experiment plan: warmup, baselines, co-run, repeats, launch order.
struct ExperimentPlan {
  ExperimentId id;
  ExperimentGeneration generation;
  WorkloadId primary;
  WorkloadId neighbor;
  InterferenceDomain domain = InterferenceDomain::Unknown;
  LaunchOrder launch_order = LaunchOrder::Simultaneous;
  bool warmup = true;
  std::uint64_t warmup_repetitions = 1;
  std::uint64_t repetitions = 3;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;

  [[nodiscard]] std::uint64_t total_trials() const noexcept {
    return repetitions + (warmup ? warmup_repetitions : 0);
  }
};

// A completed controlled experiment result, preserving directionality.
struct ExperimentResult {
  ExperimentId experiment_id;
  ExperimentGeneration generation;
  std::optional<PairwiseInterference> primary_as_victim;  // primary degraded by neighbor
  std::optional<PairwiseInterference> neighbor_as_victim; // neighbor degraded by primary
  std::uint64_t repetitions_executed = 0;
  bool complete = false;
};

}  // namespace iobs
