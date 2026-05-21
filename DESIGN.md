---
version: alpha
name: Rinha4 Back End C Evidence Console
description: Low-glare CRT benchmark console for a pure C Rinha 2026 fraud detector, preserving low-level terminal identity while making official proof, CI evidence, and fd-pass topology easy to verify without black-metal gloom.
colors:
  primary: "oklch(89% 0.010 160)"
  secondary: "oklch(79% 0.010 160)"
  tertiary: "oklch(82% 0.165 78)"
  neutral: "oklch(22% 0.012 160)"
  bg-deep: "oklch(22% 0.012 160)"
  bg-metal: "oklch(29% 0.014 160)"
  bg-panel: "oklch(34% 0.016 160)"
  bg-panel-strong: "oklch(39% 0.018 160)"
  text-main: "oklch(94% 0.008 160)"
  text-muted: "oklch(79% 0.010 160)"
  c-gray: "oklch(71% 0.010 160)"
  c-gray-readable: "oklch(89% 0.010 160)"
  c-gray-dim: "color-mix(in oklch, c-gray 34%, transparent)"
  warn-amber: "oklch(82% 0.165 78)"
  kill-red: "oklch(67% 0.225 18)"
  violet-signal: "oklch(72% 0.145 292)"
  terminal-green: "oklch(82% 0.125 158)"
  focus-ring: "oklch(82% 0.165 78)"
typography:
  display:
    fontFamily: Share Tech Mono
    fontSize: 4rem
    fontWeight: 400
    lineHeight: 0.92
    letterSpacing: "-0.04em"
  h1:
    fontFamily: Share Tech Mono
    fontSize: 3rem
    fontWeight: 400
    lineHeight: 1
    letterSpacing: "-0.03em"
  h2:
    fontFamily: Share Tech Mono
    fontSize: 2rem
    fontWeight: 400
    lineHeight: 1.12
    letterSpacing: "0.02em"
  body-md:
    fontFamily: JetBrains Mono
    fontSize: 1rem
    fontWeight: 400
    lineHeight: 1.7
    letterSpacing: "0em"
  label:
    fontFamily: Share Tech Mono
    fontSize: 0.875rem
    fontWeight: 400
    lineHeight: 1.2
    letterSpacing: "0.12em"
rounded:
  sm: 0px
  md: 0px
  lg: 0px
spacing:
  xs: 4px
  sm: 8px
  md: 16px
  lg: 24px
  xl: 48px
components:
  button-primary:
    backgroundColor: "{colors.kill-red}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 16px
    typography: "{typography.label}"
  button-secondary:
    backgroundColor: "{colors.neutral}"
    textColor: "{colors.primary}"
    rounded: "{rounded.sm}"
    padding: 14px
    typography: "{typography.label}"
  evidence-panel:
    backgroundColor: "{colors.bg-metal}"
    textColor: "{colors.text-main}"
    rounded: "{rounded.sm}"
    padding: 24px
  evidence-panel-subtle:
    backgroundColor: "{colors.bg-metal}"
    textColor: "{colors.secondary}"
    rounded: "{rounded.sm}"
    padding: 16px
  terminal-muted-row:
    backgroundColor: "{colors.bg-deep}"
    textColor: "{colors.c-gray-readable}"
    rounded: "{rounded.sm}"
    padding: 8px
  ambient-rule:
    backgroundColor: "{colors.c-gray-dim}"
    textColor: "{colors.c-gray-readable}"
    rounded: "{rounded.sm}"
    padding: 4px
  decorative-c-gray:
    backgroundColor: "{colors.c-gray}"
    textColor: "{colors.text-main}"
    rounded: "{rounded.sm}"
    padding: 4px
  status-warning:
    backgroundColor: "{colors.warn-amber}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 8px
  score-chip:
    backgroundColor: "{colors.tertiary}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 8px
  status-danger:
    backgroundColor: "{colors.kill-red}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 8px
  status-violet:
    backgroundColor: "{colors.violet-signal}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 8px
  telemetry-green:
    backgroundColor: "{colors.terminal-green}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 8px
  focus-visible:
    backgroundColor: "{colors.focus-ring}"
    textColor: "{colors.neutral}"
    rounded: "{rounded.sm}"
    padding: 4px
  muted-copy:
    backgroundColor: "{colors.neutral}"
    textColor: "{colors.text-muted}"
    rounded: "{rounded.sm}"
    padding: 8px
---

## Overview

This design system describes the public GitHub Pages surface for `rinha4-back-end-c`. The site is a brand-register benchmark evidence console, not a neutral documentation theme.

The visual intent is a dark CRT terminal used by a systems programmer to verify an official performance result at night. The screen should feel mechanical, low-level, and source-backed. It should not feel like a generic hacker toy or a SaaS dashboard with terminal decoration.

