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

The SDL pins are Git gitlinks. `scripts/bootstrap-macos-dependencies.sh` clones phosg and resource_dasm at the exact reviewed commits, rejects dirty or mismatched checkouts, configures from fresh CMake caches, builds universal `x86_64;arm64` libraries for macOS 13.3, and verifies the resulting archives and CMake package files. Configure Realmz with the installation prefix it reports; do not substitute unrecorded system packages for release builds. A strict CI burn-in job uses those exact inputs to build, test, stage, and development-verify an expanded unsigned universal app without invoking the DMG path. It discards the app and creates no artifact upload rather than publishing unresolved content. Keep it outside the required branch ruleset during burn-in; promotion and final DMG construction remain separate release decisions after repeated runner evidence and the applicable content gates.

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
must explicitly supply the source root, plus a version-1 manifest for
verification or staging. Its census mode performs no explicit content writes
and first produces a closed, mechanical `census_unreviewed` record of canonical
paths, sizes, hashes, tree digest, slot, and bounds. Filesystem reads may update
access-time metadata, so use a backed-up copy or snapshot when preserving that
metadata matters. It never infers provenance or authorization:

```sh
python3 scripts/semantic_replay_fixture.py census \
  --source-root /path/to/user-owned-fixture \
  --slot A
```

A human-reviewed manifest combines that evidence with:

- `source_class` and a human-readable `authorization_basis` establishing why
  the bytes may be used locally;
- `redistribution_allowed`, which must remain `false` for a private or
  user-owned save unless a separate review establishes redistribution rights;
- the expected Classic slot letter from `A` through `J`;
- an exact, canonically ordered census of relative paths, byte sizes, and
  lowercase SHA-256 digests, capped at 1 GiB of declared fixture bytes; and
- a domain-separated `tree_sha256` over that census.

The checked-in JSON Schemas are
`tests/semantic/replay-fixture-census.schema.json` and
`tests/semantic/replay-fixture-manifest.schema.json`. Schema conformance and a
mechanical census do not replace authorization review, and a manifest does not
make its source bytes redistributable. Keep private fixture bytes, census,
manifest, and review artifacts outside Git and release artifacts.

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

Verification and staging keep the exact manifest descriptor pinned throughout
their source work, fail if its bytes or namespace drift, and emit its SHA-256
for the equivalence request. Staging refuses existing, aliased, or nested
destinations. It creates two
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

Successful verification and staging emit machine-readable JSON containing
`"semantic_equivalence":"not_evaluated"`; the separate census emits
`"status":"census_unreviewed"` and no equivalence field. These operations
establish mechanical identity and isolation only.

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
invalidate a run. The runner pins each staged input directory's identity; the
live equivalence gate described below adds continuous file-level byte and
metadata attestation. The explicitly named executable and higher same-user
filesystem namespace are trusted. A
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
configured presentation, and installs the deterministic replay RNG. After
action preflight, the child explicitly loads the isolated input slot, enters
normal loaded gameplay, captures canonical initial and post-action snapshots,
and delivers movement through the selected Classic or guarded semantic route.
After every action settles, it explicitly saves to a rechecked fresh slot,
verifies the output tree, and exclusively publishes its state, save, action,
and RNG measurements before exiting successfully.

Synthetic tests pin the parent protocol, and linked native tests exercise both
delivery routes against controlled engine globals for all eight outdoor and
four first-person dungeon movement commands. Those linked tests establish
delivery mapping and next-poll settlement, not Tutorial traversal. The runner
deliberately does not compare child-reported state and save hashes. The
repository deliberately contains no selected live Tutorial fixture. A
standalone runner invocation therefore emits
`"runner_scope":"process_isolation_only"` and
`"semantic_equivalence":"not_evaluated"`; only the separate comparison gate
can establish engine and save equivalence for an externally reviewed fixture.

