# Stabilize C Candidate Before Official Promotion Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Improve the kept vectorized C candidate before any new official submission, then run repeated CI comparison rounds and report whether the new candidate is promotion-worthy.

**Current status as of 2026-05-16:** the active kept candidate image has advanced to `ghcr.io/jonathanperis/rinha4-back-end-c:ci-619f0ccb4b88ccf6860a80db8f442970f7a6084d` after docs/cleanup-only changes. Its candidate CI report (`25950973660`) is clean with p99 `0.41ms`, score `6000`, and `0 FP / 0 FN / 0 HTTP errors`. Prior kept performance evidence remains image `ci-ab157f4d7e286f8676f419c7e7815068251f4757`, report `25947443368`, p99 `0.36ms`. Subsequent config-only sweeps rejected `INDEX_NPROBE=5`, `INDEX_NPROBE=2`, CPU split `0.10/0.45/0.45`, disabled `repair0+repair5`, disabled only `repair5`, disabled only `repair0`, and narrowed repair breadth (`INDEX_REPAIR_NPROBE=16`). Next work should stop tweaking these existing env gates and move to lightweight search instrumentation or a code-level hot-path experiment.

**Architecture:** Keep the legal current architecture: non-inspecting FD-passing load balancer plus two C API workers using the compact IVF index. Focus first on p99 variance: request-loop tail latency, IVF search hot-path costs, and default search-policy tuning. Use GitHub Actions comparison rounds as the decision source because local Docker/Compose is unavailable here.

**Tech Stack:** C11, AVX2/FMA intrinsics, Make, Docker Compose, GitHub Actions, `gh`, `jq`, Python stdlib for result aggregation.

---

## Evidence Baseline

### Current official preview snapshot

Source: `https://rinhadebackend.com.br/` via `results-preview.json`, fetched 2026-05-15.

| Official rank | Submission | Official p99 | Score | Fail |
| ---: | --- | ---: | ---: | ---: |
| 1 | `fksegundo (fksegundo-rust)` | `0.83ms` | `6000` | `0%` |
| 2 | `crepao-da-massa (silent-index)` | `0.98ms` | `6000` | `0%` |
| 8 | `macedot (macedot-rinha-2026-c)` | `1.08ms` | `5967.32` | `0%` |
| 9 | `rafaelcoelhox (eu-sou-o-ze-pamonha)` | `1.08ms` | `5965.52` | `0%` |
| 21 | `jonathanperis (jonathanperis-c)` | `1.44ms` | `5843.05` | `0%` |

Current official `jonathanperis-c` is stale relative to the kept candidate; official timestamp is `2026-05-15T14:11:52Z`.

### Current kept candidate

- Repo: `/opt/data/github/jonathanperis/rinha4-back-end-c`
- Current docs/cleanup commit on `main`: `619f0cc` (`chore: cleanup stale transport docs and helpers`)
- Kept performance commit in main history: `ab157f4` (`perf: tune repair policy for current candidate`)
- Current candidate image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-619f0ccb4b88ccf6860a80db8f442970f7a6084d`
- Latest candidate benchmark: run `25950973660`, p99 `0.41ms`, final score `6000`, `0 FP / 0 FN / 0 HTTP errors`.
- Prior pre-cleanup performance image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`, report `25947443368`, p99 `0.36ms`, score `6000`.

### Recent comparison evidence

| Run | jonathanperis | macedot | rafaelcoelhox | Notes |
| ---: | ---: | ---: | ---: | --- |
| 59 | `0.52ms` | `0.35ms` | `0.44ms` | before final kept image |
| 60 | `0.38ms` | `0.41ms` | `0.34ms` | before final kept image |
| 61 | `0.39ms` | `0.36ms` | `0.35ms` | vectorized candidate image `c07ab00` |
| 62 | `0.29ms` | `0.30ms` | `0.61ms` | final kept image, us 1st |
| 63 | `0.41ms` | `0.30ms` | `0.30ms` | final kept image, us 3rd |

All listed runs had final score `6000`, `0 FP`, `0 FN`, and `0 HTTP errors` for all active participants.

### Local verification status

- Docker daemon is not reachable from this user, so local Compose benchmark is unavailable.
- Use local compile/unit guardrails plus GitHub-hosted comparison workflow for performance.
- Current API already performs immediate read/process after FD receipt in `drain_ctrl_conn()`, so do **not** duplicate that prior improvement.

