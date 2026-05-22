#!/usr/bin/env python3
from pathlib import Path

# Rinha rule compliance: runtime code must not embed public evaluator request IDs,
# expected labels, or any lookup/correction table derived from test/test-data.json.
FORBIDDEN_MARKERS = {
    "test-data.json",
    "expected_approved",
    "expected-label lookup",
    "official_lookup",
    "official-preview lookup",
    "RINHA_CURRENT_CORPUS_FIX",
    "k_current_corpus_corrections",
    "rinha_current_corpus_correction",
    "Current official main corpus corrections",
}

violations = []
for path in Path(".").rglob("*"):
    if not path.is_file() or ".git" in path.parts or "__pycache__" in path.parts:
        continue
    path_str = str(path)
    if path_str == "tests/check_purity.py":
        continue
    if path_str.startswith("docs/"):
        continue
    try:
        text = path.read_text(errors="ignore")
    except Exception:
        continue
    for marker in FORBIDDEN_MARKERS:
        if marker in text:
            violations.append(f"{path_str}: forbidden marker {marker!r}")

if violations:
    raise SystemExit("Rinha purity check failed:\n" + "\n".join(violations))

print("purity check passed")
