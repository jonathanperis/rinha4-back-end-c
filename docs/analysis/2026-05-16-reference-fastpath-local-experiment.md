# Reference fast-path local controlled experiment

Date: 2026-05-16

This note records local Docker-free replay evidence only. It is not an official leaderboard result and should not be compared directly against official bot p99 values.

## Baseline env

All replay runs used the current official-like search env unless noted:

- `INDEX_NPROBE=3`
- `INDEX_REPAIR_NPROBE=24`
- `INDEX_REPAIR_MIN_FRAUD=1`
- `INDEX_REPAIR_MAX_FRAUD=4`
- `INDEX_REPAIR0_WORST_THRESHOLD=4021242`
- `INDEX_REPAIR5_WORST_THRESHOLD=3748534`
- `INDEX_EXACT_FALLBACK=0`
- `BUCKET_PROFILE_FASTPATH=1`
- `BUCKET_PROFILE_LEGIT_MIN_COUNT=1000`
- `BUCKET_PROFILE_FRAUD_MIN_COUNT=1000`

The correctness gate for local experiments is exact preservation of replay fraud-count buckets versus the current profile-fastpath baseline:

- Full 3M baseline buckets: `1951657,16133,32838,32691,16183,950498`

## Env/config sweeps rejected

Before changing code, I ran local controlled sweeps around Ronie-style `5/20` probe shapes and alternate repair thresholds.

| Variant | First-250k buckets | Result |
| --- | --- | --- |
| baseline old index | `162649,1290,2770,2730,1334,79227` | Reference for first-250k gate |
| Ronie-like `nprobe=5`, repair `20`, threshold-only `2/3/4` | `162582,1352,2767,2766,1373,79160` | Rejected: changed buckets |
| `nprobe=5`, repair `20`, current `0/5` thresholds + `1..4` repair | `162643,1291,2774,2731,1334,79227` | Rejected: changed buckets |
| `nprobe=5`, repair `24`, current `0/5` thresholds + `1..4` repair | `162644,1291,2773,2730,1334,79228` | Rejected: changed buckets |
| `nprobe=4`, repair `20`, current `0/5` thresholds + `1..4` repair | `162643,1293,2771,2731,1335,79227` | Rejected: changed buckets |
| `nprobe=3`, repair `20`, current `0/5` thresholds + `1..4` repair | full buckets `1951656,16138,32825,32700,16185,950496` | Rejected: full 3M changed buckets |
| profile threshold `750/750` | full buckets `1951657,16133,32838,32691,16183,950498` | Clean but no logical-work improvement versus `1000/1000` |

Conclusion: do not promote a pure env/probe change from this batch.

## Code experiment: reference-purity fast paths

I ported a scoped version of the .NET reference-purity table idea into the C IVF block8 index:

- `reference-fastpath1`: 24-bit table over feature bins `[0,7,10,1,9,11,12,3]` with bits `[4,3,6,1,3,4,1,2]`.
- `reference-fastpath2`: 20-bit table over feature bins `[5,13,6,1,12]` with bits `[4,4,4,4,4]`.
- Edges are build-time quantiles from the reference vectors.
- Runtime executes profile fast-path first, then the reference fast-path.
- Safe runtime defaults only allow fraud decisions from these tables:
  - `BUCKET_REFERENCE_FASTPATH=1`
  - `BUCKET_REFERENCE_FASTPATH_LEGIT=0`
  - `BUCKET_REFERENCE_FASTPATH_FRAUD=1`
  - `BUCKET_REFERENCE_FASTPATH2_LEGIT=0`
  - `BUCKET_REFERENCE_FASTPATH2_FRAUD=1`
- Build-time fraud support defaults are intentionally conservative:
  - `BUCKET_REFERENCE_FASTPATH1_FRAUD_MIN_COUNT=6000`
  - `BUCKET_REFERENCE_FASTPATH2_FRAUD_MIN_COUNT=6000`

Aggressive .NET-like thresholds were rejected locally: fraud thresholds around `200..1500` changed the first-250k bucket distribution. Raising both fraud thresholds to `6000` restored the first-250k and full-3M replay buckets.

## Full replay result

Command shape:

```bash
make clean test search-stats-replay preprocess CFLAGS_ARCH='-march=haswell -mtune=haswell -mavx2 -mfma'
./build/build-index /tmp/references.json.gz /tmp/rinha4-ref-fastpath-default.bin 4096
RINHA_SEARCH_STATS=1 INDEX_NPROBE=3 INDEX_REPAIR_NPROBE=24 \
  INDEX_REPAIR_MIN_FRAUD=1 INDEX_REPAIR_MAX_FRAUD=4 \
  INDEX_REPAIR0_WORST_THRESHOLD=4021242 INDEX_REPAIR5_WORST_THRESHOLD=3748534 \
  INDEX_EXACT_FALLBACK=0 \
  ./build/search-stats-replay /tmp/rinha4-ref-fastpath-default.bin /tmp/references.json.gz
```

| Variant | Full buckets | Fast vectors | Repair vectors | Repair attempts | Local wall |
| --- | --- | ---: | ---: | ---: | ---: |
| Profile fastpath baseline, reference disabled | `1951657,16133,32838,32691,16183,950498` | `1119446249` | `1866115879` | `121327` | `31697ms` |
| Reference fraud fastpath, safe `6000/6000` thresholds | `1951657,16133,32838,32691,16183,950498` | `495196013` | `1815158906` | `118014` | `16667ms` |

Local interpretation:

- Full replay buckets are identical to the current baseline.
- Fast-path vector work drops by about `55.8%` versus the profile-fastpath baseline on this local replay.
- Repair vector work drops slightly, by about `2.7%`.
- The mmaped index grows from the older local baseline file `87801928` bytes to `118211032` bytes with reference tables. This still looks viable under the `160M` API memory limit, but hosted/container measurement is required.
- Local wall time roughly halved in this replay harness, but wall-clock numbers are machine-local and not ranking evidence.

## Required next evidence

This candidate is locally promising and correctness-clean in the replay harness. It still needs CI/official-like evidence before any submission claim:

1. Push the code and let the repository build an immutable image.
2. Verify the candidate benchmark correctness: score `6000`, `0 FP / 0 FN / 0 HTTP`.
3. Run same-window CI comparison against current C/top-global controls with immutable image pins.
4. Keep CI-vs-CI and official-vs-official lanes separate.
