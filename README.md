# Realmz Remastered — Unofficial

This repository is an unofficial, free, noncommercial evolution of Realmz,
forked from upstream commit `4089d550ab606172bac850ac055677c36c6ff547`.
It preserves the native SDL3/Cocoa engine, game rules, scenario/save formats,
and complete Classic presentation while introducing a live Classic/Remastered
renderer boundary and an independently reviewed asset pipeline. Selected
bitmap resources now pass through a pack-aware runtime coverage check after
the legacy search chain chooses their source fork; Classic mode bypasses this
check entirely. The code-native Remastered shell separately resolves its four
surface materials by public manifest key and never reads a Classic resource
payload to choose them.

Remastered mode currently runs a responsive compatibility shell with a
1024×768 minimum. During exploration, dungeon play, and combat, it uniformly
fits the complete interactive 800×600 Classic framebuffer into the gameplay
area and uses widescreen space for an interactive party rail, semantic action
controls, details, and event log (or compact drawer tabs). Eligible outdoor and
dungeon movement buttons dispatch typed `MovePartyAction` commands through the
legacy event loop. On those eligible exploration and dungeon screens, party
cards dispatch typed, idempotent
`SelectPartyMemberAction` commands through the same guarded top-level route;
selecting the active member never emulates the Classic second click that opens
the character modal. World commands are organized into persistent, directly
selectable **Travel**, **Party**, and **Game** pages so every target keeps the
44-point minimum at the 1024×768 floor. Party contains Items, Spells, Scroll,
and Character; Game contains Save, Load, and Rest. Selecting the current page
is an idempotent presentation action.
A code-native Items control carries the selected member in
a typed `OpenInventoryAction`. A neighboring Spells control is available only
for a conscious selected member with spell points and carries that member in a
typed `OpenSpellbookAction`. The guarded top-level route revalidates either
selection before translating it to the preserved Classic `i` or `s` key, so the
inventory and spell-selection screens remain intact compatibility flows. The
non-combat Use Scroll control carries the selected member in a distinct typed
`OpenScrollCaseAction`. It is available only while that member has positive
stamina, an equipped scroll case, and no spell flow already in progress; a
fresh guarded handoff revalidates the selection and eligibility, then emits
Classic's exact outdoor `l` message or dungeon `p` message. It deliberately
does not inspect the five case entries:
an equipped empty case still opens the preserved chooser, and Classic owns all
browsing, selection, targeting, and consumption after handoff. Save and Load
controls carry distinct typed `OpenSaveGameAction`
and `OpenLoadGameAction` commands. After late surface validation, they become
the exact preserved Game > Save Current Game `(129, 3)` and Game > Revert To A
Previous Game `(129, 2)` choices. Each opens the Classic slot chooser; neither
semantic action chooses a slot, writes save data, or replaces engine state.
The Character control carries the exact selected member in an
`OpenCharacterSheetAction`. After fresh surface and selection checks, its tag
becomes a neutral one-shot `app1Evt`; the outdoor or dungeon outer loop then
revalidates both Classic selection variables and enters the existing
`charmainbut`/`buttonchoice` path. No mouse click, dungeon key shortcut, or
synchronous modal call is forged. Classic remains authoritative for the sheet,
all browsing, and every nested modal.
The member-free `RestPartyAction` is available only while the party is already
in camp. Its single-use tag is late-validated against the completed semantic
scope and freshly captured Legacy and snapshot state: adaptive eligibility,
the exact exploration or dungeon surface, the matching outdoor or dungeon
map/first-person presentation, and `in_camp` must all still hold. It then
becomes Classic's exact lowercase `r` message `0x00000F72`. Pointer activation
is dispatched after release; keyboard and pointer delivery are both rejected
when EventManager's non-pumping cached SDL/Classic state reports a held mouse
button. An accepted activation therefore begins with one mandatory iteration
of Classic's preserved Rest loop rather than synthesizing a hold. A distinct
physical press after delivery remains ordinary Classic input. Classic remains
authoritative for the rest sound, fatigue update, elapsed time and resulting
encounters, and the preserved `revertgame` exit.
The adjacent member-free Camp control carries an explicit desired state in
`SetCampStateAction`: Camp requests `true`, while Break Camp requests `false`.
It is deferred only during ordinary outdoor or dungeon navigation with no
active encounter. Its single-use world tag is late-validated against a fresh
adaptive screen, the exact outdoor or dungeon map/first-person presentation,
and a current camp state still opposite to the requested state. Only then does
it become Classic's exact lowercase `c` message `0x00000863`. Encoding the
desired state prevents a stale Camp activation from breaking a newly made camp
or a stale Break Camp activation from re-entering one. The remastered route
does not pre-evaluate Classic's historically inverted `cancamp` permission
(`false` permits entry and `true` denies it) or add replay vocabulary. Classic
remains authoritative for denial feedback,
music and sound, camp and movement state, time advancement, control refresh,
and the preserved `revertgame` exits.
The GAME page then orders SAVE, LOAD, REST, CAMP/BREAK CAMP,
SEARCH/STOP SEARCH, TORCH, and the contextual AREA SEARCH/MAKE SCROLL control.
Its Search control is available during ordinary
outdoor or
dungeon navigation with no active encounter, including while camped. It shows
`SEARCH` with accessibility text `Start searching` when searching is off and
`STOP SEARCH` with `Stop searching` when searching is on. The typed
`SetSearchStateAction` carries the absolute desired searching state rather than
a relative toggle. Its single-use `0x5753` tag carries the originating world
surface plus a strict Boolean destination and is late-validated against the
fresh adaptive screen, exact outdoor or dungeon map/first-person presentation,
and a current any-nonzero Classic searching state still opposite to the
request. A real live Classic `search` control is also required. Acceptance
produces one neutral `app1Evt` sideband; it forges no key or pointer event and
does not mutate search state directly. The preserved `buttonchoice` path with
`theControl == search` alone owns sound, state, and icon changes. Secret checks and their search
time cost still occur only when subsequent Classic movement invokes
`checkforsecret`. The action adds no replay vocabulary.
The adjacent typed `UseTorchAction` is a one-shot request. Its optional
`TorchSource` carries the member and slot of the first exact item 805 when that
item has a positive charge. It is a freshness locator, not a stable item
identity, and deliberately carries neither charge nor current Light duration.
An empty or negative-charge first exact match blocks every later match, exactly
as Classic does. The single-use `0x5754` tag binds that locator to its outdoor
or dungeon origin. Late validation requires the same
freshly captured first usable source, exact outdoor or dungeon map/first-person
presentation, and a live real Classic `torch` control before a neutral
`app1Evt` enters the preserved `buttonchoice` branch. The action is unavailable
when no usable source exists, but remains valid while camped, searching, or
already lit. It forges no key or pointer, performs no direct inventory or Light
mutation, and adds no `timeclick` or replay vocabulary. Classic alone owns
charge consumption, item dropping and slot shifts, item/spell loading, RNG,
sound, Light duration, darkness, and icon updates. Automated tests do not close
the private manual-QA gap: a disposable private outdoor fixture and both
dungeon presentations must still confirm those effects without redistributing
fixture data.
The final GAME control is a single discriminated
`ContextualOverviewAction`. Outside camp it carries the explicit
`area_search` mode with no member; in camp it carries `make_scroll` plus the
member selected when the control was composed. A disabled Make Scroll control
may carry no member, but that payload is never dispatchable. Its strict
single-use `0x574F` tag binds the originating outdoor or dungeon surface, mode,
and canonical absent-or-bounded member. Fresh validation requires the same
adaptive surface and outdoor or dungeon map/first-person presentation, an
unchanged camp mode, and, for Make Scroll, the same selected member with the
live case/stamina/spell-flow capability. A stale activation can therefore
neither change from Area Search to Make Scroll nor retarget another member.
Acceptance produces only Classic's exact lowercase `a` key record
`0x00000061` for Area Search or lowercase `k` record `0x0000286B` for Make
Scroll. EventManager applies its non-pumping cached SDL/Classic held-mouse gate
only to Area Search, whose preserved `Button()` loop can repeat; Make Scroll is
not subject to that extra restriction. No pointer, control handle, direct
secret/party/item/spell mutation, Classic-source change, or replay vocabulary
is introduced. Classic remains authoritative for contextual control gating and
feedback, forced secret discovery, sound, time, random encounters and revert
handling, and the complete scroll modal, caster checks, spell-point and
parchment effects, slot selection, and cleanup. Automated tests do not replace
private, no-redistribution manual QA of both modes on disposable outdoor and
dungeon fixtures.
During combat, code-native Guard, Finish, Delay, Center, Switch Weapon, Center
Previous/Next, Auto, Range, Bandage, Undo, Cast, Target, Escape, Use Scroll,
and Center Cursor controls carry the stable active-party combatant ID in typed
`GuardCombatantAction`, `FinishCombatantAction`, `DelayCombatantAction`,
`CenterActiveCombatantAction`, `SwitchWeaponSetAction`,
`CycleCombatFocusAction`, `AutoCombatantAction`,
`ShowCombatRangeAction`, `BandageCombatantAction`, and `UndoCombatantAction`
commands. Combat Cast and Target carry that actor in distinct
`OpenCombatSpellbookAction` and `OpenCombatTargetingAction` commands. Cast does
not reuse the exploration `OpenSpellbookAction` or choose a spell or target;
Target identifies no recipient or cell. An actor-only `EscapeCombatAction`
requests Classic's existing escape attempt without predicting its result. The
actor-only `OpenCombatScrollCaseAction` opens the equipped combat scroll-case
chooser without selecting a scroll, spell, power, or target. The
`CenterCombatCursorAction` additionally carries the absolute 0–89 battlefield
cell sampled from the visible Classic gameplay crop; it never reuses the
ambient mouse point. The
combat Items control carries both that acting ID and the stable selected
party-member ID in an `OpenCombatItemsAction`. Their combat-only guarded routes
late-validate the fresh acting combatant and fail closed before returning the
exact preserved Classic key record; Items also revalidates that the same member
still exists and remains selected. Delay is
available only before movement and revalidates that
eligibility before returning the exact Classic `d` message `0x00000264`; Center
returns the exact Classic `c` message `0x00000863`, and Switch Weapon returns
the exact Classic `w` message `0x00000D77`. Center Previous returns the exact
Classic `p` message `0x00002370`, while Center Next returns the exact Classic
`n` message `0x00002D6E`; Combat Items returns the exact Classic `i` message
`0x00002269`, Auto returns the exact Classic `a` message `0x00000061`, Range
returns the exact Classic `r` message `0x00000F72`, and Bandage—available only
while Classic's current-turn bandage gate remains open—returns the exact
Classic `b` message `0x00000B62`. Undo is independently late-gated by the same
fresh Classic `canundo` state and returns the exact Classic `u` message
`0x00002075`. Combat Cast revalidates a read-only projection of Classic's
current `cancast` prerequisites and returns the exact Classic `s` message
`0x00000173`. Combat Target mirrors Classic's visible Target-button gate and
returns the exact Classic `t` message `0x00001174`. Combat Escape uses only the
fresh live-actor gate and returns the exact Classic `e` message `0x00000E65`.
Use Scroll mirrors Classic's visible equipped-case gate and returns the exact
Classic `l` message `0x0000256C`.
Center Cursor is exposed only while a fresh battlefield hover remains bound to
the same actor and viewport. That sample survives only the in-window trip
across Classic or shell chrome and is cleared outside the window. It returns
the exact Classic `m` message
`0x00002E6D` with its cell staged separately from `EventRecord.where`; physical
`m` input keeps Classic's original mouse-point behavior.
The weapon action does not encode a desired set, and focus
cycling does not encode a destination. The preserved Classic handlers
remain authoritative for the Guard, Finish, and Delay combat-state mutations
and turn advance, Center's existing camera sequence, Weapon's live relative
toggle and feedback, the focus queue's relative destination resolution, and
the complete Items modal—including selection, use, mutation, targeting, and
any resulting attack, movement, or turn effects. Classic also owns every Auto
decision, random choice, animation/movement or attack mutation, and turn
effect. Classic owns Range's overlay drawing, event flush, raw mouse/key
dismissal wait, recentering, and redraw path after the key handoff. Classic also
owns Bandage's raw party-member picker, abort behavior, bleeding mutation,
portrait refresh, and turn advance. Classic also owns Undo's further condition
checks and every position, field, queue, redraw, and turn-state mutation.
Classic owns Combat Cast's authoritative `cancast` check, spell and power
chooser, targeting loops, spell-point charges and refunds, RNG, resolution,
redraws, movement/attack costs, and turn handling. Classic also owns Target's
live equipment and quiver resolution, target-mode selection, charge consumption
and item drops, RNG, raw target loops, abort/launch costs, spell resolution,
redraws, and turn handling. Classic owns Escape's range calculation, warning
precedence, raw confirmation dialog, confirmed field/queue/position and
prestige mutations, last-loyal-member handling, and subsequent turn flow.
Classic owns Use Scroll's modal character and slot browsing, selection and
early scroll consumption, spell validation, targeting, RNG, spell effects,
movement/attack costs, and turn handling. Cancelling a later target does not
restore the selected scroll.
Classic also owns Center Cursor's existing `centerfield` bounds, camera,
redraw, range-overlay, and button effects after the absolute cell handoff.
These bounded routes are not a camera,
range-overlay, bandage-target, spell-selection, target-selection, undo mutation,
escape confirmation, scroll selection, combat, modal, automation, RNG, turn,
or save replay;
other combat commands remain inside the interactive Classic frame.
The combat action bar presents these seventeen commands through four
persistent, directly selectable groups: **Turn**, **Gear**, **Tactics**, and
**Special**. During an eligible live-party turn, all four named tabs remain
visible on every combat command page, the current tab has a double border and
underline in addition to its color, and selecting it again is an idempotent
presentation action. This replaces the prototype's relative More/Back paging
without changing any combat payload, combat-command eligibility check, or
legacy handoff. The code-native controls share a
keyboard route with wrapping Tab and Shift-Tab focus plus
Return or Space activation, suppresses repeat dispatch, and uses a
high-contrast non-color focus outline. In compact layouts the
read-only Details and Event Log surfaces open and close through pointer or the
same keyboard route; their tabs expose an explicit open-state label in addition
to color. The selected-member Details inspector is a bounded, read-only
renderer milestone shared by the wide panel and compact Details drawer. It
shows the detached model's stamina and spell-point values, consciousness,
armor, movement, and complete normalized condition/status sequence with
explicit marker text rather than color alone. When compact space cannot display
every state, the visible summary ends with `+N` for the exact number elided;
the complete semantic sequence remains retained and no state is silently
dropped. This inspector adds no command, legacy handoff, or game-state mutation,
and it does not make the cropped gameplay frame, Event Log, or remaining action
migration complete.

