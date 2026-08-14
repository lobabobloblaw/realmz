# Content and dependency provenance

Realmz Remastered — Unofficial is a noncommercial derivative. A file being technically usable is not sufficient for inclusion: source, reference rights, distribution rights, and review status must all be recorded before a release artifact is produced.

## Reviewed source baseline

The fork is derived from Realmz-Castle/realmz commit `4089d550ab606172bac850ac055677c36c6ff547` (Enable high pixel density rendering). Later fork commits must retain that commit as an ancestor. Local working changes are normal during development; a release must be built from a clean, recorded commit.

The inherited project and game material are distributed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International. A release must bundle the complete `LICENSE`, attribution to Tim Phillips, an unofficial/no-endorsement statement, and a modification notice. This record does not replace a trademark or no-endorsement review before public release.

## Dependency pins

These values are release inputs, not floating recommendations:

| Dependency | Reviewed source | Commit |
| --- | --- | --- |
| SDL | `https://github.com/libsdl-org/SDL` | `8e37db5e797b6167f3a00d697d816a684bd259c7` |
| SDL_image | `https://github.com/libsdl-org/SDL_image` | `bec9134a26c7d0f31b36d6083c25296e04cabff5` |
| SDL_ttf | `https://github.com/libsdl-org/SDL_ttf` | `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b` |
| phosg | `https://github.com/fuzziqersoftware/phosg` | `b2e0c12edb7e274a5e20c460f44eee44f49f57ef` |
| resource_file from resource_dasm | `https://github.com/fuzziqersoftware/resource_dasm` | `27f64c89a5fed855e68c2a5e97b6c6c389d8eb19` |

The SDL pins are Git gitlinks. `scripts/bootstrap-macos-dependencies.sh` clones phosg and resource_dasm at the exact reviewed commits, rejects dirty or mismatched checkouts, configures from fresh CMake caches, builds universal `x86_64;arm64` libraries for macOS 13.3, and verifies the resulting archives and CMake package files. Configure Realmz with the installation prefix it reports; do not substitute unrecorded system packages for release builds. CI intentionally keeps the full application artifact job non-required until this bootstrap and DMG creation are reliable on the selected macOS runner image.

## City of Bywater authorization gate

Phase one specifically targets the Mac 7.1.2 City of Bywater data, which is not assumed to be identical to the repository copy. The authorized baseline must be acquired lawfully from a user-owned source. It must never be silently substituted with repository data and must not be committed.

Approval metadata belongs at `provenance/content/city-of-bywater-mac-7.1.2.json` with this versioned shape:

```json
{
  "schema_version": 1,
  "content_id": "city-of-bywater-mac-7.1.2",
  "version": "Mac 7.1.2",
  "status": "approved-reference",
  "artifact": {
    "sha256": "64 lowercase hexadecimal digits",
    "size_bytes": 1,
    "stored_in_repository": false
  },
  "authorization": {
    "basis": "How lawful possession and reference permission were established",
    "reviewer": "Human reviewer",
    "reviewed_at": "YYYY-MM-DD",
    "approved_as_generation_reference": true,
    "approved_for_distribution": false
  },
  "differences_from_repository": [
    "Concrete, reviewed difference"
  ]
}
```

Only the digest, byte size, authorization record, and documented differences belong in Git. The original baseline stays outside the repository and release bundle. The asset census accepts `--city-baseline PATH --city-sha256 HEX` only as a paired, local invocation and must reject a mismatch. If the record is absent or the bytes cannot be produced locally, City extraction for generation, generation itself, and release are blocked. The repository-bundled City fork may appear only in a clearly labeled, non-generative audit/contact sheet; it remains generation-ineligible and must never be called Mac 7.1.2.

## Music gate

Composition and sample provenance are separate gates. The upstream license or presence of a module file does not, by itself, establish that every included composition and embedded sample is cleared for redistribution. A public package may contain only reviewed tracks marked `bundled-cleared`; every unresolved track must be absent and offered only through an importer for a user-owned installation.

The review record belongs at `provenance/content/phase1-music.json`:

```json
{
  "schema_version": 1,
  "content_id": "phase1-music",
  "status": "reviewed",
  "tracks": [
    {
      "path": "base/Realmz/Realmz Music/Battle Music",
      "sha256": "64 lowercase hexadecimal digits",
      "composition_provenance": "Source and rights basis",
      "sample_provenance": "Source and rights basis",
      "distribution": "bundled-cleared",
      "reviewer": "Human reviewer",
      "reviewed_at": "YYYY-MM-DD"
    },
    {
      "path": "user-imports/Unresolved Music",
      "distribution": "user-import-only",
      "reviewer": "Human reviewer",
      "reviewed_at": "YYYY-MM-DD"
    }
  ]
}
```

