# Beat C Competitors Implementation Plan

## Status note — superseded

This was the first competitive plan and is retained for historical context. The active state is now tracked in `docs/plans/2026-05-15-stabilize-c-candidate-before-promotion.md` and `docs/analysis/2026-05-16-cross-repo-approaches.md`. Current baseline uses `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair range `1..4`, thresholds `repair0=4021242` and `repair5=3748534`, and immutable image `ci-ab157f4d7e286f8676f419c7e7815068251f4757`. The later sweeps rejected `INDEX_NPROBE=5`, `INDEX_NPROBE=2`, CPU split `0.10/0.45/0.45`, disabled `repair0+repair5`, and disabled only `repair5`.


> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Reduce `jonathanperis/rinha4-back-end-c` p99 below macedot and rafaelcoelhox while preserving perfect correctness: final score `6000`, `0` false positives, `0` false negatives, `0` HTTP errors.

**Architecture:** Keep the current legal C architecture: standalone non-inspecting FD-passing load balancer plus two C API processes over Unix/control sockets. The plan prioritizes the search/index hot path first, then request-loop tail latency, then CPU split/build-flag tuning, using the `comparison` branch GitHub Actions benchmark as the authoritative scoreboard.

**Tech Stack:** C11, GCC/Clang, AVX2/FMA where available, manual HTTP/JSON parsing, mmap/compact IVF index, Docker Compose, GitHub Actions comparison workflow.

---

## Evidence Baseline

### Repositories inspected

- Ours: `/opt/data/github/jonathanperis/rinha4-back-end-c`
  - `origin/main`: `32600fc` (`docs: archive rinha benchmark`)
  - `origin/submission`: `5100631` (`chore: promote C split 0.08 lb 0.46 apis`)
  - `origin/comparison`: `21fcba5` (`ci: archive comparison benchmark [skip ci]`)
- macedot: `/opt/data/github/competitors/rinha-2026-c`
  - `origin/main`: `58dce28`
  - `origin/submission`: `4e4ba85`
- rafaelcoelhox: `/opt/data/github/competitors/eu-sou-o-ze-pamonha`
  - `origin/main`: `d6d2576`
  - `origin/submission`: `5d905cd` after refresh
  - `origin/restore/rc1-exact`: `1ff2f91`

### Current comparison evidence

From `origin/comparison:comparison-results/latest.json`, run `58` (triggered during this inspection):

| participant | p99 | score | fp | fn | http errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| jonathanperis | `0.39ms` | `6000` | `0` | `0` | `0` |
| macedot | `0.46ms` | `6000` | `0` | `0` | `0` |
| rafaelcoelhox | `0.36ms` | `6000` | `0` | `0` | `0` |

Previous latest run `57` had jonathanperis `0.52ms`, macedot `0.31ms`, rafaelcoelhox `0.38ms`; one run is noisy, so promotion must use median over repeated runs.

Historical comparison summary from `comparison-results/run-*.json`:

| participant | n | min p99 | median p99 | latest p99 |
| --- | ---: | ---: | ---: | ---: |
| jonathanperis | 24 | `0.36ms` | `0.41ms` | `0.52ms` |
| macedot | 12 | `0.30ms` | `0.32ms` | `0.31ms` |
| rafaelcoelhox | 9 | `0.24ms` | `0.34ms` | `0.38ms` |
| silent-index | 5 | `0.23ms` | `0.28ms` | `0.27ms` |
| luan-latest | 15 | `0.27ms` | `0.31ms` | `0.31ms` |

A fresh comparison CI run was dispatched and completed successfully for all active participants:

- Run: <https://github.com/jonathanperis/rinha4-back-end-c/actions/runs/25939676820>
- Branch: `comparison`
- Workflow: `benchmark.yml`
- Inputs: `compose_file=all-comparison`, `official_ref=main`, `benchmark_repetitions=1`
- Archived to `origin/comparison:comparison-results/run-58.json`

### Local verification status

- `make test` passed locally on `main`.
- Offline policy evaluator against the official generated `54100` payload set passed `0 FP / 0 FN` for `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair range `1..4`, existing `0/5` thresholds, `INDEX_EXACT_FALLBACK=0`; sampled local p99 was lower than the previous default in the ctypes harness, so this became the first CI candidate.
- Main build/benchmark run `25943232377` passed with image `ghcr.io/jonathanperis/rinha4-back-end-c:ci-3e8462e72904748e2f567fe4df15203bcdcc37c0`: p99 `0.40ms`, score `6000`, `0 FP / 0 FN / 0 HTTP errors`.
- Comparison branch run `25943420370` passed: jonathanperis `0.38ms`, macedot `0.41ms`, rafaelcoelhox `0.34ms`, all with score `6000` and `0 FP / 0 FN / 0 HTTP errors`.
- Docker local benchmarking cannot run here because Docker daemon is unreachable:
  - `Cannot connect to the Docker daemon at unix:///var/run/docker.sock`.

