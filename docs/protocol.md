# Protocol

The distributed path uses a real framed TCP protocol. Frame layout is a little-endian
fixed header (magic, protocol version, message type, payload length) followed by the
payload and a CRC-32 checksum. The strict decoder rejects oversized frames (hard cap)
truncated frames, unknown protocol versions, unknown message types, bad magic, and
checksum mismatches. Lossless 64-bit identities travel in the payload as integers.

Messages carry relevant authority such as CoordinatorEpoch, SourceId,
SourceGeneration, WorkerId, WorkerBootId, PlacementDecisionId, PlacementAttemptId,
ObservationGeneration, and request identity. The coordinator/collector process,
source/worker processes, and client/driver run as real OS processes over sockets.

## Authority rejection

The collector rejects stale coordinator epochs, stale source generations, stale
worker boots, duplicate decision events, obsolete placement attempts, malformed
observations, invalid source identity, and outcomes attached to the wrong
generation.
