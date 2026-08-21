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

### Private semantic replay foundation

`scripts/semantic_replay_fixture.py` is the first, byte-handling stage of the
live semantic-replay milestone. It accepts no default save location: callers
must explicitly supply both a version-1 manifest and the source root. The
manifest records:

- `source_class` and a human-readable `authorization_basis` establishing why
  the bytes may be used locally;
- `redistribution_allowed`, which must remain `false` for a private or
  user-owned save unless a separate review establishes redistribution rights;
- the expected Classic slot letter from `A` through `J`;
- an exact, canonically ordered census of relative paths, byte sizes, and
  lowercase SHA-256 digests, capped at 1 GiB of declared fixture bytes; and
- a domain-separated `tree_sha256` over that census.

The checked-in JSON Schema is
`tests/semantic/replay-fixture-manifest.schema.json`. Schema conformance does
not replace authorization review, and a manifest does not make its source
bytes redistributable. Keep private fixture bytes outside Git and outside
release artifacts.

Verify an explicitly supplied source without copying it:

```sh
python3 scripts/semantic_replay_fixture.py verify \
  --manifest /path/to/fixture-manifest.json \
  --source-root /path/to/user-owned-fixture
```

Stage verified bytes into two new, independent roots:

```sh
install -d -m 700 \
  /new/temporary/classic-user-root/Save \
  /new/temporary/semantic-user-root/Save

python3 scripts/semantic_replay_fixture.py stage \
  --manifest /path/to/fixture-manifest.json \
  --source-root /path/to/user-owned-fixture \
  --classic-root '/new/temporary/classic-user-root/Save/Game A' \
  --semantic-root '/new/temporary/semantic-user-root/Save/Game A'
```

Here `A` is the slot declared by the example manifest. The `Save` directories
are caller-owned trust boundaries and the two `Game A` destinations must not
already exist. The process runner below receives the user-root paths, not the
slot paths.

Staging refuses existing, aliased, or nested destinations. It creates two
byte-exact copies with identities independent from the source and from one
another, rehashes both copies, and verifies the source again before reporting
success. Manifest reads, source traversal, destination creation, and copying
are anchored to opened filesystem descriptors. Symlinks and special files are
rejected; component, inode, link, and file-stability checks make untrusted
ancestor replacement and mutation races fail closed. Each destination parent
is an explicit trust boundary: it must be owned by the current user and must
not be group- or world-writable, and another process able to mutate that
parent as the same user is outside the verifier's threat model. The stager
builds random mode-0700 sibling roots, then publishes each with the platform's
native atomic no-replace rename. A platform without the required
descriptor-relative, no-follow, and no-replace operations is rejected rather
than silently using a pathname-only fallback.

The two final names cannot be published as one atomic filesystem operation.
If either publish or an earlier staging step fails, the tool retains private
staging roots and any first root already published and reports their last
known names. It does not attempt race-prone rollback deletion. A
namespace-tainted report means the displayed name may be missing or may refer
to a replacement; there is no race-free way to recover a path after another
process renames the open directory. Retained roots may contain private fixture
bytes and require deliberate cleanup by the caller after the parent namespace
and reported names have been reviewed.

Both successful fixture commands emit machine-readable JSON containing
`"semantic_equivalence":"not_evaluated"`. Verification and staging establish
fixture identity and isolation only.

`scripts/semantic_replay_runner.py` adds the process-isolation layer. Its
version-1 request, child-config, child-result, and run-envelope contracts are
the four `tests/semantic/semantic-replay-*.schema.json` files. Invoke it only
with an explicit request:

```sh
python3 scripts/semantic_replay_runner.py \
  --request /path/to/semantic-replay-request.json
```

The request names one absolute physical executable, two distinct and
non-nested user-data roots, an existing input slot, a fresh output slot, a
normalized action sequence, a timeout, and explicit 64-bit RNG seed and stream
values. Action identifiers are normalized lowercase ASCII, and string argument
values are printable ASCII so the Python and dependency-free native validators
enforce the same byte-for-byte contract. The runner starts the requested
absolute executable path in separate Classic-then-semantic process groups
without a shell. Both children receive the same action and RNG inputs,
write-disabled preferences,
user-root-only input lookup, a fresh output-slot policy, and the
`next_semantic_gameplay_poll` settlement barrier; presentation is Classic for
the first run and Remastered for the second. Configs and results live in private
protocol directories, and identity, size, route, process, nonce, and output-slot
checks fail closed. User-root identity and its direct parent namespace remain
mutation-pinned while root-level working-file creation is allowed; higher shared
ancestors remain identity-pinned so unrelated filesystem activity does not
invalidate a run. The staged input slot remains fully pinned. The explicitly
named executable and higher same-user filesystem namespace are trusted. A
same-user actor can transiently redirect a higher ancestor between identity
checkpoints. Process sessions isolate legacy globals and support same-session
process termination, but they are not a sandbox and cannot contain a child that
deliberately daemonizes into another session.

Protocol workspaces are mode `0700`, retained, and reported under
`workspace_retention` on both success and post-creation failure. The runner
never recursively deletes them because a same-user process can replace a path
between an identity check and deletion. Clean up an authoritative reported path
only after reviewing it. If `path_authoritative` is false, the candidate may be
a replacement and the original workspace may remain at an unknown renamed
location; inspect the parent namespace deliberately rather than deleting the
candidate automatically. These workspaces contain protocol configs and results,
not staged fixture bytes.

Realmz now recognizes `--semantic-replay-child CONFIG`, strictly validates the
bounded v1 config before SDL startup, fixes the isolated user root, suppresses
ambient preference reads and all preference writes, selects and locks the
configured presentation, and installs the deterministic replay RNG. The
bootstrap deliberately exits nonzero without writing a result because native
save loading, action driving, settled snapshots, and save emission are not yet
implemented. Synthetic tests pin the parent protocol, but their child-reported
state and save hashes are deliberately not compared. Even a successful runner
invocation therefore emits `"runner_scope":"process_isolation_only"` and
`"semantic_equivalence":"not_evaluated"`; it is not evidence of engine or save
equivalence. The synthetic-only replay foundation suite is:

```sh
python3 -m unittest discover \
  -s tests/semantic \
  -p 'test_semantic_replay_*.py' \
  -v
```

## Automated check

Run `scripts/verify-source-baseline.sh --mode development` during implementation. It verifies ancestry and pins without rejecting intentional working changes, validates a complete asset census when present, and validates any content approval records. `scripts/run-core-tests.sh` also runs the synthetic replay foundation suites by default; set `REALMZ_SKIP_PYTHON_TESTS=1` only when a caller deliberately runs the Python gates separately. `--mode release` additionally requires a clean checkout and both City and music approvals.