## Verified Competitor Mechanisms

### macedot

- External FD-passing LB with `0.2 CPU / 30M`, APIs `0.4 CPU / 150M` each.
- C API with epoll, manual vectorizer, IVF index, AoSoA/block scanning, AVX2 centroid/list scan.
- Official p99 `1.08ms`; local comparison p99 is usually `0.30ms`–`0.41ms`.

### rafaelcoelhox

- C LB + C API, `0.10 CPU` LB and `0.45 CPU` per API.
- API uses immediate processing after FD receipt, FD flag assumptions, `EPOLLET | EPOLLRDHUP`, write batching, and warmup.
- Official p99 `1.08ms`; local comparison p99 is usually `0.30ms`–`0.44ms`, with one noisy `0.61ms` round.

## Hypotheses to Validate

1. **Request-loop variance:** Our client epoll mode and `read_conn()` behavior may leave extra epoll turns under keep-alive/pipelined traffic. We already process immediately after FD pass, but normal client events remain level-triggered and read one positive chunk per event.
2. **IVF bound/ranking cost:** `search_ivf()` ranks every list for every query. The kept AVX2 bound check only vectorizes dimensions `0..7`; dimensions `8..13` remain scalar. Fully vectorizing the 14-dim lower-bound path may reduce the baseline cost for every request.
3. **Block scan overhead:** `scan_list_block8()` stores eight distances to stack and calls `rinha_top5_worst_dist()` for every lane. Tightening this loop may reduce search tail without changing search policy.
4. **Search-policy variance:** Current defaults `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair range `1..4`, plus `0/5` thresholds are correct. Later sweeps below rejected `INDEX_NPROBE=5`, `INDEX_NPROBE=2`, disabling either/both thresholded repairs, and narrowing repair breadth to `16`; further work should instrument repair/search distribution or change the code/index layout instead of continuing blind env sweeps.
5. **Runner noise is large:** Since local CI p99 moved `0.29ms -> 0.41ms` with the same image, do not promote based on one run. Use repeated rounds and medians.

## Hard Gates

Every code/config candidate must satisfy:

1. `make clean test test-assume-passed-fd-flags` passes.
2. AVX2 test build passes:
   ```bash
   make clean test CFLAGS_TEST="-std=c11 -O2 -g -march=haswell -mavx2 -mfma -Wall -Wextra -Wshadow -Werror -I src"
   ```
3. Production build passes:
   ```bash
   make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math"
   ```
4. No Rinha resource-limit regression: total `1 CPU / 350 MB`.
5. LB remains non-inspecting; no payload parsing or fraud response in LB.
6. No official test payloads, answer tables, correction tables, or payload-derived reference data.
7. CI comparison rounds must preserve `final_score=6000`, `0 FP`, `0 FN`, `0 HTTP errors`.

### Promotion decision gates

After improvements, run **at least 3 fresh comparison rounds** using the exact immutable candidate image.

Promote only if:

- all 3 rounds are clean (`6000`, `0/0/0` errors), and
- our median p99 is no worse than both tracked C competitors' median p99 over the same rounds, or
- our median p99 is within `0.03ms` of the best tracked competitor **and** the candidate includes a clear correctness-safe improvement over the stale official submission.

Do not promote if:

- any round has FP/FN/HTTP errors,
- our p99 is worse than both competitors in 2 of 3 rounds by more than `0.05ms`, or
- the improvement is only visible in a single noisy round.

---

## Task 1: Add a comparison-result aggregation helper

**Objective:** Make round-to-round decisions from medians/min/latest instead of eyeballing single noisy runs.

**Files:**
- Create: `tools/summarize_comparison.py`
- Modify: none initially

**Step 1: Create the script**

Create `tools/summarize_comparison.py` that accepts result JSON paths and prints per-participant count, min, median, max, latest p99, and whether all runs are clean.

Expected behavior:

```bash
python3 tools/summarize_comparison.py \
  <(git show origin/comparison:comparison-results/run-62.json) \
  <(git show origin/comparison:comparison-results/run-63.json)
