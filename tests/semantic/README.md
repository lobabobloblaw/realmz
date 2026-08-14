# Exploration semantic-equivalence harness

This focused harness compares the smallest migrated exploration command set
without reading or writing a Realmz character or save file:

- all 12 `MovementCommand` values;
- outdoor arrow/keypad scan codes from `realmz_orig/checkkeypad.c`;
- first-person dungeon movement keys from `realmz_orig/threed.c`;
- party portrait selection behavior modeled on `realmz_orig/buttonchoice.c`;
- the Classic `i` key and typed selected-member `OpenInventoryAction`;
- the Classic `s` key and guarded selected-member `OpenSpellbookAction`;
- the Classic Game > Save Current Game menu choice `(129, 3)` and typed
  member-free `OpenSaveGameAction`;
- the Classic Game > Revert To A Previous Game choice `(129, 2)` and typed
  member-free `OpenLoadGameAction`;
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
production runtime connects eligible `MovePartyAction`,
`SelectPartyMemberAction`, `OpenInventoryAction`, `OpenSpellbookAction`, and
`OpenSaveGameAction`/`OpenLoadGameAction` commands to the guarded legacy event
loop. The fixture
models deterministic exploration mutations, the inventory-screen transition,
the spellbook modal request, both slot-chooser requests, and a stable byte image;
opening any compatibility flow leaves its snapshot and save-facing bytes
unchanged. It does not select a save slot, replace live state, reproduce, or
alter Realmz's binary save format.

The first two bounded combat actions are covered outside this exploration
fixture by the presentation, runtime-bridge, semantic-boundary, top-level-loop,
and keyboard contract tests. They verify that typed `GuardCombatantAction` and
`FinishCombatantAction` commands each carry the stable acting-combatant ID, are
late-validated against the fresh acting party member, and become the exact
Classic `g` and `f` key records. The preserved Classic handlers still own the
resulting combat-state mutations and turn advance. This is not a full combat
replay or save-equivalence claim; every other combat command remains on the
Classic input route.

`SemanticCombatLegacyAdapterTest` closes the narrow adapter-composition seam:
fixture-owned legacy globals flow through the real presentation-context and
snapshot adapters before the semantic boundary emits those exact key records.
It performs no Classic combat mutation and reads or writes no user data.

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
