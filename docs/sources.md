# Sources

Sources are entities that produce placement evidence. Each source has a SourceId, a
monotonic SourceGeneration, a type, a reliability class, and an endpoint. Source
health is tracked as current, stale, unavailable, restarted, corrupt, degraded, or
partial. A stale source reduces explanation confidence where relevant.

## Source generation authority

SourceGenerations are monotonic. On a source restart, the generation increases and a
new WorkerBootId is trusted. Observations carrying a superseded generation or a
worker boot that does not match the trusted boot for the current generation are
rejected; stale evidence never mutates current state. Registering a source with a new
generation re-adopts the boot from the first observation of the new generation,
describing a genuine restart rather than reviving an old boot.