```

Expected output shape:

```text
participant        n  min_ms  median_ms  max_ms  latest_ms  clean
jonathanperis      2  0.29    0.35       0.41    0.41       yes
macedot            2  0.30    0.30       0.30    0.30       yes
rafaelcoelhox      2  0.30    0.455      0.61    0.30       yes
```

**Step 2: Verify against existing comparison data**

Run:

```bash
git fetch origin comparison
python3 tools/summarize_comparison.py \
  <(git show origin/comparison:comparison-results/run-62.json) \
  <(git show origin/comparison:comparison-results/run-63.json)
```

Expected: values match the recent evidence table.

**Step 3: Commit**

```bash
git add tools/summarize_comparison.py
git commit -m "tools: summarize comparison benchmark rounds"
```

---

## Task 2: Add a focused API event-loop regression test

**Objective:** Create a guardrail before touching `src/api/main.c` so request-loop changes cannot break pipelined or partial reads.

**Files:**
- Modify or create tests near: `tests/test_api_fdpass_immediate.c`
- Modify: `Makefile` if a new test binary is needed

**Step 1: Inspect existing immediate-FD test**

Run:

```bash
sed -n '1,220p' tests/test_api_fdpass_immediate.c
```

Expected: existing test covers immediate processing and FD flag behavior.

**Step 2: Add a pipelined-request case**

Add a test that sends two complete HTTP requests on a socketpair/client FD before or immediately after FD handoff and verifies two valid responses are produced without waiting for a second epoll cycle in the test harness.

Requirements:

- Do not use official fraud payload fixtures.
- Use `/ready` requests or a tiny synthetic `/fraud-score` body if index lookup can be stubbed.
- Verify both responses are present and ordered.

**Step 3: Run the focused test**

Run:

```bash
make clean test-api-fdpass-immediate
make clean test-assume-passed-fd-flags
```

Expected: both pass.

**Step 4: Commit**

```bash
git add tests/test_api_fdpass_immediate.c Makefile
git commit -m "test: cover pipelined FD-passed API reads"
```

---

## Task 3: Experiment with edge-triggered client reads behind a small code diff

**Objective:** Reduce event-loop p99 variance by draining readable client sockets until `EAGAIN` and using `EPOLLRDHUP`/optional `EPOLLET` for clients.

**Files:**
- Modify: `src/api/main.c`
- Test: `tests/test_api_fdpass_immediate.c`

**Step 1: Refactor `read_conn()` to drain positive reads**

Current `read_conn()` returns after the first positive `read()`. Change it so it loops while space remains, processing buffered complete requests after each read, and returns `1` only on `EAGAIN/EWOULDBLOCK` with a healthy connection.

Safety constraints:

- If `process_buffer()` returns `0`, close as before.
- If buffer fills, close as before.
- Preserve current `CONNECTION_CLOSE` behavior.

**Step 2: Use client event flags that expose hangups**

Change client registrations from plain `EPOLLIN` to at least:

```c
ev.events = EPOLLIN | EPOLLRDHUP;
```

Then test an optional variant with:

```c
ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
```

Only keep `EPOLLET` if tests and CI improve; otherwise keep only `EPOLLRDHUP` or revert the event-flag part.

**Step 3: Run local guardrails**

Run:

```bash
make clean test test-assume-passed-fd-flags
make clean test CFLAGS_TEST="-std=c11 -O2 -g -march=haswell -mavx2 -mfma -Wall -Wextra -Wshadow -Werror -I src"
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math"
```

Expected: all pass.

**Step 4: Commit only if retained**

```bash
git add src/api/main.c tests/test_api_fdpass_immediate.c Makefile
git commit -m "perf: drain API client reads"
```

---

## Task 4: Fully vectorize the 14-dim IVF list lower-bound path

**Objective:** Reduce per-request list ranking cost in `search_ivf()` without changing search policy.

**Files:**
- Modify: `src/common/search.c`
- Test: existing search/unit tests via `make test`

**Step 1: Replace the mixed AVX2/scalar lower-bound implementation**

In `list_lower_bound()`, keep the first 8-dim AVX2 block and add a second vectorized block for dims `8..13` without unsafe overreads.

Implementation guidance:

- Use safe local arrays or scalar loads for the 6 remaining int16 lanes before building a vector register.
- Do not read beyond `query[13]`, `mins[13]`, or `maxs[13]`.
- Preserve the scalar fallback under `#else`.

**Step 2: Verify correctness with both scalar and AVX2 builds**

