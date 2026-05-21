# Beat rafaelcoelhox Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task. Keep each experiment isolated, benchmarked, and reversible.

**Goal:** Make `jonathanperis/rinha4-back-end-c` consistently beat `rafaelcoelhox/eu-sou-o-ze-pamonha` in the active Rinha4 C comparison lane while preserving `final_score=6000`, `0` false positives, `0` false negatives, and `0` HTTP errors unless Jonathan explicitly authorizes a correctness-risk experiment.

**Architecture:** Keep the legal non-inspecting FD-passing topology, but replace the current projection-bucket/block8 search core with a Rafael-inspired, Jonathan-optimized k-means IVF index: transposed centroid ranking, block16 AoSoA vector storage, and an integer AVX2 scan path. Treat transport/parser changes as secondary tail-latency work after the search/index gap is attacked.

**Tech Stack:** C11, GCC, AVX2/FMA on Haswell target, deterministic offline index builder, mmap/anonymous index loading, manual HTTP/JSON, Docker Compose, GitHub Actions comparison workflow.

---

## Evidence snapshot — 2026-05-16

### Candidate repository

- Repo: `/opt/data/github/jonathanperis/rinha4-back-end-c`
- Current local branch: `main`
- Current local HEAD: `4544ab9ecf5bb9ed1c57cd219f68874f462e3c72`
- Current performance commit under latest archived candidate report: `3d8d114b8d90f1a8102a0fbfb124a6d1536afd31`
- Latest candidate image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-3d8d114b8d90f1a8102a0fbfb124a6d1536afd31`
- Latest archived candidate report: `docs/public/reports/latest.json`
- Latest archived candidate result: p99 `0.35ms`, score `6000`, `0 FP / 0 FN / 0 HTTP`.

### Active comparison set

Comparison branch active set is intentionally limited to:

- `jonathanperis`
- `macedot`
- `rafaelcoelhox`

Latest completed tracked comparison round available from the current session:

| Run | Participant | p99 | Score | FP | FN | HTTP errors |
|---:|---|---:|---:|---:|---:|---:|
| 96 | rafaelcoelhox | `0.29ms` | `6000` | `0` | `0` | `0` |
| 96 | macedot | `0.31ms` | `6000` | `0` | `0` | `0` |
| 96 | jonathanperis | `0.34ms` | `6000` | `0` | `0` | `0` |

Recent comparison history inspected by subagent shows Jonathan clean but usually behind Rafael:

| Participant | Recent p99 pattern | Notes |
|---|---:|---|
| `rafaelcoelhox` | median around `0.315ms`, min around `0.28ms` | Usually leads C comparison lane. |
| `jonathanperis` | median around `0.375ms`, min around `0.28ms` | Can tie/beat in noisy windows, but not stable enough. |
| `macedot` | median around `0.325ms`, min around `0.30ms` | Secondary target after Rafael. |

### Rafael runtime shape verified

Repo: `/opt/data/github/competitors/eu-sou-o-ze-pamonha`

- `origin/main`: `15e613f9f67833764b2ac60d95e29ab3580d14ac`
- `origin/submission`: `dc4809305f5a75acc93a413aa61fe0881aae8978`
- Submission image: `ghcr.io/rafaelcoelhox/eu-sou-o-ze-pamonha:pmh`
- Submission compose: two APIs at `0.45 CPU / 160MB`, LB at `0.10 CPU / 30MB`, `seccomp:unconfined`, `nofile=65535`.
- Source files on `origin/main`:
  - `src_c/api.c`
  - `src_c/build_ivf.c`
  - `src_c/lb.c`
  - `Dockerfile`
  - `Makefile`

### Rafael mechanisms to beat

Verified Rafael advantages:

1. **FD-passing LB with no HTTP proxying**
   - `src_c/lb.c` accepts TCP and passes client FDs with `SCM_RIGHTS` over `SOCK_SEQPACKET`.
   - APIs process the original client socket.
   - Jonathan already broadly matches this topology with the standalone yolo LB and C APIs.

2. **Immediate FD processing**
   - Rafael calls `handle_conn(nc, efd)` immediately after receiving a passed client FD in `drain_control_fds()`.
   - Jonathan already has the same important behavior in current `drain_ctrl_conn()`.

3. **Edge-triggered API event loop and response batching**
   - Rafael uses `EPOLLIN | EPOLLRDHUP | EPOLLET` for client/control descriptors.
   - Rafael accumulates multiple responses into `struct iovec iovs[MAX_IOVECS]` and replies with `writev()`.
   - Jonathan currently uses plain `EPOLLIN` and per-response `send()`/`write_all()` behavior.
   - This is a secondary opportunity; previous broad read-drain experiments regressed.

4. **True k-means IVF index**
   - Rafael builds `4096` k-means clusters using deterministic k-means++ seeding and threaded assignment.
   - `src_c/build_ivf.c` uses `DEFAULT_K=4096`, `KMEANS_ITERS=50`, `INIT_SAMPLE=50000`, `BLOCK_VECS=16`.
   - Jonathan currently uses projection-key sorted buckets in `src/preprocess/build_index.c`, not true k-means.

5. **Transposed centroid storage**
   - Rafael stores centroids as `centroids[dim][cluster]`.
   - `compute_centroid_dists()` in `src_c/api.c` computes all `4096` centroid distances with contiguous AVX2/FMA loads.
   - Jonathan stores centroids list-major and loops list-by-list, computing lower bounds and centroid distances in `search_ivf()`.

6. **Block16 AoSoA candidate scan**
   - Rafael uses `BLOCK_VECS=16`.
   - `scan_cluster()` processes two 8-lane AVX2 halves per block, rejects on partial distance after dims `0..7`, prefetches future blocks, then finishes dims `8..13` only for candidates.
   - Jonathan currently uses block8 with a similar partial-distance cut; block8 is already optimized, so the next leverage is layout, not tiny edits.

7. **Adaptive 5 -> 24 probe widening**
   - Rafael fast path probes `5` centroids.
   - Rafael widens to `24` only for ambiguous top-5 fraud counts, currently `2..4` in inspected `origin/main`.
   - Jonathan probes `3`, repairs to `24`, and needs fraud range `1..4` plus `repair0`/`repair5` distance thresholds for correctness on the projection index.

8. **Startup memory and cache warmup**
   - Rafael copies index into anonymous memory, uses `MADV_HUGEPAGE`, attempts `mlock()`, then runs 2000 synthetic searches before serving.
   - Jonathan mmaps file-backed index, uses `posix_fadvise`, `madvise(MADV_WILLNEED)`, and page touching, but no synthetic search warmup.

### Known rejected Jonathan experiments — do not repeat blindly

| Variant | Result | Decision |
|---|---|---|
| `INDEX_NPROBE=2` | Clean but median not better; noisy tail worse in one run | Reject for current projection index |
| `INDEX_NPROBE=5` | Clean but slower than baseline | Reject for current projection index |
| CPU split `0.10 / 0.45 / 0.45` | Clean but median worse | Reject for current baseline; retest only after search changes |
| Disable both `repair0` and `repair5` thresholds | `7 FP / 8 FN` | Hard reject |
| Disable only `repair5` | `7 FP / 0 FN` | Hard reject |
| Disable only `repair0` | `0 FP / 8 FN` | Hard reject |
| `INDEX_REPAIR_NPROBE=16` | `6 FP / 12 FN` | Hard reject |
| Broad read-drain/event-loop experiment | Clean but slow median around `0.47ms` | Reject as broad approach |
| Tail-only/lower-bound local AVX2 patch | Regressed/reverted | Avoid exact repeat |
| Direct k-means port keeping current block8/runtime shape | Built, but replay slower than projection baseline | Only retry with runtime layout/search rewrite |

---

## Hard gates

Every candidate must pass these before any promotion:

1. `make test` passes.
2. Candidate image is immutable and named with the candidate commit SHA.
3. Docker Compose total resources remain within `1 CPU / 350 MB`.
4. LB remains non-inspecting: no fraud payload parsing or scoring in LB.
5. No answer tables, official payload memorization, or payload-derived correction tables.
6. Candidate comparison result is correctness-clean:
   - `final_score = 6000`
   - `false_positive_detections = 0`
   - `false_negative_detections = 0`
   - `http_errors = 0`
7. Beat target uses repeated rounds, not a single lucky hosted runner:
   - **minimum pass:** Jonathan p99 below Rafael in at least `2/3` same-window comparison rounds;
   - **strong pass:** Jonathan median p99 at least `0.03ms` below Rafael over `3` rounds;
   - **stretch:** Jonathan p99 `<= 0.27ms` while clean.

---

## Strategy

Do **not** keep sweeping current projection-index environment variables. Rafael is not winning because of one env knob; he is winning because index quality and memory layout fit the CPU. The path to beat him is:

1. Keep Jonathan's already-good FD-passing topology.
2. Build index v2 with real k-means clusters and transposed centroids.
3. Use block16 AoSoA like Rafael, but avoid Rafael's float conversion overhead by keeping Jonathan's int16 query/vector distance arithmetic.
4. Retune repair policy only after the new index exists.
5. Add only targeted transport/parser micro-optimizations after search starts winning or profiling proves search is no longer dominant.

---

## Phase 0: Baseline freeze and measurement discipline

### Task 0.1: Create a benchmark ledger for this campaign

**Objective:** Avoid losing track of noisy CI evidence.

**Files:**
- Create: `docs/analysis/2026-05-16-beat-rafaelcoelhox-ledger.md`

**Steps:**

1. Create a table with columns:
   - experiment branch
   - candidate SHA
   - API image
   - LB image
   - comparison branch SHA
   - run URL
   - Rafael p99
   - Jonathan p99
   - score
   - FP/FN/HTTP
   - decision
2. Backfill the current baseline:
   - candidate image `ci-3d8d114...`
   - latest candidate p99 `0.35ms`
   - comparison run 96: Rafael `0.29ms`, Jonathan `0.34ms`.
3. Commit the ledger.

**Verification:**

```bash
git diff -- docs/analysis/2026-05-16-beat-rafaelcoelhox-ledger.md
```

Expected: ledger contains current baseline and has no speculative claims marked as facts.

### Task 0.2: Run local non-Docker verification before any code experiment

**Objective:** Ensure the baseline is locally buildable before changing it.

**Commands:**

```bash
cd /opt/data/github/jonathanperis/rinha4-back-end-c
make clean test
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS"
```

**Expected:** all tests pass and `build/api`, `build/build-index` exist.

**Note:** full Compose benchmark cannot be trusted locally here because Docker daemon is not reachable from this user; CI comparison is the benchmark source of truth.

### Task 0.3: Generate fresh search stats for the current baseline

**Objective:** Know exactly how much work the current projection/block8 index does before rewriting it.

**Commands:**

```bash
cd /opt/data/github/jonathanperis/rinha4-back-end-c
make clean preprocess search-stats-replay \
  CFLAGS_ARCH="-DRINHA_SEARCH_STATS -march=haswell -mtune=haswell -mavx2 -mfma"
