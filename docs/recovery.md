# Recovery

On restart, Placement Observatory loads valid persisted evidence, rejects corruption
and truncated records, preserves source generations, rebuilds indices, reconstructs
timelines, and preserves decision/outcome links. Recovery never invents live source
state from old trace data; it marks historical and live boundaries clearly.

A corrupt or truncated record stops loading at that boundary; records before it are
kept valid. Orphan temp files are cleaned. Recovery is deterministic and preserves
valid historical state.
