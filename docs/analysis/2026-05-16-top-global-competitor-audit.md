# Top global competitor audit: RonieNeubauer/rinha2026 and fksegundo/rinha-rust

Date: 2026-05-16

Scope:

- Current official #1 global reference observed from issue [`zanfranceschi/rinha-de-backend-2026#4682`](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4682): [`RonieNeubauer/rinha2026`](https://github.com/RonieNeubauer/rinha2026) — C API + FD-passing LB.
- Prior/top CI reference: [`fksegundo/rinha-rust`](https://github.com/fksegundo/rinha-rust) — Rust API + FD-passing LB.
- Jonathan comparison branch update: `884662190d7aaaf7cd1a13bbb4b82f03def81a0f` added both competitors to the benchmark matrix.
- Follow-up comparison run with Jonathan immutable hugepage candidate: `25967006547`, run number `101`.

## Evidence lane separation

Keep these lanes separate:

- **Official leaderboard / bot:** compare official issue results only with official issue results.
- **GitHub Actions CI comparison:** compare same-workflow CI numbers only with same-workflow CI numbers.

The official runner has materially different absolute p99 values from this repository's hosted comparison workflow. Do **not** compare Ronie's official `0.86ms` directly to Jonathan's CI `0.34ms`, or Jonathan's older official `1.45ms` directly to a CI competitor result.

## Official leaderboard lane

### RonieNeubauer/rinha2026 official issue #4682

Source: [`zanfranceschi/rinha-de-backend-2026#4682`](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4682), opened by `RonieNeubauer`, closed with bot result from `arinhadebackend` at `2026-05-16T06:34:12Z`.

| Repo | Commit | Image | Official p99 | Final score | Correctness |
| --- | --- | --- | ---: | ---: | --- |
| `RonieNeubauer/rinha2026` | `557ac41` | `ronieneubauer/rinha2026:2.0.0-preview13` | `0.86ms` | `6000` | `0 FP / 0 FN / 0 HTTP` |

Runtime resources from the official result:

- Total: `1 CPU`, `350 MB`.
- `lb`: `0.06 CPU`, `30M`, port `9999`, shared `/sockets` volume.
- `api1`, `api2`: `0.47 CPU`, `160M` each.
- API `HostConfig.CapAdd` was `null`; `Privileged` was `false` in the official runtime info.
- Submission branch at inspection time: `origin/submission = 557ac41b2fdbec4b991e25c917773fae31f2ca5c`.

### Jonathan official baseline currently archived here

Source: `docs/public/official/latest.json`, synced from official issue [`#4375`](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4375).

| Repo | Commit | API image | Official p99 | Final score | Correctness |
| --- | --- | --- | ---: | ---: | --- |
| `jonathanperis/rinha4-back-end-c` | `08ba2f5` in official runtime info | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-5175f6ec242a2567ec373cfac387caabc3c6f529` | `1.45ms` | `5839.52` | `0 FP / 0 FN / 0 HTTP` |

Official-lane read: Ronie's current official result is materially ahead of Jonathan's archived official C result (`0.86ms` vs `1.45ms`) while preserving perfect correctness. Jonathan has newer CI-clean commits since that official result, but those are **not** official leaderboard evidence until a new official/bot run exists.

## CI comparison lane

### Run 101 after Jonathan hugepage mmap candidate

Source: `origin/comparison:comparison-results/latest.json`, workflow run [`25967006547`](https://github.com/jonathanperis/rinha4-back-end-c/actions/runs/25967006547).

Inputs: `official_ref=main`, `k6_image=grafana/k6:latest`, `benchmark_repetitions=1`, Jonathan image override `ghcr.io/jonathanperis/rinha4-back-end-c:ci-9908938ece617bf765c98d6a6a3fbf299bab2b02`.

| Participant | CI p99 | Score | Correctness |
| --- | ---: | ---: | --- |
| `fksegundo-rust` | `0.28ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `rafaelcoelhox` | `0.33ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `jonathanperis` | `0.34ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `ronieneubauer-rinha2026` | `0.37ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `macedot` | `0.42ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |

CI-lane read: in this hosted comparison round Jonathan's current candidate beat Ronie's preview13 image (`0.34ms` vs `0.37ms`) but trailed fksegundo and rafaelcoelhox. This does not contradict the official leaderboard, because absolute p99/rank ordering differs by lane.

### Earlier run 100 snapshot

Single earlier CI round from `comparison-results/run-100.json` / workflow run `25965492615`:

| Participant | CI p99 | Score | FP | FN | HTTP errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| fksegundo-rust | 0.32ms | 6000 | 0 | 0 | 0 |
| macedot | 0.33ms | 6000 | 0 | 0 | 0 |
| RonieNeubauer/rinha2026 | 0.33ms | 6000 | 0 | 0 | 0 |
| rafaelcoelhox | 0.35ms | 6000 | 0 | 0 | 0 |
| jonathanperis | 0.36ms | 6000 | 0 | 0 | 0 |

Top CI participants are often separated by only `0.03–0.04ms`, so require multiple comparison rounds before trusting small rank changes.

## Submission/runtime shapes

### RonieNeubauer/rinha2026

Submission branch: `origin/submission` at `557ac41b2fdbec4b991e25c917773fae31f2ca5c`.

Implementation branch inspected: `origin/main` at `ff9c6c228760420caf08e13549b79a6f30f30886`; submitted image is preview13 lineage/tag.

Submitted compose shape from `origin/submission:docker-compose.yml`:

- Single image for all services: `ronieneubauer/rinha2026:2.0.0-preview13`.
- `lb`: command `./lb`, `0.06 CPU`, `30M`.
- `api1`, `api2`: command `./server`, `0.47 CPU`, `160M` each.
- Total resources: `1.00 CPU`, `350M`.
- API ulimits: `nofile 65535`, `rtprio: 99`, `memlock: -1`.
- API security: `seccomp:unconfined`.
- Transport: LB accepts TCP and passes accepted client FDs to APIs over Unix sockets using `SCM_RIGHTS`; API writes directly to client sockets.
- Manifest inspection for submitted `linux/amd64` image: `sha256:31819cdb530df17629cafe9c21fbc1c5dc2971af4070a5ef39773e5ae1fa9731`.

Important source-control caveat: `origin/main/docker-compose.yml` is stale (`2.0.0-preview5`, different CPU/probe settings). The official submission branch/issue are the authoritative runtime for ranking.

Key implementation points:

- Actual hot path is blocking C + pthread-per-connection, not io_uring despite stale README/info metadata.
- LB accepts sockets and FD-hands them to APIs; no byte proxying after accept.
- API uses `mlockall`, `mmap(MAP_POPULATE)`, `mlock`, `MADV_HUGEPAGE`, and `MADV_WILLNEED` for the index.
- Preview13 uses `SCHED_FIFO` worker threads via `rtprio`; `origin/main` also contains an optional wakeup-only RT mode (`RINHA_RT_MODE=wakeup`) to reduce starvation risk, but submitted compose does not set it.
- Parser is a fixed-schema handwritten JSON/HTTP parser with pre-rendered fraud responses.
- Index layout:
  - 2048 IVF clusters;
  - cluster offsets;
  - per-cluster bbox min/max;
  - pair-packed 14 dims as 7 pairs for AVX2 `_mm256_madd_epi16`;
  - labels separately.
- Search:
  - computes bbox lower-bound for every cluster;
  - probes clusters by increasing lower bound;
  - caps full probes at `RINHA_MAX_PROBES=20` in preview13 submission;
  - fast pass with `RINHA_FAST_PROBES=5`;
  - threshold-triggered full rerun for risky fraud counts/distances: `T2=3876355`, `T3=3144476`, `T4=5217400`; adaptive min/max effectively disabled with `99/0`.
- Server IO model:
  - LB does a tiny amount of work: accept, optional GET `/ready` handling, round-robin backend, `sendmsg(SCM_RIGHTS)`, then closes its copy of the client FD.
  - API receives FDs on UDS and spawns a detached worker thread per connection with a `256 KiB` stack.
  - Per-client socket options include `TCP_NODELAY`, `TCP_QUICKACK`, and `SO_RCVTIMEO=15s`.
  - Worker handles HTTP/1.1 keep-alive and pipelined complete requests in one buffer.
- Build/preprocess:
  - Docker build downloads official references from `zanfranceschi/rinha-de-backend-2026` at `OFFICIAL_REF=7387fad26bd2195d59e26e0273e619853df94745`.
  - Offline builder parses gzipped JSON, runs 2048-cluster k-means for 20 iterations, sorts vectors by cluster, writes bbox + pair SoA layout + labels, then runs a 2000-query brute-force-equivalence verifier.
- Compile flags are aggressive and Haswell-targeted: `-O3 -march=haswell -mtune=haswell -flto -fwhole-program -fno-plt -fno-stack-protector -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections`, plus linker GC/strip.

Important risk: docs and `info.json` are stale/misleading about io_uring and architecture, but submitted code/results are strong.

### fksegundo/rinha-rust

Submission branch: `origin/submission` at `0ef144fbeda3aecdcbfd94f924ed0db71152543f`.

Main implementation branch inspected earlier: `origin/main` at `9ea539e84101d2c871cc6dde6af1ac404246b32e`.

Compose shape:

- `api1`, `api2`: `ghcr.io/fksegundo/rinha-rust-api:latest`.
- API resources: `0.42 CPU`, `165M` each.
- `lb`: `ghcr.io/fksegundo/rinha-api-lb:latest`.
- LB resources: `0.16 CPU`, `20M`.
- Shared tmpfs volume at `/sockets`, `size=10m`.
- Total resources: `1.00 CPU`, `350M`.
- Transport: LB accepts TCP and passes client FDs to APIs over Unix sockets using `SCM_RIGHTS`; API writes directly to client sockets.

Important risk: submission uses mutable `latest` tags. A prior commit used an immutable-ish API tag (`sha-7586732`), but final submission reverted to `latest`; exact runtime is therefore not fully reproducible from compose alone.

Key implementation points from earlier audit:

- Build-time preprocessing downloads official references and writes `rinha-specialist.idx` into the image.
- Runtime uses a memory-mapped binary index.
- Search is exact-safe branch-and-bound, not just approximate IVF:
  - partitions by fraud-profile key;
  - stores partition/node bounding boxes;
  - searches matching profile first;
  - prunes remaining partitions/nodes by lower-bound distance.
- Vector layout is AoSoA, 8 lanes, int16, 14 dims packed to 16 dims.
- Default search mode is `key-first`; documented scale is `10000`; leaf size is `48`.
- API parser is a hand-written ordered fast parser with serde fallback.
- Responses for fraud counts `0..5` are precomputed bytes.
- Threading is a fixed 512-thread pool with 256 KiB stacks.

## What top references have in common

1. FD-handoff LB, not byte proxying.
2. Build-time reference preprocessing into a binary index.
3. Runtime memory mapping and aggressive page advice/pinning.
4. No general-purpose HTTP framework.
5. Handwritten JSON fast path.
6. Pre-rendered fraud responses for the six possible kNN fraud counts.
7. int16 quantized feature vectors.
8. AVX2-oriented distance layout.
9. Bounding-box lower-bound pruning before scanning candidate vectors.
10. Exact/correctness validation tools around any approximate pruning.

## Lessons for Jonathan C

Jonathan already has the highest-value broad choices: C hot path, fd-passing LB/API split, mmap/index preprocessing, SIMD, pre-rendered responses, and correctness-first benchmark gating. The remaining gap is probably not one single rewrite; it is a set of tail-latency wins around candidate ordering, pruning metadata, scheduler behavior, CPU split, and official-submission freshness.

### Priority 1 — official submission refresh after CI-clean candidate

Ronie's official `0.86ms` result proves a C implementation can be top global. Jonathan's newest CI-clean C candidate is much faster than the archived official result in the CI lane, but the official lane still points at older commit/image `08ba2f5` / `ci-5175f6...` with `1.45ms`.

Implementation direction:

1. Keep immutable image tags in submission compose.
2. Submit only a CI-clean candidate with documented comparison evidence.
3. After bot result, update `docs/public/official/*` and this audit.
4. Compare official-vs-official only.

Expected upside: closes the evidence gap before overfitting to CI-only measurements.

### Priority 2 — test Ronie-style 2048-cluster / 5-fast / 20-full policy as a controlled experiment

Ronie succeeds with 2048 IVF clusters, bbox ordering, `fast=5`, `max=20`, and fraud-bucket thresholds. Jonathan currently uses a different index/search layout with profile fastpath and repair policy; direct parameter copying is not guaranteed to work, but Ronie's policy is a concrete target for local replay sweeps.

Implementation direction:

- Add/sweep Jonathan equivalents only through env/config where possible.
- Use `search-stats-replay` against the same local artifact before CI.
- Require unchanged correctness buckets or an explicit exact-equivalence explanation.
- If local evidence is promising, run 3+ CI comparison rounds.

Expected upside: may reduce repair variance or expose a better fast/full split.

### Priority 3 — extend list/block lower-bound metadata

Ronie's C implementation and Rust both show that bounding boxes are central to p99. Jonathan already has block/list-oriented metadata; the next step is to check whether smaller group bboxes or better group ordering can reduce vector visits without changing semantics.

Implementation direction:

- Add per-list and optionally per-block-group AABB min/max where missing.
- Test group sizes near Rust's winning leaf size: `48`, `64`, maybe `96` vectors.
- Compute lower bound for group before scanning vectors.
- Stop scanning groups/lists once lower bound cannot improve top-k.

Expected upside: reduce vector visits per request without changing scoring semantics.

### Priority 4 — scheduler/RT tuning, but treat as dangerous

Ronie's preview13 uses all-worker `SCHED_FIFO` with `rtprio`; main adds wakeup-only mode because all-FIFO can starve clients/softirq/other containers.

Experiment matrix:

- baseline `SCHED_OTHER`;
- all-worker FIFO with low priority;
- wakeup-only FIFO around blocking receive/FD receive;
- optional `rtprio` compose variant.

Success criteria:

- p99 improves across multiple rounds;
- HTTP errors remain zero;
- no degradation for competing containers on 1 CPU;
- logs show no starvation symptoms.

Expected upside: wakeup latency reduction. Risk: noisy or catastrophic if CPU starvation occurs.

### Priority 5 — compare CPU split against top submissions

Current top references use:

- Ronie C preview13: API `0.47 + 0.47`, LB `0.06`.
- Rust: API `0.42 + 0.42`, LB `0.16`.
- Jonathan archived official: API `0.45 + 0.45`, LB `0.10`.

Run a small matrix for Jonathan:

- LB `0.06`, API `0.47/0.47`;
- LB `0.10`, API `0.45/0.45`;
- LB `0.16`, API `0.42/0.42`.

Expected upside: if Jonathan's LB is already near-zero byte work, Ronie's tiny LB budget may free useful API cycles. Historical note: a Rafael-like `0.10/0.45/0.45` split was already rejected in earlier CI rounds; do not rerun identical setups without a new reason.

### Priority 6 — keep submission reproducibility stronger than mutable competitors

Jonathan should avoid mutable tag ambiguity:

- use immutable GHCR `ci-*`/SHA tag or digest in submission compose;
- keep source branch and image tag mapping documented;
- include benchmark result JSON in docs/reports;
- record official bot issue URL and runtime-info after every submission.

## Suggested next work plan

1. Prepare a new official submission candidate from the current CI-clean C state only if the user wants a leaderboard refresh.
2. Otherwise, run local replay sweeps for Ronie-style `fast=5/full=20` and bbox/group-size ideas.
3. Add exact-vs-optimized verification output for any pruning change if existing replay stats are insufficient.
4. Run local verifier/replay before every benchmark push.
5. Run 3+ comparison rounds against Jonathan, fksegundo, Ronie, macedot, rafaelcoelhox for promising variants.
6. Promote only candidates with `0 FP / 0 FN / 0 HTTP`, stable CI-vs-CI evidence, and a clear official-submission path.

Do not start with scheduler tuning unless search/index changes stall: RT scheduling is tempting, but index/search changes are more portable and less likely to create runner-specific wins that fail elsewhere.