Gameplay-frame cropping is governed by a bounded, fail-closed gameplay-chrome
coverage contract. Its inventory treats outdoor exploration, dungeon play, and
combat as separate surfaces, while inventory validity and completeness remain
global: every Classic interaction and essential-information role on every
surface must be present and valid before any surface may be crop-ready. Every
surface/role entry has exactly one status: `retained_in_crop` when the complete
Classic role and all of its required pixels and hit area remain inside the
crop; `semantic_complete` when a code-native control or information surface has
a complete detached-model, rendering, input, guarded-handler, and fallback
contract; or `missing` when either proof is incomplete. A declared action type,
captured field, placeholder panel, or visually similar summary is not by itself
`semantic_complete`. The inventory revision covers every manifest field,
including status and evidence, and every cited Classic UI/control-map source
anchor; changing any of them requires a new reviewed revision.

Before a future production call site may request the crop, readiness must be a
deterministic conjunction over independently established evidence. The static
inventory must have no missing, unknown, duplicate, or unaccounted role. A typed
expected context variant must equal the typed live variant; only the ordinary
`standard` gameplay variant is currently recognized. Camp, nested or modal,
spell, targeting, and text-entry variants are explicitly unsupported and retain
the full frame. Legacy evidence must also establish the expected gameplay
window, the correct front-window relationship, and every granular reason that
can require full-frame fallback. Snapshot and shell-model revisions must match
and both be nonzero, and the shell model's context must match the live and
snapshot contexts.

