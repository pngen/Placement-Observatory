# Observation Model

A placement observation is an immutable record of evidence about a placement
context. Each record carries identity, authority, context ids, a set of
measurements, a lifecycle state, and a reliability class.

## Classification

Every measurement is one of measured, reported, derived, estimated, or unknown.
The system never converts missing evidence into fabricated certainty. If two
sources disagree, both are preserved; the conflict is not resolved by overwriting
one with the other.

## Immutability

Evidence is immutable after publication. Corrections or later evidence create a new
observation revision rather than silently mutating history. Duplicate observation
ids are rejected deterministically. The canonical log is authoritative; secondary
indices are validated against it.

## Lifecycle

Collected, Normalized, Correlated, DecisionLinked, OutcomeLinked, Reconstructed,
Explained, Replayed, Superseded, Invalid, Corrupt, Retired. Invalid or corrupt
evidence is never admitted to authoritative replay.
