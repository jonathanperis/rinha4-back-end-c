# Getting Started

Build the app:

```bash
make all
```

Run focused tests:

```bash
make test
```

Run local stack:

```bash
docker compose up --build
curl -i http://localhost:9999/ready
```

Smoke the fraud endpoint:

```bash
curl -i -X POST http://localhost:9999/fraud-score \
  -H 'Content-Type: application/json' \
  --data '{"id":"tx-smoke","transaction":{"amount":1,"installments":1,"requested_at":"2026-03-11T20:23:35Z"},"customer":{"avg_amount":1,"tx_count_24h":0,"known_merchants":[]},"merchant":{"id":"MERC-001","mcc":"5912","avg_amount":1},"terminal":{"is_online":false,"card_present":true,"km_from_home":0},"last_transaction":null}'
```

Build or rebuild index data without Docker when the reference dataset is present:

```bash
./build/build-index resources/references.json.gz /tmp/index.bin
```

Runtime search controls are environment variables in `docker-compose.yml`, such
as `INDEX_NPROBE`, repair range knobs, exact fallback controls, index mmap/lock
hints, and realtime scheduling hints. Index layout is selected at build time by
Docker args such as `RINHA_INDEX_KD_TREE`, `RINHA_KD_LEAF_SIZE`,
`RINHA_INDEX_V2`, and `RINHA_IVF_LISTS`. See the Runtime Tuning docs page for the
full matrix. Candidate changes must pass local tests and CI benchmark correctness
before promotion.

Run docs locally:

```bash
cd docs
bun install
bun run dev
```
