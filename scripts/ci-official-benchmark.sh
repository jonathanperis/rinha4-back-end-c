#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OFFICIAL_REPO="${OFFICIAL_REPO:-https://github.com/zanfranceschi/rinha-de-backend-2026.git}"
OFFICIAL_REF="${OFFICIAL_REF:-main}"
COMPOSE_FILE="${COMPOSE_FILE:-docker-compose.yml}"
RESULTS_DIR="${RESULTS_DIR:-benchmark-results}"
K6_IMAGE="${K6_IMAGE:-grafana/k6:latest}"
BENCHMARK_K6_MODE="${BENCHMARK_K6_MODE:-docker}"
BENCHMARK_REPETITIONS="${BENCHMARK_REPETITIONS:-1}"
BENCHMARK_PULL_IMAGE="${BENCHMARK_PULL_IMAGE:-true}"
BENCHMARK_NO_BUILD="${BENCHMARK_NO_BUILD:-false}"

cd "$ROOT_DIR"
mkdir -p "$RESULTS_DIR"
rm -rf "$RESULTS_DIR/official"

if ! [[ "$BENCHMARK_REPETITIONS" =~ ^[0-9]+$ ]] || [[ "$BENCHMARK_REPETITIONS" -lt 1 ]]; then
    echo "BENCHMARK_REPETITIONS must be a positive integer" >&2
    exit 1
fi

compose_args=(--compatibility -f "$COMPOSE_FILE")

capture_docker_state() {
    local phase="$1"
    local output="$RESULTS_DIR/docker-state-$phase.txt"
    {
        printf 'phase=%s
' "$phase"
        printf 'official_ref=%s
' "$OFFICIAL_REF"
        printf 'compose_file=%s
' "$COMPOSE_FILE"
        printf 'benchmark_k6_mode=%s
' "$BENCHMARK_K6_MODE"
        printf 'host_uname=%s
' "$(uname -a)"
        docker compose "${compose_args[@]}" ps || true
        for container in $(docker compose "${compose_args[@]}" ps -q); do
            docker inspect --format 'name={{.Name}} image={{.Config.Image}} nano_cpus={{.HostConfig.NanoCpus}} memory={{.HostConfig.Memory}}' "$container" || true
        done
    } > "$output" 2>&1 || true
}

echo "==> Fetching official Rinha test suite at $OFFICIAL_REF"
git clone --depth 1 --branch "$OFFICIAL_REF" "$OFFICIAL_REPO" "$RESULTS_DIR/official"

cleanup() {
    docker compose "${compose_args[@]}" logs --no-color > "$RESULTS_DIR/docker-compose.log" 2>&1 || true
    capture_docker_state cleanup
    docker compose "${compose_args[@]}" down --volumes --remove-orphans > /dev/null 2>&1 || true
}
trap cleanup EXIT

if [[ "$BENCHMARK_PULL_IMAGE" == "true" ]]; then
    for attempt in {1..3}; do
        if docker compose "${compose_args[@]}" pull; then
            break
        fi
        if [[ "$attempt" == "3" ]]; then
            echo "docker compose pull failed after retries" >&2
            exit 1
        fi
        sleep 5
    done
fi

if [[ "$BENCHMARK_NO_BUILD" != "true" ]]; then
    docker compose "${compose_args[@]}" build
fi

docker compose "${compose_args[@]}" up -d --no-build --pull never
capture_docker_state before

for attempt in {1..20}; do
    if curl -fsS http://localhost:9999/ready > /dev/null 2>&1; then
        break
    fi
    if [[ "$attempt" == "20" ]]; then
        echo "Service did not become ready" >&2
        exit 1
    fi
    sleep 3
done

