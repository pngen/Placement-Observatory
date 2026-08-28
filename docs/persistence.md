# Persistence

Placement Observatory persists decision traces, source observations, snapshots,
timelines, replay bundles, and comparison bundles. Persistence uses a versioned
binary record format: each record carries a magic, format version, record kind,
body length, body, and a SHA-256 checksum. Records are appended safely; metadata is
replaced transactionally (write-temp-then-rename).

The decoder rejects bad magic, unknown version, body lengths beyond a hard cap,
truncated records, trailing data, and checksum mismatches. Indices, timelines, and
decision/outcome links are rebuilt on load; unknown or missing evidence is retained
explicitly and never invented.
