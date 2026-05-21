# Rinha4 C cross-repo approaches — 2026-05-16

## Scope

Repos/images compared:

| participant | source inspected | runtime image / compose reference | notes |
| --- | --- | --- | --- |
| `jonathanperis` | `/opt/data/github/jonathanperis/rinha4-back-end-c` | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757` | current best isolated C baseline |
| `macedot` | `/opt/data/github/jonathanperis/competitors-rinha4-c/rinha-2026-c` | `ghcr.io/macedot/rinha-2026-c:0.0.6` | source main + submission compose inspected; search bridge submodule not checked out locally |
| `rafaelcoelhox` | `/opt/data/github/jonathanperis/competitors-rinha4-c/eu-sou-o-ze-pamonha` | `ghcr.io/rafaelcoelhox/eu-sou-o-ze-pamonha:rc1` in comparison; submission uses `rc1-v7` | source main + submission compose inspected |

Local Docker/Compose is unavailable in this environment, so performance evidence below comes from GitHub Actions comparison artifacts and source/config inspection.

## Current benchmark baseline

Current Jonathan candidate after isolation:

- main performance commit: `ab157f4` (`perf: revert tail AVX2 bound experiment`)
- report archive commit: `fa29d7f`
- image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`
- latest main archived official-like report: `0.36ms`, score `6000`, `0` FP, `0` FN, `0` HTTP errors

Three-round comparison for this baseline (`run-73` / `run-74` / `run-75`):

| participant | n | min | median | max | clean |
| --- | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` | 3 | `0.36ms` | `0.38ms` | `0.48ms` | yes |
| `macedot` | 3 | `0.30ms` | `0.32ms` | `0.38ms` | yes |
| `rafaelcoelhox` | 3 | `0.30ms` | `0.31ms` | `0.35ms` | yes |

Decision at this point: keep Jonathan's baseline, but do not promote yet; the median still trails both tracked C competitors.

## Architecture comparison

### Runtime topology and transport

| participant | LB / transport | API model | immediate FD processing | notable request-path details |
| --- | --- | --- | --- | --- |
| `jonathanperis` | external/in-house LB image `rinha4-lb-yolo-mode`, `LB_MODE=fdpass`, accepted TCP FDs delivered over Unix sockets with `SCM_RIGHTS` | two single-threaded C API processes, epoll, manual HTTP/JSON | yes: newly passed client FDs are registered and read immediately | static responses, manual parser, `MAX_CONNS=512`, fd-pass path already avoids byte proxying |
| `macedot` | external `jrblatt/so-no-forevis:v1.0.0`, FD passing to API `.ctrl` sockets | two C API processes, epoll, custom HTTP/vectorizer | source indicates FD passing; exact LB internals external | `MAX_EVENTS=128`, static connection pool, single `send()` response path, env-tunable IVF |
| `rafaelcoelhox` | in-image C LB `carro-da-pamonha`, `accept4` + `SCM_RIGHTS` over `SOCK_SEQPACKET`, closes local FD after handoff | two C API binaries (`canjica`/`pamonha`), epoll, control sockets only | yes | `MAX_CONNS=512`, `writev` batching up to 16 iovecs, owns LB and API source in same repo |

Takeaways:

- Jonathan already has the important transport shape: fd-passing LB plus immediate API processing.
- The remaining gap is unlikely to be fixed by changing from proxy to fd-pass; that work is already done.
- Rafael's in-image LB is worth mining for small ideas (`SOCK_SEQPACKET`, CFS quota style, simple two-upstream toggle), but Jonathan's current LB CPU usage is low enough that search policy/index work is higher leverage.

### CPU and memory split

| participant | LB CPU / memory | API CPU / memory each | total CPU | total declared memory | current signal |
| --- | ---: | ---: | ---: | ---: | --- |
| `jonathanperis` main | `0.08 / 30M` | `0.46 / 160M` | `1.00` | `350M` | main compose baseline |
| `jonathanperis` comparison baseline | `0.09 / 30M` | `0.455 / 160M` | `1.00` | `350M` | used in comparison branch baseline |
| `macedot` | `0.20 / 30M` | `0.40 / 150M` | `1.00` | `330M` | faster in recent comparison medians |
| `rafaelcoelhox` comparison | `0.20 / 30M` | `0.40 / 160M` | `1.00` | `350M` | faster in recent comparison medians |
| `rafaelcoelhox` submission branch | `0.10 / 30M` | `0.45 / 160M` | `1.00` | `350M` | uses explicit CFS quota fields |

Takeaways:

- Competitor comparison compose currently gives LB `0.20` CPU, but Rafael's submission branch uses a Jonathan-like `0.10 / 0.45 / 0.45` split.
- Since Jonathan's LB is fd-pass and search dominates API CPU, shifting much more CPU to LB is not obviously beneficial.
- CPU split remains a valid cheap sweep, but should be measured as a compose-only experiment before committing.

### Search/index design

| participant | index layout | probes / repair policy | runtime knobs | warmup / memory |
| --- | --- | --- | --- | --- |
| `jonathanperis` | in-repo IVF block8, 4096 lists, int16 vectors, 14 dims, bounds, mmaped `index.bin` | `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair when fraud count `1..4` or thresholded fraud count `0/5`, exact fallback off | many: `INDEX_NPROBE`, repair nprobe/min/max, exact fallback, thresholds | `mmap`, `posix_fadvise`, `madvise`, page-touch warmup |
| `macedot` | IVF/AVX2 via `bridge` submodule, README says 4096 clusters and AoSoA scan | `IVF_NPROBE=8`, `IVF_FULL_NPROBE=24`, `CANDIDATES=0` | env knobs for nprobe/full/candidates | 500 random search warmup |
| `rafaelcoelhox` | in-tree IVF, 4096 clusters, transposed centroids, block16 SoA, int16, AVX2/FMA | hardcoded `FAST_NPROBE=5`, `FULL_NPROBE=24`, full scan only for ambiguous fraud count `1..4` | mostly compile-time constants | copies index to anonymous private memory, `MAP_POPULATE`, `MADV_HUGEPAGE`, `mlock`, 2000 warmup queries |

