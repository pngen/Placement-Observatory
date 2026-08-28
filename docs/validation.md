# Validation

Placement Observatory ships a dedicated test suite, an adversarial suite, a
concurrency suite, a fixed-seed randomized/property suite, a persistence and
recovery suite, and a multiprocess framed-TCP scenario. All tests run without
explicit timeouts.

## Adversarial coverage

The adversarial suite covers empty and malformed observations, duplicate ids, zero
and rolled-back generations, unknown and stale sources, stale worker boot and
coordinator epoch, malformed candidate sets, selected candidates absent from a
complete set, contradictory constraints, NaN and infinite costs, impossible memory
values, outcome-before-decision, duplicate and wrong-generation outcomes, malformed
JSON, and corrupt/truncated/unknown-version/trailing binary records.

## Concurrency

The concurrency suite runs 16 threads performing thousands of mixed operations,
asserting no dropped authoritative evidence, no duplicate decision links, no
negative accounting, no deadlock, deterministic replay after a replay cutoff, and
snapshot consistency.
