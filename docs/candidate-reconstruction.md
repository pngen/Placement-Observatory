# Candidate Reconstruction

Placement Observatory can reconstruct candidate placement sets from available
evidence, using discovered nodes/devices, runtime capabilities, memory headroom,
compatibility, queue state, topology, locality, reservations, policy constraints,
health state, and namespace/tenant rules.

Reconstructed candidate sets are always labelled derived and carry a source chain.
Reconstruction is never presented as identical to the original scheduler's internal
candidate set unless exact evidence proves it. Recorded candidate sets that are
fully observed are labelled complete; reconstructed sets are labelled
reconstructed.

## Missing evidence

Fields that were not available are recorded in the candidate set missing_fields.
Partial-evidence ranking places candidates that lack cost evidence last and flags
them explicitly; the ranking never invents a cost for a candidate that has none.