Takeaways:

- Jonathan has the most runtime tuning surface, which is useful for CI sweeps without rebuilding images.
- Rafael's block16/transposed-centroid design is the clearest structural search/index difference. Jonathan currently uses block8 and partially scalar lower-bound tails.
- Macedot and Rafael both use a `24` full/repair probe ceiling; Jonathan already matches that.
- Probe policy alone is not automatically better: Rafael-like `nprobe=5` was tested and rejected below.

## Experiment run this session: Rafael-like `INDEX_NPROBE=5`

Hypothesis: increasing Jonathan's fast probe count from `3` to Rafael's `5`, while keeping repair `24` and clean thresholds, might reduce repair variance enough to improve p99 stability.

Config tested on `comparison` branch commit `361dbdb`:

- image fixed: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-ab157f4d7e286f8676f419c7e7815068251f4757`
- `INDEX_NPROBE: "5"`
- `INDEX_REPAIR_NPROBE: "24"`
- `INDEX_REPAIR_MIN_FRAUD: "1"`
- `INDEX_REPAIR_MAX_FRAUD: "4"`
- existing thresholds preserved
- CPU split aligned to main: LB `0.08`, APIs `0.46` each

Results:

| run | event / scope | jonathanperis p99 | comparison p99s | clean |
| --- | --- | ---: | --- | --- |
| `run-76` / `25948228355` | push, all three participants | `0.42ms` | macedot `0.32ms`, rafaelcoelhox `0.31ms` | yes |
| `run-77` / `25948232812` | dispatch, Jonathan only; summary push conflicted but artifact downloaded | `0.40ms` | n/a | yes |
| `run-78` / `25948234623` | dispatch, Jonathan only | `0.42ms` | n/a | yes |

Decision: reject `INDEX_NPROBE=5` for now.

Reason: all runs were correct, but the median was `0.42ms`, worse than the prior tested baseline median (`0.38ms`) and not competitive with the same push-run competitors (`0.32ms` / `0.31ms`). The comparison branch was restored to the baseline config in commit `479ac20` (`bench: restore Jonathan comparison baseline [skip ci]`).

## Approach backlog

> Status note: this backlog is append-only. Items marked rejected below have already been tested; the active next step is at the bottom of this document.

Ranked by expected leverage and risk.

### A. Low-risk compose/env sweeps using the current image

These do not require rebuilding the image and should be evaluated on the comparison branch with immutable image `ci-ab157f4...`.

1. **CPU split sweep — partially rejected**
   - Current comparison baseline: `0.09 / 0.455 / 0.455`; main split: `0.08 / 0.46 / 0.46`.
   - Rafael-like `0.10 / 0.45 / 0.45` was tested in runs `83/84/85`: clean but median Jonathan p99 was `0.40ms`, worse than baseline.
   - Untested variants such as `0.12 / 0.44 / 0.44` or `0.20 / 0.40 / 0.40` should only run after higher-leverage search instrumentation or if transport evidence changes.

2. **Probe-count lower sweep — rejected for current cheap variants**
   - `INDEX_NPROBE=2`, repair `24`, current thresholds was tested in runs `79/80/81`: clean but median Jonathan p99 was `0.39ms`, not better than baseline.
   - `INDEX_REPAIR_NPROBE=16` with `INDEX_NPROBE=3` and current thresholds failed correctness in run `89` (`6 FP / 12 FN`).
   - Conclusion: the current policy needs the full repair breadth of `24`; do not narrow repair without changing thresholds/index layout.
   - Correctness risk: medium; lower probes may introduce FP/FN if repair thresholds do not catch boundary cases.

3. **Repair threshold sweep — partially rejected**
   - Keep `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`.
   - Thresholds disabled (`repair0=0`, `repair5=0`) failed correctness in run `86` (`7 FP / 8 FN`).
   - Only `repair0` enabled (`repair5=0`) failed correctness in run `87` (`7 FP / 0 FN`).
   - Only `repair5` enabled (`repair0=0`) failed correctness in run `88` (`0 FP / 8 FN`).
   - Conclusion: both thresholded repair gates are required for the current index/policy; do not spend more p99 rounds on single-threshold removal.
   - Correctness risk: medium-high; must be gated by full benchmark correctness.

4. **Connection behavior A/B**
   - Test `CONNECTION_CLOSE=1` versus current keep-alive.
   - Expected to be neutral or worse, but cheap to test if p99 variance continues.

### B. Low/medium-risk image rebuild experiments

These require a code or Dockerfile/Makefile change, image build, and normal CI benchmark.

1. **Production `RINHA_ASSUME_PASSED_FD_FLAGS`**
   - Rafael compiles API with `-DRINHA_ASSUME_PASSED_FD_FLAGS`.
   - Jonathan already has tests for this path (`test-assume-passed-fd-flags`).
   - Hypothesis: avoid redundant fd flag setup on passed client sockets if LB guarantees nonblocking/CLOEXEC.
   - Risk: medium; only safe if the LB contract is verified.

2. **Response/write path comparison with Rafael**
   - Rafael uses `writev` batching for multiple responses; Jonathan uses prebuilt responses and direct write path.
   - Instrument or micro-benchmark before changing; pipelining may not matter under k6's workload.

3. **LB socket/control details**
   - Compare Jonathan LB against Rafael's `SOCK_SEQPACKET` control channel and exact two-upstream toggle path.
   - Risk: medium; LB changes can hurt correctness/FD handling and must be isolated.

### C. Higher-leverage search/index structural work

These are likely where the remaining gap lives.

1. **Block16 AoSoA index layout**
   - Rafael uses `BLOCK_VECS=16`; Jonathan uses block8.
   - Potential benefit: scan more candidates per loop and better SIMD amortization.
   - Cost/risk: high; requires build-index/runtime format changes and regression tests.

2. **Transposed centroid/lower-bound layout**
   - Rafael stores transposed centroids and fully vectorizes centroid distance.
   - Jonathan's lower-bound path still has scalar dims `8..13` after the tail AVX2 revert.
   - Prior tail-vectorization experiment regressed; a different layout may be needed instead of local patching.

3. **Search instrumentation before rewrite**
   - Add optional counters behind env/compile flag:
     - repair frequency by fraud count (`0..5`)
     - average/max lists scanned
     - block count scanned
     - certification early-exit hits
     - distribution of `worst` distance when repair is triggered
   - Use this to tune thresholds and decide whether block16/layout work is worth it.

4. **Index memory strategy**
   - Rafael copies index to anonymous private memory and calls `MADV_HUGEPAGE`/`mlock`.
   - Jonathan mmaps and warms pages.
   - Experiment with `MADV_HUGEPAGE`, `MAP_POPULATE`, or optional copy-to-anonymous-memory only after confirming memory stays within `350M`.

### D. Submission/benchmark hygiene

- Use immutable image tags for every comparison and any official submission.
- Avoid trusting a single GitHub-hosted runner p99; require at least three rounds for promotion decisions.
- Keep comparison branch restored to a known baseline after rejected sweeps.
- Pull `main` after report-archive workflows because docs commits move the branch head.

## Search-policy sweep result: `INDEX_NPROBE=2`

Tested after the `INDEX_NPROBE=5` rejection on comparison config `e514554` with the same immutable Jonathan image:

- `INDEX_NPROBE=2`
- `INDEX_REPAIR_NPROBE=24`
- current repair thresholds
- `INDEX_EXACT_FALLBACK=0`

Results:

| run | jonathanperis p99 | macedot p99 | rafaelcoelhox p99 | clean |
| --- | ---: | ---: | ---: | --- |
| `run-79` / `25948636969` | `0.39ms` | `0.32ms` | `0.36ms` | yes |
| `run-80` / `25948782192` | `0.38ms` | `0.31ms` | `0.34ms` | yes |
| `run-81` / `25948782718` | `0.49ms` | `0.30ms` | `0.33ms` | yes; summary push conflicted, artifacts downloaded manually |

Decision: reject. Median Jonathan p99 was `0.39ms` and mean was `0.42ms`, not better than the current `INDEX_NPROBE=3` baseline median `0.38ms`. The comparison branch was restored to baseline config in `dd89678`.

## Next recommended concrete step

Cheap first-pass probe changes have not improved the candidate:

- `INDEX_NPROBE=5`: rejected, median `0.42ms`.
- `INDEX_NPROBE=2`: rejected, median `0.39ms`.

The CPU split `0.10 / 0.45 / 0.45` was tested next and rejected: three clean all-participant comparison rounds produced Jonathan p99s `0.35ms`, `0.44ms`, and `0.40ms` (median `0.40ms`, mean `0.397ms`), worse than the current baseline median `0.38ms`. The comparison branch was restored to the baseline CPU split in `973ee9a`.

Then the first repair-threshold variant disabled both thresholded repair paths (`repair0=0`, `repair5=0`) at baseline `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`. `run-86` / `25949850100` had Jonathan p99 `0.39ms`, but correctness failed with `7` false positives and `8` false negatives, so the variant was rejected after one run. The comparison branch was restored to baseline thresholds in `aba9a7d`.

The less aggressive variant disabling only `repair5` was also rejected: `run-87` / `25950330221` had Jonathan p99 `0.39ms`, but correctness still failed with `7` false positives and `0` false negatives. The comparison branch was restored to baseline thresholds in `3b48090`.

Next, test one of these isolated paths:

1. Opposite single-threshold correctness gate: disable only `repair0` while keeping `repair5=3748534`.
2. Add lightweight search instrumentation before further threshold tuning.

Do not promote any new candidate until it beats or matches macedot/rafaelcoelhox in repeated clean comparison rounds.
