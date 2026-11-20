#pragma once

#include "TraceModel.h"

// The processing chain for one trace, as pure functions so the bench can run
// it without a model or an event loop:
//
//   S(f)  ->  port extension  ->  gating  ->  math with memory
//         ->  [time-domain transform]  ->  format  ->  smoothing
//         ->  markers, bandwidth search, limit test

namespace TracePipeline {

TraceResult compute(const TraceConfig& trace, const Workspace& ws, const SweepData* sweep);

// The complex data a "data -> memory" capture should store: after port
// extension and gating, before math.
bool memorySource(const TraceConfig& trace, const Workspace& ws, const SweepData* sweep,
                  std::vector<double>& x, std::vector<Cplx>& data);

// Fills in result.markers from the trace's marker list.
void evaluateMarkers(const TraceConfig& trace, const Workspace& ws, TraceResult& result);

} // namespace TracePipeline