Model, layout, font, expected-control, live-handler, and information-completeness
values are fail-closed evidence inputs to the evaluator, not proof generated by
the evaluator itself. No authoritative production evidence builder exists yet,
so an evaluator result alone is not authorized to request cropping. A future
builder must prove those inputs from current runtime objects, including complete
non-overlapping layout, one guarded handler for every visible control, and
authoritative capture, retention, and rendering for every information role. Any
false, unknown, absent, zero-revision, or mismatched input retains the complete
800×600 frame. Production continues to hardcode
`semantic_controls_ready` to `false`, so no cropped gameplay route is enabled.

The current 95-row inventory remains deliberately incomplete: six roles are
`retained_in_crop`, 45 are `semantic_complete`, and 44 remain `missing`, so
cropping stays disabled. Known missing outdoor and dungeon roles include Heal,
context-sensitive Shop/Temple/seamless-encounter entry,
Trade, Money/Swap, active-member inspection, item and
condition drilldowns, and the per-member Auto controls. Known missing combat
roles include conditional Turn Undead, per-member Auto, and the distinct
focused-combatant inspection controls for character or monster details, items,
conditions, and monster attacks. Information gaps include ordered capture and
retention of Classic messages for Event Log; pooled money and fatigue;
party-wide condition indicators; complete all-member vitals and combat values,
including armor class, spell points, and noncaster attack cadence; authoritative
coordinates, calendar/clock, and complete combined Search/Torch state (the
persistent Torch-state presentation remains absent); focused-combatant details;
and combat round and enemies-remaining counts. The bounded selected-member
Details renderer does not implicitly satisfy those distinct all-member,
party-wide, or combat roles.