./build/build-index /tmp/references.json.gz /tmp/rinha4-baseline-index.bin 4096
INDEX_NPROBE=3 \
INDEX_REPAIR_NPROBE=24 \
INDEX_REPAIR_MIN_FRAUD=1 \
INDEX_REPAIR_MAX_FRAUD=4 \
INDEX_REPAIR0_WORST_THRESHOLD=4021242 \
INDEX_REPAIR5_WORST_THRESHOLD=3748534 \
INDEX_EXACT_FALLBACK=0 \
RINHA_SEARCH_STATS=1 \
./build/search-stats-replay /tmp/rinha4-baseline-index.bin /tmp/references.json.gz 250000
```

**Expected:** output includes fast/repair/exact counts, fraud buckets, scanned vectors, and no crash.

**Decision rule:** do not tune repair thresholds again until this distribution is captured in the ledger.

---

## Phase 1: Index v2 file format scaffold

### Task 1.1: Add a new versioned layout constant

**Objective:** Let v1 and v2 coexist so stale indexes fail loudly.

**Files:**
- Modify: `src/common/index_format.h`
- Modify: `src/common/index.h`
- Modify: `src/common/index.c`
- Modify: `tests/test_search.c`

**Implementation:**

Add a new layout value, for example:

```c
#define RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16 4U
#define RINHA_INDEX_FLAG_TRANSPOSED_CENTROIDS 2U
```

Extend `rinha_index_t` only as needed for v2:

```c
const int16_t *centroids_t;   /* [dims][list_count], v2 */
const uint32_t *block_offsets;/* [list_count + 1], block units for block16 */
```

**Verification:**

```bash
make clean test
```

Expected: existing v1 tests still pass.

### Task 1.2: Teach the loader to reject malformed v2 indexes

**Objective:** Make the new format safe before writing any v2 files.

**Files:**
- Modify: `src/common/index.c`
- Test: `tests/test_search.c`

**Implementation notes:**

- Validate header `dims == RINHA_DIMS`.
- Validate `list_count > 0`.
- Validate all sections fit within mapped file size.
- Validate offsets monotonic.
- Validate block count is consistent with padded vector count.
- Keep v1 loader path untouched.

**Verification:**

Add a small fake-header test that invalid v2 layout fails to load or produces a safe null index, following existing test conventions.

Run:

```bash
make clean test
```

### Task 1.3: Add an env-gated v2 builder mode without changing default output

**Objective:** Keep production default stable while iterating on the new builder.

**Files:**
- Modify: `src/preprocess/build_index.c`

**Implementation:**

Add:

```c
static int env_enabled(const char *name) { ... }
```

In `main()`:

```c
if (env_enabled("INDEX_BUILD_KMEANS_V2")) {
    return write_kmeans_v2(...);
}
return existing_write_ivf(...);
```

**Verification:**

```bash
make clean preprocess
./build/build-index /tmp/references.json.gz /tmp/index-v1.bin 4096
INDEX_BUILD_KMEANS_V2=0 ./build/build-index /tmp/references.json.gz /tmp/index-v1b.bin 4096
cmp /tmp/index-v1.bin /tmp/index-v1b.bin
```

Expected: v1 output unchanged when v2 flag is disabled.

---

## Phase 2: Real k-means index builder

### Task 2.1: Port deterministic streaming reference loader from Rafael, adapted to Jonathan's int16 scale

**Objective:** Avoid loading/parsing the giant gzip via slow whole-file string scans when building v2.

**Files:**
- Modify: `src/preprocess/build_index.c`

**Reference:**
- Rafael: `src_c/build_ivf.c`, `GzStream`, `load_references()`.

**Implementation notes:**

- Keep Jonathan's final storage as int16 quantized with `rinha_qround()`.
- For k-means training, either:
  - keep a temporary float copy of the 14 dims, or
  - use int16 vectors and compute centroid sums in double/int64.
- Deterministic seed must be fixed and documented.

**Verification:**

```bash
make clean preprocess
INDEX_BUILD_KMEANS_V2=1 ./build/build-index /tmp/references.json.gz /tmp/index-v2-smoke.bin 16
```

Expected: exits `0`, writes a v2 index file, no crash.

### Task 2.2: Implement k-means++ sampled seeding

**Objective:** Match Rafael's cluster quality baseline.

**Files:**
- Modify: `src/preprocess/build_index.c`

**Reference:**
- Rafael: `kmeanspp_init()` with `INIT_SAMPLE=50000`.

**Implementation notes:**

- Use deterministic RNG.
- Use sample size `min(n, 50000)`.
- Start with `k=4096` for real builds, but test with `16` and `128` first.
- Store centroids initially list-major for builder convenience.

**Verification:**

Add debug-only print under env `INDEX_BUILD_DEBUG=1`:

```bash
INDEX_BUILD_KMEANS_V2=1 INDEX_BUILD_DEBUG=1 ./build/build-index /tmp/references.json.gz /tmp/index-v2-128.bin 128
```

Expected: selected centroid count equals `128`, no empty fatal path.

### Task 2.3: Implement threaded assignment with transposed centroid scratch

**Objective:** Make full `4096` cluster build feasible.

**Files:**
- Modify: `src/preprocess/build_index.c`

**Reference:**
- Rafael: `nearest_centroid()`, `parallel_assign()`, `centroid_transpose()`.

**Implementation notes:**

- Use AVX2 when available for nearest-centroid assignment.
- Keep max threads capped (`16` is enough).
- Recompute centroids after each assignment.
- Stop early when changed count is below `0.1%` of rows, matching Rafael's stopping rule.

**Verification:**

```bash
INDEX_BUILD_KMEANS_V2=1 INDEX_BUILD_DEBUG=1 ./build/build-index /tmp/references.json.gz /tmp/index-v2-4096.bin 4096
```

Expected: completes within a few minutes on CI-class CPU; no OOM.

### Task 2.4: Write v2 sections with 32/64-byte alignment

**Objective:** Make runtime loads aligned and cache-friendly.

**Files:**
- Modify: `src/preprocess/build_index.c`
- Modify: `src/common/index.c`

**V2 section order:**

1. Header.
2. Transposed int16 or float centroids `[dim][cluster]`.
3. Cluster offsets `[cluster+1]`, preferably in block units.
4. Labels padded to `total_blocks * 16`.
5. Block16 vectors `[block][dim][lane16]`, int16.
6. Optional bounds/certification metadata only if needed after correctness testing.

**Important decision:** Use **int16 transposed centroids** first to keep Jonathan's distance semantics and memory small. Add float centroids only if integer centroid ranking loses to Rafael.

**Verification:**

```bash
INDEX_BUILD_KMEANS_V2=1 ./build/build-index /tmp/references.json.gz /tmp/index-v2-4096.bin 4096
stat -c '%s' /tmp/index-v2-4096.bin
```

Expected: file size fits comfortably in two `160MB` API containers.

---

## Phase 3: Runtime v2 centroid ranking

### Task 3.1: Add aligned distance scratch for `4096` clusters

**Objective:** Compute all centroid distances once per request and reuse for fast/full probes.

**Files:**
- Modify: `src/common/search.c`

**Implementation notes:**

- Use stack only if bounded and aligned safely; otherwise add per-call static thread-local or small heap-free scratch.
- APIs are single-threaded per process, so a static aligned scratch like Rafael's `g_dists[4096]` is acceptable if documented.
- Use `uint32_t` or `uint64_t` distances depending on quantized centroid range.

**Verification:**

```bash
make clean test
```

### Task 3.2: Implement AVX2 transposed centroid distance

**Objective:** Replace current list-major per-centroid `rinha_dist_i16()` loop for v2.

**Files:**
- Modify: `src/common/search.c`
- Test: `tests/test_search.c`

**Algorithm:**

For each dimension `d`:

1. Broadcast query `q[d]`.
2. Load 16 centroid values from `centroids_t[d][ci..ci+15]`.
3. Compute squared differences into accumulators.
4. Store one distance per centroid.

**Implementation options:**

- First implementation: AVX2 int16 -> int32 accumulation.
- If overflow risk is awkward, use two 8-lane halves with int32 accumulators; final distances can fit in `uint32_t` for normalized scale.

**Verification:**

Add scalar-vs-AVX test on a small synthetic transposed centroid array.

Run:

```bash
make clean test
```

### Task 3.3: Implement top-N selection for fast and full probes

**Objective:** Avoid current lower-bound-first sorted insertion path for v2 fast path.

**Files:**
- Modify: `src/common/search.c`

**Algorithm:**

- Compute all centroid distances into scratch.
- Select `fast_nprobe` nearest clusters.
- Only if repair needed, select `repair_nprobe` nearest clusters or keep the full top-24 from the first selection.

**Reference:**
- Rafael: `top_n()`.

**Jonathan improvement:**

- Select top `repair_nprobe` once, then use first `nprobe` for fast phase.
- Current `max_nprobe` is only `24` or `25`, so top-24 once avoids a second top-N pass if we store it.

**Verification:**

Synthetic test: known distance array returns expected top-N order.

---

## Phase 4: Runtime v2 block16 integer scan

### Task 4.1: Add block16 scanner behind v2 layout check

**Objective:** Keep v1 block8 untouched while adding v2 scan path.

**Files:**
- Modify: `src/common/search.c`

**Implementation:**

Add:

```c
static void scan_list_block16_v2(...)
```

Dispatch in `scan_list()` or a v2-specific search function when layout is `RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16`.

**Verification:**

`make clean test` still passes with v1 fixtures.

### Task 4.2: Implement scalar block16 scan first

**Objective:** Establish correctness before AVX2.

**Files:**
- Modify: `src/common/search.c`
- Modify: `tests/test_search.c`

**Algorithm:**

- For each block of 16 lanes:
  - For each valid lane:
    - compute full 14-dim int16 squared distance;
    - update top5.
- Respect padded labels/vectors so padded lanes cannot enter top5.

**Verification:**

- Build a tiny v2 fixture with 2 clusters and known labels.
- Query a known vector.
- Assert top5 fraud count matches scalar expected.

### Task 4.3: Implement AVX2 block16 partial-distance scan

**Objective:** Beat Rafael's float scanner by keeping integer distances.

**Files:**
- Modify: `src/common/search.c`

**Algorithm:**

1. Load low 8 lanes and high 8 lanes per dimension.
2. Accumulate dims `0..7` first.
3. Compare partial sums with current top5 worst.
4. If neither low nor high lanes can beat worst, skip block.
5. Finish dims `8..13` only for candidate block halves.
6. Store candidate distances to stack only when mask nonzero.
7. Update top5 using worst-slot tracking.

**Reference:**
- Rafael: `scan_cluster()`.
- Jonathan current: `block8_dist_avx2()`.

**Verification:**

- Scalar-vs-AVX test on randomized small block16 arrays.
- Compile both with and without `-mavx2` if feasible.

### Task 4.4: Add prefetching only after scalar/AVX correctness is stable

**Objective:** Reduce memory stalls without complicating correctness debugging.

**Files:**
- Modify: `src/common/search.c`

**Implementation:**

- Prefetch `8` blocks ahead like Rafael as first attempt.
- Try `4`, `8`, `12` blocks ahead only in replay/microbench, not CI sweeps.

**Verification:**

Search replay output buckets unchanged versus no-prefetch v2.

---

## Phase 5: V2 repair policy retuning

### Task 5.1: Start with conservative clean policy

**Objective:** Get first v2 candidate correctness-clean before optimizing probes.

**Initial env:**

```yaml
INDEX_NPROBE: "5"
INDEX_REPAIR_NPROBE: "24"
INDEX_REPAIR_MIN_FRAUD: "1"
INDEX_REPAIR_MAX_FRAUD: "4"
INDEX_EXACT_FALLBACK: "0"
```

**Reason:** Rafael uses `5 -> 24`, Jonathan's current correctness needs `1..4`; combine them for first clean v2.

**Verification:**

Run offline replay:

```bash
INDEX_BUILD_KMEANS_V2=1 ./build/build-index /tmp/references.json.gz /tmp/index-v2.bin 4096
INDEX_NPROBE=5 INDEX_REPAIR_NPROBE=24 INDEX_REPAIR_MIN_FRAUD=1 INDEX_REPAIR_MAX_FRAUD=4 INDEX_EXACT_FALLBACK=0 RINHA_SEARCH_STATS=1 ./build/search-stats-replay /tmp/index-v2.bin /tmp/references.json.gz 250000
```

Expected: no crash; bucket changes recorded. Full correctness still requires CI benchmark.

### Task 5.2: Sweep v2 fast probes in replay before CI

**Objective:** Avoid spending GitHub Actions runs on obvious losers.

**Matrix:**

| Fast nprobe | Repair nprobe | Repair range | Expected use |
|---:|---:|---|---|
| 3 | 24 | `1..4` | Jonathan aggressive candidate |
| 4 | 24 | `1..4` | Middle ground |
| 5 | 24 | `1..4` | Conservative Rafael-like |
| 3 | 24 | `2..4` | At-cost candidate; likely faster, riskier |
| 4 | 24 | `2..4` | Balanced risk |
| 5 | 24 | `2..4` | Rafael-style repair |

**Decision rule:** Only CI-test variants that preserve replay fraud bucket distribution or have a plausible explanation for differences.

### Task 5.3: CI-test only the top two v2 policies

**Objective:** Respect GitHub rate-limit/CI caution.

**Steps:**

1. Commit the best v2 candidate to a feature branch, not `main` initially.
2. Let CI build immutable image.
3. Update `comparison` branch `competitor-compose/jonathanperis/docker-compose.yml` to the immutable image.
4. Run comparison against active set only.
5. Repeat 3 rounds if first round is clean and competitive.

**Commands:**

```bash
gh workflow run benchmark.yml \
  --repo jonathanperis/rinha4-back-end-c \
  --ref comparison \
  -f compose_file=all-comparison \
  -f official_ref=main \
  -f benchmark_repetitions=1
