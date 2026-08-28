# Explanation

The explanation engine answers the governing question: why did workload X land on
device/node Y. It reports which hard constraints eliminated other candidates, which
soft costs favored the selection, whether locality, queue pressure, memory headroom,
topology, deadline, state reuse, recomputation cost, fallback, quota, or reservation
was decisive, whether a deterministic tie-break applied, which evidence was measured
versus derived, which important inputs were missing, how confident the explanation
is, what alternative ranked next, and what minimal change would have changed the
placement.

## No opaque master score

A scalar overall cost may exist, but every cost and score component is retained and
reported. Operators can inspect how the total was formed. Deterministic tie-breaking
is required; the explanation is never reduced to a claim that a candidate merely had
the lowest score.

## Confidence

Explanation confidence is a structured class derived from evidence completeness and
source quality, with an exact documented derivation when a numeric value is
present. Stale sources reduce confidence. Text and JSON output are both available.