Pointer, popup, text-input, and cursor
coordinates continue through the embedded Classic frame. Title and modal
flows—including inventory, spell selection, and the save/load choosers after
they open, plus shop and encounters—use an intact full-frame compatibility
fallback. Neither route
reveals additional map or combat terrain.

The remaster is still under active development. In Remastered mode, the mixed
phase-one runtime manifest now replaces 11 hash-locked, human-approved raster
resources (four UI materials, four portraits, two world/title pictures, and one
terrain icon) and leaves the other 1,509 covered resources as exact Classic
passthroughs. Presentation-mode changes rehydrate cached patterns and pictures,
so switching between Classic and Remastered does not require a restart. Every
production consumer of those public approved PNGs now uses one verified raster
loader: it reads a bounded native-path file into memory, checks its approved
SHA-256 and bounded PNG dimensions, and decodes that exact buffer. Failures
report only a stable resource key and phase before the consumer falls back.

The native shell tiles the exact approved `ppat` materials directly: 131 for
panels, 129 for ordinary controls, 128 for selected controls, and 130 for
pressed or inactive controls. Hash-bound contrast scrims retain texture while
preserving the established text palette and non-color state cues. Its party
rail can also draw only the four approved `Data Files/Portraits:cicn` resources
257, 267, 297, and 337 in their 44×44 portrait slots. Immediately before each
portrait draw, the shell asks the Resource Manager for fresh post-selection
proof of the actual winning resource. The exact pack/type/ID, immutable Classic
payload digest and approved coverage, override path, master key, logical size,
and approved PNG digest must all match the public catalog; there is no ID-only
authorization path. A custom, unapproved, other-pack, or user-edited resource
therefore remains visible as a code-native name monogram instead of borrowing
approved art.

