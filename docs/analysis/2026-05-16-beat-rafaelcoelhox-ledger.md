# Beat Rafael Coelho X Ledger

Campaign branch: `perf/beat-rafael-v2`

## Baseline

| Field | Value |
|---|---|
| Candidate source | `main` / `4544ab9ecf5bb9ed1c57cd219f68874f462e3c72` before campaign branch |
| Latest archived candidate image | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-3d8d114b8d90f1a8102a0fbfb124a6d1536afd31` |
| Latest archived candidate p99 | `0.35ms` |
| Latest archived correctness | score `6000`, `0 FP`, `0 FN`, `0 HTTP` |
| Latest active comparison noted | run `96`: Rafael `0.29ms`, macedot `0.31ms`, Jonathan `0.34ms`, all clean |
| Current production env | `INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`, repair range `1..4`, `repair0=4021242`, `repair5=3748534`, exact fallback `0` |

## Baseline local verification

```text
make clean test: passed
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS": passed
```

## Baseline search replay stats

Command:

```bash
make clean preprocess search-stats-replay CFLAGS_ARCH="-DRINHA_SEARCH_STATS -march=haswell -mtune=haswell -mavx2 -mfma"
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

Output:

```text
RINHA_REPLAY rows=250000 reference_bytes=297924757 labels=166678,83322 fraud_counts=162649,1290,2770,2730,1334,79227
RINHA_SEARCH_STATS requests=250000 flat=0 ivf=250000 fast_certified=0 repair_attempts=10154 repair_certified=9717 exact_fallbacks=0
RINHA_SEARCH_STATS fast_fraud=162584,1360,2742,2796,1361,79157 final_fraud=162649,1290,2770,2730,1334,79227
RINHA_SEARCH_STATS repair_before=152,1360,2742,2796,1361,1743 repair_after=217,1290,2770,2730,1334,1813
RINHA_SEARCH_STATS phase=fast lists=750000 max_lists=3 blocks=69000000 max_blocks=276 vectors=549307838 max_vectors=2199
RINHA_SEARCH_STATS phase=repair lists=213234 max_lists=21 blocks=19617528 max_blocks=1932 vectors=156177565 max_vectors=15386
RINHA_SEARCH_STATS phase=exact lists=0 max_lists=0 blocks=0 max_blocks=0 vectors=0 max_vectors=0
RINHA_SEARCH_STATS fast_worst_avg=1127502 fast_worst_max=144977720 final_worst_avg=1088741 final_worst_max=15259637
```

## Experiment ledger

| Experiment | Branch/SHA | API image | Comparison run(s) | Jonathan p99 | Rafael p99 | Correctness | Decision |
|---|---|---|---|---:|---:|---|---|
| Baseline | `3d8d114` image / `4544ab9` docs state | `ci-3d8d114...` | run `96` | `0.34ms` | `0.29ms` | clean | Need v2 search/index rewrite |
| Explicit v2 4096-list k-means/block16 | `82554c6` experiment report | `ci-82554c6...-v2` | build run `25969969351` | `0.45ms` | n/a | `1 FN` | Reject; keep default legacy index |

## 2026-05-16 v2 k-means/block16 builder

Changes:

- Added env-gated builder path: `RINHA_INDEX_V2=1 ./build/build-index refs index lists` writes `RINHA_INDEX_LAYOUT_IVF_KMEANS_BLOCK16`.
- Default builder behavior is preserved: without `RINHA_INDEX_V2`, `./build/build-index refs out 4096` still emits `ivf-block8` layout `3`.
- V2 builder writes block-unit offsets, dim-major transposed centroids, labels, and AoSoA block16 vectors; partial tail blocks are padded with far non-fraud vectors to avoid duplicating fraud votes.
- Added practical deterministic approximate Lloyd assignment using projection-ordered initialization and env knobs: `RINHA_KMEANS_TRAIN`, `RINHA_KMEANS_ITERS`, `RINHA_KMEANS_WINDOW`.
- Added AVX2 block16 scan path for v2 runtime; scalar path remains for non-AVX2 builds.

Verification:

```text
make clean test: passed
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS": passed
cc -std=c11 -O3 -DNDEBUG -march=haswell -mavx2 -mfma -Wall -Wextra -Wshadow -Werror -I src tests/test_search.c src/common/search.c src/common/index.c src/common/distance.c -o /tmp/test_search_avx && /tmp/test_search_avx: passed
./build/build-index /tmp/references.json.gz /tmp/rinha4-default-after-v2.bin 4096: wrote layout=ivf-block8
RINHA_INDEX_V2=1 RINHA_KMEANS_TRAIN=4096 RINHA_KMEANS_ITERS=1 RINHA_KMEANS_WINDOW=8 ./build/build-index /tmp/references.json.gz /tmp/rinha4-v2-smoke.bin 128: wrote layout=ivf-kmeans-block16
RINHA_INDEX_V2=1 RINHA_KMEANS_TRAIN=8192 RINHA_KMEANS_ITERS=1 RINHA_KMEANS_WINDOW=16 ./build/build-index /tmp/references.json.gz /tmp/rinha4-v2-4096.bin 4096: wrote layout=ivf-kmeans-block16
INDEX_NPROBE=3 RINHA_SEARCH_STATS=1 ./build/search-stats-replay /tmp/rinha4-v2-4096.bin /tmp/references.json.gz 50000:
  fraud_counts=32649,284,547,573,267,15680
  fast vectors=231,937,296, max_vectors=15,728
Baseline same 50k with current ivf-block8 + production repair:
  fraud_counts=32641,269,560,581,250,15699
  fast+repair vectors=141,777,257, max_vectors=15,386
```