`scripts/semantic_replay_equivalence.py` is the separate opt-in comparison
layer. Its closed v1 request explicitly pins the exact reviewed manifest and
fixture-tree digests, manifest and source paths, physical executable, output
slot, native-v1 movement action plan, timeout, and RNG seed and stream. Invalid
native actions and digest mismatches fail before private staging. The gate
creates its own private Classic and semantic
user roots, stages the fixture twice, and holds one descriptor-backed lease over
the exact manifest, source tree, and both staged inputs throughout the two
process runs. It finalizes that lease on runner success, failure, or
interruption. Finalization rehashes all three trees, rechecks their captured
metadata and namespaces, and reproves root and file independence before any
behavioral verdict is possible.

Inspect a reviewed local request without staging or launching children:

```sh
python3 scripts/semantic_replay_equivalence.py \
  --request /path/to/semantic-replay-equivalence-request.json \
  --inspect-profile
```

The closed `profile_inspected` record contains only manifest, tree, and action
digests plus counts, slots, settlement barrier, and RNG inputs. It is not an
equivalence verdict or evidence that the intended route was covered. Invoke the
real gate only after separately reviewing that profile and its human coverage
narrative:

```sh
python3 scripts/semantic_replay_equivalence.py \
  --request /path/to/semantic-replay-equivalence-request.json
```

Two valid child results are compared exactly on state-trace digest, save-tree
digest, settled action count, and RNG draw count. Equal values produce
`"semantic_equivalence":"equivalent"` and exit zero. Valid unequal values
produce the completed verdict `"not_equivalent"` and exit one. Fixture drift,
launch or child failure, engine-identity disagreement, malformed evidence, or
post-run attestation failure produces `"not_evaluated"`. The result records a
canonical action-plan digest and bounded non-content fixture evidence. Completed
evidence never emits the manifest's authorization text, file records, or
fixture bytes.
The runner first requires each settled count to equal the request; a bad or
divergent count is consequently a protocol failure, not a completed mismatch.

Gate-owned fixture workspaces are mode `0700`, contain private staged inputs and
save outputs, and are always retained. Their records explicitly say
`contains_fixture_bytes:true`. The nested runner protocol workspace is retained
separately. Automatic recursive cleanup is forbidden for both; inspect the
reported namespace and `path_authoritative` value first. These envelopes are
local audit artifacts and can contain absolute candidate paths even though they
contain no fixture payloads.

The harness is implemented and synthetically tested. The repository still
contains no private fixture, request, raw envelope, or absolute private path,
but it can carry a digest-only receipt for an externally reviewed run. Every
verdict remains scoped to its exact fixture tree, action plan, executable, and
deterministic inputs; a zero-action or narrow movement profile must not be
presented as broader release coverage. V1 also compares RNG draw counts rather
than a separate trace of every value drawn.

#### Private outdoor replay receipt (2026-08-21)

A private, nonredistributable Tutorial fixture completed the reviewed
eight-command outdoor profile through the real Classic and semantic child
routes. The canonical envelope and all fixture-bearing artifacts remain in
private storage. This receipt contains only non-content identities and results:

- comparison contract: `realmz.semantic-replay.exact.v1`;
- result: `equivalent`, exit status `0`, empty stderr, and no mismatched fields;
- build source: commit
  `da7e076fb37aeb67259fa14b8f1caa231d3bcd15`, engine identity
  `Realmz-8.1.0-native-replay-v1`, executable SHA-256
  `b72d1b6c68655de3382fbe40b76a414e157c679fb77b50e8084c6ed2dd44d0c8`;
- fixture manifest SHA-256
  `bb8a43f89d85efb650ca101f9ea4281176a39314c33e98537c6adf8f3502a4fd`
  and fixture-tree SHA-256
  `e3366c4c022ddd1b9bc6002b0d94c597a54cb77e272b42b20c9be6ff7ba3b50e`;
- eight-action outdoor profile SHA-256
  `a796f53599aeb166191ac14583d7180dc69c8d34ecd66131b677602fbd672207`,
  input/output slots `A`/`B`, RNG seed `0123456789abcdef`, RNG stream
  `fedcba9876543210`, and settlement barrier
  `next_semantic_gameplay_poll`;
