#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>

namespace iobs {

// Typed error model. Every framework-level failure carries one of these codes.
enum class Status : std::uint8_t {
  Ok = 0,
  InvalidInput,
  IncompatibleBaseline,
  MissingBaseline,
  StaleBaseline,
  StaleAuthority,
  StaleSource,
  InsufficientEvidence,
  NoInterferenceDetected,
  Unsupported,
  OutcomeUnknown,
  Confounded,
  PersistenceCorrupt,
  ProtocolError,
  Overflow,
  ResourceExhausted,
  Cancelled,
  ShuttingDown
};

[[nodiscard]] constexpr std::string_view to_string(Status s) noexcept {
  using enum Status;
  switch (s) {
    case Ok: return "OK";
    case InvalidInput: return "INVALID_INPUT";
    case IncompatibleBaseline: return "INCOMPATIBLE_BASELINE";
    case MissingBaseline: return "MISSING_BASELINE";
    case StaleBaseline: return "STALE_BASELINE";
    case StaleAuthority: return "STALE_AUTHORITY";
    case StaleSource: return "STALE_SOURCE";
    case InsufficientEvidence: return "INSUFFICIENT_EVIDENCE";
    case NoInterferenceDetected: return "NO_INTERFERENCE_DETECTED";
    case Unsupported: return "UNSUPPORTED";
    case OutcomeUnknown: return "OUTCOME_UNKNOWN";
    case Confounded: return "CONFOUNDED";
    case PersistenceCorrupt: return "PERSISTENCE_CORRUPT";
    case ProtocolError: return "PROTOCOL_ERROR";
    case Overflow: return "OVERFLOW";
    case ResourceExhausted: return "RESOURCE_EXHAUSTED";
    case Cancelled: return "CANCELLED";
    case ShuttingDown: return "SHUTTING_DOWN";
  }
  return "UNKNOWN_STATUS";
}

// Exception carrying a typed Status.
class Error : public std::runtime_error {
 public:
  explicit Error(Status status, std::string message = {})
      : std::runtime_error(message.empty() ? std::string(to_string(status)) : std::move(message)),
        status_(status) {}
  [[nodiscard]] Status status() const noexcept { return status_; }
 private:
  Status status_;
};

}  // namespace iobs
