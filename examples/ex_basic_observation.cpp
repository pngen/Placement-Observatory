#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  ProviderContext ctx; ctx.source_id = SourceId(1); ctx.source_generation = 1; ctx.workload_id = WorkloadId(10);
  WindowsHostProvider prov;
  for (auto& o : prov.collect(ctx)) obs.ingest(o);
  std::printf("basic observation: %zu observation(s) ingested\n", obs.observations().size());
  return 0;
}