Preserve the existing aesthetic family: dark surface, C-gray signal, amber and red status accents, monospaced labels, scanline/console motifs, hard rectangular geometry, and proof-first benchmark copy.

The redesign direction is not “make it prettier.” The direction is: make the same CRT terminal identity behave like an accountable benchmark dashboard.

## Colors

### Strategy

Color strategy: **Low-glare terminal with status accents**.

The page should read as a lifted CRT/evidence console, not a black-metal hacker surface. Keep the machine-room darkness, but use OKLCH green-gray panels and readable C-gray foregrounds so the homepage and docs can sustain actual reading. C gray remains the brand signal, but `c-gray` is now a mid-light terminal foreground, not a decorative low-contrast `#555555` trap. Use `c-gray-readable` or `text-main` for body text, labels that matter, navigation, and metric values. Reserve dimmed C gray mixes for glows, borders, inactive scanlines, and ambient atmosphere.

### Semantic roles

- `bg-deep`: page background and terminal void.
- `bg-metal`: panels, evidence boxes, and dashboard regions.
- `text-main`: readable primary text.
- `text-muted`: supporting text that still needs to pass contrast checks.
- `c-gray`: identity accent, decorative glow, faint rules, and non-essential console residue.
- `c-gray-readable`: readable C-family foreground for important terminal rows.
- `warn-amber`: official source, warning, score, provenance, and “verify” semantics.
- `kill-red`: failure, target, primary CTA, and decisive action emphasis.
- `violet-signal`: rare contrast accent for depth or secondary telemetry only.
- `terminal-green`: optional telemetry accent. Use sparingly and name it. Do not leave hardcoded `rgba(0, 255, 65, ...)` values without a token.
- `focus-ring`: keyboard focus, never decorative only.

Do not use pure black or pure white. Every neutral should remain slightly tinted toward the metal/terminal palette.

## Typography

Use monospace because instrumentation is part of the brand, not because “developer sites use monospace.” The current families are part of the identity:

- `Share Tech Mono` for logo, hero display, section titles, labels, and terminal chrome.
- `JetBrains Mono` for body copy, code, command rows, and technical descriptions.

Rules:

- Body copy must be readable before it is atmospheric. Use larger size and looser line-height for paragraphs.
- Avoid long all-caps sentences. All-caps is for labels, short CTAs, status chips, and terminal headers.
- Limit paragraph line length to 65 to 75 characters.
- Keep display typography brutal and rectangular, but do not let glitch effects compete with metric comprehension.
- If an overhaul introduces a second non-mono body face, it must still feel mechanical and must not soften the site into generic editorial tech branding.

## Layout

The current page order is strong but should become more proof-led.

Recommended high-level structure:

1. Hero with proof-first official result capsule.
2. Official provenance strip with issue, result comment, ranking, closed time, and sync source.
3. Reproduction terminal with copyable image/command and latest CI run link.
4. Topology cutaway for fd handoff and fraud decision path.
5. CI candidate stream with latest and experiment reports clearly separated.
6. Report archive entry points.

Spatial principles:

- Use a strict machine grid, but vary density between proof, topology, and archive sections.
- Do not use identical feature-card grids for the main story.
- Metric cards should read as rows in an evidence console, not generic “stat cards.”
- The official result must be visible above the first scroll on desktop.
- Official and CI data must never share the same visual hierarchy without labels.
- Use diagrams or terminal rows for mechanism explanations when paragraphs become dense.

## Elevation & Depth

Depth should feel like a phosphor display and a metal console, not glass.

Allowed:

- Inset panel shadows.
- Thin full borders.
- Faint scanlines.
- Low-opacity radial atmosphere behind major sections.
- Small glows around active status text or primary CTAs.

Avoid:

- Glassmorphism.
- Blur panels.
- Floating cards with generic drop shadows.
- Heavy glow around long body text.
- Colored side-tab borders as the primary card accent.

## Shapes

Use hard rectangles and terminal geometry. Radius is zero by default.

Acceptable shape language:

- Square panels.
- Full borders.
- Top rails.
- Corner brackets for topology cutaways.
- Inverse-video focus states.
- ASCII or terminal-grid dividers.

Do not use pill-heavy SaaS components except when representing compact status chips. Even then, square or nearly square chips fit the brand better.

## Components

### Hero

The hero must communicate both identity and result. It should not make visitors hunt for proof.

Required hero content:

- Rinha 2026 and pure C context.
- Official result capsule: p99, failures, score, issue, and resource budget.
- Clear official-vs-CI wording when both metrics appear.
- Primary CTA to verify the official issue or result comment.
- Secondary CTA to reports or system notes.

A/B hero variants may change headline copy and proof density, but not the core terminal/CRT identity.

