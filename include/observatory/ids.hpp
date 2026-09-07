#pragma once
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <optional>

namespace iobs {

// A strongly-typed identity. Each Tag creates a distinct C++ type, so identities from different
// authority domains cannot be silently interchanged. Value 0 is the nil/sentinel identity.
template <typename Tag>
class Id {
 public:
  constexpr Id() noexcept = default;
  constexpr explicit Id(std::uint64_t v) noexcept : value_(v) {}

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_nil() const noexcept { return value_ == 0; }

  friend constexpr bool operator==(Id a, Id b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(Id a, Id b) noexcept { return a.value_ != b.value_; }
  friend constexpr bool operator<(Id a, Id b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator<=(Id a, Id b) noexcept { return a.value_ <= b.value_; }
  friend constexpr bool operator>(Id a, Id b) noexcept { return a.value_ > b.value_; }
  friend constexpr bool operator>=(Id a, Id b) noexcept { return a.value_ >= b.value_; }

  friend std::ostream& operator<<(std::ostream& os, Id id) { return os << id.value_; }

 private:
  std::uint64_t value_{0};
};

namespace detail {
struct CoordinatorEpochTag {};
struct ObservatoryIdTag {};
struct ObservatoryGenerationTag {};
struct SourceIdTag {};
struct SourceGenerationTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct HostIdTag {};
struct HostGenerationTag {};
struct DeviceIdTag {};
struct DeviceGenerationTag {};
struct ResourceIdTag {};
struct ResourceGenerationTag {};
struct WorkloadIdTag {};
struct WorkloadGenerationTag {};
struct ExperimentIdTag {};
struct ExperimentGenerationTag {};
struct EpisodeIdTag {};
struct EpisodeGenerationTag {};
struct BaselineIdTag {};
struct BaselineGenerationTag {};
struct EvidenceIdTag {};
struct EvidenceGenerationTag {};
struct ObservationIdTag {};
struct ObservationGenerationTag {};
struct ComparisonIdTag {};
struct ComparisonGenerationTag {};
struct InterferenceClassIdTag {};
struct TopologyGenerationTag {};
struct TransferGenerationTag {};
struct CollectiveGenerationTag {};
struct StorageGenerationTag {};
struct NUMAGenerationTag {};
}  // namespace detail

using CoordinatorEpoch = Id<detail::CoordinatorEpochTag>;
using ObservatoryId = Id<detail::ObservatoryIdTag>;
using ObservatoryGeneration = Id<detail::ObservatoryGenerationTag>;
using SourceId = Id<detail::SourceIdTag>;
using SourceGeneration = Id<detail::SourceGenerationTag>;
using WorkerId = Id<detail::WorkerIdTag>;
using WorkerBootId = Id<detail::WorkerBootIdTag>;
using HostId = Id<detail::HostIdTag>;
using HostGeneration = Id<detail::HostGenerationTag>;
using DeviceId = Id<detail::DeviceIdTag>;
using DeviceGeneration = Id<detail::DeviceGenerationTag>;
using ResourceId = Id<detail::ResourceIdTag>;
using ResourceGeneration = Id<detail::ResourceGenerationTag>;
using WorkloadId = Id<detail::WorkloadIdTag>;
using WorkloadGeneration = Id<detail::WorkloadGenerationTag>;
using ExperimentId = Id<detail::ExperimentIdTag>;
using ExperimentGeneration = Id<detail::ExperimentGenerationTag>;
using EpisodeId = Id<detail::EpisodeIdTag>;
using EpisodeGeneration = Id<detail::EpisodeGenerationTag>;
using BaselineId = Id<detail::BaselineIdTag>;
using BaselineGeneration = Id<detail::BaselineGenerationTag>;
using EvidenceId = Id<detail::EvidenceIdTag>;
using EvidenceGeneration = Id<detail::EvidenceGenerationTag>;
using ObservationId = Id<detail::ObservationIdTag>;
using ObservationGeneration = Id<detail::ObservationGenerationTag>;
using ComparisonId = Id<detail::ComparisonIdTag>;
using ComparisonGeneration = Id<detail::ComparisonGenerationTag>;
using InterferenceClassId = Id<detail::InterferenceClassIdTag>;
using TopologyGeneration = Id<detail::TopologyGenerationTag>;
using TransferGeneration = Id<detail::TransferGenerationTag>;
using CollectiveGeneration = Id<detail::CollectiveGenerationTag>;
using StorageGeneration = Id<detail::StorageGenerationTag>;
using NUMAGeneration = Id<detail::NUMAGenerationTag>;

// Parsing of a decimal identity. Returns std::nullopt on non-numeric/empty input.
[[nodiscard]] inline std::optional<std::uint64_t> parse_id_value(std::string_view s) noexcept {
  if (s.empty()) return std::nullopt;
  std::uint64_t value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return std::nullopt;
    value = value * 10 + digit;
  }
  return value;
}

template <typename Tag>
[[nodiscard]] inline std::optional<Id<Tag>> parse_id(std::string_view s) noexcept {
  auto v = parse_id_value(s);
  if (!v) return std::nullopt;
  return Id<Tag>(*v);
}

}  // namespace iobs

// std::hash specialization for Id<Tag> so identities can key unordered containers.
template <typename Tag>
struct std::hash<iobs::Id<Tag>> {
  [[nodiscard]] std::size_t operator()(iobs::Id<Tag> id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
