# Interference Observatory

Interference Observatory is an open-source, vendor-neutral C++20 observability runtime for
measuring, attributing, and explaining cross-workload accelerator interference across shared
resources: cache, memory bandwidth, memory capacity, PCIe, collectives, NUMA, storage, and shared
execution resources.

## The systems question

When one workload slows down, stalls, loses throughput, or becomes less predictable while sharing
infrastructure with other workloads, what measurable interference is occurring, which shared
resource is implicated, which workloads are associated with the degradation, how strong is the
evidence, and what remains unknown?

## The thesis

Contention exists before it becomes an actionable conflict. A GPU can remain highly utilized while
two workloads destroy each other's memory locality. Two kernels can execute correctly while shared
memory bandwidth collapses useful throughput. Interference Observatory makes these effects
measurable — **without pretending correlation is proof.**

It observes. It establishes controlled comparisons. It distinguishes temporal association from
attributable interference. It quantifies degradation. It preserves uncertainty. **It does not
become Contention Governor.**

## Exact boundary

Interference Observatory **owns**: cross-workload interference evidence; shared-resource
observation; workload-pair and workload-set comparison; baseline construction;
isolated-versus-co-run comparison; interference episodes; degradation measurement; interference
classification; interference confidence; temporal association; counterfactual/reference
comparison; source provenance; freshness; UNKNOWN attribution; deterministic explanations;
timelines; episode reconstruction; persistence; replay; stable digests; real multiprocess
interference experiments; real CUDA interference experiments where supported.

Interference Observatory does **not own**: active contention resolution, scheduling, admission,
placement, preemption, quota enforcement, bandwidth enforcement, capacity reservation, memory
remediation, topology control, collective scheduling, resource arbitration, cost optimization, SLO
enforcement, utilization accounting, or generic performance profiling.

- **Utilization Observatory** is the previous layer, which explains where accelerator capacity is
  going. It is **not rebuilt** here.
- **Contention Governor** is the next architectural layer, which consumes interference evidence and
  decides corrective action. Interference Observatory stops at OBSERVE, MEASURE, COMPARE,
  ATTRIBUTE, EXPLAIN. It never THROTTLEs, PREEMPTs, MIGRATEs, REASSIGNs, REBALANCEs, SHEDs, or
  SCHEDULEs anything, except through optional read-only recommendation/intention metadata that is
  clearly separated from authority. Every explanation carries `authority_only = true`.

## Interference-domain model

Typed domains: `COMPUTE_EXECUTION`, `CACHE`, `MEMORY_BANDWIDTH`, `MEMORY_CAPACITY`, `PCIE`,
`TRANSFER`, `COLLECTIVE`, `NETWORK`, `NUMA`, `STORAGE`, `HOST_CPU`, `RESIDENCY`,
`SHARED_ENGINE`, and `UNKNOWN`. Only domains justified by implementation are reported available on
a platform; every other domain remains `UNSUPPORTED`. Domains are never mandatory on every platform.

## Outcome model

`NO_INTERFERENCE_DETECTED`, `POTENTIAL_INTERFERENCE`, `INTERFERENCE_DETECTED`,
`SEVERE_INTERFERENCE`, `INSUFFICIENT_EVIDENCE`, `BASELINE_INVALID`, `REVALIDATION_REQUIRED`,
`UNSUPPORTED`, and `UNKNOWN`. There are no hidden thresholds: an explicit `Policy` defines
significance, sampling, and freshness gates.

## Attribution strength

`TEMPORALLY_ASSOCIATED`, `CORRELATED`, `CONTROLLED_COMPARISON`,
`STRONG_COUNTERFACTUAL_EVIDENCE`, `DIRECT_RESOURCE_EVIDENCE`, and `UNKNOWN`. Correlation is never
presented as causality. `DIRECT_RESOURCE_EVIDENCE` is claimed only when the measured shared
resource and degradation relationship are actually established.

## Baseline semantics

A baseline is a first-class proof obligation bound to: device generation, worker boot, workload
generation, runtime/configuration fingerprint, problem-size fingerprint, topology generation,
baseline generation, and evidence generation. A baseline from the wrong generation is rejected
(`BASELINE_INVALID`); a stale or expired baseline triggers `REVALIDATION_REQUIRED`. No baseline
means no confident interference claim (`INSUFFICIENT_EVIDENCE`).

Supported baseline types: `ISOLATED`, `HISTORICAL_ISOLATED`, `CONTROL_RUN`, `RECENT_HEALTHY`,
`CONFIGURED_REFERENCE`, and `SYNTHETIC_REFERENCE`.

## Isolated-vs-co-run comparison