Run:

```bash
make clean test
make clean test CFLAGS_TEST="-std=c11 -O2 -g -march=haswell -mavx2 -mfma -Wall -Wextra -Wshadow -Werror -I src"
```

Expected: both pass.

**Step 3: Commit only if the diff is simple and correct**

```bash
git add src/common/search.c
git commit -m "perf: complete AVX2 IVF bound vectorization"
```

---

## Task 5: Tighten block8 scan top-k update overhead

**Objective:** Reduce search scan overhead by avoiding unnecessary worst-distance reloads and stack work in `scan_list_block8()`.

**Files:**
- Modify: `src/common/search.c`

**Step 1: Cache top-k worst distance per block**

Current code does:

```c
if (dist[lane] < rinha_top5_worst_dist(top)) rinha_top5_add(...);
```

Change the AVX2 lane loop to cache `worst` once per block and refresh only after an insertion:

```c
uint64_t worst = rinha_top5_worst_dist(top);
for (uint32_t lane = 0; lane < valid; ++lane) {
    if (dist[lane] < worst) {
        rinha_top5_add(top, dist[lane], index->labels[slot + lane]);
        worst = rinha_top5_worst_dist(top);
    }
}
```

**Step 2: Consider but do not force a bigger rewrite**

Only attempt removing the `dist[8]` stack store if the first small change is clean. A larger SIMD/top-k rewrite is higher risk and should be a separate experiment.

**Step 3: Run guardrails**

```bash
make clean test test-assume-passed-fd-flags
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math"
```

Expected: pass.

**Step 4: Commit**

```bash
git add src/common/search.c
git commit -m "perf: reduce block8 top-k scan overhead"
```

---

## Task 6: Run main CI benchmark and build immutable image

**Objective:** Produce an immutable image for the improved candidate before comparison rounds.

**Files:**
- No source changes unless workflow-generated reports are committed

**Step 1: Push main**

```bash
git push origin main
```

Expected: GitHub Actions starts benchmark/build workflows for `main`.

**Step 2: Watch relevant runs**

```bash
gh run list --branch main --limit 8 --json databaseId,workflowName,status,conclusion,headSha,url
```

Then watch the new benchmark and build/release runs:

```bash
gh run watch <run_id> --exit-status
```

Expected: both succeed.

**Step 3: Read latest report**

```bash
git pull --ff-only origin main
jq -r '[.metadata.run_id,.metadata.sha[0:8],.metadata.image,.result.p99,.result.scoring.final_score,.result.scoring.breakdown.false_positive_detections,.result.scoring.breakdown.false_negative_detections,.result.scoring.breakdown.http_errors] | @tsv' docs/public/reports/latest.json
```

Expected: `final_score=6000` and `0/0/0` errors. Record the immutable image tag.

---

## Task 7: Run 3 fresh comparison rounds with the improved image

**Objective:** Decide from repeated CI data whether the improved candidate beats the current kept candidate and tracked competitors.

**Files:**
- Modify on branch `comparison`: `competitor-compose/jonathanperis/docker-compose.yml`
- Generated by workflow: `comparison-results/run-*.json`, `comparison-results/latest.json`

**Step 1: Update comparison branch image**

```bash
git fetch origin comparison main
git checkout comparison
git pull --ff-only origin comparison
```

Replace both `rinha4-back-end-c:ci-...` references in `competitor-compose/jonathanperis/docker-compose.yml` with the new immutable image from Task 6.

Verify before commit:

```bash
grep -n 'rinha4-back-end-c:ci-' competitor-compose/jonathanperis/docker-compose.yml
```

Commit/push:

```bash
git add competitor-compose/jonathanperis/docker-compose.yml
git commit -m "bench: compare improved C candidate"
git push origin comparison
```

**Step 2: Run two additional explicit workflow_dispatch rounds**

The push triggers round 1. After it finishes, run two more:

```bash
gh workflow run benchmark.yml --ref comparison \
  -f compose_file=all-comparison \
  -f official_ref=main \
  -f k6_image=grafana/k6:latest \
  -f webapi_image=<NEW_IMMUTABLE_IMAGE> \
  -f benchmark_repetitions=1
```

Repeat once more for the third round.

**Step 3: Watch each run**

```bash
gh run watch <run_id> --exit-status
```

Expected: all benchmark jobs and summarize job succeed.