The approved `Scenarios/Tutorial/Scenario:PICT:32128` replacement deliberately
contains no baked title text. Only that exact key, approved output digest, and
320×320 logical size are eligible for a deterministic code-rendered
`TUTORIAL` title in the reviewed plaque; a mismatch or font/composition failure
uses the complete Classic picture. Both native four-texture caches publish only
after all four textures succeed, are destroyed before their renderer, and are
invalidated on window replacement, presentation-mode changes, and SDL render-
device reset. A failed material draw is covered by its flat fill, while a failed
portrait draw uses its monogram. macOS and Windows package rules install the
same allow-listed public runtime tree.
This is an integration milestone, not full-bake or release
approval; ten attempted style-proof assets remain rejected for human art
direction, and the broader provenance/release gates remain in force.

Realmz is a classic, turn-based RPG, originally developed for early Macintosh computers. It was originally released as shareware, with additional scenarios available for purchase. Tim has graciously agreed to a release of the original code under a non-commercial license (see "License" section below).

# License

<p xmlns:cc="http://creativecommons.org/ns#">Realmz, copyright © 1994 by Tim Phillips. Modified for compatibility with modern systems (see CHANGELOG.md for detailed modification notes). Realmz and its associated software, in both source code and binary formats, its game assets, and its documentation (the Licensed Material), are distributed under the terms of the <a href="https://creativecommons.org/licenses/by-nc-sa/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/sa.svg?ref=chooser-v1" alt=""></a>. The Licensed Material is provided on an as-is basis, with no warranties of any kind.</p>

# Installing

_WARNING: This is a development build. The game may be unstable. The fork uses
its own `Realmz Remastered` application-support directory and does not write to
the original Fantasoft directory. Keep independent backups of important saves._

Existing characters and saves can be copied into the isolated directory with
the hash-verifying importer. It backs up every selected source file before
publishing any live copy, never overwrites an existing destination file, and
leaves the source tree untouched:

```sh
Realmz --import-classic-data \
  "$HOME/Library/Application Support/Fantasoft/Realmz"
```

Download the latest release for your system from the releases page. Scroll down to and expand the "Assets" section. Download the `.dmg` file for Mac, and the `.exe` or `.zip` files for Windows.

On Mac, double click the `.dmg` file you downloaded, then click and drag the Realmz bundle into your Applications folder.

