# Docs

Astro static site deployed to GitHub Pages.

## Commands

Run from this directory (`docs/`):

| Command | Action |
|---|---|
| `bun install` | Install dependencies |
| `bun run dev` | Start dev server |
| `bun run build` | Build to `./out/` |
| `bun run preview` | Preview production build locally |

## Data sources

| Path | Description |
|---|---|
| `public/official/latest.json` | Latest official Rinha issue result synced from GitHub |
| `public/reports/latest.json` | Latest archived CI benchmark, regardless of report kind |
| `public/reports/latest-candidate.json` | Latest `report_kind=candidate` CI benchmark |
| `public/reports/index.json` | Benchmark report history with `report_kind` metadata |
| `wiki/*.md` | Long-form docs rendered under `/docs/` |
