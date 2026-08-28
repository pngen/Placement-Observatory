# Timeline

A placement timeline orders the observation, decision, and outcome events for a
workload deterministically. Ordering uses monotonic time when available and falls
back to wall-clock time; ties are broken by event kind and identity.

Timelines support out-of-order observations, duplicate observations handled
deterministically, and source restart via a new SourceGeneration. Unrelated events
are never merged merely because their timestamps are close.
