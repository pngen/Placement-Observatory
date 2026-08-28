# Provenance

Every evidence quantity carries explicit provenance. At minimum it records the
SourceId and SourceGeneration, source type and collection method, wall clock and
monotonic timestamp plus clock uncertainty, reliability class, confidence and the
measured/derived/estimated/reported/unknown classification, collection error where
applicable, raw and normalized field identity, and trusted worker boot and
coordinator epoch where distributed evidence is involved.

## Source disagreements

If two sources disagree, both claims are retained side by side. The system reports
the conflict rather than silently choosing one. Conflict diagnostics preserve the
full provenance of each claim.

## Confidence

Confidence is structured (CompleteMeasured, StrongMixedEvidence, PartialEvidence,
Reconstructed, InsufficientEvidence) and, when a numeric value is present, its
derivation is documented exactly. A stale source reduces explanation confidence
where relevant; stale measurements are never silently used as current.
