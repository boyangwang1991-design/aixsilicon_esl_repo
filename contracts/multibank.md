# Multibank configuration, workload and run contracts v1

The configuration SSOT is multibank.schema.json. YAML resolves defaults before
execution; cross-field geometry and connection checks run before invoking SystemC.
The flat `key=value` runtime configuration is fully resolved, versioned and rejects
missing/unknown/duplicate fields. C++ repeats safety constraints at its public input
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
