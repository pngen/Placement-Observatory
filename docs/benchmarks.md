# Benchmarks

The measured benchmark suite reports observation/decision ingest rates, binary and
JSON serialization throughput, replay and explanation latency, snapshot and
timeline latency, persistence write time, and recovery time at multiple workload
sizes. Benchmarks are measured, not simulated; they include real CUDA device
allocations when available. The tool reports observed values and tolerance rather
than inventing exact recovery when driver behavior fluctuates.