### Verified competitor mechanisms

#### macedot

- Submission branch only keeps `docker-compose.yml`, `info.json`, `README.md`, and `LICENSE`; implementation lives in image `ghcr.io/macedot/rinha-2026-c:0.0.6`.
- Compose split: LB `0.2 CPU / 30M`; each API `0.4 CPU / 150M`; total memory ~`330M`.
- Uses external LB `jrblatt/so-no-forevis:v1.0.0` with SCM_RIGHTS FD passing.
- Main branch implementation:
  - HTTP server: `src/http_server.c`, epoll, FD-passing control socket, manual HTTP parse, `REQ_BUF_SIZE=32768`, `MAX_EVENTS=128`.
  - Vectorizer: `src/vectorizer.c`, order-assuming single-pass JSON parser, 14-dim features, custom float parser.
  - Search bridge: `bridge/bridge.c` from `macedot/rinha-2026-base`.
  - Index format: `IVF1`, 4096 clusters, transposed centroids, labels padded to block boundary, AoSoA blocks of 8 vectors, int16 quantization scale `10000`.
  - Search policy: `IVF_NPROBE=8`, `IVF_FULL_NPROBE=24`, rerun full pass only when fast fraud count is `2` or `3`.
  - SIMD: AVX2 centroid distance and AoSoA scan with early termination after dims `0..7`.
- Build flags: `-O3 -march=haswell -mtune=haswell -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DNDEBUG`, static link, stripped.

#### rafaelcoelhox

- Submission branch only keeps `docker-compose.yml` and `info.json`; implementation lives in image `ghcr.io/rafaelcoelhox/eu-sou-o-ze-pamonha:rc1-v4`.
- Submission compose split: LB `0.10 CPU / 30M`; each API `0.45 CPU / 160M`; total CPU `1.0`.
- Main branch implementation:
  - Single C source API: `src_c/api.c`, 955 LOC.
  - Own C LB: `src_c/lb.c`, accepts TCP and passes FDs over `SOCK_SEQPACKET` control sockets.
  - API immediately tries to process a newly passed FD before waiting for another epoll turn (`handle_conn(nc, efd)` inside `drain_control_fds`).
  - Uses `EPOLLET | EPOLLRDHUP`, fixed connection pool of 512, response batching with `writev` up to 16 responses.
  - Index format: `CIVF2`, 4096 clusters, transposed centroids, labels, int16 AoSoA blocks of 16 vectors.
  - Search policy: `FAST_NPROBE=8`, `FULL_NPROBE=24`; refreshed `origin/main` code repairs when result is `1..4` (`fc >= 1 && fc <= 4`).
  - Warmup: loads index into anonymous mmap, `mlock`, `madvise(MADV_HUGEPAGE)`, then executes 2000 synthetic searches before serving.
  - Build flags include `-O3 -march=haswell -mavx2 -mfma`; API built with `-DRINHA_ASSUME_PASSED_FD_FLAGS` to remove fd flag syscalls.

