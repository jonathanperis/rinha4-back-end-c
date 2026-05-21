#!/usr/bin/env python3
"""Summarize Rinha comparison benchmark result JSON files."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class ParticipantRuns:
    p99_ms: list[float] = field(default_factory=list)
    latest_ms: float = 0.0
    clean: bool = True


def parse_p99_ms(value: Any) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        raise ValueError(f"unsupported p99 value {value!r}")
    text = value.strip().lower()
    if text.endswith("ms"):
        text = text[:-2]
    return float(text)


def is_clean(result: dict[str, Any]) -> bool:
    return (
        float(result.get("final_score", 0)) == 6000.0
        and int(result.get("false_positive_detections", 0)) == 0
        and int(result.get("false_negative_detections", 0)) == 0
        and int(result.get("http_errors", 0)) == 0
    )


def load_results(path: str) -> list[dict[str, Any]]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if isinstance(data, dict) and isinstance(data.get("results"), list):
        return data["results"]
    if isinstance(data, list):
        return data
    raise ValueError(f"{path}: expected comparison JSON object with results[]")


def fmt_ms(value: float) -> str:
    return f"{value:.3f}".rstrip("0").rstrip(".")


def summarize(paths: list[str]) -> dict[str, ParticipantRuns]:
    participants: dict[str, ParticipantRuns] = {}
    for path in paths:
        for result in load_results(path):
            participant = str(result.get("participant", "")).strip()
            if not participant:
                raise ValueError(f"{path}: result missing participant")
            p99 = parse_p99_ms(result.get("p99"))
            runs = participants.setdefault(participant, ParticipantRuns())
            runs.p99_ms.append(p99)
            runs.latest_ms = p99
            runs.clean = runs.clean and is_clean(result)
    return participants


def print_table(participants: dict[str, ParticipantRuns]) -> None:
    headers = ["participant", "n", "min_ms", "median_ms", "max_ms", "latest_ms", "clean"]
    rows: list[list[str]] = []
    for participant in sorted(participants):
        runs = participants[participant]
        if not runs.p99_ms:
            continue
        rows.append(
            [
                participant,
                str(len(runs.p99_ms)),
                fmt_ms(min(runs.p99_ms)),
                fmt_ms(statistics.median(runs.p99_ms)),
                fmt_ms(max(runs.p99_ms)),
                fmt_ms(runs.latest_ms),
                "yes" if runs.clean else "no",
            ]
        )

    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    print("  ".join(h.ljust(widths[i]) for i, h in enumerate(headers)))
    for row in rows:
        print("  ".join(cell.ljust(widths[i]) for i, cell in enumerate(row)))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_paths", nargs="+", help="comparison result JSON files")
    args = parser.parse_args(argv)
    try:
        print_table(summarize(args.json_paths))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