The primary pattern measures A isolated, B isolated, and A+B co-run under comparable conditions,
computing degradation for latency, throughput, device time, transfer time, collective duration,
stall/wait, and tail latency where supplied. Results are **directional** and **never averaged away**:
A's degradation due to B and B's degradation due to A are preserved separately.

## Directional pairwise interference

A→B and B→A are distinct cells (`victim`, `neighbor`, resource domain, baseline, co-run result,
degradation, confidence, evidence sources, episode, generation). Asymmetry is preserved: A may hurt
B much more than B hurts A.

## Multi-workload semantics

Sets of 3+ workloads are represented as a victim plus a co-running set with observed degradation,
known pairwise evidence, residual multi-party effect, and `UNKNOWN` contribution. Group interference
is **never silently split** across neighbors without evidence.

## Degradation metrics

`MetricDelta` carries a typed metric (`LATENCY`, `THROUGHPUT`, `DEVICE_TIME`, `TRANSFER_TIME`,
`COLLECTIVE_TIME`, `STALL_TIME`, `MEMORY_BANDWIDTH`, `TAIL_LATENCY`, `COMPLETION_RATE`), an
absolute displacement, a signed relative ratio, the sample count, and the variance. There is no
single opaque "interference score"; a summary is always decomposable and policy-defined.

## Confounders

Explicitly modeled: thermal throttling, power throttling, clock change, background OS load,
different workload input, different kernel version, different driver/runtime state, cold start,
cache warmup, allocation warmup, frequency scaling, device temperature, memory pressure,
retry/recovery, worker restart, and different placement. A comparison with unresolved major
confounders lowers confidence or becomes `BASELINE_INVALID`/`UNKNOWN`.

## Controlled experiments

An explicit `ExperimentPlan` includes warmup, baselines, co-run, repeats, and launch order
(`PRIMARY_FIRST`, `NEIGHBOR_FIRST`, `SIMULTANEOUS`). Warmup is explicit and must not be accidentally
compared cold-versus-warm. Insufficient repetitions → `INSUFFICIENT_EVIDENCE`; the engine exposes
sample counts and variance.

## Source / provenance / freshness

Provenance: `MEASURED`, `REPORTED`, `DERIVED`, `CONTROLLED`, `SYNTHETIC`, `REPLAYED`, `UNKNOWN`.
Freshness: `CURRENT`, `STALE`, `EXPIRED`, `REVALIDATION_REQUIRED`, `HISTORICAL`, `UNKNOWN`.
Source health: `HEALTHY`, `DEGRADED`, `STALE`, `DISCONNECTED`, `REVALIDATION_REQUIRED`,
`UNSUPPORTED`, `UNKNOWN`. Dynamic evidence is generation-aware; historical evidence remains
historical. Dead telemetry is never silently reused.

## Capability model

`BackendCapabilities` exposes which measurements are actually available. Unsupported counters remain
`UNSUPPORTED`. This build reports real CUDA discovery/memory/H2D/D2H/kernels/events/memory
accounting on NVIDIA, and reports cache counters, memory-bandwidth counters, PCIe counters, NVLink
counters, collective metrics, NUMA topology, and storage metrics as `UNSUPPORTED`.

## Real CUDA proof (RTX 5090, sm_120)

On this development machine (NVIDIA GeForce RTX 5090, compute capability 12.0, CUDA 12.9) the
backend runs **real hardware experiments**: device discovery, `cudaMalloc`, real kernels, H2D/D2H
transfers, CUDA events/synchronization, and memory accounting. Measured (honest, not fabricated):

- **Compute/compute co-run**: alone ~62–65 ms; co-run ~65–67 ms (≈ +2–4%).
- **Memory/intensive co-run**: alone ~0.33 ms (≈ 1.5 TB/s); co-run ~0.35 ms (≈ +5% measured
  memory-bandwidth degradation). The `MEMORY_BANDWIDTH` comparison classifies this as
  `POTENTIAL_INTERFERENCE` / `CONTROLLED_COMPARISON`.
- **Transfer co-run**: no reliable effect (≈ −2% noise), reported honestly as no-interference.
- **Device memory returns to baseline**: free memory delta is 0 bytes (`BASELINE CLOSED`).

Kernels are warmed before timing so cold/warm mismatches do not confound the comparison. The CUDA
backend is isolated and the core builds and runs without CUDA.

## Real process neighbor-removal proof (distributed)