**Step 4: Aggregate the 3 new rounds**

```bash
git fetch origin comparison
python3 tools/summarize_comparison.py \
  <(git show origin/comparison:comparison-results/run-<N1>.json) \
  <(git show origin/comparison:comparison-results/run-<N2>.json) \
  <(git show origin/comparison:comparison-results/run-<N3>.json)
```

Expected: table with median/min/max for `jonathanperis`, `macedot`, and `rafaelcoelhox`.

---

## Task 8: Report promotion decision

**Objective:** Give Jonathan a concise decision report before any official submission.

**Files:**
- Optionally modify: `docs/plans/2026-05-15-stabilize-c-candidate-before-promotion.md` with final evidence

Report format:

```markdown
## Candidate decision report

Image: `<NEW_IMMUTABLE_IMAGE>`
Main commit: `<SHA>`
Comparison rounds: `<run ids>`

| participant | n | min | median | max | latest | clean |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| jonathanperis | 3 | ... | ... | ... | ... | yes/no |
| macedot | 3 | ... | ... | ... | ... | yes/no |
| rafaelcoelhox | 3 | ... | ... | ... | ... | yes/no |

Decision: `promote` / `do not promote yet`
Reason: ...
Next action: ...
```

If decision is `promote`, stop and ask for confirmation before making any official submission. If decision is `do not promote yet`, list the next highest-leverage experiment instead of submitting.

---

## Candidate decision report — 2026-05-15

Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-f755c2f58f7ec67fab4fe345dd3767256cefd06d`
Main performance commit: `f755c2f` (`perf: reduce block8 top-k scan overhead`)
Comparison branch image commit: `251e34f` (`bench: compare improved C candidate`)
Comparison workflow runs: `25945899786` / `25946070238` / `25946242703`
Comparison result files: `run-64.json` / `run-65.json` / `run-66.json`

| participant | n | min | median | max | latest | clean |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` | 3 | `0.45ms` | `0.47ms` | `0.59ms` | `0.59ms` | yes |
| `macedot` | 3 | `0.29ms` | `0.32ms` | `0.38ms` | `0.38ms` | yes |
| `rafaelcoelhox` | 3 | `0.32ms` | `0.37ms` | `0.38ms` | `0.37ms` | yes |

Raw p99 by round:

| comparison result | jonathanperis | macedot | rafaelcoelhox |
| --- | ---: | ---: | ---: |
| `run-64` | `0.47ms` | `0.29ms` | `0.38ms` |
| `run-65` | `0.45ms` | `0.32ms` | `0.32ms` |
| `run-66` | `0.59ms` | `0.38ms` | `0.37ms` |

Decision: `do not promote yet`.

Reason: all three rounds were clean, but this candidate lost to both tracked C competitors in all three rounds and missed the promotion median gate (`0.47ms` vs `0.32ms`/`0.37ms`). It also regressed versus the previously kept image's comparison range (`0.29ms`/`0.41ms` in runs 62/63), so the safe action is not to submit it officially.

Next highest-leverage experiment: revert or isolate the request-loop/read-drain change first, because the search hot-path changes are small and correctness-safe while the event-loop change may have increased p99 variance. Re-run the same three-round comparison gate on the isolated candidate before considering official promotion.

---

## Isolation log — 2026-05-16

### 1. Revert API read-drain / `EPOLLRDHUP` experiment

Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-698a2e1eeaa644cd4f232755bdc9039b3441f6ff`
Main commit: `698a2e1` (`perf: revert API read draining experiment`)
Comparison branch commit: `e0f411e` (`bench: compare API read-drain revert`)
Comparison workflow runs: `25946955908` / `25946964588` / `25946966218`
Comparison result files: `run-67.json` / `run-68.json` / `run-69.json`

| participant | n | min | median | max | latest | clean |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` | 3 | `0.36ms` | `0.45ms` | `0.46ms` | `0.46ms` | yes |
| `macedot` | 3 | `0.31ms` | `0.34ms` | `0.35ms` | `0.35ms` | yes |
| `rafaelcoelhox` | 3 | `0.34ms` | `0.36ms` | `0.37ms` | `0.34ms` | yes |

Raw p99 by round:

