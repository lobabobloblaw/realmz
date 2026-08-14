# Semantic action-equivalence harnesses

## Exploration

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

## Combat

The combat harness compares the direct Classic-key mapper request with the
typed-action path through the real runtime bridge and semantic input boundary
for Guard, Finish, Delay, Center, Switch Weapon, Center Previous, Center Next,
Combat Items, and Auto. Test-owned semantic-event sinks retain the tags instead
of running the production WindowManager/EventManager queue. Its test-owned
pre-Classic state image verifies stable acting combatant and selected-member
identities, exact semantic tags and Classic key records, unchanged fixture
bytes, single-use consumption, and fail-closed stale identity rejection.

Run the strict standalone check from the repository root:

```sh
tests/semantic/run-combat-action-equivalence.sh
```

Sanitizers and compiler selection use the same environment variables as the
exploration runner:

```sh
REALMZ_ENABLE_SANITIZERS=1 \
  tests/semantic/run-combat-action-equivalence.sh
CXX=g++ tests/semantic/run-combat-action-equivalence.sh
```

This is a contract-level, pre-Classic fixture. It does not run a live Classic
combat mutation, choose or use an item, replay a modal, execute Auto's
automation or turn, exercise RNG, or claim save equivalence.

## Classic combat-focus helper

This characterization fixture compiles and executes the unchanged
`src/realmz_orig/centerstage.c` helper with test-owned Classic globals and
presentation stubs. It covers Previous, Next, and current-focus queue
navigation; queue wrapping and empty-slot skipping; live party-member and
monster centering; Classic spell-coordinate offsets, sound, body refresh, and
combat-info bracketing; dead-target behavior; bounded empty-queue termination;
and repeat-run determinism.

Run the strict standalone check from the repository root:

```sh
tests/semantic/run-legacy-combat-focus.sh
```

Sanitizers and compiler selection are available through the same environment
style as the action-equivalence runners:

```sh
REALMZ_ENABLE_SANITIZERS=1 tests/semantic/run-legacy-combat-focus.sh
CC=clang CXX=clang++ tests/semantic/run-legacy-combat-focus.sh
```

This helper-level fixture does not inject a keyDown event or execute the
Classic combat loop. It does not characterize modal interaction, RNG, turn
effects, full combat replay, or save equivalence.

## Boundary of these results

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

The first nine bounded combat controls are covered by the combat fixture and
the presentation, runtime-bridge, semantic-boundary, top-level-loop, and
keyboard contract tests. They verify that typed `GuardCombatantAction`,
`FinishCombatantAction`, `DelayCombatantAction`, and
`CenterActiveCombatantAction`, `SwitchWeaponSetAction`,
`CycleCombatFocusAction`, and `AutoCombatantAction` commands each carry the
stable acting-combatant ID and are late-validated against the fresh acting
party member with fail-closed rejection. The cycle command also preserves its
Previous or Next direction.
`OpenCombatItemsAction` carries both the acting ID and selected party-member ID;
delivery additionally requires that the same member still exists and remains
selected.
Delay is available only before
movement and revalidates that eligibility before becoming the exact Classic
`d` message `0x00000264`; Center becomes the exact Classic `c` message
`0x00000863`, Switch Weapon becomes the exact lowercase `w` message
`0x00000D77`, Center Previous becomes the exact Classic `p` message
`0x00002370`, Center Next becomes the exact Classic `n` message `0x00002D6E`,
and Guard and Finish become the exact Classic `g` and `f` key records. Switch
Weapon carries no desired set, focus cycling carries no absolute
destination. The preserved Classic handlers still own the Guard, Finish, and
Delay combat-state mutations and turn advance, Center's existing camera
sequence, Weapon's live relative toggle and feedback, and the focus queue's
relative destination. Combat Items becomes the exact Classic `i` message
`0x00002269`; Classic owns all further modal selection, item use and mutation,
targeting, and any attack, movement, or turn effects. Auto becomes the exact
Classic `a` message `0x00000061`; Classic owns its automation, RNG, animation
and movement or attack mutations, and turn effects. This is not a camera,
modal, automation, RNG, turn, or full combat replay, nor a save-equivalence
claim; every other combat command remains on the Classic input route.

`SemanticCombatLegacyAdapterTest` closes the narrow adapter-composition seam:
fixture-owned legacy globals flow through the real presentation-context and
snapshot adapters before the semantic boundary emits those exact key records.
It covers stale acting-combatant and non-gameplay-window rejection for all
nine command records, plus stale selected-member rejection for Combat Items,
while leaving its output sentinel unchanged. It performs no Classic combat or
inventory mutation, reads or writes no user data, and is not a camera, modal,
automation, RNG, turn, full combat replay, or save-equivalence test.

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