The distributed control-plane proof uses **real OS processes** (coordinator + worker A + worker B),
**real framed/versioned/checksummed TCP** (`IOBF`, version 1, CRC-32C, bounded, partial-read-safe),
and real process termination. `io-coordinator` spawns worker processes, accepts their connections,
ingests isolated baselines, drives a co-run, **terminates worker B as a real OS process**, observes
A's post-removal recovery, records it, upgrades A→B to `STRONG_COUNTERFACTUAL_EVIDENCE`, restarts
worker A with a fresh boot id, and rejects the stale old-boot baseline. A second coordinator
incarnation loads historical state, advances the `CoordinatorEpoch`, marks dynamic evidence
`REVALIDATION_REQUIRED`, and rejects old-epoch authority.

Within this harness the measurement values are driven by the coordinator (labeled
`CONTROLLED`/`SYNTHETIC`) so the proof is deterministic and reproducible; the process, TCP, kill,
restart, and epoch mechanics are real.

## Coordinator restart

Incarnation 1 ingests baselines, records episodes, and persists historical state. After a real
coordinator process restart, incarnation 2 advances the `CoordinatorEpoch`, reloads historical
baselines/episodes (historical conclusions stay historical), marks the then-current dynamic source
state `REVALIDATION_REQUIRED`, and requires workers to reconnect and republish before current
interference detection resumes. Old-epoch traffic is rejected.

## Persistence / replay / digest

Versioned, integrity-checked persistence (magic + version + bounded length + CRC-32C) rejects
corruption, truncation, trailing garbage, unknown version, and oversized payloads, with atomic
save/replace. Replay of identical canonical histories yields identical baselines, episodes,
pairwise matrices, effect sizes, classifications, explanations, and digests. The canonical digest
excludes nondeterministic state (addresses, unordered iteration, ephemeral sockets); same semantic
history → same digest. Historical replay never implies current validity.

## REAL / CONTROLLED / DERIVED / SYNTHETIC / UNSUPPORTED

- **REAL**: actual CUDA device, actual kernels, actual transfers, actual event timing, actual OS
  process lifetime, actual process death, actual TCP.
- **CONTROLLED**: isolated/co-run experiment results; the distributed harness's driven measurements
  are labeled `CONTROLLED`/`SYNTHETIC`.
- **DERIVED**: degradation ratios, episode reconstruction, correlation.
- **SYNTHETIC**: multi-GPU/NVLink/RDMA/NUMA scenarios not physically available.
- **UNSUPPORTED**: hardware counters/backends not available (cache, PCIe, NVLink, collective,
  NUMA, storage counters in this build).

These are never blurred.

## No Contention Governor overreach

Interference Observatory may conclude: *"Workload B is strongly associated with a 31% throughput
degradation in Workload A during concurrent memory-intensive execution."* It may **not** throttle,
move, preempt, reject, reserve bandwidth, change placement, change batch size, or alter priorities.
Every conclusion is a structured evidence-bearing explanation with `authority_only = true`.

## Structured explanations

`InterferenceExplanation` carries the affected workload, associated workload(s), resource domain,
baseline, co-run evidence, absolute/relative degradation, sample count, variance, timeline overlap,
confounders checked and unresolved, provenance, freshness, confidence, attribution strength,
supporting observations, UNKNOWN components, and a structured
`what_would_strengthen_evidence` list (explanatory only — never a control action).

## Benchmarks

`io-bench` measures completed work: observation ingest, baseline lookup, comparison, episode and
pairwise-matrix reconstruction, persistence save/load, and replay at 100, 1,000, 10,000, and
100,000 observations. On this machine ingest runs at roughly 5.5M observations/second.

## Install

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

An independent downstream consumer:

```cmake
find_package(InterferenceObservatory CONFIG REQUIRED)
target_link_libraries(app PRIVATE InterferenceObservatory::InterferenceObservatory)
```

The CMake package exports the target plus the optional CUDA backend target. The core builds
without CUDA.

## CLI

`io-observatory` supports `demo`, `summary`, `workloads`, `episodes`, `pairwise`, `sources`,
`capabilities`, `compare`, `explain`, `digest`, `replay`, and `validate-state`.

## Examples

`io-examples` runs isolated baseline, asymmetric pairwise slowdown, no-interference,
insufficient-evidence, confounded, stale-baseline, historical-replay, worker-restart, and
coordinator-restart scenarios.

## Limitations

- Collective, NVLink, NUMA, and storage interference are represented as `UNSUPPORTED`/SYNTHETIC
  in this build (no multi-GPU hardware, no NUMA topology query exposed).
- The distributed harness drives measurement values (labeled `CONTROLLED`/`SYNTHETIC`) so the
  proof is deterministic; the real process/TCP/kill/restart mechanics are real.
- Direct cache counters and PCIe/NVLink counters are not exposed by this backend; cache inference
  remains conservative.
- Real hardware measurements vary; the engine reports measured truth and never fabricates
  expected interference.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
