# Profile-key-first follow-up and hugepage mmap experiment

Date: 2026-05-16

This follow-up keeps CI-vs-CI and official leaderboard evidence separate. Numbers below are local replay/build evidence only unless explicitly labeled as CI comparison results.

## Profile/key-first probe ordering attempt

I tested a low-risk runtime-only variant before committing it:

- Add `INDEX_PROFILE_KEY_FIRST` as an env-gated probe tie-breaker.
- Keep list lower-bound ordering as the primary key.
- For equal lower-bound candidates, rank lists by a compact profile-key distance between query and list centroid before centroid distance.

Local 250k replay command used current official-like search env:

- `INDEX_NPROBE=3`
- `INDEX_REPAIR_NPROBE=24`
- `INDEX_REPAIR_MIN_FRAUD=1`
- `INDEX_REPAIR_MAX_FRAUD=4`
- `INDEX_REPAIR0_WORST_THRESHOLD=4021242`
- `INDEX_REPAIR5_WORST_THRESHOLD=3748534`
- `INDEX_EXACT_FALLBACK=0`

Result: rejected before commit.

| Variant | final fraud buckets | repair attempts | fast vectors | repair vectors | wall |
| --- | --- | ---: | ---: | ---: | ---: |
| baseline | `162649,1290,2770,2730,1334,79227` | 10154 | 549307838 | 156177565 | 16642ms |
| profile-key tie | `162647,1295,2767,2728,1334,79229` | 13357 | 549311901 | 205442060 | 17209ms |

Why rejected:

- It changed final fraud bucket distribution on the first 250k replay rows.
- It increased repair attempts by ~31.5%.
- It increased repair-vector work by ~31.5%.
- It was slower locally.

Conclusion: do not port this centroid-profile tie-breaker. A true profile/key-first design probably needs index-side profile partitions/ranges, not a centroid-derived tie-breaker on the current projection lists.

## Committed low-risk competitor-derived change

The committed change adds best-effort `MADV_HUGEPAGE` on the mmapped index, before the existing `MADV_WILLNEED` warmup.

Rationale:

- Both top global references use aggressive mmap/page-advice behavior.
- Jonathan already mmaps and warms the index, but did not request hugepage backing.
- `madvise(..., MADV_HUGEPAGE)` is best-effort and should not change search semantics.

Local verification after the change:

- `make clean test search-stats-replay CFLAGS_ARCH='-march=haswell -mtune=haswell -mavx2 -mfma'` passed.
- 250k replay preserved the baseline buckets exactly:
  - `162649,1290,2770,2730,1334,79227`
- Replay counters stayed identical to baseline:
  - repair attempts: `10154`
  - fast vectors: `549307838`
  - repair vectors: `156177565`
- Local wall time was `15476ms` on this run, but local wall-clock deltas are not official evidence.

## CI candidate and same-window comparison evidence

Main candidate commit/image:

- Commit: `9908938ece617bf765c98d6a6a3fbf299bab2b02`
- Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-9908938ece617bf765c98d6a6a3fbf299bab2b02`
- Build/benchmark run: <https://github.com/jonathanperis/rinha4-back-end-c/actions/runs/25966865162>
- Candidate result: p99 `0.34ms`, score `6000`, `0 FP / 0 FN / 0 HTTP`

Same-window CI comparison run:

- Run: <https://github.com/jonathanperis/rinha4-back-end-c/actions/runs/25967006547>
- Run number: `101`
- Inputs: `official_ref=main`, `k6_image=grafana/k6:latest`, `benchmark_repetitions=1`, Jonathan image override set to the immutable image above.
- Jonathan job log verified `WEBAPI_IMAGE=ghcr.io/jonathanperis/rinha4-back-end-c:ci-9908938ece617bf765c98d6a6a3fbf299bab2b02`.

CI-vs-CI results from the same run:

| Participant | p99 | Score | Correctness |
| --- | ---: | ---: | --- |
| `fksegundo-rust` | `0.28ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `rafaelcoelhox` | `0.33ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `jonathanperis` | `0.34ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `ronieneubauer-rinha2026` | `0.37ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |
| `macedot` | `0.42ms` | 6000 | `0 FP / 0 FN / 0 HTTP` |

Interpretation, CI lane only:

- The hugepage-advice candidate is correctness-clean.
- It does not establish a clear speedup versus the prior profile-fastpath candidate; hosted p99 stayed in the same noisy `0.33–0.34ms` solo-candidate range.
- In same-window CI comparison, Jonathan placed behind `fksegundo-rust` and `rafaelcoelhox`, but ahead of `ronieneubauer-rinha2026` and `macedot`.
- Because this was one hosted-runner round, treat rank movement and small deltas as noisy. The safe conclusion is “clean, neutral-to-small-positive at best,” not a proven performance win.

## Official leaderboard lane

No new official leaderboard/bot result was produced by this change during this follow-up. Do not compare the `0.34ms` CI p99 directly against official leaderboard numbers; only compare CI-vs-CI above, and compare official leaderboard entries only against other official leaderboard entries.
