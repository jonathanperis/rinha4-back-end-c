# Runtime Tuning

This page maps the code-level knobs in `Dockerfile`, `docker-compose.yml`, and
GitHub Actions to the benchmark behavior they change. Keep candidate reports
annotated with these values so a Pages `/reports/` result can be reproduced from
source.

## Index build layouts

The image build downloads the allowed `references.json.gz` and runs
`./build/build-index` inside the Docker build. The current Docker defaults are
optimized for the KD-tree/block8 layout:

| Knob | Default | Source | Effect |
| --- | --- | --- | --- |
| `RINHA_INDEX_KD_TREE` | `1` | Docker build arg | Enables semantic-partitioned KD-tree/block8 index output. Disable it to use IVF-style builders. |
| `RINHA_KD_LEAF_SIZE` | `192` | Docker build arg | KD-tree maximum leaf size before a node splits. |
| `RINHA_IVF_LISTS` | `4096` | Docker build arg / `build-index` arg | IVF list count, or the compatibility list-count argument that keeps the KD builder on the indexed path. |
| `RINHA_INDEX_V2` | `0` | Docker build arg | Enables experimental k-means/block16 only when `RINHA_INDEX_KD_TREE=0`. |
| `RINHA_KMEANS_TRAIN` | `131072` | Docker build arg | Training sample count for the k-means/block16 builder. |
| `RINHA_KMEANS_ITERS` | `3` | Docker build arg | K-means refinement iterations. |
| `RINHA_KMEANS_WINDOW` | `64` | Docker build arg | Nearest-centroid search window during k-means assignment. |

Local layout experiments can build indexes directly:

```bash
make preprocess
./build/build-index resources/references.json.gz /tmp/ivf.bin 4096
RINHA_INDEX_KD_TREE=1 RINHA_KD_LEAF_SIZE=192 ./build/build-index resources/references.json.gz /tmp/kd.bin 4096
RINHA_INDEX_KD_TREE=0 RINHA_INDEX_V2=1 ./build/build-index resources/references.json.gz /tmp/v2.bin 4096
```

## API runtime knobs

These are set per API container in `docker-compose.yml` unless overridden by the
benchmark environment.

| Knob | Default | Effect |
| --- | --- | --- |
| `API_FD_SOCK` | `/run/rinha/api1.sock`, `/run/rinha/api2.sock` | Unix control socket where the LB passes accepted client FDs. |
| `INDEX_PATH` | `/app/resources/index.bin` | mmaped index file loaded at API startup. |
| `INDEX_NPROBE` | `3` | First-pass probe/search breadth. |
| `INDEX_REPAIR_NPROBE` | `24` | Wider repair pass used near the top-five fraud boundary. |
| `INDEX_REPAIR_MIN_FRAUD` | `1` | Lower fraud-count boundary that triggers repair. |
| `INDEX_REPAIR_MAX_FRAUD` | `4` | Upper fraud-count boundary that triggers repair. |
| `INDEX_REPAIR0_WORST_THRESHOLD` | `4021242` | Worst-distance threshold used to repair borderline zero-fraud decisions. |
| `INDEX_REPAIR5_WORST_THRESHOLD` | `3748534` | Worst-distance threshold used to repair borderline five-fraud decisions. |
| `INDEX_EXACT_FALLBACK` | `1` | Allows certified/exact fallback when approximate search cannot prove the answer. |
| `INDEX_MAP_POPULATE` | `1` | Requests eager mmap population where supported. |
| `INDEX_MLOCK` | `1` | Attempts to lock the mapped index in memory. |
| `API_MLOCKALL` | `1` | Attempts to lock current/future process memory. |
| `API_RT_PRIORITY` | `10` | SCHED_FIFO priority when realtime scheduling is available. |
| `API_RT_MODE` | `wakeup` | Uses realtime priority only around the epoll/read wakeup window instead of the full request path. |
| `BUCKET_PROFILE_FASTPATH` | `0` | Enables profile-bucket fastpath decisions stored in the index. |
| `BUCKET_REFERENCE_FASTPATH` | `0` | Enables reference-bucket fastpath decisions stored in the index. |
| `CONNECTION_CLOSE` | unset | Forces close-after-response mode when set to `1`/`true`; normally keep unset for the LB fd-pass path. |

The current defaults favor correctness first: wider repair and exact fallback are
on, while optional bucket fastpaths stay off unless an experiment proves they do
not introduce false positives or false negatives.

## Load balancer knob

| Knob | Default | Effect |
| --- | --- | --- |
| `LB_FDPASS_SNDBUF` | `262144` | Send-buffer size for the LB's Unix fd-pass socket. |

The LB distributes traffic only. It must not parse `/fraud-score` payloads or
return fraud decisions.

## Benchmark and report workflow inputs

Automatic `main` builds create an immutable `ci-${sha}` image, run the
submission-like compose stack, archive benchmark results under
`docs/public/reports`, and refresh Pages.

Manual workflows expose the same experiment surface:

| Input / env | Workflow | Purpose |
| --- | --- | --- |
| `rinha_index_kd_tree`, `rinha_kd_leaf_size`, `rinha_ivf_lists` | Build and Release dispatch | Build an alternate KD-tree or IVF candidate image. |
| `rinha_index_v2`, `rinha_kmeans_train`, `rinha_kmeans_iters`, `rinha_kmeans_window` | Build and Release dispatch | Build an experimental k-means/block16 candidate image. |
| `image_tag_suffix` | Build and Release dispatch | Adds a suffix to the immutable `ci-${sha}` image tag for experiments. |
| `benchmark_report_kind` / `report_kind` | Build and Release / Official-like Benchmark | Use `candidate` only for submission-shaped runs; use `experiment` for alternates. |
| `webapi_image` | Official-like Benchmark | Reuses a prebuilt GHCR image instead of rebuilding from checkout. |
| `official_ref` | Official-like Benchmark | Rinha official repo ref used for the public k6 suite. |
| `benchmark_repetitions` | Official-like Benchmark | Number of k6 repetitions to run before archiving. |
| `benchmark_k6_mode` | Official-like Benchmark | Runs k6 natively or via Docker. |

Candidate promotion rule of thumb: a tuning change should pass local tests and a
candidate benchmark with `0` false positives, `0` false negatives, and `0` HTTP
errors before being treated as the new documented baseline.