| comparison result | jonathanperis | macedot | rafaelcoelhox |
| --- | ---: | ---: | ---: |
| `run-67` | `0.36ms` | `0.34ms` | `0.36ms` |
| `run-68` | `0.45ms` | `0.31ms` | `0.37ms` |
| `run-69` | `0.46ms` | `0.35ms` | `0.34ms` |

Decision: `keep as baseline, do not promote yet`.

Reason: removing the request-loop change improved the candidate versus the prior `0.47ms` median, but it still lost the median gate (`0.45ms` vs `0.34ms`/`0.36ms`).

### 2. Revert block8 top-k cache experiment

Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-27604dfcb21faf3d89557ec808d9304850d40242`
Main commit: `27604df` (`perf: revert block8 top-k cache experiment`)
Comparison branch commit: `90d46d0` (`bench: compare block8-cache revert`)
Comparison workflow runs: `25947269542` / `25947274049` / `25947275973`
Comparison result files: `run-70.json` / `run-71.json` / `run-72.json`

| participant | n | min | median | max | latest | clean |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` | 3 | `0.44ms` | `0.47ms` | `0.48ms` | `0.44ms` | yes |
| `macedot` | 3 | `0.31ms` | `0.36ms` | `0.37ms` | `0.31ms` | yes |
| `rafaelcoelhox` | 3 | `0.31ms` | `0.33ms` | `0.35ms` | `0.31ms` | yes |

Raw p99 by round:

| comparison result | jonathanperis | macedot | rafaelcoelhox |
| --- | ---: | ---: | ---: |
| `run-70` | `0.48ms` | `0.36ms` | `0.33ms` |
| `run-71` | `0.47ms` | `0.37ms` | `0.35ms` |
| `run-72` | `0.44ms` | `0.31ms` | `0.31ms` |

Decision: `reject revert`.

Reason: reverting the block8 top-k cache did not improve p99 versus the read-drain revert baseline, so `14e6e36` restored the block8 cache.

### 3. Revert tail AVX2 bound experiment while keeping block8 cache

Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`
Main commits: `14e6e36` (restore block8 cache), `ab157f4` (`perf: revert tail AVX2 bound experiment`)
Comparison branch commit: `bf24c7b` (`bench: compare tail-AVX2 revert`)
Comparison workflow runs: `25947671210` / `25947676868` / `25947678837`
Comparison result files: `run-73.json` / `run-74.json` / `run-75.json`

| participant | n | min | median | max | latest | clean |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` | 3 | `0.36ms` | `0.38ms` | `0.48ms` | `0.36ms` | yes |
| `macedot` | 3 | `0.30ms` | `0.32ms` | `0.38ms` | `0.32ms` | yes |
| `rafaelcoelhox` | 3 | `0.30ms` | `0.31ms` | `0.35ms` | `0.35ms` | yes |

Raw p99 by round:

| comparison result | jonathanperis | macedot | rafaelcoelhox |
| --- | ---: | ---: | ---: |
| `run-73` | `0.38ms` | `0.38ms` | `0.31ms` |
| `run-74` | `0.48ms` | `0.30ms` | `0.30ms` |
| `run-75` | `0.36ms` | `0.32ms` | `0.35ms` |

Decision: `keep as current baseline, do not promote yet`.

Reason: the tail-AVX2 revert is the best tested isolation candidate so far and restored the previous lower range, but it still misses the promotion gate on median (`0.38ms` vs `0.32ms`/`0.31ms`).

Next highest-leverage experiment: keep current baseline (`ab157f4` + archived report `fa29d7f`) and investigate CPU/memory split or search-policy knobs (`INDEX_NPROBE`, repair/exact thresholds) rather than more micro-optimizing the small lower-bound path. Any candidate should still go through the same three-round comparison gate before official promotion.

---

## Cross-repo analysis and search-policy sweep — 2026-05-16

Full cross-repo notes and approach backlog: [`docs/analysis/2026-05-16-cross-repo-approaches.md`](../analysis/2026-05-16-cross-repo-approaches.md).

Additional source repos inspected:

- `macedot/rinha-2026-c` at `/opt/data/github/jonathanperis/competitors-rinha4-c/rinha-2026-c`.
- `rafaelcoelhox/eu-sou-o-ze-pamonha` at `/opt/data/github/jonathanperis/competitors-rinha4-c/eu-sou-o-ze-pamonha`.