### Hypotheses to validate

1. We are behind primarily because our current search/index layout scans less efficiently than the competitors' AVX2 AoSoA int16 IVF kernels.
2. Historical note: this plan originally assumed `INDEX_NPROBE=1`; the current baseline is `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair range `1..4`, and thresholds `repair0=4021242` / `repair5=3748534`. Later sweeps rejected `nprobe=5`, `nprobe=2`, disabled `repair0+repair5`, and disabled only `repair5`.
3. rafaelcoelhox gains tail latency by immediate processing after FD receipt and by avoiding fd flag syscalls in the hot path.
4. macedot spends more CPU on LB (`0.2`) than us (`0.09`) but has faster search; rafaelcoelhox spends only `0.10` on LB, so matching their API hot path is more important than simply increasing LB CPU.

## Hard Gates

Every implementation task must preserve these gates:

1. `make test` passes.
2. No Docker/compose resource total above Rinha limits: `1 CPU / 350 MB`.
3. LB remains non-inspecting: no fraud payload parsing or `/fraud-score` answering in LB.
4. No official test payloads, answer tables, correction tables, or payload-derived reference data.
5. Comparison CI must show:
   - `final_score=6000`
   - `false_positive_detections=0`
   - `false_negative_detections=0`
   - `http_errors=0`
6. Performance target sequence:
   - Gate 1: stable p99 `< 0.38ms` to beat rafaelcoelhox latest.
   - Gate 2: stable p99 `< 0.31ms` to beat macedot latest.
   - Stretch: p99 `<= 0.27ms`, competitive with silent-index / best historical tracked results.

---

## Task 1: Archive the fresh comparison CI result

**Objective:** Capture the run started during reconnaissance and update this plan if it changes the baseline.

**Files:**
- Modify: `docs/plans/2026-05-15-beat-c-competitors.md`
- Read: `comparison-results/latest.json` on `origin/comparison` after workflow completion

**Step 1: Poll the CI run**

Run:

```bash
gh run view 25939676820 \
  --repo jonathanperis/rinha4-back-end-c \
  --json status,conclusion,url,jobs | jq .
```

Expected: eventually `status == "completed"` and `conclusion == "success"`.

**Step 2: Fetch comparison branch**

Run:

```bash
git fetch origin comparison
git show origin/comparison:comparison-results/latest.json | jq .
```

Expected: latest JSON includes `jonathanperis`, `macedot`, and `rafaelcoelhox` rows.

**Step 3: Update this plan's evidence table if run `25939676820` changes the baseline**

Edit this file and update the current comparison evidence table.

**Step 4: Commit only if the plan file changes**

Run:

```bash
git add docs/plans/2026-05-15-beat-c-competitors.md
git commit -m "docs: add C competitor beating plan"
```

Expected: commit succeeds if the plan changed; otherwise skip.

---

## Task 2: Build a microbenchmark harness for search-only latency

**Objective:** Measure search/vectorize separately from HTTP/LB so changes can be ranked before expensive CI runs.

**Files:**
- Create: `tests/bench_search.c`
- Modify: `Makefile`

**Step 1: Add a bench target skeleton**

Modify `Makefile`:

```make
$(BUILD_DIR)/bench_search: tests/bench_search.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) $^ -o $@ $(LDFLAGS_COMMON)

bench-search: $(BUILD_DIR)/bench_search
	./$(BUILD_DIR)/bench_search
