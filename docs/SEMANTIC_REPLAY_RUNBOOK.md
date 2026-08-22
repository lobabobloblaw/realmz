# Private semantic replay review runbook

This runbook turns one private Tutorial save and one reviewed action plan into a
narrowly scoped Classic-versus-semantic equivalence result. It does not make the
save redistributable, establish that a route is well chosen, or broaden one
successful profile into a release-wide claim.

The repository does not currently contain a Tutorial save, manifest instance,
request instance, or real-run envelope. Keep all four outside Git unless each
artifact has received a separate redistribution review. The manifest contains
the authorization statement; the request and raw envelopes contain absolute
local paths. Use a private review directory and files readable only by the
current user.

Create the review directory before any redirected output, establish private
creation permissions for the complete shell session, and refuse accidental
overwrite. Use a fresh directory name for each reviewed attempt:

```sh
install -d -m 700 /absolute/private/review
umask 077
set -o noclobber
```

Fixture commands and profile inspection write success JSON to stdout at exit
zero and error JSON to stderr otherwise. The equivalence gate writes a
completed envelope to stdout at exit zero or one and an error envelope to
stderr otherwise. Capture the streams in separate private files, record the
exit status immediately, and never merge them. An otherwise unopposed
interrupt returns `fixture.interrupted` or `gate.interrupted` with exit 130; if
a runner interrupt coincides with a final fixture-attestation failure, the
attestation error remains primary and records the interruption as its secondary
error. An interruption during output emission may leave incomplete bytes on
stdout; discard every nonselected or partial stream rather than treating it as
evidence. None of these error envelopes is a semantic verdict.

## 1. Select the source save

The fixture root is exactly one complete Classic slot directory such as
`Save/Game A`, not the parent `Save` directory and not the Tutorial scenario
data directory. Work from a backed-up copy. Before hashing it, a human reviewer
must load the copy in a compatible build and record, outside the manifest:

- that the scenario is Tutorial;
- the starting land/dungeon location and facing;
- that no combat, modal, text-entry, or unresolved encounter is active;
- why the state is suitable for the intended action route; and
- who owns or supplied the bytes and what use was authorized.

Repository presence alone is not provenance review. In particular, the five
tracked saves under `base/Realmz/Save` are not Tutorial fixtures.

## 2. Capture the mechanical census

Run the descriptor-anchored census with an explicit source root and slot. It
performs no explicit content writes, but filesystem reads may update access-time
metadata; this is another reason to use the backed-up copy selected above.
There is no default save path:

```sh
python3 scripts/semantic_replay_fixture.py census \
  --source-root /absolute/private/path/to/Save/Game\ A \
  --slot A \
  > /absolute/private/review/fixture-census.stdout.json \
  2> /absolute/private/review/fixture-census.stderr.json
census_status=$?
```

The closed `replay-fixture-census-v1` output is deliberately marked
`census_unreviewed`. It supplies only the canonical file list, sizes, hashes,
tree digest, slot, and counts. It never supplies `source_class`, an
`authorization_basis`, or redistribution permission. Continue only when
`census_status` is zero and stderr is empty.

Review the census, then create a private manifest v1 by combining its `slot`,
`files`, and `tree_sha256` with human decisions for these required fields:

```json
{
  "manifest_version": 1,
  "source_class": "reviewer-chosen-lowercase-class",
  "authorization_basis": "Human-reviewed statement specific to these bytes.",
  "redistribution_allowed": false,
  "slot": "A",
  "files": [],
  "tree_sha256": "64 lowercase hexadecimal characters"
}
```

Do not copy the empty placeholder array above; use the complete canonical
`files` array from the census. Validate the finished manifest against the
unchanged source:

```sh
python3 scripts/semantic_replay_fixture.py verify \
  --manifest /absolute/private/review/fixture-manifest.json \
  --source-root /absolute/private/path/to/Save/Game\ A \
  > /absolute/private/review/fixture-verification.stdout.json \
  2> /absolute/private/review/fixture-verification.stderr.json
verification_status=$?
```

The verification record supplies the SHA-256 of the exact manifest bytes. Pin
that value, as well as `tree_sha256`, in the equivalence request. Changing even
whitespace or authorization text changes the manifest digest and requires a
new review. Continue only when `verification_status` is zero and stderr is
empty.

## 3. Review the action profile

Choose the request schema explicitly. Schema 1 is the immutable movement-only
contract used by the existing private receipts. It accepts only `move_party`
records with contiguous zero-based ordinals and exactly one
`arguments.command` string. Outdoor movement uses:

`north`, `northeast`, `east`, `southeast`, `south`, `southwest`, `west`,
`northwest`.