repeat_files=()
chmod -R a+rwX "$RESULTS_DIR/official"
for repetition in $(seq 1 "$BENCHMARK_REPETITIONS"); do
    echo "==> k6 repetition $repetition/$BENCHMARK_REPETITIONS ($BENCHMARK_K6_MODE)"
    rm -f "$RESULTS_DIR/official/test/results.json" "$RESULTS_DIR/official/test/k6-report.html"
    case "$BENCHMARK_K6_MODE" in
        native)
            if ! command -v k6 >/dev/null 2>&1; then
                echo "BENCHMARK_K6_MODE=native requires k6 on PATH" >&2
                exit 1
            fi
            (cd "$RESULTS_DIR/official" && bash ./run.sh > "../k6-output-repetition-$repetition.json")
            ;;
        docker)
            docker run --rm                 --network host                 --user "$(id -u):$(id -g)"                 -e K6_NO_USAGE_REPORT=true                 -e K6_WEB_DASHBOARD=true                 -e K6_WEB_DASHBOARD_PORT=-1                 -e K6_WEB_DASHBOARD_EXPORT=test/k6-report.html                 -v "$ROOT_DIR/$RESULTS_DIR/official:/official"                 -w /official                 "$K6_IMAGE" run test/test.js
            ;;
        *)
            echo "Unsupported BENCHMARK_K6_MODE=$BENCHMARK_K6_MODE (expected native or docker)" >&2
            exit 1
            ;;
    esac
    capture_docker_state "after-$repetition"
    result_file="$RESULTS_DIR/results-repetition-$repetition.json"
    cp "$RESULTS_DIR/official/test/results.json" "$result_file"
    repeat_files+=("$result_file")
    if [[ -f "$RESULTS_DIR/official/test/k6-report.html" ]]; then
        cp "$RESULTS_DIR/official/test/k6-report.html" "$RESULTS_DIR/k6-report-repetition-$repetition.html"
    elif [[ "$BENCHMARK_K6_MODE" == "docker" ]]; then
        echo "k6 HTML report was not generated for repetition $repetition" >&2
    fi
done

if [[ "$BENCHMARK_REPETITIONS" -eq 1 ]]; then
    cp "${repeat_files[0]}" "$RESULTS_DIR/results.json"
    if [[ -f "$RESULTS_DIR/k6-report-repetition-1.html" ]]; then
        cp "$RESULTS_DIR/k6-report-repetition-1.html" "$RESULTS_DIR/k6-report.html"
    fi
else
    selected_repetition="$(jq -s '
        def median_index: ((length - 1) / 2 | floor);
        [to_entries[] | { repetition: (.key + 1), score: .value.scoring.final_score, p99_ms: (.value.p99 | sub("ms"; "") | tonumber) }]
        | sort_by(.score)
        | (.[median_index].score) as $median_score
        | map(select(.score == $median_score))
        | sort_by(.p99_ms)
        | .[median_index].repetition
    ' "${repeat_files[@]}")"
    cp "$RESULTS_DIR/results-repetition-$selected_repetition.json" "$RESULTS_DIR/results.json"
    if [[ -f "$RESULTS_DIR/k6-report-repetition-$selected_repetition.html" ]]; then
        cp "$RESULTS_DIR/k6-report-repetition-$selected_repetition.html" "$RESULTS_DIR/k6-report.html"
    fi
    jq -s --argjson selected_repetition "$selected_repetition" '{
        repetitions: length,
        selected: "median_score_then_median_p99",
        selected_repetition: $selected_repetition,
        selected_p99_ms: (.[($selected_repetition - 1)].p99 | sub("ms"; "") | tonumber),
        p99_ms: (map(.p99 | sub("ms"; "") | tonumber) | sort | {min: .[0], median: .[((length - 1) / 2 | floor)], max: .[-1]}),
        final_score: (map(.scoring.final_score) | sort | {min: .[0], median: .[((length - 1) / 2 | floor)], max: .[-1]}),
        failure_rate: map(.scoring.failure_rate)
    }' "${repeat_files[@]}" > "$RESULTS_DIR/repetition-summary.json"
fi

jq . "$RESULTS_DIR/results.json"
