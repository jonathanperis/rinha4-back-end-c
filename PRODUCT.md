# PRODUCT.md

## Product Identity

`rinha4-back-end-c` is a pure C implementation for the Rinha de Backend 2026 fraud scoring challenge. It exists to prove correctness-first low-latency engineering under the official `1 CPU / 350 MB` resource budget.

The public website is not a generic project homepage. It is a benchmark evidence surface: part landing page, part report console, part technical handoff for people who want to verify how the result was produced.

## Register

brand

## Primary Users

1. **Rinha judges and participants** who want to verify the official result, compare implementation choices, and inspect the source.
2. **Systems programmers** who care about C, Linux sockets, `epoll`, Unix control sockets, mmaped indexes, and low-p99 hot paths.
3. **Future maintainers and agents** who need source-backed context before changing copy, benchmark claims, or visual presentation.
4. **Recruiters or technical reviewers** who may not know the Rinha context but can recognize disciplined performance work when evidence is clear.

Their state of mind is skeptical and evidence-seeking. They are usually scanning under time pressure, comparing p99, failures, resource limits, topology, and reproducibility.

## Product Purpose

The site should answer four questions quickly:

1. What is this? A pure C fraud scoring backend for Rinha de Backend 2026.
2. Did it pass? Yes, the latest official result shows `0%` failures with no false positives, false negatives, or HTTP errors.
3. How fast is it? Official p99 is `1.45ms`; the latest CI candidate report records `0.44ms` p99 with score `6000`.
4. Why is it interesting? It combines an external `rinha4-lb-yolo-mode` fd-passing load balancer with two pure C API loops, manual HTTP/JSON handling, prebuilt responses, and an mmaped int16 vector index.

## Canonical Source-Backed Facts

Use these facts as canonical unless the source files or latest reports change.

### Repository and stack

- Source repository: `https://github.com/jonathanperis/rinha4-back-end-c`.
- Challenge: Rinha de Backend 2026 fraud detection.
- Runtime stack: C, Linux, Docker, `epoll`, Unix sockets, `SCM_RIGHTS`, mmaped binary index, int16 vectors, external `rinha4-lb-yolo-mode` load balancer.
- Public endpoints: `GET /ready` and `POST /fraud-score`.
- Production runtime budget: no more than `1 CPU / 350 MB`.

### Runtime topology

- Load balancer accepts TCP on port `9999`.
- Load balancer passes accepted client file descriptors to APIs with `SCM_RIGHTS` over Unix `SOCK_SEQPACKET` control sockets.
- Two C API containers receive accepted client FDs and write responses directly to those sockets.
- LB resource envelope in the current compose: `0.08 CPU`, `30M` memory.
- API resource envelope in the current compose: two containers, each `0.46 CPU`, `160M` memory.
- Current active LB image family: `ghcr.io/jonathanperis/rinha4-lb-yolo-mode`, ASM fd-pass lane.
- Current active API image family: `ghcr.io/jonathanperis/rinha4-back-end-c`.

### Implementation claims

- Manual HTTP/1 parsing.
- Manual JSON field extraction for the challenge payload.
- Fraud scoring through a mmaped binary index built from the allowed challenge reference data.
- Compact int16 vector/index representation.
- Prebuilt response bytes for fraud decisions.
- Correctness-first repair/fallback around approximate nearest-neighbor search.
- Optional search diagnostics are compile-gated behind `RINHA_SEARCH_STATS` and must not be enabled in candidate benchmark images.

### Official benchmark result

Source: `docs/public/official/latest.json`, synced from `zanfranceschi/rinha-de-backend-2026`.

