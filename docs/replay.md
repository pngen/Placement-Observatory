# Deterministic Replay

Placement Observatory replays placement explanations from recorded evidence. Replay
input includes enough canonical evidence to reconstruct the candidate set as
recorded or reconstructed, constraints, score and cost components, policy
generation, tie-breaking, and the selected result. Replay is deterministic for
identical input.

Replay produces a replay digest, a decision digest, an evidence digest, and mismatch
diagnostics. If a historical placement cannot be reproduced because required
evidence was absent, replay reports exactly what is missing and never fabricates
replay success.

## Policy fidelity

Historical evidence is replayed under the policy generation that was in force at the
time; counterfactual replay under an alternate policy generation is distinctly
labelled.
