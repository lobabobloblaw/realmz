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
Combat Items, Auto, Range, Bandage, Undo, Combat Cast, Combat Target, Combat
Escape, Use Scroll, and Center Cursor.
Test-owned semantic-event sinks retain the tags instead of running the
production WindowManager/EventManager queue.
Its test-owned pre-Classic state image verifies stable acting combatant and
selected-member identities, exact semantic tags and Classic key records,
unchanged fixture bytes, single-use consumption, and fail-closed stale identity
rejection.

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
automation or turn, execute Range's overlay or raw dismissal wait, run
Bandage's party-member picker or mutation and turn advance, execute Undo's
condition checks or mutations, run Combat Cast's spell/power chooser, target
loops, costs/refunds, resolution, or turn handling, resolve Target's equipment
or quiver, consume/drop its item, choose its target mode, run its raw target
loop, pay abort/launch costs, resolve its spell, evaluate or confirm Escape,
perform flee mutations or turn flow, browse/select/consume a scroll, run its
targeting/effects/costs/turn flow, exercise RNG, or claim save equivalence.
Center Cursor equivalence likewise stops at the exact lowercase-`m` handoff and
the one-shot absolute cell; it does not reproduce `centerfield` or camera state.

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

The first seventeen bounded combat controls are covered by the combat fixture and
the presentation, runtime-bridge, semantic-boundary, top-level-loop, and
keyboard contract tests. They verify that typed `GuardCombatantAction`,
`FinishCombatantAction`, `DelayCombatantAction`, and
`CenterActiveCombatantAction`, `SwitchWeaponSetAction`,
`CycleCombatFocusAction`, `AutoCombatantAction`,
`ShowCombatRangeAction`, `BandageCombatantAction`, and `UndoCombatantAction`
commands each carry the stable acting-combatant ID and are late-validated
against the fresh acting party member with fail-closed rejection. The cycle
command also preserves its Previous or Next direction. A distinct
`OpenCombatSpellbookAction` carries only that actor and remains separate from
both the exploration `OpenSpellbookAction` and the fully specified
`CastSpellAction`. `OpenCombatTargetingAction` also carries only the actor; it
does not identify an entity, area, or cell target.
`EscapeCombatAction` carries only the actor and does not encode a range result,
warning, confirmation response, or flee outcome.
`OpenCombatScrollCaseAction` carries only the actor and does not identify a
scroll-case slot, scroll, spell, power, recipient, cell, or target.
`CenterCombatCursorAction` carries the actor and an explicit absolute 0–89
battlefield cell, never an `EventRecord.where` or ambient Classic mouse point.
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
and movement or attack mutations, and turn effects. Range becomes the exact
Classic `r` message `0x00000F72`; Classic owns its overlay drawing, event
flush, raw mouse/key dismissal wait, recentering, and redraw path. Bandage is
late-gated by the fresh Classic `canundo` state and becomes the exact Classic
`b` message `0x00000B62`; Classic owns its raw party-member picker, abort path,
bleeding mutation, portrait refresh, and turn advance. This is not a camera,
range-overlay, bandage-target, modal, automation, RNG, turn, or full combat
replay, nor a save-equivalence claim. Undo is independently late-gated by the
same fresh Classic `canundo` state and becomes the exact Classic `u` message
`0x00002075`; Classic owns its further condition checks and every position,
field, queue, redraw, and turn-state mutation. The fixture claims no undo
mutation equivalence. Combat Cast is late-gated by the fresh read-only
projection of Classic's current `cancast` prerequisites and becomes the exact
Classic `s` message `0x00000173`; Classic owns its repeated gate, chooser,
targeting, costs and refunds, RNG, resolution, redraws, and turn handling. This
fixture stops before all of those behaviors and claims no combat-spell
equivalence. Combat Target mirrors Classic's visible Target gate and becomes
the exact Classic `t` message `0x00001174`; Classic then owns equipment/quiver
resolution, target type and selection, charges/drops, RNG, raw modal input,
abort/launch costs, spell effects, redraws, and turn handling. The fixture stops
before all of those behaviors and claims no targeting equivalence. Combat Escape
uses only the fresh actor gate and becomes the exact Classic `e` message
`0x00000E65`; Classic owns its range and condition checks, warning precedence,
raw confirmation dialog, field/queue/position/prestige/coward mutations, and
turn or post-combat flow. The fixture stops at that key handoff and claims no
flee equivalence.
Use Scroll mirrors Classic's visible equipped-case gate and becomes the exact
Classic `l` message `0x0000256C`; Classic owns `getscroll`, modal browsing and
selection, immediate accepted-scroll consumption, spell validation, targeting,
RNG, effects, movement/attack costs, and turn handling. The fixture stops at
the key handoff and claims no scroll-use equivalence; every remaining command
stays on the Classic input route.
Center Cursor requires a fresh actor-bound hover sample from the visible combat
crop and becomes the exact Classic `m` message `0x00002E6D`. The sample is
retained across in-window shell or Classic chrome, cleared for outside-window
or invalid pointer targets, and revalidated before use. Its absolute cell is
staged once outside the key record, so a camera move cannot retarget it and a
later physical `m` cannot reuse it. Classic retains `centerfield` and all
camera and redraw effects; the fixture claims only the key-and-cell handoff.