```

**Pass gate:** clean and below Rafael in at least `2/3` rounds.

---

## Phase 6: Memory loading and warmup

### Task 6.1: Add synthetic search warmup after index load

**Objective:** Reduce cold p99 and TLB/cache variance like Rafael.

**Files:**
- Modify: `src/api/main.c`

**Implementation:**

After `rinha_index_load()` and before binding/ready:

```c
for (uint32_t i = 0; i < warmup_count; ++i) {
    int16_t q[RINHA_DIMS];
    /* deterministic pseudo-random normalized int16 query */
    (void)rinha_search_fraud_count(&index, q);
}
```

Gate with env:

```c
INDEX_WARMUP_SEARCHES=2000
```

Default to `2000` only after replay/CI confirms no startup timeout issue.

**Verification:**

```bash
make clean test
```

### Task 6.2: Try anonymous index copy + hugepage + nonfatal mlock

**Objective:** Test Rafael's memory-loading advantage.

**Files:**
- Modify: `src/common/index.c`

**Implementation:**

Env-gate first:

```bash
INDEX_ANON_COPY=1
INDEX_MLOCK=1
```

Behavior:

- `mmap` anonymous private memory with `MAP_POPULATE` if available.
- Read file into it.
- `madvise(..., MADV_HUGEPAGE)` if available.
- `mlock()` nonfatal; log only in debug builds, no request-path logging.
- Free/close original fd.

**Risk controls:**

- Do not double memory after startup.
- If anonymous allocation fails, fall back to current file mmap.
- Keep candidate within `160MB` per API.

**Verification:**

Run tests and check index file size vs memory estimate before CI.

---

## Phase 7: Secondary API/transport micro-optimizations

Only start this phase after v2 search is clean or if instrumentation shows search is no longer dominant.

### Task 7.1: Replace linear connection allocation with a free stack

**Objective:** Remove O(MAX_CONN) scan from `add_conn()`.

**Files:**
- Modify: `src/api/main.c`

**Implementation:**

- Add `int free_stack[MAX_CONN]`, `int free_top`.
- Initialize with all connection indices.
- `add_conn()` pops one index.
- `close_conn()` pushes it back.

**Verification:**

```bash
make clean test
```

### Task 7.2: Add response batching with `writev()`

**Objective:** Match Rafael's lower syscall count for pipelined requests.

**Files:**
- Modify: `src/api/main.c`

**Implementation notes:**

- Keep existing buffer parsing loop.
- Instead of sending each response inside `process_buffer()`, collect up to `16` response pointers/lengths into `struct iovec`.
- Call `writev()` once.
- Preserve bad-request/not-found close behavior.

**Verification:**

Add or extend HTTP tests for two pipelined `/ready` or `/fraud-score` requests in one buffer.

Run:

```bash
make clean test
```

### Task 7.3: Edge-triggered epoll as isolated experiment only

**Objective:** Determine whether Rafael's EPOLLET helps without repeating the failed broad drain experiment.

**Files:**
- Modify: `src/api/main.c`

**Implementation:**

- Env/compile-gate `RINHA_EPOLL_ET`.
- If enabled, `EPOLLIN | EPOLLRDHUP | EPOLLET` for client and control fds.
- Drain reads until `EAGAIN` only in ET mode.

**Decision rule:** Reject immediately if any HTTP errors or p99 regression in one CI round.

### Task 7.4: Parser fast path only if needed

**Objective:** Reduce JSON overhead after search is optimized.

**Files:**
- Modify: `src/common/vectorize.c`
- Test: `tests/test_vectorize.c`

**Implementation:**

- Add order-assuming parser path modeled after Rafael's `next_val()` approach.
- Keep existing key-search parser as fallback if canonical order check fails.
- Preserve exact int16 feature output versus current parser for all existing tests.

**Verification:**

`tests/test_vectorize.c` must compare canonical and fallback payloads.

---

## Phase 8: Promotion workflow

### Task 8.1: Build immutable candidate image from main or feature branch

**Objective:** Never compare mutable `latest`.

**Steps:**

1. Commit candidate code.
2. Push branch.
3. Wait for CI image build.
4. Record image tag in ledger.

### Task 8.2: Update comparison branch with candidate image

**Files:**
- Modify on `comparison`: `competitor-compose/jonathanperis/docker-compose.yml`

**Rules:**

- Keep active set limited to Jonathan, macedot, Rafael.
- Do not alter competitor configs unless refreshing from latest `origin/submission`.
- Use immutable candidate image.

### Task 8.3: Run 3 same-window comparisons

**Objective:** Beat runner noise.

**Commands:**

```bash
for i in 1 2 3; do
  gh workflow run benchmark.yml \
    --repo jonathanperis/rinha4-back-end-c \
    --ref comparison \
    -f compose_file=all-comparison \
    -f official_ref=main \
    -f benchmark_repetitions=1
  # wait/parse/archive before triggering the next if rate limits are a concern
