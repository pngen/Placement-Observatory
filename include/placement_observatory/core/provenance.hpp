#pragma once
// Source provenance: every evidence quantity must carry provenance. Two
// disagreeing sources are preserved side by side, never overwritten.
#include "placement_observatory/core/ids.hpp"
#include "placement_observatory/core/enums.hpp"
#include "placement_observatory/core/time.hpp"

namespace placement_observatory {

// Provenance of a single source claim about evidence.
struct Provenance {
  SourceId source_id;                  // which source produced the claim
  SourceGeneration source_generation = 0; // that source's generation
  SourceType source_type = SourceType::Provider;
  CollectionMethod method = CollectionMethod::Unknown;
  ReliabilityClass reliability = ReliabilityClass::Unknown;
  Classification classification = Classification::Unknown;
  Timestamp timestamp;                 // when the source produced the claim
  std::int64_t uncertainty_ns = 0;     // clock / measurement uncertainty
  WorkerBootId worker_boot = 0;        // trusted worker boot id where relevant
  CoordinatorEpoch coordinator_epoch = 0;
  std::string collection_error;        // non-empty => error while collecting
  std::string normalized_field;        // canonical field identity
  std::string raw_field;               // source-specific field name

  [[nodiscard]] bool operator==(const Provenance&) const noexcept = default;
};

// Structured confidence. The numeric value (when present) has an EXACT
// documented derivation; it is never a magic percentage.
struct Confidence {
  ConfidenceClass cls = ConfidenceClass::InsufficientEvidence;
  double numerator = 0.0;
  double denominator = 0.0;
  std::string derivation;              // exact explanation of the derivation

  [[nodiscard]] double numeric() const noexcept {
    return denominator > 0.0 ? numerator / denominator : 0.0;
  }
  [[nodiscard]] bool operator==(const Confidence&) const noexcept = default;
};

} // namespace placement_observatory
