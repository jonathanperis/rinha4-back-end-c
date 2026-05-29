# Runtime Tuning

This page maps the code-level knobs in `Dockerfile`, `docker-compose.yml`,
`scripts/`, and GitHub Actions to the benchmark behavior they change. Keep
candidate reports annotated with these values so a Pages `/reports/` result can
be reproduced from source.

## Compose images and topology

The canonical stack is the root `docker-compose.yml`: one standalone yolo
FD-passing load balancer and two C API containers.

| Knob | Default | Source | Effect |
| --- | --- | --- | --- |
| `LB_IMAGE` | `ghcr.io/jonathanperis/rinha4-lb-yolo-mode:c-ci-0e7bbceed1eb8142dca5f83a449840381b30a785` | Compose env | Load-balancer image used by local and CI compose runs. |
| `WEBAPI_IMAGE` | `rinha4-back-end-c:local` | Compose env / benchmark env | API image used by both API containers. CI sets this to the immutable GHCR `ci-${sha}` image. |
| `API_FD_SOCK` | `/run/rinha/api1.sock`, `/run/rinha/api2.sock` | Compose env | Unix control socket where the LB passes accepted client FDs. |
| `LB_FDPASS_SNDBUF` | `262144` | Compose env | Send-buffer size for the LB's Unix fd-pass socket. |
| `CONNECTION_CLOSE` | unset | API env | Forces close-after-response mode when set to `1`/`true`; keep unset for the normal LB fd-pass path. |

The LB distributes traffic only. It must not parse `/fraud-score` payloads or
return fraud decisions.

## Index build layouts

The image build downloads the allowed `references.json.gz` and runs
`./build/build-index` inside the Docker build. The current Docker defaults are
optimized for the KD-tree/block8 layout:

| Knob | Default | Source | Effect |
| --- | --- | --- | --- |
| `RINHA_INDEX_KD_TREE` | Dockerfile: `1`; direct `build-index`: off unless env is set | Docker build arg / builder env | Enables semantic-partitioned KD-tree/block8 index output when an indexed list-count argument is present. Disable it to use IVF-style builders. |
| `RINHA_KD_LEAF_SIZE` | Dockerfile: `192`; direct builder fallback: `32` | Docker build arg / builder env | KD-tree maximum leaf size before a node splits. Runtime accepts `8..512`. |
| `RINHA_IVF_LISTS` | `4096` | Docker build arg / `build-index` arg | IVF list count, or the compatibility list-count argument that keeps the KD builder on the indexed path. |
| `RINHA_INDEX_V2` | `0` | Docker build arg / builder env | Enables experimental k-means/block16 only when `RINHA_INDEX_KD_TREE=0`. |
| `RINHA_KMEANS_TRAIN` | `131072` | Docker build arg / builder env | Training sample count for the k-means/block16 builder. |
| `RINHA_KMEANS_ITERS` | `3` | Docker build arg / builder env | K-means refinement iterations. Runtime accepts `0..32`. |
| `RINHA_KMEANS_WINDOW` | `64` | Docker build arg / builder env | Nearest-centroid search window during k-means assignment. |

Local layout experiments can build indexes directly:

```bash
make preprocess
./build/build-index resources/references.json.gz /tmp/ivf.bin 4096
RINHA_INDEX_KD_TREE=1 RINHA_KD_LEAF_SIZE=192 ./build/build-index resources/references.json.gz /tmp/kd.bin 4096
RINHA_INDEX_KD_TREE=0 RINHA_INDEX_V2=1 ./build/build-index resources/references.json.gz /tmp/v2.bin 4096
```

## Index loading and memory hints

These are API runtime knobs read by `src/common/index.c` and set in compose when
safe for the benchmark container limits.

| Knob | Default in compose | Source fallback | Effect |
| --- | --- | --- | --- |
| `INDEX_PATH` | `/app/resources/index.bin` | empty string | mmaped index file loaded at API startup. Startup fails when it is missing. |
| `INDEX_MAP_POPULATE` | `1` | off | Adds `MAP_POPULATE` to the read-only mmap when available. |
| `INDEX_MLOCK` | `1` | off | Attempts to `mlock` the mapped index. Requires the compose `memlock` ulimit. |
| `INDEX_WARM_PAGES` | unset | on | Touches one byte per mapped page after mmap so the index is faulted before traffic. |
| `API_MLOCKALL` | `1` | off | Attempts to lock current/future process memory with `mlockall`. |

`posix_fadvise(..., WILLNEED)`, `madvise(..., WILLNEED)`, and
`madvise(..., HUGEPAGE)` are attempted opportunistically by code when the host
supports them; they do not need explicit env vars.

## Search, repair, and exact fallback

These defaults favor correctness first: wider repair and exact fallback are on,
while optional bucket fastpaths stay off unless an experiment proves they do not
introduce false positives or false negatives.

