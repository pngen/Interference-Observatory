#pragma once
#include <cstdint>
#include <string_view>
#include <string>
#include <optional>

namespace iobs {

// The shared resource / interference axis under investigation. Only domains with a real
// implementation on a platform are reported as available; the rest remain UNSUPPORTED.
enum class InterferenceDomain : std::uint8_t {
  ComputeExecution,
  Cache,
  MemoryBandwidth,
  MemoryCapacity,
  Pcie,
  Transfer,
  Collective,
  Network,
  Numa,
  Storage,
  HostCpu,
  Residency,
  SharedEngine,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(InterferenceDomain d) noexcept {
  using enum InterferenceDomain;
  switch (d) {
    case ComputeExecution: return "COMPUTE_EXECUTION";
    case Cache: return "CACHE";
    case MemoryBandwidth: return "MEMORY_BANDWIDTH";
    case MemoryCapacity: return "MEMORY_CAPACITY";
    case Pcie: return "PCIE";
    case Transfer: return "TRANSFER";
    case Collective: return "COLLECTIVE";
    case Network: return "NETWORK";
    case Numa: return "NUMA";
    case Storage: return "STORAGE";
    case HostCpu: return "HOST_CPU";
    case Residency: return "RESIDENCY";
    case SharedEngine: return "SHARED_ENGINE";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_DOMAIN";
}

// Parses a domain from a canonical name. Returns std::nullopt for invalid/unknown strings.
[[nodiscard]] inline std::optional<InterferenceDomain> parse_interference_domain(std::string_view s) noexcept {
  using enum InterferenceDomain;
  if (s == "COMPUTE_EXECUTION") return ComputeExecution;
  if (s == "CACHE") return Cache;
  if (s == "MEMORY_BANDWIDTH") return MemoryBandwidth;
  if (s == "MEMORY_CAPACITY") return MemoryCapacity;
  if (s == "PCIE") return Pcie;
  if (s == "TRANSFER") return Transfer;
  if (s == "COLLECTIVE") return Collective;
  if (s == "NETWORK") return Network;
  if (s == "NUMA") return Numa;
  if (s == "STORAGE") return Storage;
  if (s == "HOST_CPU") return HostCpu;
  if (s == "RESIDENCY") return Residency;
  if (s == "SHARED_ENGINE") return SharedEngine;
  return std::nullopt;
}

// Decision outcome. Thresholds are policy-defined; the engine never invents a threshold.
enum class Outcome : std::uint8_t {
  NoInterferenceDetected,
  PotentialInterference,
  InterferenceDetected,
  SevereInterference,
  InsufficientEvidence,
  BaselineInvalid,
  RevalidationRequired,
  Unsupported,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(Outcome o) noexcept {
  using enum Outcome;
  switch (o) {
    case NoInterferenceDetected: return "NO_INTERFERENCE_DETECTED";
    case PotentialInterference: return "POTENTIAL_INTERFERENCE";
    case InterferenceDetected: return "INTERFERENCE_DETECTED";
    case SevereInterference: return "SEVERE_INTERFERENCE";
    case InsufficientEvidence: return "INSUFFICIENT_EVIDENCE";
    case BaselineInvalid: return "BASELINE_INVALID";
    case RevalidationRequired: return "REVALIDATION_REQUIRED";
    case Unsupported: return "UNSUPPORTED";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_OUTCOME";
}

// Attribution strength. Correlation is never presented as causality.
enum class AttributionStrength : std::uint8_t {
  Unknown = 0,
  TemporallyAssociated = 1,
  Correlated = 2,
  ControlledComparison = 3,
  StrongCounterfactualEvidence = 4,
  DirectResourceEvidence = 5
};

[[nodiscard]] constexpr std::string_view to_string(AttributionStrength a) noexcept {
  using enum AttributionStrength;
  switch (a) {
    case Unknown: return "UNKNOWN";
    case TemporallyAssociated: return "TEMPORALLY_ASSOCIATED";
    case Correlated: return "CORRELATED";
    case ControlledComparison: return "CONTROLLED_COMPARISON";
    case StrongCounterfactualEvidence: return "STRONG_COUNTERFACTUAL_EVIDENCE";
    case DirectResourceEvidence: return "DIRECT_RESOURCE_EVIDENCE";
  }
  return "UNKNOWN_STRENGTH";
}

// Source provenance of an observation or conclusion.
enum class Provenance : std::uint8_t {
  Measured,
  Reported,
  Derived,
  Controlled,
  Synthetic,
  Replayed,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(Provenance p) noexcept {
  using enum Provenance;
  switch (p) {
    case Measured: return "MEASURED";
    case Reported: return "REPORTED";
    case Derived: return "DERIVED";
    case Controlled: return "CONTROLLED";
    case Synthetic: return "SYNTHETIC";
    case Replayed: return "REPLAYED";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_PROVENANCE";
}

// Evidence freshness. Only CURRENT dynamic evidence is authoritative for current decisions.
enum class Freshness : std::uint8_t {
  Current,
  Stale,
  Expired,
  RevalidationRequired,
  Historical,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(Freshness f) noexcept {
  using enum Freshness;
  switch (f) {
    case Current: return "CURRENT";
    case Stale: return "STALE";
    case Expired: return "EXPIRED";
    case RevalidationRequired: return "REVALIDATION_REQUIRED";
    case Historical: return "HISTORICAL";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_FRESHNESS";
}

// Health of a telemetry source (or worker).
enum class SourceHealth : std::uint8_t {
  Healthy,
  Degraded,
  Stale,
  Disconnected,
  RevalidationRequired,
  Unsupported,
  Unknown
};

[[nodiscard]] constexpr std::string_view to_string(SourceHealth h) noexcept {
  using enum SourceHealth;
  switch (h) {
    case Healthy: return "HEALTHY";
    case Degraded: return "DEGRADED";
    case Stale: return "STALE";
    case Disconnected: return "DISCONNECTED";
    case RevalidationRequired: return "REVALIDATION_REQUIRED";
    case Unsupported: return "UNSUPPORTED";
    case Unknown: return "UNKNOWN";
  }
  return "UNKNOWN_HEALTH";
}

}  // namespace iobs
