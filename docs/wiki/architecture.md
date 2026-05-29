# Architecture

```text
k6 / judge
    |
    v
rinha4-lb-yolo-mode :9999
    |  SCM_RIGHTS fd handoff over SOCK_SEQPACKET
    +-- unix:/run/rinha/api1.sock -> C API process -> client fd
    |
    +-- unix:/run/rinha/api2.sock -> C API process -> client fd
```

## Request path

1. The standalone yolo load balancer accepts TCP on port `9999` in FD-pass mode.
2. It passes accepted client file descriptors to API instances over `SOCK_SEQPACKET`
   Unix control sockets with `SCM_RIGHTS`. The LB accepts client sockets as
   nonblocking/close-on-exec and tunes TCP once; the API trusts nonblocking and
   receives the descriptor with `MSG_CMSG_CLOEXEC`. CPU quotas still total `1.00`;
   the LB does not inspect fraud payloads.
3. The C API event loop reads the HTTP/1 request directly from the inherited client FD.
4. The parser recognizes `/ready` and `/fraud-score` and extracts `Content-Length`.
5. The JSON path extracts only the fields needed by the vectorizer.
6. The vectorizer builds the normalized 14-dimensional fraud vector.
7. The index search returns the top-five label count.
8. The API writes one of the prebuilt HTTP/JSON responses.

## Data pipeline

`src/preprocess` converts the allowed `references.json.gz` dataset into a compact
binary index during image build.

The production image builds its index from the official `references.json.gz` during
Docker build. Current Docker defaults enable the KD-tree/block8 layout
(`RINHA_INDEX_KD_TREE=1`, `RINHA_KD_LEAF_SIZE=192`, `RINHA_IVF_LISTS=4096`). The
builder can still emit the legacy IVF/block8 layout, or the experimental
k-means/block16 layout when `RINHA_INDEX_V2=1` is used with KD-tree disabled.

The index stores layout-specific sections, but every layout shares the same
runtime contract:

- metadata and format version
- packed int16 vectors derived from allowed reference data
- fraud labels
- lookup/search metadata for the selected layout
- optional profile/reference fastpath tables
- optional repair/fallback metadata for correctness-first candidate search

## Classifier

The retained runtime mode is approximate nearest-neighbor search over the allowed
reference dataset. Candidate builds must replay the public workload with `0`
false positives and `0` false negatives before they are promoted.

Runtime implementation is split into focused C files:

- `index.c`: binary loading, validation, mmap/storage, and runtime helpers
- `search.c`: nearest-cluster selection, candidate scan, repair/fallback logic
- `vectorize.c`: fraud request feature extraction
- `http.c` / `net.c` / `fdpass.c`: raw transport and socket handoff helpers

## Startup readiness

Each API process recreates its Unix control socket file on startup in the shared
tmpfs volume. The standalone LB consumes the socket paths and keeps transport
fd handoff separate from fraud scoring.