### Evidence capsule

Use for official metrics. It should be compact, source-labeled, and visually more important than decorative terminal output.

Required fields when space allows:

- `official.p99`
- `official.failures`
- `official.score`
- `official.issue`
- `budget`

### Terminal panel

Terminal panels must contain real or clearly labeled illustrative content.

Preferred terminal uses:

- Copyable Docker image or command.
- Official proof transcript.
- CI run transcript.
- ASCII topology cutaway.

Do not use fake commands or fake metrics. If content is decorative, mark it decorative for assistive tech or keep it visually subordinate.

### Metric and report rows

Metric modules must include provenance labels:

- `official`
- `ci candidate`
- `experiment`
- `historical`

When the value is latest, say latest and link to the source. When the value is historical, include date or run ID.

### Topology cutaway

Prefer terminal-style diagrams over long paragraphs:

```text
k6 / judge
    |
    v
rinha4-lb-yolo-mode :9999
    | SCM_RIGHTS over SOCK_SEQPACKET
    +--> api1.sock -> C epoll loop
    +--> api2.sock -> C epoll loop
```

Pair the diagram with short annotations for resource split and data path.

### Links and CTAs

Primary CTA language should reinforce verification, not vague engagement.

Preferred:

- `VERIFY OFFICIAL ISSUE`
- `OPEN RESULT COMMENT`
- `VIEW REPORT HISTORY`
- `READ SYSTEM NOTES`
- `COPY IMAGE`

Avoid:

- `Learn more`
- `Get started` without a specific target.
- Primary buttons that link to generic project pages instead of proof artifacts.

### Accessibility states

Every hover affordance needs an equivalent `:focus-visible` state. Focus should be high-contrast and obvious inside the CRT aesthetic.

Use amber or readable C-gray outlines with offset. Do not rely on glow alone.

Reduced-motion support is mandatory for:

- body flicker;
- scanline animation if animated;
- blinking cursor;
- terminal line reveal;
- hover transforms;
- smooth scrolling.

## Do's and Don'ts

Do:

- Keep numbers source-backed.
- Label official and CI results distinctly.
- Use the C gray identity, but switch to readable tokens for text.
- Make official result proof visible earlier than decorative atmosphere.
- Use terminal diagrams to explain fd handoff and fraud scoring.
- Preserve the no-radius, machine-console geometry.
- Include keyboard focus and reduced-motion checks in every UI change.

Do not:

- Manually type benchmark values into UI copy when JSON data exists.
- Use unsourced “fastest” or “top” claims without official ranking evidence.
- Use colored side borders as the primary card accent.
- Let scanlines or flicker reduce readability in text-heavy sections.
- Use gradient text.
- Use glassmorphism.
- Turn the site into a generic SaaS or editorial tech page.
- Hide whether a metric is official, CI, experiment, or historical.

## Same-Aesthetic A/B Plan

### A/B lane A: Benchmark Proof Console

Hypothesis: proof-first hierarchy increases trust and official-result clicks.

- Control: current hero plus metric cards below.
- Variant: hero includes official proof capsule under the title.
- Primary metric: official issue CTR and report history CTR.
- Secondary metric: bounce rate and scroll depth to topology.

### A/B lane B: Systems Cutaway

Hypothesis: architecture-first storytelling increases docs engagement among technical visitors.

- Control: paragraph-based topology cards.
- Variant: ASCII fd-pass cutaway in the hero or immediately after provenance.
- Primary metric: docs CTR.
- Secondary metric: scroll depth through topology and time on page.

### A/B lane C: Reproduction Terminal

Hypothesis: making the Docker image/command copyable turns atmosphere into utility.

- Control: static terminal lines.
- Variant: copyable command row with `COPY IMAGE` and latest CI run link.
- Primary metric: copy interaction count.
- Secondary metric: GitHub and reports CTR.

### A/B lane D: Low-Glare CRT

Hypothesis: reducing scanlines over text-heavy sections improves completion without weakening identity.

- Control: current global CRT treatment.
- Variant: CRT intensity is strongest in hero/terminal panels and reduced in body sections.
- Primary metric: scroll depth to CI stream.
- Secondary metric: accessibility audit score and reduced-motion compliance.

## Quality Bar

Before shipping a redesign:

- Build the Astro site with Bun.
- Preview the correct route and base path.
- Verify official and CI values against JSON sources.
- Run `npx impeccable detect` on changed Astro/CSS files.
- Run `npx @google/design.md lint DESIGN.md` after design-token changes.
- Check keyboard focus order and visible focus states.
- Check reduced-motion mode.
- Check mobile layout for no horizontal overflow.
- Verify root, docs, reports, and latest report pages return HTTP 200 locally and after deploy.