On Windows, you can either use the installer wizard for automatic installation, or a ZIP archive for custom installations. To use the installer, double click the `.exe` you downloaded. Accept the license agreement, choose an install location for Realmz, and continue through the "components" section of the installer.

# Reporting Bugs

- Save the crash report file (if possible)
- Zip up your remastered userdata directory (`%AppData%\Realmz Remastered` on Windows, `~/Library/Application Support/Realmz Remastered` on Mac)
- Submit an issue to the Github repository
- Attach the crash report and archive of your userdata directory
- List the steps necessary to reproduce the bug

# Contributing

Pull requests are welcome. Upstream's [Contributing Guide](https://github.com/Realmz-Castle/realmz?tab=contributing-ov-file) remains the guide for preservation-oriented changes inherited from the native port. This unofficial fork additionally accepts changes within its documented remaster scope, subject to the compatibility, provenance, review, and noncommercial-distribution gates in [QA_AND_RELEASE.md](docs/QA_AND_RELEASE.md) and [CONTENT_PROVENANCE.md](docs/CONTENT_PROVENANCE.md).

- AI-assisted code is acceptable, but must be human reviewed by you before you submit for maintainer review.
- When modifying code under `src/realmz_orig`, please include comments indicating the changes from the original
  implementation ([example](https://github.com/Realmz-Castle/realmz/blob/fc143ecb7d54b1f7be3ff7e714fea450297b8bb9/src/realmz_orig/warn.c#L61)).

## Building on Mac

Initialize the pinned SDL dependencies, build the two pinned external packages,
then pass the verified installation prefix to Realmz:

```sh
git submodule update --init --recursive
scripts/bootstrap-macos-dependencies.sh \
  --work-dir build/dependencies \
  --prefix build/dependencies/install
cmake --preset macOS \
  -DCMAKE_PREFIX_PATH="$PWD/build/dependencies/install" \
  -DCMAKE_BUILD_TYPE=Release \
  -DREALMZ_ENABLE_APP_SANITIZERS=OFF
cmake --build build_mac --parallel
ctest --test-dir build_mac --output-on-failure
scripts/stage-macos-app.sh \
  --build-dir build_mac \
  --output-dir build/staged
scripts/verify-macos-artifact.sh --mode development \
  "build/staged/Realmz Remastered — Unofficial.app"
```

The bootstrap uses fresh CMake caches, rejects modified or incorrectly pinned
dependency checkouts, and verifies universal `x86_64;arm64` libraries targeting
macOS 13.3 before returning successfully. Its reviewed external commits are
phosg `b2e0c12edb7e274a5e20c460f44eee44f49f57ef` and resource_dasm
`27f64c89a5fed855e68c2a5e97b6c6c389d8eb19` (which provides resource_file).

Run `scripts/run-core-tests.sh` for the dependency-free contracts. A configured
full build additionally registers a resource-fork integration test that parses
all five phase-one forks and verifies all 1,520 immutable selected payloads.
The staging command creates only the expanded unsigned development app. It does
not sign, notarize, publish, or invoke the `hdiutil`-backed DMG target. With the
same Release-configured development tree, `cmake --build --preset macOS`
invokes the current DMG path on a capable Mac. That remains unsigned
development packaging, not the final release build.

## Cross-compiling for Windows from Mac

- Install [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
  - Download latest llvm-mingw-$DATE-ucrt-macos-universal.tar.xz
  - Extract the archive
  - `sudo mv ~/Downloads/llvm-mingw-$DATE-ucrt-macos-universal /opt/llvm-mingw`
- Install NSIS for installer generation `brew install nsis`
- Create a [toolchain file](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Cross%20Compiling%20With%20CMake.html#toolchain-files)
- Clone and build phosg, resource_dasm, and zlib dependencies and install to ~/mingw-install
  - `cmake --fresh -B build -D CMAKE_TOOLCHAIN_FILE=~/workspace/TC-mingw.cmake -D CMAKE_INSTALL_PREFIX=~/mingw-install -D CMAKE_BUILD_TYPE=Debug`
- Set up a CMake build directory for windows using the toolchain file
  - `VERBOSE=1 cmake -B build_win -DCMAKE_BUILD_TYPE=Debug -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON -DSDLTTF_VENDORED=ON -DDISABLE_SDL:BOOL=ON -DCMAKE_TOOLCHAIN_FILE=~/TC-mingw.cmake`
- Build for windows using llvm-mingw `cmake --build build_win --target package`