done
```

**Decision table:**

| Outcome | Action |
|---|---|
| Clean and beats Rafael in `2/3` | Merge/promote to `main`, update docs, consider official submission. |
| Clean but ties/noisy | Run one more pair only if margin is under `0.02ms`; otherwise keep tuning. |
| Any FP/FN/HTTP error | Hard reject; revert comparison to previous baseline with `[skip ci]`. |
| Clean but slower | Revert experiment or archive as negative evidence. |

### Task 8.4: Official promotion only after a written report

**Objective:** Avoid submitting a lucky or stale candidate.

**Report must include:**

- Candidate commit and immutable API/LB image tags.
- Current official submission config SHA.
- Comparison run URLs and table.
- Correctness all clean.
- Explanation of why it should improve official rank.
- Explicit user approval before opening/updating official submission issue.

---

## Ranked backlog

1. **Index v2: k-means + transposed centroids + block16 integer scan** — highest leverage, highest risk.
2. **V2 repair/probe tuning** — required after index v2; do not reuse current projection-index conclusions blindly.
3. **Startup search warmup / memory loading** — likely tail-stability win, medium-low risk if gated.
4. **Response batching/free-stack allocation** — modest transport win, relatively contained.
5. **Order-assuming parser fast path** — medium risk; do only after search gap narrows.
6. **Edge-triggered event loop** — low confidence because a broader read-drain experiment regressed.
7. **More current env sweeps without new index** — deprioritized; previous sweeps are exhausted.

---

## Why this should beat Rafael

Rafael's strongest components are k-means IVF, transposed centroid ranking, and block16 AoSoA scan. Jonathan already matches the important FD-passing topology and has stricter correctness infrastructure. The winning combination is therefore not to copy Rafael blindly; it is to combine:

- Rafael's better cluster quality and memory layout;
- Jonathan's quantized int16 distance semantics;
- Jonathan's existing repair/correctness gates;
- a block16 AVX2 integer scanner that avoids Rafael's int16-to-float conversion cost;
- repeated same-window CI gates to avoid promoting noise.

If implemented cleanly, the expected result is a candidate that keeps Jonathan's current correctness reliability while removing the main structural reason Rafael's p99 is lower.