`SemanticCombatLegacyAdapterTest` closes the narrow adapter-composition seam:
fixture-owned legacy globals flow through the real presentation-context and
snapshot adapters before the semantic boundary emits those exact key records.
It covers stale acting-combatant and non-gameplay-window rejection for all
seventeen command records, plus stale selected-member rejection for Combat Items,
while leaving its output sentinel unchanged. It performs no Classic combat or
inventory mutation, reads or writes no user data, and is not a camera, modal,
automation, RNG, turn, full combat replay, or save-equivalence test.

`SemanticTopLevelLoopContractTest` pins the untouched Classic `r`, `b`, `u`,
`s`, `t`, `e`, `l`, and `m` branches and their ownership: the semantic gameplay
scope has ended before each late key handoff, `showrange()` owns its raw
dismissal wait,
`getchoice()` owns Bandage's raw member picker, Classic owns Undo's condition
checks and mutations, the combat `s` branch still selects `castspellsbut`, and
the `t` branch still selects `combatitem` before the shared `combatchoice()` path.
Classic's `e` branch still owns `getrange`, warning precedence, raw `question3`
confirmation, confirmed flee mutations, and `getup`.
Classic's `l` branch still selects `viewspellsbut`, then `combatchoice` owns
`getscroll`, its modal loop, accepted-scroll consumption, the shared `wand`
target/effect path, abort/success costs, and turn decision.
Classic's `m` branch consumes a semantic absolute cell at most once and falls
back unchanged to `point.h / 32`, `point.v / 32` for physical input; no Classic
mouse hit map synthesizes `m`.
Inactive-surface dequeue rejects later shell
gameplay tags. This is source-contract evidence, not executable range-overlay,
Bandage, Undo, Combat Cast, Combat Target, Combat Escape, Use Scroll, or modal
replay.

## Replay foundation

Center Cursor is the terminal bounded production handler in the current
ordered semantic-control slice. Live engine equivalence is a separate, larger
milestone; it is no longer waiting on another handler in that slice.

Step 1 plumbing for that milestone is available in
`scripts/semantic_replay_fixture.py`, with its versioned contract in
`replay-fixture-manifest.schema.json`. A caller provides a manifest and source
root explicitly; there is no default save location. The manifest records a
source class, authorization basis, redistribution decision, and Classic slot;
its domain-separated tree digest binds the canonical path/size/SHA-256 census,
whose aggregate declared size is capped at 1 GiB. Private or user-owned
fixture bytes remain outside the repository.

Verify a declared fixture without modifying it:

```sh
python3 scripts/semantic_replay_fixture.py verify \
  --manifest /path/to/fixture-manifest.json \
  --source-root /path/to/authorized-fixture
```

Or stage it into two new, independent roots for later Classic and semantic
runs:

```sh
install -d -m 700 \
  /new/temporary/classic-user-root/Save \
  /new/temporary/semantic-user-root/Save

python3 scripts/semantic_replay_fixture.py stage \
  --manifest /path/to/fixture-manifest.json \
  --source-root /path/to/authorized-fixture \
  --classic-root '/new/temporary/classic-user-root/Save/Game A' \
  --semantic-root '/new/temporary/semantic-user-root/Save/Game A'
```

The example manifest declares slot `A`. Its two `Save` parents are explicit
caller-owned trust boundaries, and the new `Game A` paths become the isolated
input slots. The process runner receives the two user-root paths.