Experiment tested: Rafael-like `INDEX_NPROBE=5` using the current immutable Jonathan image `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`.

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-76` / `25948228355` | push, all three participants | `0.42ms` | macedot `0.32ms`, rafaelcoelhox `0.31ms` | yes |
| `run-77` / `25948232812` | dispatch, Jonathan only; benchmark succeeded but summary push conflicted | `0.40ms` | n/a | yes |
| `run-78` / `25948234623` | dispatch, Jonathan only | `0.42ms` | n/a | yes |

Decision: `reject INDEX_NPROBE=5`.

Reason: correctness stayed clean, but median p99 was `0.42ms`, worse than the current baseline's `0.38ms` comparison median. The comparison branch was restored to the baseline config in `479ac20` (`bench: restore Jonathan comparison baseline [skip ci]`).

Next candidate was tested below: lower/narrower `INDEX_NPROBE=2` with repair `24`.

## Search-policy sweep: `INDEX_NPROBE=2` — 2026-05-16

Experiment tested: lower/narrower first pass using the current immutable Jonathan image `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`, `INDEX_NPROBE=2`, `INDEX_REPAIR_NPROBE=24`, current repair thresholds, `INDEX_EXACT_FALLBACK=0`.

Comparison branch commits:

- Experiment config: `e514554` (`bench: compare Jonathan nprobe 2`).
- Restored baseline config after rejection: `dd89678` (`bench: restore Jonathan comparison baseline after nprobe 2`). The automatic restore run `25948925764` was cancelled because it was only a config reset.

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-79` / `25948636969` | push, all three participants | `0.39ms` | macedot `0.32ms`, rafaelcoelhox `0.36ms` | yes |
| `run-80` / `25948782192` | dispatch, all three participants | `0.38ms` | macedot `0.31ms`, rafaelcoelhox `0.34ms` | yes |
| `run-81` / `25948782718` | dispatch, all three participants; benchmark artifacts valid, summary push conflicted | `0.49ms` | macedot `0.30ms`, rafaelcoelhox `0.33ms` | yes |

Decision: `reject INDEX_NPROBE=2`.

Reason: correctness stayed clean, but the three Jonathan p99s were `0.39ms`, `0.38ms`, and `0.49ms` (median `0.39ms`, mean `0.42ms`), which does not beat the current baseline comparison median (`0.38ms`, runs `73`/`74`/`75`) and keeps Jonathan behind the tracked C competitors.

Next candidate was tested below: CPU split `0.10 / 0.45 / 0.45` with baseline search policy.

## CPU split sweep: `0.10 / 0.45 / 0.45` — 2026-05-16

Experiment tested: give the FD-passing LB a slightly larger CPU slice and reduce each API from `0.455` to `0.45`, keeping the current immutable Jonathan image `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757` and baseline search policy (`INDEX_NPROBE=3`, repair `24`, current thresholds).

Comparison branch commits:

- Experiment config: `b5c4646` (`bench: compare Jonathan cpu split 0.10 0.45 0.45`).
- Restored baseline config after rejection: `973ee9a` (`bench: restore baseline cpu split after test [skip ci]`).

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-83` / `25949133164` | push, all three participants | `0.35ms` | macedot `0.32ms`, rafaelcoelhox `0.41ms` | yes |
| `run-84` / `25949255398` | dispatch, all three participants | `0.44ms` | macedot `0.31ms`, rafaelcoelhox `0.33ms` | yes |
| `run-85` / `25949255793` | dispatch, all three participants | `0.40ms` | macedot `0.36ms`, rafaelcoelhox `0.31ms` | yes |

Decision: `reject CPU split 0.10 / 0.45 / 0.45`.

Reason: one run was promising (`0.35ms`), but the repeated gate regressed to median `0.40ms` and mean `0.397ms`, worse than the current baseline median `0.38ms`. Correctness was clean, but the split did not improve the candidate over noise.

Next candidate was tested below: disable both thresholded repair paths (`repair0=0`, `repair5=0`) at baseline CPU/search config.

## Repair-threshold sweep: disabled `repair0` and `repair5` — 2026-05-16

Experiment tested: keep baseline `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair fraud range `1..4`, and set both thresholded repair paths to `0` (`INDEX_REPAIR0_WORST_THRESHOLD=0`, `INDEX_REPAIR5_WORST_THRESHOLD=0`). The image stayed immutable at `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`.

