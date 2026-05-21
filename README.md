# rinha4-back-end-c

C implementation for [Rinha de Backend 2026](https://github.com/zanfranceschi/rinha-de-backend-2026).

## Official evaluation gate

The repository includes a manual benchmark workflow and `scripts/ci-official-benchmark.sh` to run the public Rinha 2026 k6 suite pinned to `645165cbc88a637c78bd6d5cc07bae4dbe422567` by default. See `docs/official-evaluation.md` for scoring thresholds and how to run the gate locally.

Default local command:

```sh
OFFICIAL_REF=645165cbc88a637c78bd6d5cc07bae4dbe422567 \
BENCHMARK_REPETITIONS=3 \
BENCHMARK_K6_MODE=docker \
bash scripts/ci-official-benchmark.sh
```

## Branches

- `main`: source and documentation.
- `submission`: official-submission compose/image lane.
- `comparison`: comparison stacks and benchmark workflow.

## License

MIT
