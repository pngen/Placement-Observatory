# Limitations

- Placement Observatory does not own placement decisions; it observes and
  reconstructs them. Reconstructed candidate sets are derived and never presented as
  an observed scheduler candidate set unless exact evidence proves it.
- A single physical device is observed directly. Multi-device ranking that is not
  physically available uses a deterministic synthetic topology explicitly labelled
  synthetic; it is never presented as real multi-GPU hardware.
- Clock synchronization between distributed sources is not assumed. Ordering prefers
  monotonic time and explicit sequence/generation data.
- If measurements fluctuate due to driver/runtime behavior (for example GPU memory
  occupancy or kernel timing), the system reports the actual observed values and the
  tolerance, and does not invent an exact recovery value.
- Replay under an alternate policy generation is counterfactual, never historical
  reproduction.
- A stale or unavailable source reduces explanation confidence; stale measurements
  are never silently used as current.