First-person dungeon movement uses:

`step_forward`, `step_backward`, `turn_left`, `turn_right`.

A command from the wrong live context fails closed. A profile may cross from
outdoor exploration into a dungeon only when the reviewed route and fixture
actually make that transition. Record an index-by-index coverage narrative
that identifies the expected context, location change, trigger avoidance or
intent, and the reason each action is present. Zero actions remain legal for
generic harness tests but are not an acceptable milestone profile.

Schema 2 preserves all movement records and adds exactly one closed action:

```json
{
  "ordinal": 0,
  "kind": "select_party_member",
  "arguments": {"member": 2}
}
```

`member` is the zero-based roster ID and must be a JSON integer from 0 through
5; booleans, strings, missing or extra fields, and out-of-range values fail
before fixture staging. Selection is eligible only on the live exploration or
dungeon surfaces. Both changing the selection and reselecting the active member
are valid, but the latter remains an idempotent no-op rather than the Classic
second portrait click that could open a modal. The action settles on the next
gameplay poll only after the narrow adapter accepts it, and the captured
post-action state must report that member selected. Review the live roster and
surface, the intended member, and the absence of a modal or unresolved
encounter for every selection action.

Create a private request using exact absolute physical paths:

```json
{
  "schema_version": 1,
  "manifest": "/absolute/private/review/fixture-manifest.json",
  "source_root": "/absolute/private/path/to/Save/Game A",
  "fixture_manifest_sha256": "reviewed manifest SHA-256",
  "fixture_tree_sha256": "reviewed tree SHA-256",
  "executable": "/absolute/path/to/current/Realmz",
  "output_slot": "B",
  "actions": [
    {
      "ordinal": 0,
      "kind": "move_party",
      "arguments": {"command": "north"}
    }
  ],
  "timeout_seconds": 30,
  "rng_seed": "0123456789abcdef",
  "rng_stream": "fedcba9876543210"
}
```

The output slot must differ from the input slot and must begin absent in the
gate-owned roots. The seed, stream, timeout, executable, and every action are
part of the reviewed profile.

For a schema-2 profile, set `schema_version` to `2` and use only schema-2 action
records. The version propagates through the runner, child configs, child
results, and completed envelopes; it selects a distinct engine identity,
action-digest domain, and exact-comparison contract. Never change the version
on an already reviewed request without repeating profile review.

Inspect the complete profile without staging or launching either child:

```sh
python3 scripts/semantic_replay_equivalence.py \
  --request /absolute/private/review/equivalence-request.json \
  --inspect-profile \
  > /absolute/private/review/profile-inspection.stdout.json \
  2> /absolute/private/review/profile-inspection.stderr.json
inspection_status=$?
```

The closed inspection record contains no paths or fixture content. Sign off on
its exact manifest, tree, and action-plan digests together with the human
coverage narrative. `profile_inspected` is not an equivalence verdict.
Continue only when `inspection_status` is zero and stderr is empty.

## 4. Execute and archive the real gate

Use the same request only after the executable and profile review are final:

```sh
python3 scripts/semantic_replay_equivalence.py \
  --request /absolute/private/review/equivalence-request.json \
  > /absolute/private/review/equivalence-completed.stdout.json \
  2> /absolute/private/review/equivalence-error.stderr.json
gate_status=$?

case "$gate_status" in
  0|1) echo "archive completed stdout; reject the attempt if stderr is nonempty" ;;
  *) echo "archive error stderr; discard any partial or nonselected stdout" ;;
esac
```

Exit zero with `semantic_equivalence:equivalent` is meaningful only for the
exact manifest, fixture-tree, action-plan, RNG, slots, settlement barrier, and
engine identity recorded by that envelope. Exit one is a completed
`not_equivalent` result. Any fixture, launch, protocol, identity, or attestation
failure is `not_evaluated` and must not be converted into either verdict.

Archive the raw local envelope from the stream selected by `gate_status`, its
independent SHA-256, both captured streams, the profile inspection record, the
request, manifest, census, verification record, recorded command exit statuses,
and human review notes together in private storage. A later digest-only receipt
may summarize those artifacts, but it does not replace the canonical local
envelope. Do not treat an empty stdout file from a failed run as the envelope.

Both gate and runner workspaces are intentionally retained. The gate workspace
contains private staged inputs and save outputs. The nested runner workspace
contains sensitive configs, child results, and absolute paths, but no staged
fixture tree. Inspect each reported `candidate_path`, its `path_authoritative`
flag, and its parent namespace before deliberately removing that exact
candidate. Neither tool performs automatic recursive cleanup.