```

**Step 2: Create `tests/bench_search.c`**

Use allowed/reference-style synthetic queries only. Do not embed official test payloads.

```c
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "common/search.h"
#include "common/vectorize.h"

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void) {
    puts("bench_search placeholder: wire to generated allowed-reference queries");
    return 0;
}
```

**Step 3: Compile the skeleton**

Run:

```bash
make clean bench-search CFLAGS_ARCH="-march=x86-64-v3 -mavx2 -mfma -flto -fomit-frame-pointer"
```

Expected: builds and prints placeholder.

**Step 4: Commit**

```bash
git add Makefile tests/bench_search.c
git commit -m "test: add search microbenchmark skeleton"
```

---

## Task 3: Port rafaelcoelhox-style immediate FD processing

**Objective:** Remove one epoll round-trip for newly passed client FDs.

**Files:**
- Modify: `src/api/main.c`
- Test: `tests/test_fdpass.c` if needed

**Step 1: Locate FD receive/register path**

Read `src/api/main.c` around the control-FD handler.

Expected: find where a passed client fd is registered with epoll.

**Step 2: Add a helper that can process one newly registered client immediately**

Implementation requirement:

- The helper must behave exactly like the normal client EPOLLIN handler.
- If `recv` returns `EAGAIN/EWOULDBLOCK`, keep the fd registered.
- If request is already available, parse/respond immediately.
- Do not close healthy keep-alive sockets after successful response unless existing behavior does.

**Step 3: Run tests**

```bash
make test
```

Expected:

```txt
./build/test_http
./build/test_vectorize
./build/test_search
./build/test_fdpass
```

**Step 4: Build optimized binary**

```bash
make clean all CFLAGS_ARCH="-march=x86-64-v3 -mavx2 -mfma -flto -fomit-frame-pointer"
```

Expected: `build/api` and `build/build-index` are produced.

**Step 5: Commit**

```bash
git add src/api/main.c tests/test_fdpass.c

git commit -m "perf: process passed fds immediately"
```

---

## Task 4: Add a compile-time passed-FD-flags fast path

**Objective:** Match rafaelcoelhox's `-DRINHA_ASSUME_PASSED_FD_FLAGS` optimization to skip redundant `fcntl` syscalls when LB already passes nonblocking/cloexec sockets.

**Files:**
- Modify: `src/api/main.c`
- Modify: `Makefile` or `Dockerfile`
- Modify: `tests/test_fdpass.c` if tests need explicit default behavior

**Step 1: Identify current fd flag setup**

Search:

```bash
rg "fcntl|set_nonblock|CLOEXEC|NONBLOCK" src/api src/common tests
```

Expected: find the fd registration path for newly accepted/passed client sockets.

**Step 2: Guard redundant flag setup**

Wrap passed-client fd flag setup like:

```c
#ifndef RINHA_ASSUME_PASSED_FD_FLAGS
    if (rinha_set_nonblocking(fd) != 0) { /* existing error path */ }
