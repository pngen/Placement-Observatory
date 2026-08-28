# Decision Record

A placement decision records what was observed about a placement decision. It
contains workload/request identity and decision epoch/generation, the candidate set
as observed (or reconstructed, labelled derived), the selected candidate and
rejected candidates where available, exact hard constraints, soft preferences, cost
and score components, source evidence supporting each component, tie-break
semantics and the final selected-placement reason, rejection reason per candidate
where known, missing-evidence markers, and a post-placement outcome link (linked
separately, never retroactively rewriting the original placement evidence).

## No fabricated candidate sets

If only the chosen placement is observable, the record does not fabricate a complete
candidate set. Partial-evidence decisions are supported explicitly, with missing
fields reported rather than invented.

## Policy generations

Historical decisions stay bound to their original PolicyGeneration. Replaying old
evidence under a new policy is labelled counterfactual, not historical
reproduction.