- Official issue: `#4375`, `rinha/test jonathanperis-c`.
- Official issue URL: `https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4375`.
- Official result comment: `https://github.com/zanfranceschi/rinha-de-backend-2026/issues/4375#issuecomment-4452078127`.
- Closed at: `2026-05-14T15:23:24Z`.
- Synced at: `2026-05-14T15:25:51.769Z`.
- Official p99: `1.45ms`.
- Official failure rate: `0%`.
- Official final score: `5839.52`.
- Official p99 score: `2839.52`.
- Official detection score: `3000`.
- Official HTTP errors: `0`.
- Official false positives: `0`.
- Official false negatives: `0`.

### Latest CI candidate result

Source: `docs/public/reports/latest-candidate.json`.

- Timestamp: `2026-05-18T23:46:13Z`.
- SHA: `6297f86fa495ecd8a5428c33c9fe25bae564135d`.
- Run ID: `26067160162`.
- Run URL: `https://github.com/jonathanperis/rinha4-back-end-c/actions/runs/26067160162`.
- Image: `ghcr.io/jonathanperis/rinha4-back-end-c:ci-6297f86fa495ecd8a5428c33c9fe25bae564135d`.
- CI p99: `0.44ms`.
- CI failure rate: `0%`.
- CI final score: `6000`.
- CI p99 score: `3000`.
- CI detection score: `3000`.

## Brand Voice

Voice words: terse, mechanical, verified, hostile to fluff.

Copy should feel like an operator console written by someone who understands the hot path. It can be dramatic, but every number must be traceable. Prefer exact nouns over marketing adjectives.

Good copy:

- `0% failures under the official resource envelope.`
- `SCM_RIGHTS fd handoff over SOCK_SEQPACKET.`
- `Latest CI candidate: read p99 and score from docs/public/reports/latest-candidate.json.`
- `Verify issue #4375.`

Avoid:

- Generic speed claims such as “blazing fast” or “high performance.”
- Unsourced rankings or “best” claims without a linked artifact.
- Decorative terminal output that looks real but cannot be verified.
- Copy that hides whether a metric is official, CI, historical, or illustrative.

## Anti-References

Do not make the site look like:

- A generic SaaS launch page with soft gradients and feature cards.
- A fake hacker dashboard with decorative numbers and no provenance.
- An over-polished corporate case study that loses the C/Linux character.
- A benchmark wall of data where the primary result is hard to find.
- A full-screen CRT effect that sacrifices readability for atmosphere.

## Current Aesthetic to Preserve

Preserve the same aesthetic family:

- Dark CRT terminal surface.
- GitHub Linguist C gray as the identity accent.
- Monospace command language.
- Scanline and console motifs, kept subordinate to readability.
- Amber and red status accents for warning and failure semantics.
- Thin machine-like borders, report panels, and evidence strips.
- Real metrics pulled from official and CI JSON, not static vanity counters.

## Current-Site Evaluation

The current homepage is visually strong and source-backed. It already avoids the worst benchmark-site mistake: it does not invent metrics. The primary opportunity is hierarchy.

What works:

1. The CRT/terminal identity is memorable and fits a low-level C benchmark project.
2. The official result, source issue, result comment, ranking, CI run, and report history are all present.
3. The topology and fraud-decision sections communicate actual implementation differences.

Main issues to address in an overhaul:

1. The hero currently sells the mood before the benchmark result. The visitor should see `0% failures`, official p99, score, and resource budget earlier.
2. Official and CI metrics are both present, but the distinction should be visually impossible to miss.
3. The Docker command in the terminal panel should become a useful copyable command, not only atmosphere.
4. Long monospace paragraphs and global scanlines increase reading cost.
5. The deterministic Impeccable detector flagged the stat-card side accent border as a recognizable AI UI tell. Future cards should use full borders, top rails, labels, or terminal row structure instead.
6. Keyboard focus states are not encoded strongly enough in the current visual system.

## Overhaul Strategy

Keep the current aesthetic family. Do not switch palettes, abandon the terminal motif, or redesign into a clean SaaS template. The overhaul should make the same identity behave more like an accountable benchmark console.

Recommended page order:

