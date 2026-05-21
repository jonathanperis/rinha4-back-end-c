# Performance

Hot path choices:

- pure C API process
- raw HTTP/1 parsing
- `SOCK_SEQPACKET` Unix control sockets for `SCM_RIGHTS` fd handoff from the standalone yolo LB; passed client FDs are already nonblocking
- manual request parsing
- no framework/model binding
- prebuilt response bytes
- mmaped binary index
- int16 vector representation
- no fraud-payload parsing in the proxy layer

## Current bottleneck

Transport is fast enough for the current target. Recent yolo-LB CI and official
preview runs have shown `0` HTTP errors. The remaining p99 work is inside index
candidate layout, SIMD scan cost, repair/fallback policy, and CPU split between
the API containers and the standalone FD-passing LB.

## Competitor signals

The compact active comparison matrix tracks only:

- `RonieNeubauer/rinha2026`: official global #1 and current C #1 reference
  (`zanfranceschi/rinha-de-backend-2026#4682`).
- `macedot/rinha-2026-c`: next C-language reference after Ronie in the official
  table (`zanfranceschi/rinha-de-backend-2026#4342`), using a C API around an
  AoSoA IVF bridge with AVX2+FMA centroid/block scanning.
- `jonathanperis/rinha4-back-end-c`: our candidate.

Historical competitors remain useful for old analysis, but they are no longer in
the active `comparison` branch matrix by default.

## Accuracy experiments

Recent rejected sweeps kept the immutable `ci-ab157f4d7e286f8676f419c7e7815068251f4757` image unless noted:

- `INDEX_NPROBE=5`: clean but slower, median Jonathan p99 `0.42ms`.
- `INDEX_NPROBE=2`: clean but not better, median Jonathan p99 `0.39ms`.
- Ronie-style `INDEX_NPROBE=5` + `INDEX_REPAIR_NPROBE=20`: local replay looked cheaper than baseline, but CI benchmark rejected it with `2` false positives.
- CPU split `0.10 / 0.45 / 0.45`: clean but median Jonathan p99 `0.40ms`.
- disabled `repair0` and `repair5`: rejected with `7` false positives and `8` false negatives.
- disabled only `repair5`: rejected with `7` false positives.
- disabled only `repair0`: rejected with `8` false negatives.
- `INDEX_REPAIR_NPROBE=16`: rejected with `6` false positives and `12` false negatives.

Next optimization should keep both current threshold gates enabled and keep
repair breadth at `24`; further probe/repair-breadth tuning is unlikely to help
without changing the index layout or adding per-row diagnostics.

## Search instrumentation

`src/common/search.c` has optional diagnostics behind the `RINHA_SEARCH_STATS`
compile flag. It is disabled in the normal Dockerfile/candidate build. To gather
evidence for index-layout rewrites, compile an instrumentation image/binary with
`-DRINHA_SEARCH_STATS` and set `RINHA_SEARCH_STATS=1` at runtime.

The process prints aggregate counters to stderr on exit:

- total flat/IVF requests
- fast-certified, repair-attempted, repair-certified, and exact-fallback counts
- fast and final fraud-count buckets
- repair before/after fraud-count buckets
- fast/repair/exact scanned lists, block8 blocks, and vectors, including per-request maxima
- average and max top-5 worst distance before and after repair

Use these counters to decide whether the p99 tail is dominated by baseline fast
scan, repair breadth, exact fallback, or unproductive list/block work before
starting the larger kmeans/transposed-centroid/block16 index rewrite.

An offline replay helper, `tools/search_stats_replay.c`, can load an index and
replay vectors from `references.json.gz` without Docker or the HTTP/LB layer.
This is not a substitute for official benchmark traffic, but it is useful for
quickly measuring search-shape changes on the allowed corpus.

Current projection/block8 index replay over the first `250000` reference rows,
with the candidate thresholds (`INDEX_NPROBE=3`, `INDEX_REPAIR_NPROBE=24`,
repair fraud range `1..4`, `repair0=4021242`, `repair5=3748534`), showed:

| Metric | Value |
| --- | ---: |
| repair attempts | `10154 / 250000` (`4.062%`) |
| exact fallbacks | `145 / 250000` (`0.058%`) |
| fast scan work | `3` lists/request, `276.0` block8 blocks/request, `2197.2` vectors/request |
| repair scan work | `15380.9` vectors per repaired request |
| exact fallback work | `22346.4` vectors per exact fallback |
| average total vector visits | `2834.9` vectors/request |

Interpretation: exact fallback is rare on corpus replay, and repair triggers on
only about `4%` of rows, but each repaired request adds roughly seven fast scans'
worth of vector visits. The fixed fast path still scans about `2197` candidates
per request before any repair, so the index v2 work should prioritize reducing
baseline cluster/list candidate volume and centroid-ranking cost, not only
shaving the rare exact fallback.

## FD-passing load balancer

The retained load balancer path for our own submissions is the standalone
`rinha4-lb-yolo-mode` image. It accepts TCP on port `9999` and passes accepted
client FDs to API containers over Unix control sockets; the APIs parse HTTP from
the inherited client sockets.

The benchmark workflow runs the canonical root `docker-compose.yml` used by the
submission. Competitor comparison compose files keep each competitor's own proxy
and image choices.
