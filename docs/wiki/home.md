# rinha4-back-end-c

Pure C implementation for Rinha de Backend 2026.

The current build is optimized for correctness first and latency second:

- standalone `rinha4-lb-yolo-mode` FD-passing load balancer
- two C API processes receiving accepted client FDs over Unix control sockets
- manual HTTP/1 request parsing
- manual JSON field extraction
- prebuilt HTTP responses
- mmaped binary vector index built from `references.json.gz`
- archived CI k6 results after each main build
- comparison branch against the fastest public competitors

The project target is explicit: become the top C entry, keep score `6000`, and keep 0 failures.

## Current signal

Latest CI benchmark history lives at `/reports/`.

The home page reads the latest official Rinha issue result from
`docs/public/official/latest.json` and the latest candidate CI result from
`docs/public/reports/latest-candidate.json`. Experiment reports still appear in
`docs/public/reports/index.json`, but do not move the candidate pointer.

CI results are useful for regression tracking. They are not official Rinha
hardware results. Candidate CI runs keep the canonical `docker-compose.yml`
fd-pass standalone-yolo layout used by the submission branch.

## Active lane

Transport is currently stable in CI and official preview. The active lane is the
nearest-neighbor index/search path: scan fewer candidates without losing the
zero-false-positive / zero-false-negative guarantee.

## Repository map

| Path | Purpose |
| --- | --- |
| `src/api` | C fraud-score server |
| `src/common` | shared HTTP, fd-passing, vectorization, index, and search code |
| `src/preprocess` | converts official reference data into `index.bin` |
| `tests` | focused C validation tests |
| `scripts` | benchmark/report automation |
| `docs/public/reports` | versioned benchmark JSON and HTML history |