- Classic/semantic settled-action counts `8`/`8` and RNG-draw counts `24`/`24`;
- matching state SHA-256
  `401447ac9f948341e335bbdf0bd0c56fa67722217885d29f59c73a3c8481448c`
  and output save-tree SHA-256
  `9558d0fa241a8abbd1dad4470ed292c8bc5b6088c8041d259404915ab45336ff`;
- request SHA-256
  `911284a637bebe9da6df9dbde4bb5f91cecaec7207533854d8a23cc6ff737840`
  and independently recorded raw-envelope SHA-256
  `3e3cac012d2dd1930d4ad0cf1dbf3bc3128e3045db2daebacd6b7122c5f0d589`.

This is outdoor-only evidence for those exact identities. It establishes no
first-person dungeon, combat, City, release-wide, or current-HEAD equivalence,
and it does not make the private source fixture redistributable.

#### Private first-person dungeon replay receipt (2026-08-21)

A separate private, nonredistributable Tutorial fixture completed the reviewed
eight-action first-person dungeon profile through the real Classic and semantic
child routes. The canonical envelope and all fixture-bearing artifacts remain
in private storage. This receipt contains only non-content identities and
results:

- comparison contract: `realmz.semantic-replay.exact.v1`;
- result: `equivalent`, exit status `0`, empty stderr, and no mismatched fields;
- build source: commit
  `d41821abf75a68812a7351698cbd585ce129cd15`, engine identity
  `Realmz-8.1.0-native-replay-v1`, executable SHA-256
  `61d40d9fae0911d4a2ae8ff5717b4e675dc3520347ca8fd8db6a961a90e61ed1`;
- fixture manifest SHA-256
  `86bf0b12de0e263f5c4c1c2e544b050313d21269cc77c1777ca50cc92ed4744c`
  and fixture-tree SHA-256
  `2bf79f662d61b3b50c9e59f8d534b7b9fa9148cf76abae254e51dbc40a31f15b`;
- eight-action dungeon profile SHA-256
  `c669ce9bbae72ca7988098e8f9e32938ca245b7cb1f804010a28c5f2b05e0635`,
  input/output slots `A`/`B`, RNG seed `0123456789abcdef`, RNG stream
  `fedcba9876543210`, and settlement barrier
  `next_semantic_gameplay_poll`;
- Classic/semantic settled-action counts `8`/`8` and RNG-draw counts `9`/`9`;
- matching state SHA-256
  `a9e2d0791b74c8e395f1ffe4bb8dc644404edfb0966dec68dd40540e9e9efe80`
  and output save-tree SHA-256
  `de94712f897c4d36a67384bf179f56c5d0c0c790a7601e1c8bc9d2c2c5dbc876`;
- request SHA-256
  `c3fba37178dce49784116bbd242fb50ef7104b06f425841dbcebf2165b4ec18d`
  and independently recorded raw-envelope SHA-256
  `89a1e435d9951b4ff4a1cb0e752868ac21d497d425bac70a9eb2147e96814d33`.

This is first-person dungeon movement evidence for those exact identities. It
establishes no outdoor, combat, City, release-wide, or current-HEAD equivalence,
and it does not make the private source fixture redistributable. Together, the
outdoor and dungeon receipts exercise all 12 native-v1 movement command names,
but they remain two separately scoped verdicts over different exact fixtures
and executables rather than one combined release claim.

The complete private review and archival checklist is
[`docs/SEMANTIC_REPLAY_RUNBOOK.md`](SEMANTIC_REPLAY_RUNBOOK.md).

The synthetic replay foundation suite is:

```sh
python3 -m unittest discover \
  -s tests/semantic \
  -p 'test_semantic_replay_*.py' \
  -v
```

## Automated check

Run `scripts/verify-source-baseline.sh --mode development` during implementation. It verifies ancestry and pins without rejecting intentional working changes, validates a complete asset census when present, and validates any content approval records. `scripts/run-core-tests.sh` also runs the synthetic replay foundation suites by default; set `REALMZ_SKIP_PYTHON_TESTS=1` only when a caller deliberately runs the Python gates separately. `--mode release` additionally requires a clean checkout and both City and music approvals.
