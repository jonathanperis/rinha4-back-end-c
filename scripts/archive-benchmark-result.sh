#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULTS_JSON="${RESULTS_JSON:-$ROOT_DIR/benchmark-results/results.json}"
K6_HTML_REPORT="${K6_HTML_REPORT:-$ROOT_DIR/benchmark-results/k6-report.html}"
BENCHMARK_K6_MODE="${BENCHMARK_K6_MODE:-docker}"
REPORTS_DIR="${REPORTS_DIR:-$ROOT_DIR/docs/public/reports}"
REPORT_PREFIX="${REPORT_PREFIX:-rinha-benchmark}"
TIMESTAMP="${BENCHMARK_TIMESTAMP:-${GITHUB_RUN_STARTED_AT:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}}"
SHA="${BENCHMARK_SHA:-${GITHUB_SHA:-$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)}}"
SHORT_SHA="${SHA:0:12}"
RUN_ID="${BENCHMARK_RUN_ID:-${GITHUB_RUN_ID:-}}"
RUN_URL="${BENCHMARK_RUN_URL:-${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-jonathanperis/rinha4-back-end-c}/actions/runs/${RUN_ID}}"
IMAGE="${BENCHMARK_IMAGE:-${WEBAPI_IMAGE:-}}"
REPORT_KIND="${BENCHMARK_REPORT_KIND:-candidate}"

if [[ ! -f "$RESULTS_JSON" ]]; then
    printf 'Result file not found: %s\n' "$RESULTS_JSON" >&2
    exit 1
fi

mkdir -p "$REPORTS_DIR"
stamp="$(date -u '+%Y%m%d%H%M%S')"
report_file="${REPORT_PREFIX}-${stamp}-${SHORT_SHA}.json"
html_report_file=""
if [[ -f "$K6_HTML_REPORT" ]]; then
    html_report_file="${REPORT_PREFIX}-${stamp}-${SHORT_SHA}.html"
    cp "$K6_HTML_REPORT" "$REPORTS_DIR/$html_report_file"
fi

jq -n \
    --arg timestamp "$TIMESTAMP" \
    --arg sha "$SHA" \
    --arg short_sha "$SHORT_SHA" \
    --arg run_id "$RUN_ID" \
    --arg run_url "$RUN_URL" \
    --arg image "$IMAGE" \
    --arg report_kind "$REPORT_KIND" \
    --arg benchmark_k6_mode "$BENCHMARK_K6_MODE" \
    --arg html_report "$html_report_file" \
    --slurpfile result "$RESULTS_JSON" \
    '{metadata:{timestamp:$timestamp,sha:$sha,short_sha:$short_sha,run_id:$run_id,run_url:$run_url,image:$image,report_kind:$report_kind,benchmark_k6_mode:$benchmark_k6_mode,html_report:(if $html_report == "" then null else $html_report end)},result:$result[0]}' \
    > "$REPORTS_DIR/$report_file"

tmp_index="$(mktemp)"
: > "$tmp_index"
for report in "$REPORTS_DIR"/${REPORT_PREFIX}-*.json; do
    [[ -e "$report" ]] || continue
    jq --arg file "$(basename "$report")" '{file:$file,timestamp:.metadata.timestamp,sha:.metadata.sha,short_sha:.metadata.short_sha,run_id:.metadata.run_id,run_url:.metadata.run_url,image:.metadata.image,report_kind:(.metadata.report_kind // "candidate"),html_report:.metadata.html_report,benchmark_k6_mode:(.metadata.benchmark_k6_mode // "docker"),p99:.result.p99,failure_rate:.result.scoring.failure_rate,final_score:.result.scoring.final_score,http_errors:.result.scoring.breakdown.http_errors,false_positive_detections:.result.scoring.breakdown.false_positive_detections,false_negative_detections:.result.scoring.breakdown.false_negative_detections}' "$report" >> "$tmp_index"
done
jq -s 'sort_by(.file) | reverse' "$tmp_index" > "$REPORTS_DIR/index.json"
rm -f "$tmp_index"
cp "$REPORTS_DIR/$report_file" "$REPORTS_DIR/latest.json"
if [[ "$REPORT_KIND" == "candidate" ]]; then
    cp "$REPORTS_DIR/$report_file" "$REPORTS_DIR/latest-candidate.json"
fi
printf 'Archived benchmark report: %s\n' "$REPORTS_DIR/$report_file"
