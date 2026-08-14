# Remastered asset metadata

Phase one covers exactly five Classic resource forks declared by
`../scopes/phase1.json`. Its selected gameplay bitmap types are `PICT`, `cicn`,
`crsr`, and `ppat`.

Generate the deterministic raw-resource census and zero-cost placeholder
manifest from the repository root:

```sh
python3 scripts/remaster_asset_census.py generate
python3 scripts/remaster_asset_census.py validate
```

The census hashes the immutable raw resource payload, before any QuickDraw
decoder mutates a loaded handle. `ResourceKey` is `(pack, type, id)`, so IDs
that intentionally collide between scenario-local resource forks stay
distinct. Entries with identical payload hashes name one canonical
`master_key`; approved duplicates must reuse the same generated PNG.

The checked-in City of Bywater fork is recorded as
`repository-bundled-unverified-version`. It is never called Mac 7.1.2. A
lawfully sourced baseline can replace it for a private census only when both
arguments are supplied:

```sh
python3 scripts/remaster_asset_census.py generate \
  --city-baseline /path/to/user-owned/Scenario.rsrc \
  --city-sha256 EXPECTED_LOWERCASE_SHA256 \
  --census /private/output/census.json \
  --manifest /private/output/placeholder-manifest.json
```

The supplied file must match the expected hash and must differ from the
repository City fork by both resolved path and SHA-256. The hash and
user-supplied provenance are recorded in both outputs. Its selected resource
keys must match the repository City compatibility surface. The external source
path itself is not recorded.

The JSON Schema files document the portable shape. The standard-library
Python validator is authoritative for cross-file invariants that JSON Schema
cannot express: source hashes, canonical ordering, exact census reproduction,
manifest parity, duplicate-master reuse, PNG path containment, and status-
specific provenance requirements. Runtime C++ loading independently checks
the same security and coverage properties.
