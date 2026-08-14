# Exploration semantic-equivalence harness

This focused harness compares the smallest migrated exploration command set
without reading or writing a Realmz character or save file:

- all 12 `MovementCommand` values;
- outdoor arrow/keypad scan codes from `realmz_orig/checkkeypad.c`;
- first-person dungeon movement keys from `realmz_orig/threed.c`;
- party portrait selection behavior modeled on `realmz_orig/buttonchoice.c`;
- lower/upper valid party-member IDs and rejected out-of-range IDs;
- rejected movement at the 90-by-90 fixture boundary;
- per-action status, detail, event stream, `GameSnapshot`, and deterministic
  save-facing bytes;
- repeat-run determinism for each route.

Run the strict standalone check from the repository root:

```sh
tests/semantic/run-exploration-action-equivalence.sh
```

Run it with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
REALMZ_ENABLE_SANITIZERS=1 \
  tests/semantic/run-exploration-action-equivalence.sh
```

Select another compiler with `CXX`, for example:

```sh
CXX=g++ tests/semantic/run-exploration-action-equivalence.sh
```

## Boundary of this result

This is a contract-level fixture, not a live-engine equivalence claim. The
production runtime now connects eligible `MovePartyAction` commands to the
legacy event loop, while `SelectPartyMemberAction` remains intentionally
unwired. The fixture models deterministic exploration mutations and a stable
byte image; it does not reproduce or alter Realmz's binary save format.

The full live equivalence test should land with authorized save fixtures and
the remaining production handlers. It should:

1. Copy an authorized Tutorial save fixture into two isolated temporary user
   roots and verify the source fixture hash before each replay.
2. Start each copy in a separate process because the legacy engine is global.
3. Drive one process through Classic scan-code/portrait inputs and the other
   through the semantic bridge using the same normalized action sequence.
4. Capture a snapshot after every settled action and compare world position,
   facing, selection, fatigue, inventory, encounter, and timing state.
5. Save both runs into new temporary slots and require byte-for-byte equality.
6. Verify both original fixture copies and their hashes are unchanged.

That follow-on test, rather than this model, is the release gate for actual
engine and save compatibility.