The verifier and stager are descriptor-anchored and fail closed on symlinks,
special files, mutations, or untrusted ancestor replacement races. The stager
refuses existing, aliased, or nested roots, verifies independent byte-exact
copies, and rechecks the source after staging. Destination parents are an
explicit trust boundary: they must be owned by the current user and must not
be group- or world-writable; a same-user process able to mutate one is outside
the threat model. Copies are assembled under random mode-0700 sibling names
and published with the platform's native atomic no-replace rename; platforms
lacking the required filesystem operations are rejected.

Publishing both final roots cannot be one atomic operation. On any failure,
the stager retains its private staging roots and any first root already
published and reports their last known names; it performs no rollback
deletion. A namespace-tainted name may be missing or may refer to a
replacement because no race-free path recovery exists after another process
renames an open directory. Retained roots may contain private fixture bytes
and require deliberate caller cleanup after the parent namespace and reported
names have been reviewed.

Step 2 process isolation is available in `scripts/semantic_replay_runner.py`.
The four `semantic-replay-*.schema.json` files close its version-1 request,
child-config, child-result, and run-envelope contracts. A request explicitly
names one physical executable, the two distinct user-data roots, existing input
and fresh output slots, normalized actions, a timeout, and fixed 64-bit RNG seed
and stream values. Action identifiers and keys use normalized lowercase ASCII;
string argument values use printable ASCII so both protocol implementations
apply the same dependency-free validation. Run it with:

```sh
python3 scripts/semantic_replay_runner.py \
  --request /path/to/semantic-replay-request.json
```

The parent launches the requested absolute executable path twice, without a
shell, in separate Classic-then-semantic process groups. It pins independent
user-data roots, Classic and Remastered presentation, disabled preference
writes, no bundled save fallback, fresh output slots, identical actions and RNG
inputs, and a settlement barrier at the next semantic gameplay poll. Private
configs and strict child results are bounded and identity-checked. Root-level
engine working files may change without invalidating the user-root identity,
while its direct parent namespace and staged input slot remain mutation-pinned.
Higher shared ancestors remain identity-pinned, so unrelated sibling activity
does not invalidate a run. The named executable and higher same-user filesystem
namespace are trusted: a same-user actor can transiently redirect a higher
ancestor between identity checkpoints. Separate process sessions isolate global
engine state and support same-session process termination, but do not sandbox a
child that deliberately daemonizes.

Every mode-`0700` protocol workspace is retained and reported under
`workspace_retention`; automatic recursive cleanup is forbidden because a
same-user process could replace the pathname between checking and deletion.
Clean up only an authoritative reported path after inspection. A
non-authoritative candidate may be a replacement while the original workspace
remains under an unknown renamed path. Protocol workspaces contain configs and
results, not staged fixture bytes.

The Realmz binary now recognizes `--semantic-replay-child CONFIG`. Its bounded
native bootstrap validates and installs the v1 policy before `ToolBoxInit`,
selects the isolated root and presentation, disables preference persistence and
bundled fallback for the staged input subtree, and supplies deterministic RNG
draws. It then exits with the explicit driver-unavailable status and writes no
result: save loading, action driving, settled snapshots, and save emission are
still pending. The parent tests therefore continue to use a synthetic child and
do not compare its reported state or save hashes.

The synthetic replay foundation suites run directly and through the project
quality gates on both Linux and macOS:

```sh
python3 -m unittest discover \
  -s tests/semantic \
  -p 'test_semantic_replay_*.py' \
  -v
```

Successful fixture and runner results intentionally report
`"semantic_equivalence":"not_evaluated"`; runner results additionally report
`"runner_scope":"process_isolation_only"`. The foundation therefore makes no
live engine or save-equivalence claim.

## Live replay roadmap

The remaining milestone work is to:

1. Select a provenance-reviewed Tutorial fixture manifest and use the
   foundation to verify the source and create isolated Classic and semantic
   copies. This is byte-identity and isolation plumbing only.
2. Extend the native `--semantic-replay-child` bootstrap to load the configured
   input slot and emit a result only after all actions and the fresh output save
   complete.
3. Drive one process through Classic scan-code/portrait inputs and the other
   through the semantic bridge using the same normalized action sequence.
4. Capture a snapshot after every settled action and compare world position,
   facing, selection, fatigue, inventory, encounter, and timing state.
5. Save both runs into new temporary slots and require byte-for-byte equality.
6. Verify the source and both staged fixture trees remain bound to their
   declared hashes.

That future live test, rather than the current contract fixtures or staging
foundation, is the release gate for actual engine and save compatibility.