Assessment: the first v2 candidate is correct enough to build/load/replay, but it scans many more vectors than the current block8+repair baseline at `nprobe=3`. Treat it as scaffolding, not a promotion candidate, unless CI unexpectedly shows a latency win from block16 locality.

## 2026-05-16 v2 centroid/scan tuning

Changes:

- Kept default Docker/image build on `ivf-block8` unless explicit Docker build args enable v2.
- Added Docker build args for future v2 experiment images: `RINHA_INDEX_V2`, `RINHA_IVF_LISTS`, `RINHA_KMEANS_TRAIN`, `RINHA_KMEANS_ITERS`, `RINHA_KMEANS_WINDOW`.
- Made v2 runtime honor the same repair env knobs as legacy IVF, so replay/benchmark comparisons are not accidentally fast-path-only.
- Added AVX2 centroid ranking across 8 transposed v2 centroids at a time.
- Added AVX2 block16 scan early-abort after dims `0..7`, matching the block8 two-stage pattern.

Verification:

```text
make clean test: passed
make clean preprocess search-stats-replay CFLAGS_ARCH="-DRINHA_SEARCH_STATS -march=haswell -mtune=haswell -mavx2 -mfma": passed
cc -std=c11 -O3 -DNDEBUG -march=haswell -mavx2 -mfma -Wall -Wextra -Wshadow -Werror -I src tests/test_search.c src/common/search.c src/common/index.c src/common/distance.c -o /tmp/test_search_avx && /tmp/test_search_avx: passed
make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS": passed
./build/build-index /tmp/references.json.gz /tmp/rinha4-baseline-current.bin 4096: wrote layout=ivf-block8
```

Replay evidence on 250k local references with production-style repair knobs:

| Index/layout | Runtime policy | Final fraud counts | Fast vectors | Repair vectors | Replay wall time | Assessment |
|---|---|---:|---:|---:|---:|---|
| baseline `ivf-block8` / 4096 lists | `nprobe=3`, repair `24` | `162649,1290,2770,2730,1334,79227` | `549,307,838` | `156,177,565` | `14.02s` | Current safe default |
| v2 block16 / 4096 lists | `nprobe=3`, repair `24` | `162622,1316,2773,2738,1354,79197` | `1,157,985,648` | `183,121,904` | `10.48s` | Faster replay despite much higher vector visits; correctness still needs official scoring |
| v2 block16 / 8192 lists | `nprobe=2`, repair `24` | `162638,1312,2765,2733,1348,79204` | `605,783,296` | `104,827,616` | `13.38s` | Candidate-volume near baseline, but runtime only slightly faster locally |

Assessment: AVX2 transposed-centroid ranking is the first v2 change that makes local replay time plausibly competitive. The v2 4096-list path is the most interesting CI experiment despite high padded block16 vector visits. Do not change the default submission image yet; build an explicit v2 experiment image before any comparison/promotion round.

## 2026-05-16 explicit v2 experiment image

Changes:

- Added `workflow_dispatch` inputs to the Build and Release workflow so v2 index images can be built explicitly without changing default `push` images.
- Manual inputs tested: `rinha_index_v2=1`, `rinha_ivf_lists=4096`, `rinha_kmeans_train=131072`, `rinha_kmeans_iters=3`, `rinha_kmeans_window=64`, `image_tag_suffix=-v2`, `benchmark_report_kind=experiment`.
- Default push path was verified separately and still built the safe legacy `ivf-block8` image/report.

Local replay evidence on 250k references:

| Index/layout | Runtime policy | Final fraud counts | Fast vectors | Repair vectors | Replay wall time | Assessment |
|---|---|---:|---:|---:|---:|---|
| baseline `ivf-block8` / 4096 lists | `nprobe=3`, repair `24`, thresholds on | `162649,1290,2770,2730,1334,79227` | `549,307,838` | `156,177,565` | `14.51s` | Safe default |
| v2 block16 / 4096 lists | `nprobe=3`, repair `24`, thresholds on | `162612,1321,2775,2742,1353,79197` | `1,488,575,456` | `191,368,560` | `11.26s` | Faster local replay, different fraud buckets |
| v2 block16 / 4096 lists | `nprobe=5`, repair `24`, thresholds on | `162608,1325,2775,2742,1355,79195` | `2,427,533,184` | `172,827,680` | `13.07s` | More scan work; no correctness improvement |
| v2 block16 / 4096 lists | `nprobe=5`, repair fraud `2..4`, thresholds off | `162610,1344,2760,2736,1355,79195` | `2,427,533,184` | `129,321,744` | `13.28s` | Less repair, but worse max worst distance |

CI evidence:

| Run | Image | Kind | p99 | Score | Correctness | Decision |
|---|---|---|---:|---:|---|---|
| `25969877363` | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-265dc76e0393e0590970d65ac58557a6e8a4ccb6` | candidate/default | `0.31ms` | `6000` | `0 FP`, `0 FN`, `0 HTTP` | Default path remains clean |
| `25969969351` | `ghcr.io/jonathanperis/rinha4-back-end-c:ci-82554c64cb04bbab1b17e57239f4da60fb99c2dd-v2` | experiment/v2 | `0.45ms` | `5819.38` | `0 FP`, `1 FN`, `0 HTTP` | Reject this v2 policy; do not promote |

Assessment: the explicit v2 experiment workflow works, but this 4096-list k-means/block16 policy is not a promotion candidate. It regressed hosted benchmark latency and introduced one false negative. Keep default images on legacy `ivf-block8`; next v2 work should focus on better cluster balance / fewer scanned vectors or a different borderline repair policy before another CI run.