Every file bundled under `base/Realmz/Realmz Music` must have a matching `bundled-cleared` record and digest. A `user-import-only` path must not exist in the repository. Adapting upstream pull request #306 is an engineering decision, not a provenance approval; its replacement Outdoor Music binary must be reviewed as a separate content change.

## Generated raster assets

The phase-one source scope is `assets/remastered/scopes/phase1.json`. Its
generated census, exhaustive zero-cost placeholder, and mixed development
runtime manifest are `phase1.census.json`, `phase1.placeholder-manifest.json`,
and `phase1.runtime-manifest.json` in the same directory. The runtime manifest
is deterministically projected from the immutable jobs and review receipts:

```sh
python3 scripts/remaster_asset_census.py validate \
  --root . \
  --scope assets/remastered/scopes/phase1.json \
  --census assets/remastered/scopes/phase1.census.json \
  --manifest assets/remastered/scopes/phase1.placeholder-manifest.json
python3 scripts/build_runtime_asset_manifest.py --root . --check
python3 scripts/remaster_asset_census.py validate \
  --root . \
  --scope assets/remastered/scopes/phase1.json \
  --census assets/remastered/scopes/phase1.census.json \
  --manifest assets/remastered/scopes/phase1.runtime-manifest.json
```

Each approved master record retains the Classic payload hash, exact approved
PNG hash, prompt and input hashes, model/generation provenance, post-processing
steps, reviewer, status, dimensions, anchors/hotspots, alpha policy, and
duplicate mapping. The C++ loader independently hashes each approved PNG before
making an override available. The placeholder and mixed runtime manifests
prove coverage and safe fallback but are not complete release artwork. A
release bundle still requires the separately verified full-approval contract.

The pre-generation style proof has an independently validated, exactly
24-reference Classic selection at
`assets/remastered/style-proof/classic-selection.json`. It covers four
references in each of six families: UI materials, portraits, tactical actors,
items/spells, world/dungeon, and Tutorial/City. The two title illustrations
carry a machine-readable requirement to remove their reference text and render
all replacement typography code-natively. The three repository City entries
are explicitly limited to Classic contact-sheet/audit use and are not eligible
for a generation prompt.

Validate the selection without decoding, or reproduce and verify the canonical
RGBA PNG references and labeled sheet with the pinned `resource_dasm` binary:

```sh
python3 scripts/prepare_style_proof_references.py --root . validate

python3 scripts/prepare_style_proof_references.py --root . build \
  --resource-dasm /path/to/pinned/resource_dasm \
  --output /new/output/directory

python3 scripts/prepare_style_proof_references.py --root . verify \
  --output assets/remastered/style-proof/classic-references
```

The output lock manifest binds every decoded PNG to its pack-aware ResourceKey,
raw Classic payload digest, source-fork digest, dimensions, alpha policy,
decoder commit and executable digest, and independently reviewed raw/canonical
decoded-input digests. It declares zero model calls and no generated art. The
contact sheet is rebuilt from those 24 normalized inputs and
verified byte-for-byte; its City header states that the source is
repository-bundled, version-unverified, and not Mac 7.1.2.

The combined sheet is an audit artifact and must not be fed to ImageGen. The
only permitted handoff command is fail-closed and excludes all three
repository-City inputs:

```sh
python3 scripts/prepare_style_proof_references.py --root . \
  export-generation-inputs \
  --references assets/remastered/style-proof/classic-references \
  --output /new/generation-input-directory

python3 scripts/prepare_style_proof_references.py --root . \
  verify-generation-inputs \
  --output /new/generation-input-directory
```

That 21-reference export is explicitly marked `complete_style_proof: false`
and `approval_eligible: false`; it cannot satisfy the planned 24-asset style
approval. The eventual ImageGen runner must call `verify-generation-inputs`
immediately before consumption. City generation remains blocked until the
three omitted references are rebuilt from the authorized Mac 7.1.2 baseline.

## Test fixtures

Committed fixtures must be synthetic or redistributable under the project license. Retail saves, user characters, and the authorized City baseline must not be committed merely for test convenience. Record the SHA-256, byte size, source class, license/authorization basis, and whether bytes may be redistributed. Tests that require private fixtures must accept an explicit local path and digest, skip with a clear reason when absent, and never rewrite the supplied source.

## Automated check

Run `scripts/verify-source-baseline.sh --mode development` during implementation. It verifies ancestry and pins without rejecting intentional working changes, validates a complete asset census when present, and validates any content approval records. `--mode release` additionally requires a clean checkout and both City and music approvals.