#endif
```

Do not remove flag setup for listener/control sockets.

**Step 3: Add production compile define**

In `Dockerfile` build flags or `Makefile` production flags, add:

```txt
-DRINHA_ASSUME_PASSED_FD_FLAGS
```

Only for production build; tests should still exercise safe default unless explicitly needed.

**Step 4: Verify**

```bash
make test
make clean all CFLAGS_ARCH="-march=x86-64-v3 -mavx2 -mfma -flto -fomit-frame-pointer -DRINHA_ASSUME_PASSED_FD_FLAGS"
```

Expected: tests pass; build succeeds.

**Step 5: Commit**

```bash
git add src/api/main.c Makefile Dockerfile tests/test_fdpass.c
git commit -m "perf: skip redundant flags for passed client fds"
```

---

## Task 5: Align production compiler flags with competitors

**Objective:** Remove avoidable compiler/runtime overhead before deeper algorithm changes.

**Files:**
- Modify: `Dockerfile`
- Optionally modify: `Makefile`

**Step 1: Update production CFLAGS_ARCH**

Current Dockerfile uses `-march=x86-64-v3 -flto -fomit-frame-pointer`. Test adding:

```txt
-mavx2 -mfma -mtune=haswell -fno-plt -fno-semantic-interposition -fno-trapping-math
```

Candidate full flags:

```txt
-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS
```

**Step 2: Verify locally without Docker**

```bash
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS"
make test
```

Expected: build succeeds and tests pass.

**Step 3: Commit**

```bash
git add Dockerfile Makefile
git commit -m "perf: tune production compiler flags"
```

**Step 4: Build/publish candidate image through existing CI**

Use the repo's existing build workflow or PR flow. Do not hand-push unverified images.

---

## Task 6: Port/experiment with int16 AoSoA search layout behind a feature flag

**Objective:** Test whether competitor-style int16 AoSoA blocks beat our current search implementation under the official benchmark.

**Files:**
- Modify: `src/common/index_format.h`
- Modify: `src/common/index.c`
- Modify: `src/common/search.c`
- Modify: `src/preprocess/build_index.c`
- Add/modify tests: `tests/test_search.c`

**Step 1: Add a new index magic/version without deleting the old reader**

Example:

```c
#define RINHA_INDEX_MAGIC_AOSOA2 "..."
```

Keep old format readable until the experiment is proven.

**Step 2: Add build-index writer for transposed centroids + labels + int16 AoSoA blocks**

Mirror the verified legal competitor pattern:

- 4096 centroids.
- 14 dims.
- int16 quantization scale `10000`.
- labels padded to block size.
- AoSoA block size experiments:
  - Variant A: 8 vectors/block, like macedot bridge.
  - Variant B: 16 vectors/block, like rafaelcoelhox.

**Step 3: Add reader support**

Expose pointers/metadata in `src/common/index.h` without copying per request.

**Step 4: Add scalar correctness tests**

In `tests/test_search.c`, create a tiny synthetic index where exact top-5 is known. Test both old and new formats.

Run:

```bash
make test
```

Expected: all tests pass.

**Step 5: Add AVX2 scan kernel**

Implement in `src/common/search.c`:

- centroid distance over transposed centroids,
- top-N cluster selection,
- AoSoA scan with early cutoff after dims `0..7`,
- top-5 label count.

Use compile-time guards:

```c
#if defined(__AVX2__) && defined(__FMA__)
/* AVX2 path */
#else
/* scalar path */
#endif
```

**Step 6: Run local guardrails**

```bash
make clean test CFLAGS_ARCH="-march=haswell -mavx2 -mfma"
make clean all CFLAGS_ARCH="-march=haswell -mavx2 -mfma -flto -fomit-frame-pointer"
```

Expected: tests pass and binaries build.

**Step 7: Commit as an experiment branch**

```bash
git checkout -b exp/aosoa-int16-search
git add src/common/index_format.h src/common/index.c src/common/index.h src/common/search.c src/preprocess/build_index.c tests/test_search.c

git commit -m "perf: experiment with int16 aosoa search layout"
```

---

## Task 7: Benchmark search policy matrix on comparison branch

**Objective:** Find the fastest legal correctness-preserving probe/repair policy.

**Files:**
- Modify: `docker-compose.yml`
- Modify or create comparison compose file on `comparison` branch after image exists

**Step 1: Test current policy family**

Run comparison CI with image overrides for each candidate image, using `webapi_image` input.

Candidate env sets:

1. Current baseline:
   - `INDEX_NPROBE=3`
   - `INDEX_REPAIR_NPROBE=24`
   - repair min/max `1..4`
   - `INDEX_REPAIR0_WORST_THRESHOLD=4021242`
   - `INDEX_REPAIR5_WORST_THRESHOLD=3748534`
2. Competitor-like:
   - fast `8`
   - full `24`
   - repair only for ambiguous `2..3`
3. Silent-index-like:
   - fast `2`
   - adaptive `1..4`
4. Exact fallback off for all first-pass trials.

**Step 2: Trigger comparison CI per candidate**

Use:

```bash
gh workflow run benchmark.yml \
  --repo jonathanperis/rinha4-back-end-c \
  --ref comparison \
  -f compose_file=competitor-compose/jonathanperis/docker-compose.yml \
  -f official_ref=main \
  -f k6_image=grafana/k6:latest \
  -f webapi_image=<candidate-image> \
  -f benchmark_repetitions=1
