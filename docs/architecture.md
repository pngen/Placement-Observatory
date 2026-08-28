# Architecture

Placement Observatory is a vendor-neutral observability boundary for placement
behavior. It does not execute placement decisions. The core is a thread-safe
Observatory that owns an immutable canonical observation log and a set of
secondary indices. Providers produce evidence records; the engine correlates,
reconstructs, explains, compares, replays, and persists them.

## Components

- Providers: Windows host/system, process, CUDA device (real RTX 5090 / sm_120),
  synthetic multi-node topology, trace/file, and framed TCP multiprocess.
- Canonical log: append-only, immutable placement observations.
- Decision record: candidate set, constraints, cost/score components, selected
  candidate, rejection reasons, tie-break, confidence.
- Engines: deterministic ranking, explanation, comparison, replay, counterfactual.
- Persistence: versioned, checksummed binary records with transactional replacement
  and corruption/truncation/unknown-version rejection.
- Protocol: framed TCP over Winsock2 with a hard frame-size cap and strict decoder.

## Concurrency

Writes take a unique lock; reads take a shared lock. There is no read-to-write lock
upgrade, no callbacks under locks, and no file or CUDA I/O under central observatory
locks. Providers post evidence through the ingest, which locks internally, so a
provider callback can never re-enter the observatory while a lock is held.

## Authority

Every source carries a SourceGeneration and a WorkerBootId. A source restart is
recognized by a generation increase with a new boot. Observations carrying a
superseded generation, a stale worker boot, or a stale coordinator epoch are
rejected rather than admitted; stale evidence never mutates current state. See
docs/sources.md and docs/protocol.md.
