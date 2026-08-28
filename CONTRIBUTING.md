# Contributing to Placement Observatory

Placement Observatory accepts contributions from individuals and organizations on
the terms of the Apache License 2.0 without requiring a Contributor License
Agreement (CLA).

## Ground rules

- Build must remain clean under /W4 /WX in both Release and Debug.
- Every observation is immutable after publication; corrections create a new
  observation revision. Do not mutate historical evidence.
- Evidence provenance and the measured / reported / derived / estimated / unknown
  classification must be preserved for every quantity.
- Reconstructed candidate sets must be labelled derived and carry their source
  chain; never present reconstruction as an observed scheduler candidate set.
- Do not add test timeouts. Tests must finish naturally.
- Keep no telemetry transmission; telemetry is written only to operator-chosen local
  files.

## Building and testing

Run tools\build.cmd Release (or Debug) from a Visual Studio developer command
prompt, then run ctest --test-dir build\Release --output-on-failure.

## Documentation

Public documentation must describe only Placement Observatory. Do not mention any
roadmap, future systems, or sibling projects. Keep the README ending block
unchanged. Mermaid diagrams must use conservative GitHub-compatible syntax with
quoted labels.

## Style

- C++20, MSVC /permissive-, /Zc:__cplusplus, /utf-8.
- Vendor-neutral architecture; CUDA/NVML are proven sources, not the semantic
  definition of the system.