```

Expected: each run produces a comparison artifact and summary.

**Step 3: Promote only stable wins**

A candidate must beat our current p99 and preserve perfect correctness on at least two runs before becoming default.

**Step 4: Commit winning compose/env changes**

```bash
git add docker-compose.yml
git commit -m "perf: tune search probe policy"
```

---

## Task 8: Tune LB/API CPU split after API hot path improves

**Objective:** Avoid starving either LB handoff or API search once the new hot path is in place.

**Files:**
- Modify: `docker-compose.yml`
- Modify: `origin/comparison:competitor-compose/jonathanperis/docker-compose.yml` equivalent when testing published image

**Step 1: Test CPU split candidates**

Candidates:

- Current-ish: LB `0.09`, APIs `0.455/0.455`.
- Rafael-like: LB `0.10`, APIs `0.45/0.45`.
- Macedot-like: LB `0.20`, APIs `0.40/0.40`.
- Middle: LB `0.14`, APIs `0.43/0.43`.

**Step 2: Run comparison CI for each candidate**

Expected: p99 changes without correctness changes.

**Step 3: Choose the split with best median p99, not a single lucky run**

Use at least two runs for the top two candidates.

**Step 4: Commit**

```bash
git add docker-compose.yml
git commit -m "perf: tune compose cpu split"
```

---

## Task 9: Add warmup without serving synthetic answer data

**Objective:** Reduce cold-cache/page-fault tail latency using only generated synthetic vectors or reference-derived data, not test payloads.

**Files:**
- Modify: `src/api/main.c`
- Modify: `src/common/search.c` if a public warmup helper is useful

**Step 1: Add startup warmup knob**

Environment variable:

```txt
INDEX_WARMUP_SEARCHES=2000
```

Default candidate: `0` locally, enabled in Docker after CI verifies.

**Step 2: Implement deterministic synthetic warmup**

Use an LCG to generate 14-dim vectors in `[0,1]`, call search, discard result.

Important: do not use official test payloads or expected answers.

**Step 3: Verify startup still passes `/ready` in CI**

Warmup must not exceed readiness timeout. Keep it bounded.

**Step 4: Commit**

```bash
git add src/api/main.c src/common/search.c docker-compose.yml
git commit -m "perf: warm search index at startup"
```

---

## Task 10: Decide promotion criteria and submission update

**Objective:** Promote only when we demonstrably beat competitors.

**Files:**
- Modify: `docker-compose.yml`
- Modify: `info.json` only if image/tag changes require it
- Update: `comparison` branch compose for our participant

**Step 1: Verify final candidate locally**

```bash
make test
make clean all CFLAGS_ARCH="<final flags>"
```

Expected: tests pass and build succeeds.

**Step 2: Verify final candidate in comparison CI**

Run all active participants:

```bash
gh workflow run benchmark.yml \
  --repo jonathanperis/rinha4-back-end-c \
  --ref comparison \
  -f compose_file=all-comparison \
  -f official_ref=main \
  -f k6_image=grafana/k6:latest \
  -f benchmark_repetitions=1
```

Expected: ours has perfect correctness and p99 below macedot and rafaelcoelhox.

**Step 3: Repeat if close**

If the gap is less than `0.03ms`, run at least two additional comparison runs. Promote only if ours wins median p99.

**Step 4: Commit and prepare PR/submission**

```bash
git status --short
git add docker-compose.yml Dockerfile src tests Makefile docs/plans/2026-05-15-beat-c-competitors.md
git commit -m "perf: beat tracked C competitors"
```

**Step 5: Final acceptance**

The final promoted state must satisfy:

- `make test`: pass.
- comparison run: ours p99 below both competitors.
- score: `6000`, no FP/FN/HTTP errors.
- no rule violations in docs/wiki/rules.md.
