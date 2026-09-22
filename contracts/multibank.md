# Multibank configuration, workload and run contracts v1

The configuration SSOT is multibank.schema.json. YAML resolves defaults before
execution; cross-field geometry and connection checks run before invoking SystemC.
The flat `key=value` runtime configuration is fully resolved, versioned and rejects
missing/unknown/duplicate fields (the additive v1 burst fields may be omitted for legacy all-ready defaults). C++ repeats safety constraints at its public input
boundary. Python never advances target time. CMake consumers may supply the resolved
file directly. The fixed topology is described in systems/multibank/capabilities.json.

## Replayable workload

The header is exactly `# aix-esl-workload-v1 period_ps=N`, with positive integer N.
Records have eight whitespace-delimited fields:

```text
id source earliest_cycle R|W address data_hex mask_hex|- dependency_ids|-
```

ID/address/cycle use unsigned decimal 64-bit integers. Source fits unsigned 32-bit.
Data is a nonempty sequence of lower-case hex bytes, at most 4096 bytes. R records
contain the initial output buffer; only enabled lanes are overwritten at read
sampling. Masks are cyclic 00/ff bytes or `-` for all enabled. Dependencies are
comma-separated unique IDs of earlier records, or `-`. Repeated IDs, forward/self
references, extra columns, malformed hex, invalid masks and wrapped addresses fail.
At most 100000 records are supported. Each profile additionally checks its own
address range, source count, interface width and alignment. The multibank profile
requires data length equal to configured bytes and a single physical bank per request.

Earliest time is `earliest_cycle * period_ps`, not a completion prediction.
Per-source input order is FIFO. A request becomes eligible only when all listed
predecessors have retired; admission can still wait for arbitration/capacity/II.
No QoS, resets/epochs or variable-width AXI bursts are implied by v1. Readers and
writers use the public C++ WorkloadTrace API. The recording is the input plan, not
a candidate-dependent acceptance/completion log. Closed-loop replay therefore
retains finite backpressure rather than replaying predetermined return times.

## Synthetic temporal bursts

`requests` is the total request count **per source**, not the number of bursts.
`burst_requests` (default 1) gives requests per batch; `burst_period_cycles`
(default 0) is the start-to-start interval. Source p request i has earliest cycle
`p * phase_cycles + floor(i / burst_requests) * burst_period_cycles`.
The final batch can be partial. All requests remain fixed-width and single-bank;
this is temporal traffic batching, not an AXI burst protocol. Address pattern and
read/write selection continue across batches without resetting RNG or the cursor.
The request ID remains `i * ports + p`, and total bytes are requests * ports * bytes.

A source still offers at most one request per cycle with finite outstanding credits.
Rejection preserves the request; delayed batches can accumulate as pending input,
but do not reschedule later releases relative to actual completion. Record/replay
stores these earliest cycles in the existing workload v1 format. When replaying,
the supplied workload controls release times, so generator burst/phase fields have
no effect on that input. Releases past max_cycles yield budget failure with the
unsubmitted head retained in pending.json; they do not silently shorten the plan.
Old resolved v1 files without burst fields retain burst_requests=1/period=0.

## Observability

The shared aix-esl-events-v1 envelope uses integer SystemC resolution ticks. This
profile fixes 1 ps resolution and records enqueue, accept, service, read/write,
complete, response_ready and retire. Completion is data visibility; response_ready
includes configured return delay; retirement additionally respects source order
and one result/port/cycle throughput. Routing is combinational in this profile;
there is no cross-bank reassembly stage, and reports do not invent one.

Wait reasons distinguish arbitration, bank interval, bank completion capacity and
global response credits. Counts are waiting requests per cycle, not mutually
exclusive whole-system stalled cycles. Each source has one eligible queued head;
its enqueue→accept span defines queue occupancy. Source phase/dependency waiting
precedes enqueue and is not included in admission latency. Pending snapshots retain
blocked dependency IDs and remaining requests on failure.

`simulation.pending.json` schema_version 1 is available in all observation modes.
It records the snapshot cycle/period_ps, global outstanding/capacity, per-bank
outstanding/capacity, per-source remaining/queued/head/dependencies, and all owned
flights (ID/source/bank, accepted/service-ready/response-ready cycles, stage).
Remaining counts requests not yet admitted; queued means the source head has been
enqueued but not accepted. Bank/global credits include completed data until response
retirement; their totals must equal the number of flights. Flight stage is processed
state: service (data not yet visible), response (visible but not marked response-ready),
or retirement (response-ready but not yet retired). Ready cycles are scheduled times,
not proof that those transitions occurred. On max_cycles exhaustion, that boundary's
events have not been processed. Successful drain leaves zero credits/flights/remaining.
The cycle limit is an execution budget, not a progress-based deadlock detector; a
long valid service or future workload release may exceed it. This snapshot does not
claim generic wait-graph analysis or integration of the public ProgressWatchdog.

The analysis window begins at warmup_cycles and ends at drain completion. Latency
samples are transactions accepted within that interval, including drained tails.
p95/p99 use nearest rank. Queue/service occupation is clipped to the window;
retired bytes include the final completion boundary. Zero windows remain explicit.
Truncated trace metrics are diagnostic partial observations; complete percentiles
are suppressed. Complete event counts remain in the simulation summary even if the
trace storage limit is reached. Off/counters runs never fabricate a timeline.

## Reproducibility and failures

Each new run directory contains requested.json, resolved.json/cfg, topology.json,
capabilities.json, command logs, full workload, bounded events, summary and pending
snapshot. run.json binds those artifacts by SHA256 and includes executable/source
hashes, seed, named RNG algorithm, timeout, SystemC version and measurement window.
Successful runs require drained transactions and an independent byte scoreboard.
Input or execution failure remains FAIL with error/logs; existing output directories
are never overwritten. External executables are explicitly unverified against the
recorded source. Default builds compare source hashes before and after execution.

Sweep matrices enumerate all Cartesian points, including invalid/failed points.
A parent sweep binds its shared binary to source; children bind to the same binary.
Speedup is computed only for successful points with the identical recorded workload,
period, transaction count and final memory hash. Reported speedup is simulation
completion-time ratio; no area/PPA or calibration claim is made.
