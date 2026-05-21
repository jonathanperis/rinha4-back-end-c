# Rules

Allowed in this repo:

- preprocess `references.json.gz`
- preprocess `mcc_risk.json`
- preprocess `normalization.json`
- use a classifier built from allowed reference data
- build ANN or IVF indexes from `references.json.gz`
- run the public official k6 script in CI
- compare against public ranking previews and public competitor repositories

Not allowed:

- using official test payloads as reference data
- hardcoding expected answers from preview runs
- building correction tables from misclassified test payloads
- letting the reverse proxy inspect fraud payloads or answer `/fraud-score`

The CI benchmark only mounts official test data into the k6 container. API
containers do not receive test payload files.

The C scorer follows the same boundary: it trains and packs only allowed
challenge resources and fails startup when the index is unavailable.