1. Hero with proof-first result capsule.
2. Official provenance strip with issue, result comment, ranking, closed time, and sync source.
3. Reproduction terminal with copyable image/command and run links.
4. Topology cutaway for fd handoff and fraud decision path.
5. CI candidate stream with latest and experiment reports clearly labeled.
6. Report archive entry points.

## A/B Testing Plan

### Test 1: Hero proof density

- **Hypothesis:** Visitors will trust and understand the project faster when official result proof appears inside the hero instead of below it.
- **A/control:** Current hero: title, tagline, CTA pair, terminal panel, then metric cards.
- **B variant:** Add an inline “official proof capsule” under the title with `p99 1.45ms`, `failures 0%`, `score 5839.52`, `issue #4375`, and `1 CPU / 350 MB`.
- **C variant:** Replace the right terminal panel with a proof transcript that groups official and CI values in clearly labeled rows.
- **Primary metric:** Click-through to official issue and report history.
- **Recommendation:** Try B first. It changes hierarchy without discarding the existing terminal panel.

### Test 2: Copy specificity

- **Hypothesis:** Precise operator copy will outperform poetic terminal copy for technical visitors.
- **A/control:** `FRAUD SIGNAL IN C.` with current tagline.
- **B variant:** `0% FAILURES. PURE C. 1 CPU / 350MB.` with current visual treatment.
- **C variant:** `TCP IN. FD PASSED. FRAUD SCORED.` for an architecture-led variant.
- **Primary metric:** Docs click-through and scroll depth to topology.
- **Recommendation:** Run B against control for proof comprehension. Keep C as a technical-audience variant if docs clicks matter more than official-result clicks.

### Test 3: Proof module order

- **Hypothesis:** Moving official provenance before decorative terminal output will reduce confusion between official and CI metrics.
- **A/control:** Hero terminal first, official cards second, source strip third.
- **B variant:** Hero, official cards, source strip, then terminal reproduction panel.
- **C variant:** Hero with compact official capsule, source strip, then expanded cards.
- **Primary metric:** Official issue CTR, result comment CTR, lower bounce rate.
- **Recommendation:** Try C when implementation work starts. It preserves drama while moving proof earlier.

### Test 4: Mechanism explanation format

- **Hypothesis:** ASCII cutaway diagrams will communicate the fd-pass architecture faster than paragraph cards.
- **A/control:** Two paragraph cards for Transport Path and Fraud Decision.
- **B variant:** Replace paragraphs with terminal-style flow diagrams plus short annotations.
- **C variant:** Keep cards but convert paragraphs into numbered hot-path rows.
- **Primary metric:** Docs click-through, scroll depth through topology, time on page.
- **Recommendation:** Try B. It is more native to the aesthetic and less text-heavy.

### Test 5: CRT intensity and readability

- **Hypothesis:** Reduced visual interference will increase reading completion without reducing perceived identity.
- **A/control:** Current global scanlines and flicker.
- **B variant:** Keep scanlines in hero/terminal panels only; reduce or remove them over paragraph-heavy sections.
- **C variant:** Add a terminal-styled `CRT ON/OFF` or `LOW GLARE` toggle persisted locally.
- **Primary metric:** Scroll depth to CI stream and report links, plus accessibility audit score.
- **Recommendation:** Try B first. It improves accessibility without adding UI state.

## Success Metrics

For live A/B testing, capture at least:

- Official issue click-through.
- Result comment click-through.
- Report history click-through.
- Docs click-through.
- Scroll depth through topology and CI sections.
- Copy-command usage if a copyable terminal command is added.
- Lighthouse or axe accessibility regressions.
- Reduced-motion behavior and keyboard focus visibility checks.

## Non-Goals

- Do not rewrite the benchmark implementation as part of the design overhaul.
- Do not change benchmark numbers manually. Read them from report JSON or clearly mark them historical.
- Do not introduce new official-ranking claims without source links.
- Do not add generic marketing sections that do not help verify the result, reproduce the stack, or understand the architecture.