Comparison branch commits:

- Experiment config: `3aa43ba` (`bench: compare Jonathan repair thresholds disabled`).
- Restored baseline config after rejection: `aba9a7d` (`bench: restore repair threshold baseline [skip ci]`).

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-86` / `25949850100` | push, all three participants | `0.39ms` | macedot `0.30ms`, rafaelcoelhox `0.35ms` | no: 7 FP, 8 FN |

Decision: `reject thresholds disabled`.

Reason: disabling both thresholded repair paths broke correctness (`7` false positives and `8` false negatives), so there is no reason to spend more noisy p99 rounds on this variant. The comparison branch was restored to the known baseline config.

Next candidate was tested below: disable only `repair5` while keeping `repair0=4021242`.

## Repair-threshold sweep: disabled `repair5` only — 2026-05-16

Experiment tested: keep baseline `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair fraud range `1..4`, keep `INDEX_REPAIR0_WORST_THRESHOLD=4021242`, and set only `INDEX_REPAIR5_WORST_THRESHOLD=0`. The image stayed immutable at `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`.

Comparison branch commits:

- Experiment config: `aebc690` (`bench: compare Jonathan repair5 threshold disabled`).
- Restored baseline config after rejection: `3b48090` (`bench: restore repair5 threshold baseline [skip ci]`).

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-87` / `25950330221` | push, all three participants | `0.39ms` | macedot `0.32ms`, rafaelcoelhox `0.39ms` | no: 7 FP, 0 FN |

Decision: `reject repair5 disabled`.

Reason: disabling only `repair5` still broke correctness (`7` false positives), so it was rejected after one run. The comparison branch was restored to the known baseline config.

Next candidate tested below: a Ronie-inspired wider fast pass with narrower repair breadth (`INDEX_NPROBE=5`, `INDEX_REPAIR_NPROBE=20`).

## Search-policy sweep: Ronie-style `INDEX_NPROBE=5`, repair `20` — 2026-05-16

Local replay motivation: Ronie uses a `fast=5` / `full=20` shape. On Jonathan's current projection/block8 index, offline replay over the first `250000` reference rows showed this shape reduced elapsed replay time and final worst-distance average, but it also changed fraud-count buckets versus the known baseline, so it needed an official-like correctness gate instead of being trusted from replay alone.

Offline replay summary with current thresholds (`repair0=4021242`, `repair5=3748534`, repair fraud range `1..4`, exact fallback disabled):

| variant | elapsed | fast lists | repair vectors | final fraud counts |
| --- | ---: | ---: | ---: | --- |
| baseline `nprobe=3`, repair `24` | `15.499s` | `750000` | `156177565` | `162649,1290,2770,2730,1334,79227` |
| Ronie-style `nprobe=5`, repair `20` | `14.016s` | `1250000` | `105271594` | `162643,1291,2774,2731,1334,79227` |
| `nprobe=5`, repair `24` | `14.553s` | `1250000` | `133344986` | `162644,1291,2773,2730,1334,79228` |
| `nprobe=3`, repair `20` | `15.860s` | `750000` | `126428305` | `162648,1290,2771,2731,1334,79226` |
| Ronie fraud-band-only repair `2..4` | `15.528s` | `1250000` | `74894138` | `162634,1733,2438,2621,1284,79290` |

Comparison branch commits:

- Experiment config: `877b15d` (`experiment: test ronnie-style n5 r20 [skip ci]`).
- Restored baseline config after rejection: `8790d87` (`bench: restore Jonathan search baseline [skip ci]`).

| run | event / scope | image | knobs | p99 | clean |
| --- | --- | --- | --- | ---: | --- |
| `run-102` / `25969611462` | manual, Jonathan only | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-9908938ece617bf765c98d6a6a3fbf299bab2b02` | `INDEX_NPROBE=5`, `INDEX_REPAIR_NPROBE=20`, current thresholds | `0.39ms` | no: 2 FP, 0 FN, 0 HTTP errors |

Decision: `reject Ronie-style nprobe=5 repair=20`.

Reason: even though the replay counters were attractive, the official-like benchmark caught `2` false positives. Correctness failures are decisive, so the comparison branch was restored to baseline. Further work should move toward index-layout/search diagnostics rather than blind probe/repair-width sweeps.
