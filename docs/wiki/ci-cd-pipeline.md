# CI/CD Pipeline

Main build flow:

1. Build amd64 Docker image.
2. Push immutable `ci-${GITHUB_SHA}` tag to GHCR.
3. Start Docker Compose with that exact image.
4. Clone official Rinha 2026 repo.
5. Run public `test/test.js` through k6.
6. Upload raw benchmark artifacts.
7. Archive summarized JSON into `docs/public/reports`.
8. GitHub Pages deploys the docs site.

The automatic main-branch benchmark runs against the immutable image tag built in
the same workflow, not a locally rebuilt image. The canonical submission/runtime
shape is root `docker-compose.yml` with the standalone yolo FD-passing load
balancer and two API containers while Docker resource limits remain active.

The `comparison` branch is isolated from main. It keeps public competitor compose
files under `competitor-compose/` and runs a same-matrix benchmark against our
current candidate plus the compact active matrix:

- `RonieNeubauer/rinha2026` — official global #1 and current C #1 reference
  from issue [`zanfranceschi/rinha-de-backend-2026#4682`](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4682).
- `macedot/rinha-2026-c` — next C-language reference from issue
  [`zanfranceschi/rinha-de-backend-2026#4342`](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4342).

Other historical competitor compose files are intentionally not kept active on
the branch unless Jonathan requests a broader/full-field comparison.

Manual **Official-like Benchmark** runs can archive experiment reports too. Use
`report_kind=experiment` for non-default search/runtime parameters so failed or
alternate runs stay visible in `index.json`/`latest.json` without moving
`latest-candidate.json`. The Build and Release dispatch path can also produce
alternate indexed images through `rinha_index_kd_tree`, `rinha_kd_leaf_size`,
`rinha_index_v2`, and the k-means inputs; keep non-baseline runs marked as
`experiment` until they pass the correctness gate.

## Report files

| File | Purpose |
| --- | --- |
| `latest.json` | latest archived benchmark result, regardless of report kind |
| `latest-candidate.json` | latest `report_kind=candidate` submission-stack result |
| `index.json` | sorted benchmark history with `report_kind` metadata for candidate and experiment runs |
| `rinha-benchmark-*.json` | immutable benchmark records |
| `rinha-benchmark-*.html` | k6 HTML reports when generated |

The report archive commit is docs-only. The build workflow ignores docs-only
report commits, so report archiving does not trigger a new benchmark loop.

When benchmark reports change, the build workflow triggers the Pages workflow so
`/reports/` refreshes without manual action.
