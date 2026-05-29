# rinha4-back-end-c

Pure C implementation for [Rinha de Backend 2026](https://github.com/zanfranceschi/rinha-de-backend-2026) fraud detection.

Goal: official-valid, low-p99 backend under `1 CPU / 350 MB`, with correctness protected before latency experiments. The active competitive target is the public top C lane.

## Current stack

- standalone `rinha4-lb-yolo-mode` FD-passing load balancer on port `9999`
- two pure C API instances receiving accepted client FDs over Unix control sockets
- manual HTTP/1 parsing and manual JSON field extraction
- prebuilt HTTP responses for fraud decisions
- mmaped binary index built from allowed challenge reference data; Docker builds currently default to the KD-tree/block8 index layout, with legacy IVF/block8 and experimental k-means/block16 still available behind build args
- GitHub Actions benchmark archive and GitHub Pages report history

## Contract

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/ready` | readiness probe |
| `POST` | `/fraud-score` | fraud decision |

Runtime shape:

- load balancer accepts TCP on port `9999` and passes client FDs via `SCM_RIGHTS` over `SOCK_SEQPACKET` Unix control sockets
- production API images trust the LB fd-passing contract: passed client FDs are already nonblocking, while received descriptors are marked close-on-exec with `MSG_CMSG_CLOEXEC`
- Docker bridge network
- public `linux/amd64` images for submission
- total limits <= `1 CPU / 350 MB`
- load balancer only distributes traffic; it does not inspect fraud payloads

## Architecture

```text
k6 / judge
    |
    v
rinha4-lb-yolo-mode :9999
    |  SCM_RIGHTS fd handoff over SOCK_SEQPACKET
    +-- unix:/run/rinha/api1.sock -> C API epoll loop -> client fd
    |
    +-- unix:/run/rinha/api2.sock -> C API epoll loop -> client fd
```

Hot path goals:

- raw socket HTTP
- no request-path logging
- minimal JSON scanning
- prebuilt response bytes
- compact int16 vector/index layout
- correctness-first repair/fallback around approximate nearest-neighbor search

The Docker image builds its index from the official `references.json.gz` during
the image build. Current Docker defaults are `RINHA_INDEX_KD_TREE=1`,
`RINHA_KD_LEAF_SIZE=192`, and `RINHA_IVF_LISTS=4096`. Set
`RINHA_INDEX_KD_TREE=0` to return to the legacy IVF/block8 builder, or pair that
with `RINHA_INDEX_V2=1` to build the experimental k-means/block16 layout.

## Local

```sh
make test
make all
docker compose up --build
curl -i http://localhost:9999/ready
```

Optional search diagnostics are compile-gated so the competitive image keeps the
default zero-instrumentation path. Build with `-DRINHA_SEARCH_STATS` and enable
`RINHA_SEARCH_STATS=1` at runtime to print per-process aggregate search counters
on exit:

```sh
make test-search-stats
make clean all CFLAGS_ARCH="-DRINHA_SEARCH_STATS -march=haswell -mtune=haswell -mavx2 -mfma"
RINHA_SEARCH_STATS=1 ./build/api
```

The counters report fast/repair/exact list, block, and vector scans, fraud-count
buckets before and after repair, certification counts, exact fallback use, and
top-5 worst-distance summaries. Use them for index/layout experiments only; do
not enable them in candidate benchmark images.

For offline index-search evidence without Docker, replay allowed reference
vectors directly against an index:

```sh
make preprocess search-stats-replay
./build/build-index /tmp/references.json.gz /tmp/index.bin 4096
INDEX_NPROBE=3 INDEX_REPAIR_NPROBE=24 \
  INDEX_REPAIR_MIN_FRAUD=1 INDEX_REPAIR_MAX_FRAUD=4 \
  INDEX_REPAIR0_WORST_THRESHOLD=4021242 \
  INDEX_REPAIR5_WORST_THRESHOLD=3748534 \
  RINHA_SEARCH_STATS=1 \
  ./build/search-stats-replay /tmp/index.bin /tmp/references.json.gz 250000
```

Smoke request:

```sh
curl -i -X POST http://localhost:9999/fraud-score \
  -H 'Content-Type: application/json' \
  --data '{"id":"tx-smoke","transaction":{"amount":1,"installments":1,"requested_at":"2026-03-11T20:23:35Z"},"customer":{"avg_amount":1,"tx_count_24h":0,"known_merchants":[]},"merchant":{"id":"MERC-001","mcc":"5912","avg_amount":1},"terminal":{"is_online":false,"card_present":true,"km_from_home":0},"last_transaction":null}'
```

## Docs and reports

GitHub Pages lives under `docs/` and mirrors the structure of the .NET implementation:

- `/` home dashboard
- `/docs/` long-form system notes from `docs/wiki/*.md`
- `/reports/` CI candidate and experiment benchmark archive from `docs/public/reports/index.json`

The Pages docs include a runtime/build tuning matrix under `/docs/runtime-tuning/`
so benchmark runs can be traced back to the code-level env vars and workflow
inputs that produced them.

The C site uses GitHub Linguist's C language color (`#555555`) as its accent.

## Branches

- `main`: source, tests, docs, workflows; normal cleanup and implementation work lands here.
- `submission`: official-runner snapshot, updated only when promoting a candidate.
- `comparison`: isolated benchmark stacks, used only for explicit comparison investigations.

## Official evaluation gate

This repository's benchmark workflow and `scripts/ci-official-benchmark.sh` run the public Rinha 2026 k6 suite pinned to `645165cbc88a637c78bd6d5cc07bae4dbe422567` by default. See `docs/official-evaluation.md` for scoring thresholds and how to run the gate locally.

## License

MIT