| Knob | Default in compose | Effect |
| --- | --- | --- |
| `INDEX_NPROBE` | `3` | First-pass probe/search breadth. |
| `INDEX_REPAIR_NPROBE` | `24` | Wider repair pass used near the top-five fraud boundary. |
| `INDEX_REPAIR_MIN_FRAUD` | `1` | Lower fraud-count boundary that triggers repair. |
| `INDEX_REPAIR_MAX_FRAUD` | `4` | Upper fraud-count boundary that triggers repair. |
| `INDEX_REPAIR0_WORST_THRESHOLD` | `4021242` | Worst-distance threshold used to repair borderline zero-fraud decisions. |
| `INDEX_REPAIR1_WORST_THRESHOLD` | unset / `0` | Optional worst-distance threshold for one-fraud repair experiments. |
| `INDEX_REPAIR2_WORST_THRESHOLD` | unset / `0` | Optional worst-distance threshold for two-fraud repair experiments. |
| `INDEX_REPAIR3_WORST_THRESHOLD` | unset / `0` | Optional worst-distance threshold for three-fraud repair experiments. |
| `INDEX_REPAIR4_WORST_THRESHOLD` | unset / `0` | Optional worst-distance threshold for four-fraud repair experiments. |
| `INDEX_REPAIR5_WORST_THRESHOLD` | `3748534` | Worst-distance threshold used to repair borderline five-fraud decisions. |
| `INDEX_EXACT_FALLBACK` | `1` | Allows certified/exact fallback when approximate search cannot prove the answer. |
| `INDEX_EARLY_DISTANCE_MILLI` | unset | Optional global early-stop distance, scaled from milli-units of `RINHA_SCALE`. Use only for experiments. |
| `INDEX_KD_EARLY_DISTANCE_MILLI` | unset | Optional KD-tree-specific early-stop distance, scaled the same way. Use only for experiments. |

## Optional bucket fastpaths

The index always writes profile/reference fastpath tables for layouts that
support them. Runtime defaults leave them disabled for candidate safety.

| Knob | Default | Phase | Effect |
| --- | --- | --- | --- |
| `BUCKET_PROFILE_FASTPATH` | `0` | runtime | Enables profile-bucket fastpath decisions. |
| `BUCKET_PROFILE_LEGIT_MIN_COUNT` | `1000` | runtime | Minimum profile-bucket legit support before trusting a legit mask. |
| `BUCKET_PROFILE_FRAUD_MIN_COUNT` | `1000` | runtime | Minimum profile-bucket fraud support before trusting a fraud mask. |
| `BUCKET_REFERENCE_FASTPATH` | `0` | runtime | Enables reference-bucket fastpath decisions. |
| `BUCKET_REFERENCE_FASTPATH_LEGIT` | `0` | runtime | Allows reference fastpath #1 to answer legit. |
| `BUCKET_REFERENCE_FASTPATH_FRAUD` | `1` | runtime | Allows reference fastpath #1 to answer fraud when the global reference fastpath is enabled. |
| `BUCKET_REFERENCE_FASTPATH2_LEGIT` | `0` | runtime | Allows reference fastpath #2 to answer legit. |
| `BUCKET_REFERENCE_FASTPATH2_FRAUD` | follows `BUCKET_REFERENCE_FASTPATH_FRAUD` | runtime | Allows reference fastpath #2 to answer fraud. |
| `BUCKET_REFERENCE_FASTPATH1_LEGIT_MIN_COUNT` | `100` | build | Minimum support used while writing reference fastpath #1 legit masks. |
| `BUCKET_REFERENCE_FASTPATH1_FRAUD_MIN_COUNT` | `6000` | build | Minimum support used while writing reference fastpath #1 fraud masks. |
| `BUCKET_REFERENCE_FASTPATH2_LEGIT_MIN_COUNT` | `150` | build | Minimum support used while writing reference fastpath #2 legit masks. |
| `BUCKET_REFERENCE_FASTPATH2_FRAUD_MIN_COUNT` | `6000` | build | Minimum support used while writing reference fastpath #2 fraud masks. |

Treat any fastpath enablement as an experiment until it passes the candidate
correctness gate with `0` false positives and `0` false negatives.

## Scheduling hints

| Knob | Default in compose | Source fallback | Effect |
| --- | --- | --- | --- |
| `API_RT_PRIORITY` | `10` | `0` | SCHED_FIFO priority when realtime scheduling is available. Runtime clamps `0..99`; `0` disables it. |
| `API_RT_MODE` | `wakeup` | `all` | `wakeup` uses realtime only around the epoll/read wakeup window, then returns to normal scheduling for parse/search/send. Other values keep realtime for the full process path. |
| `MALLOC_ARENA_MAX` | `1` | libc default | Limits glibc malloc arenas; retained for predictable memory behavior even though the runtime is mostly static/mmap based. |

## Search diagnostics

`RINHA_SEARCH_STATS` is both compile-gated and runtime-gated. Build with
`-DRINHA_SEARCH_STATS`, then set `RINHA_SEARCH_STATS=1` to print aggregate search
counters on process exit. Use it for local replay/index experiments only; do not
enable it in candidate benchmark images.

## Benchmark runner env

`scripts/ci-official-benchmark.sh` is the source of truth for local and workflow
benchmark runs.

| Env | Default | Effect |
| --- | --- | --- |
| `OFFICIAL_REPO` | `https://github.com/zanfranceschi/rinha-de-backend-2026.git` | Official challenge repo cloned for the public k6 suite. |
| `OFFICIAL_REF` | `main` | Ref of the official repo to benchmark against. The README documents the pinned local gate separately when used. |
| `COMPOSE_FILE` | `docker-compose.yml` | Compose file used for the benchmark stack. |
| `RESULTS_DIR` | `benchmark-results` | Output directory for official checkout, k6 JSON, docker logs/state, and repeated-run artifacts. |
| `K6_IMAGE` | `grafana/k6:latest` | Docker image used when `BENCHMARK_K6_MODE=docker`. |
| `BENCHMARK_K6_MODE` | script default: `docker`; workflows set `native` by default | Runs k6 through native `k6` on PATH or through Docker. |
| `BENCHMARK_REPETITIONS` | `1` | Number of k6 repetitions. Multiple repetitions archive the median-score/median-p99 selected result plus a repetition summary. |
| `BENCHMARK_PULL_IMAGE` | `true` | Pulls compose images before running. Workflow runs with prebuilt API images keep this true. |
| `BENCHMARK_NO_BUILD` | `false` | Skips `docker compose build` when using an immutable prebuilt API image. |

The benchmark captures docker compose logs and docker state snapshots into the
results directory regardless of k6 success or failure.

## Report archive env

`scripts/archive-benchmark-result.sh` turns benchmark outputs into the JSON/HTML
records served by `/reports/`.

| Env | Default | Effect |
| --- | --- | --- |
| `RESULTS_JSON` | `benchmark-results/results.json` | k6 result JSON to archive. |
| `K6_HTML_REPORT` | `benchmark-results/k6-report.html` | Optional HTML report to copy next to the JSON archive when present. |
| `BENCHMARK_K6_MODE` | `docker` | Metadata stored in report records and `index.json`. |
| `REPORTS_DIR` | `docs/public/reports` | Destination for immutable reports plus `latest*.json`. |
| `REPORT_PREFIX` | `rinha-benchmark` | Prefix for immutable report filenames. |
| `BENCHMARK_TIMESTAMP` | workflow timestamp / current UTC | Timestamp stored in report metadata. |
| `BENCHMARK_SHA` | workflow SHA / current git HEAD | Source commit stored in report metadata. |
| `BENCHMARK_RUN_ID` | workflow run id when present | GitHub Actions run id stored in report metadata. |
| `BENCHMARK_RUN_URL` | derived GitHub Actions URL | Link from `/reports/` back to the run. |
| `BENCHMARK_IMAGE` | falls back to `WEBAPI_IMAGE` | Image recorded in report metadata. |
| `BENCHMARK_REPORT_KIND` | `candidate` | `candidate` updates `latest-candidate.json`; `experiment` only updates `latest.json` and history. |

## GitHub Actions dispatch inputs

Automatic `main` builds create an immutable `ci-${sha}` image, run the
submission-like compose stack, archive benchmark results under
`docs/public/reports`, and refresh Pages. Manual workflows expose the same
experiment surface:

| Input / env | Workflow | Purpose |
| --- | --- | --- |
| `rinha_index_kd_tree`, `rinha_kd_leaf_size`, `rinha_ivf_lists` | Build and Release dispatch | Build an alternate KD-tree or IVF candidate image. |
| `rinha_index_v2`, `rinha_kmeans_train`, `rinha_kmeans_iters`, `rinha_kmeans_window` | Build and Release dispatch | Build an experimental k-means/block16 candidate image. |
| `image_tag_suffix` | Build and Release dispatch | Adds a suffix to the immutable `ci-${sha}` image tag for experiments. |
| `benchmark_report_kind` / `report_kind` | Build and Release / Official-like Benchmark | Use `candidate` only for submission-shaped runs; use `experiment` for alternates. |
| `webapi_image` | Official-like Benchmark | Reuses a prebuilt GHCR image instead of rebuilding from checkout. |
| `official_ref` | Official-like Benchmark | Rinha official repo ref used for the public k6 suite. |
| `k6_image` | Official-like Benchmark | Docker image used when `benchmark_k6_mode=docker`. |
| `benchmark_repetitions` | Official-like Benchmark | Number of k6 repetitions to run before archiving. |
| `benchmark_k6_mode` | Official-like Benchmark | Runs k6 natively or via Docker. |

Workflow-only values such as `GH_TOKEN`, `IMAGE_NAME`, `GITHUB_REPOSITORY`, and
GitHub-provided run metadata are intentionally treated as plumbing, not tuning
knobs. They should not be overridden in local experiments. The `image_tag_suffix`
dispatch input is only surfaced through workflow expressions, effectively as the
`IMAGE_TAG_SUFFIX` concept used to name experiment images.

Candidate promotion rule of thumb: a tuning change should pass local tests and a
candidate benchmark with `0` false positives, `0` false negatives, and `0` HTTP
errors before being treated as the new documented baseline.
