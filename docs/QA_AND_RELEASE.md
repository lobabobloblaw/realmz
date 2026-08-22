# QA and release gates

This document is the operational release contract for Realmz Remastered — Unofficial. The current repository is an implementation baseline, not a releasable product: engine-level replay tests, approved phase-one artwork, authorized City data, cleared music, signing, and notarization are still required. A pinned universal dependency bootstrap and a copy-only, hash-verified user-data importer are implemented, but the importer release-fixture matrix and first-run UI remain acceptance work.

## Fast checks available now

From the repository root:

```sh
scripts/verify-source-baseline.sh --mode development
scripts/run-core-tests.sh
python3 -m unittest discover -s tests/release -p 'test_*.py' -v
```

The source verifier is read-only. Development mode reports local changes but does not fail because of them. It does fail if source ancestry, dependency pins, a partially landed asset census, or a present provenance record is invalid.

`run-core-tests.sh` compiles dependency-free tests directly with `${CXX:-c++}` in C++23 mode (using the `c++2b` spelling accepted by the macOS 14 toolchain) and warning-as-error flags. Aggregate fixtures deliberately rely on default member initialization, so `missing-field-initializers` is the sole disabled warning. Temporary executables are created under `mktemp` and removed on exit. It covers presentation routing, adaptive-shell geometry, input transforms and pointer capture, legacy screen-context classification, party/action accessibility models, 128 state-invariant mode switches, deterministic resource failures, live legacy-snapshot copying, exact native-material and proof-bound portrait catalog bindings, exhaustive material/text contrast, asset validation, and user-data safety. The portrait catalog test also proves that no ID-only authorization API exists and that forged, wrong-payload, other-pack, passthrough, coverage-failure, and changed metadata proofs fail closed. The script deliberately does not configure the full SDL application.

The same command exercises the synthetic semantic-replay fixture, process, and
equivalence protocols. `scripts/semantic_replay_equivalence.py` now composes a
provenance-pinned fixture, two real child routes, continuous input attestation,
and exact state/save/action/RNG comparison into a fail-closed local verdict.
Its census performs no explicit content writes (filesystem reads may update
access-time metadata) and produces unreviewed mechanical fixture evidence. Its
request pins both the reviewed manifest bytes and fixture tree, and
`--inspect-profile` validates the exact versioned native action digest without
staging or launching children. Schema 1 remains movement-only and byte-stable;
schema 2 preserves those records and adds bounded party selection; schema 3
preserves both earlier vocabularies and adds actor-bound weapon-set switching.
A configured build preserves all 12 native movement commands under schema 1,
repeats them in schema-2 plans that also cover changed and idempotent party
selection, and exercises the schema-3 switch through both linked production
routes. Native state tests additionally require an eligible, state-changing
combatant and the requested member's alternate-weapon-set state to flip before
settlement.
No private Tutorial fixture is selected in the repository, so no current CI
result establishes real-engine equivalence. Separately reviewed private outdoor
and first-person dungeon profiles have completed the real gate; their narrowly
scoped, non-content identities and results are recorded in the
[outdoor receipt](CONTENT_PROVENANCE.md#private-outdoor-replay-receipt-2026-08-21)
and [dungeon receipt](CONTENT_PROVENANCE.md#private-first-person-dungeon-replay-receipt-2026-08-21).
Together they exercise all 12 native-v1 movement command names, but they bind
different exact fixtures and executables and are not a combined CI,
current-HEAD, or release-wide verdict. Broader action profiles and City coverage
remain separate acceptance work. A separate
[schema-2 party-selection receipt](CONTENT_PROVENANCE.md#private-schema-v2-party-selection-replay-receipt-2026-08-21)
covers changed and idempotent selection of one member on one exact private
dungeon fixture; it establishes no movement or broader selection equivalence.
Schema 3 has no accepted real-engine receipt; it still requires a separately
reviewed combat fixture whose requested weapon switch can change canonical
state.
Follow
[`docs/SEMANTIC_REPLAY_RUNBOOK.md`](SEMANTIC_REPLAY_RUNBOOK.md) for each private
gate and archive its canonical envelope outside the repository.

A configured full build also registers `ResourceForkSelectionIntegrationTest`.
That test parses the five real phase-one resource forks and proves that all
1,520 immutable selected payloads resolve through the live pack-aware hook as
11 hash-verified approved replacements or 1,509 exact Classic passthroughs.
Classic runtime selection never opens the manifest or hashes payloads;
Remastered selection runs only after the legacy resource chain has selected the
owning fork. Switching Presentation mode reloads the cached UI patterns and
background pictures, so Classic and Remastered can be compared in one running
session without retaining stale raster handles.

Native shell surfaces do not use that post-selection hook. They resolve the
four public `ppat` keys 128–131 directly from the bundled runtime manifest and
census after the manifest has validated all 11 approved public outputs. The
cache uses the same verified-raster loader as QuickDraw's approved post-
selection replacements and the native portrait cache: it reads each native
path into bounded memory, rechecks its approved SHA-256 and bounded PNG
dimensions, and decodes that exact buffer. It sets linear sampling and only
then publishes the renderer-owned cache. No Classic
resource handle or private resource-fork payload participates. Any catalog,
digest, decode, sampling, or texture creation failure leaves the complete
native shell on its existing flat fills; diagnostics do not disclose absolute
host paths. An individual tiled-render failure is immediately covered by the
same flat surface color.

The native portrait cache atomically realizes the four exact approved
`Data Files/Portraits:cicn` keys 257, 267, 297, and 337 at a common physical-to-
logical scale for 44×44 party-card slots. Realizing public catalog data does not
authorize a draw. Each draw obtains fresh proof for the actual Resource Manager
winner and requires the exact key, immutable Classic payload digest and
approved coverage, override path, master key, logical dimensions, and approved
PNG digest. A custom or colliding ID, unapproved replacement, Classic
passthrough, pending writable change, user edit, or any mismatched proof falls
back to the member's code-native name monogram.

The approved Tutorial `PICT` 32128 receives its code-rendered `TUTORIAL` label
only when key, approved output digest, and 320×320 logical dimensions all match.
Composition is deterministic, changes only the reviewed plaque inset on a copy,
and restores the shared font's prior size and style. Failure to load or
configure the font, fit the text, or compose the copy rejects the complete
approved override and uses Classic.

A configured full test build registers headless SDL coverage for the shared
loader, both native texture caches, and the Tutorial title compositor. It covers
the seven non-material approved rasters plus isolated public-only and Unicode
paths; exact-byte substitution, missing/truncated/malformed, encoded-size,
dimension, pixel-count, and sanitized-diagnostic failures; all-or-nothing
texture construction; correct and mismatched renderer draws; malformed
destinations and proof rejection; deterministic plaque-only composition; and
cache-before-renderer destruction. Window replacement and presentation-mode
changes destroy both native caches. SDL render-device reset explicitly
invalidates both caches before repaint even when the renderer pointer remains
stable; render-target reset repaints without retaining a render target.

The development executable exposes the migration path without touching the
legacy installation:

```sh
Realmz --import-classic-data \
  "$HOME/Library/Application Support/Fantasoft/Realmz"
```

Only `Character Files` and `Save` are considered. The importer creates and
verifies a complete backup under the isolated `Realmz Remastered` destination
before it publishes any new live file, never overwrites an existing file, and
records a hash-bound manifest and completion marker.

CI mirrors the dependency-free checks on Linux and macOS.
`scripts/bootstrap-macos-dependencies.sh` supplies the hash-pinned universal
phosg/resource_file bootstrap for a full macOS 13.3 application build. An
strict macOS 14/Xcode 15.4 burn-in job now runs that bootstrap, builds the
universal Release-configuration application and linked tests, stages an
expanded unsigned `.app` without invoking CPack or `hdiutil`, and applies the
development artifact verifier. The app is ephemeral and is not uploaded or
published, and the workflow creates no auxiliary artifact archive. Keep the
job outside the required branch ruleset until repeated runs establish
reliability. Final DMG creation remains a separate gate that requires a
disk-image-capable runner.

## Build configurations

The eventual required matrix is:

| Configuration | Architectures | Deployment target | Purpose |
| --- | --- | --- | --- |
| Debug + ASan/UBSan | native arm64 and x86_64 jobs | 13.3 | memory, undefined behavior, save/import, scripted play |
| Release universal | x86_64 + arm64 in one artifact | 13.3 | packaging and performance |
| Current macOS | universal release | 13.3 | primary interactive and accessibility acceptance |

Sanitizers must instrument `realmz_lib`, not only the thin executable and test targets. No sanitizer runtime may be present in the release application. The universal build gate covers the main executable and every packaged non-system dylib.

Before trusting remaster data, close the character-corruption and Retina-quality blockers with deterministic fixtures and 1×/2× captures. Merely requesting an SDL high-pixel-density window is not a visual acceptance result.

The live `PresentationHost` now routes Classic and Remastered through distinct
renderer objects, and the production `LegacyGameSnapshotSource` copies party,
world, combat, inventory, and encounter state into detached DTOs. Remastered
mode now renders a live responsive compatibility shell at 1024×768 and larger.
For exploration, dungeon play, and combat, it embeds the complete 800×600
Classic framebuffer in the gameplay slot and reserves separate party, action,
details, and log surfaces populated from detached, read-only snapshot models;
below the 1360-point wide breakpoint, details and log space collapses into
drawer tabs. Eligible exploration and dungeon screens expose code-native
movement buttons with typed `MovePartyAction` payloads. Their production bridge
revalidates the live screen context on release and queues the equivalent neutral
legacy key event; it does not call movement routines or mutate engine globals.
Across outdoor exploration, dungeon map/first-person play, and combat, the same
read-only party rail renders every member in three rows: name / `Lv` / `AC`,
stamina plus `SP` or `ATK`, and the existing state summary. Detached capture
copies Classic `ac`, current/maximum spell points, `normattacks`,
`attackbonus`, and condition identifiers. Matching `updatechar`, the branch is
chosen solely by `spellpointsmax != 0`, including casters with zero current
spell points. Noncasters first compute raw `normattacks + attackbonus`, double
for Speedy condition 23, then integer-half for Slow condition 6. Adjusted
half-units `0..19` render as reduced fractions (`0/1`, `1/2`, `1/1`, through
`19/2`); all other adjusted values render `> 10`. A card may visually elide its
third row, but its layout retains complete semantic accessibility text for the
name, level, armor class, stamina, selected auxiliary vital, and every state
label without elision. Outdoor and dungeon selection controls carry that text
in accessibility-label metadata; combat cards remain noninteractive. This
slice does not claim OS publication on any surface. This is a shared
information renderer, not a new action: it defines no semantic tag or input
path and changes neither Classic sources nor replay schemas/decoders.
On eligible exploration and dungeon screens, party cards expose typed
`SelectPartyMemberAction` payloads. Their distinct tagged event is
late-validated against a fresh party snapshot, then a narrow
legacy adapter changes `charselectnew` exactly once or performs an idempotent
no-op for the selected member; it never synthesizes a portrait click or opens a
modal. The non-combat action bar uses persistent Travel, Party, and Game tabs;
their direct, idempotent page actions keep movement, four selected-member
commands, and Save/Load at the 44-point target floor. The Items and Spells
actions carry that selected member in distinct typed
and tagged commands; Spells is available only while the member is conscious and
has spell points. The same guarded top-level loop revalidates the member, live
selection, eligibility, and screen before returning the exact Classic `i` or
`s` key record. Stale queued actions therefore become inert. The nested
inventory and spell-selection screens remain unmodified and full-frame.
The non-combat Use Scroll action carries the selected member in a
distinct `OpenScrollCaseAction` and tagged event. It is available only while a
fresh snapshot shows positive stamina, an equipped scroll case, and no spell
flow already in progress. After late selection, eligibility, and surface
validation, outdoor exploration maps it to Classic's exact lowercase `l`
message `0x0000256C`; dungeon map and
first-person presentations map it to the distinct lowercase `p` message
`0x00002370`. The boundary never inspects the case's five entries, so an
equipped empty case still opens the preserved chooser. Classic remains
authoritative for browsing other eligible members, choosing a slot, targeting,
consumption, cancellation, and all mutations. The typed Save action has its
own member-free tagged event. It is
late-validated against the current exploration or dungeon surface and only then
translated to the preserved Game menu ID 129, item 3 route. That route opens
the unmodified Classic slot chooser; no slot selection or save write occurs at
the semantic boundary. The neighboring typed Load action follows an independent
member-free tag and translates only to Game menu ID 129, item 2 (Revert To A
Previous Game). It opens the preserved in-game chooser without identifying a
slot; selection and live-state replacement remain inside the Classic flow.
The Party page's Character control carries the selected member in a distinct
`OpenCharacterSheetAction`. A fresh adaptive surface and exact-selection check
authorizes a one-shot neutral `app1Evt`; only the preserved outdoor or dungeon
outer loop can take that member. The loop rechecks `charnum`, `charselectnew`,
`charselectold`, and `charmainbut` immediately before entering its existing
`buttonchoice` route. EventManager never fabricates a mouse event or calls the
sheet synchronously, and Classic owns all sheet browsing and nested modals.
The Party page's sixth slot is a single member-free MONEY control carrying an
empty `OpenMoneyManagementAction`. It is available in ordinary outdoor or
dungeon navigation, both in camp and outside it, after a fresh valid current
selection check; the action carries no member identity. Its strict one-shot
`0x574DSS00` tag accepts outdoor `SS=0x01` or dungeon `SS=0x02` only and
requires the reserved low byte to be zero. Completed-scope consumption freshly
requires adaptive eligibility, the exact outdoor or dungeon map/first-person
presentation, a nonempty party bounded to six, and a current selected member
bounded to `0..5` whose selected flag is still true. EventManager converts the
neutral one-shot `app1Evt` only to Classic's exact lowercase `m` keyDown
`0x00002E6D`, clearing pointer, modifier, and window state; no held-mouse gate
applies. Camp state, funds, shop, temple, bank, and the serialized but otherwise
dead `swapavail` flag do not gate opening. Classic's `swapbut` created from
CNTL 157, outdoor `checkkeypad`, dungeon `threed`, `buttonchoice`, `swap`,
`pool`, and `share` paths retain the complete modal, bank transfer, pool/share,
shop/temple availability, and every money mutation. No Classic source or replay
schema/decoder vocabulary changes. This interaction does not satisfy either
pooled-money information row, and automated checks leave private,
no-redistribution manual QA open.
The Game page's member-free Rest control carries a distinct `RestPartyAction`
and is available only while the party is already in camp. Its single-use tag
must be consumed from a completed semantic scope matching the encoded origin.
Late consumption then freshly requires adaptive eligibility, an exact
exploration or dungeon screen match, a matching snapshot screen, outdoor
presentation for exploration or dungeon-map/first-person presentation for a
dungeon, and `in_camp == true`. Only then does it return Classic's exact
lowercase `r` message `0x00000F72`. Pointer dispatch occurs after capture is
cancelled on release, and EventManager rejects either activation when its
non-pumping cached SDL/Classic state reports a held mouse button. An accepted
activation therefore begins with one mandatory iteration of Classic's
preserved `do`/`while (StillDown())` Rest path; a distinct physical press after
delivery remains ordinary Classic input. Classic remains authoritative for
sound, tick/delay behavior, `updatefat(FALSE, -2, FALSE)`, both `timeclick`
calls and any resulting encounters, and the preserved `revertgame` return. The
semantic route does not synthesize a Rest hold, predict an encounter, or
reproduce those mutations.
Rest shares one persistent Game-page slot with a typed
`ContextualWorldEntryAction`: camp presents REST, while ordinary non-camp
outdoor or dungeon navigation presents SHOP, TEMPLE, or ENCOUNTER. Snapshot
capture derives exactly one mode with the executable Classic priority
`shopavail > templeavail > encounter`; the visual-only `canshop` flag does not
participate. The strict single-use `0x5745SSMM` tag binds `SS=0x01` to outdoor
and `SS=0x02` to dungeon, with explicit `MM=0`, `1`, and `2` mappings for shop,
temple, and encounter; unavailable and every other byte are malformed.
Completed-scope consumption freshly requires adaptive eligibility, the exact
screen and outdoor or dungeon-map/first-person presentation, non-camp state,
and a snapshot mode identical to the encoded action. EventManager converts an
accepted tag only to a neutral, zero-modifier Classic lowercase `g` keyDown
`0x00000567` for Shop or Temple or lowercase `e` keyDown `0x00000E65` for
Encounter. Queueing forges no key, pointer, control, or window state, and
delivery has no held-mouse gate. Classic's real `shopbut`, key guards, and
`buttonchoice` branch retain authoritative priority, global-macro execution,
shop and temple modal ownership, sound/music, RNG, land/dungeon saves,
door-item handoff, and seamless transition state. No Classic source or replay
schema/decoder vocabulary changes. Automated checks do not close the private,
no-redistribution manual-QA gap described below.
The adjacent member-free Camp control carries an explicit desired state in
`SetCampStateAction`: Camp requests `true`, and Break Camp requests `false`.
It is deferred only in ordinary outdoor or dungeon navigation with no active
encounter. The single-use tag records that desired state and originating world
surface. Late consumption freshly requires adaptive eligibility; the exact
exploration or dungeon screen; matching outdoor or dungeon-map/first-person
presentation; and a live camp state still opposite to the request. Only then
does EventManager return one Classic lowercase `c` keyDown with message
`0x00000863`. The desired-state mismatch gate prevents a stale Camp activation
from breaking a newly made camp and a stale Break Camp activation from
re-entering one. The semantic mapper intentionally does not duplicate
Classic's historically inverted `cancamp` permission check—`false` permits
entry and `true` produces denial feedback. Classic's existing `campbut` path
remains authoritative for that
permission and feedback, music and sound, `incamp` and related state changes,
`moveparty(0)`, time advancement, `updatecontrols()`, and every preserved
`revertgame` return. No Camp action or delivery vocabulary is added to replay.
The Game page orders SAVE, LOAD, the mutually exclusive
REST/SHOP/TEMPLE/ENCOUNTER position, CAMP/BREAK CAMP, SEARCH/STOP SEARCH,
TORCH, and the contextual AREA SEARCH/MAKE SCROLL control.
The member-free Search control remains available
during
ordinary outdoor or dungeon navigation with no active encounter, including
while camped. It presents `SEARCH` / `Start searching` when the current state is
off and `STOP SEARCH` / `Stop searching` when it is on. Its typed
`SetSearchStateAction` carries the absolute desired searching state. The
single-use `0x5753` tag encodes the originating world surface and a strict
Boolean destination; malformed Boolean values are rejected. After the
completed semantic scope is consumed, late validation freshly requires
adaptive eligibility, the exact exploration or dungeon screen and snapshot,
matching outdoor or dungeon-map/first-person presentation, and a current
any-nonzero Classic search state still opposite to the requested state. The
Classic outer loop takes the strict Boolean sideband, then requires the real
live `search` control before entering the existing `buttonchoice` path with
`theControl == search`. EventManager
returns a neutral `app1Evt`; the route fabricates no key or pointer event and
performs no direct state mutation. Classic `buttonchoice` alone owns sound,
persistent state, and icon updates. `checkforsecret` and its search-related time
cost remain effects of subsequent Classic movement, not the semantic toggle.
No Search action or delivery vocabulary is added to replay. Automated tests do
not close the private manual-QA gap described below; it remains required before
release.
The adjacent `UseTorchAction` is a one-shot request carrying an optional
`TorchSource`. When enabled, the source identifies the member and slot of
Classic's first exact item 805 with a positive charge. That locator is an
optimistic freshness token, not a stable item identity, and it carries neither
the observed charge nor current Light duration. A zero- or negative-charge
first exact match blocks every later match, matching Classic's early exit. Its
single-use `0x5754` tag binds the locator to the originating outdoor or dungeon
surface. Completed-scope
consumption freshly requires adaptive eligibility, the exact exploration or
dungeon screen and snapshot, matching outdoor or dungeon-map/first-person
presentation, and the same first usable locator. The Classic outer loop then
requires both that fresh source and the live real `torch` control before entering
the existing `buttonchoice` path with `theControl == torch`. EventManager returns
a neutral `app1Evt`; no key, pointer, direct inventory or Light mutation, or
`timeclick` is synthesized. Torch use remains available while camped, searching,
or already lit. Classic alone owns charge consumption, item dropping and slot
shifts, item/spell loading, RNG, sound, Light duration, darkness, and icon
updates. No Torch action or delivery vocabulary is added to replay. Automated
tests do not close the private manual-QA gap described below.
The final Game control carries one discriminated `ContextualOverviewAction`.
Outside camp, `area_search` has no member; in camp, `make_scroll` carries the
member selected when the action was composed. An absent Make Scroll member is
valid only for its visible disabled shell control and is rejected at dispatch.
The strict single-use `0x574F` tag binds the originating exploration or dungeon
surface, explicit mode, and canonical absent-or-bounded member. Completed-scope
consumption freshly requires adaptive eligibility, the exact outdoor or
dungeon-map/first-person presentation, an unchanged camp mode, and, for Make
Scroll, the same live selected member with Classic's case, stamina, and
spell-flow capability. A queued command can neither change contextual meaning
nor retarget a later member. Only then does EventManager yield Classic's exact
lowercase `a` key record `0x00000061` for Area Search or lowercase `k` record
`0x0000286B` for Make Scroll. The non-pumping cached SDL/Classic held-mouse gate
applies only to Area Search because its existing `Button()` loop can repeat;
Make Scroll has no such hold loop and remains subject only to its ordinary late
gates. The route forges no pointer or control handle, mutates no secret, party,
item, spell, or scroll data directly, and changes no Classic source. Classic
alone owns contextual control gating and warnings, forced secret discovery,
sound, time, encounters and revert handling, and the complete scroll dialog,
member browsing, caster and parchment checks, spell-point cost, first-free-slot
write, first matching parchment consumption/drop, and cleanup. No contextual
Overview action or delivery vocabulary is added to replay. Automated tests do
not close the private, no-redistribution manual-QA gap described below.
Combat exposes seventeen bounded semantic controls: typed `GuardCombatantAction`,
`FinishCombatantAction`, `DelayCombatantAction`,
`CenterActiveCombatantAction`, `SwitchWeaponSetAction`,
`CycleCombatFocusAction`, `AutoCombatantAction`,
`ShowCombatRangeAction`, `BandageCombatantAction`, and `UndoCombatantAction`
commands each carry the stable active-party combatant ID through distinct
combat-only tagged events. A distinct actor-only
`OpenCombatSpellbookAction` opens the preserved combat spell flow without
reusing the exploration `OpenSpellbookAction` or selecting a spell or target.
A separate actor-only `OpenCombatTargetingAction` identifies no target or cell;
Classic resolves the equipped source and target mode after handoff.
An actor-only `EscapeCombatAction` requests the preserved escape attempt without
encoding a range result, warning, confirmation choice, or flee outcome.
An actor-only `OpenCombatScrollCaseAction` opens Classic's combat scroll chooser
without identifying a case slot, scroll, spell, power, recipient, cell, or
target.
`CenterCombatCursorAction` carries the actor and one absolute battlefield cell
sampled from the visible Classic crop; it does not carry or mutate an ambient
mouse position.
The focus-cycle command also carries Previous or Next.
`OpenCombatItemsAction` independently carries the
acting combatant and selected party member so neither identity can silently
change while queued. The top-level combat loop late-validates a fresh
snapshot, including
the acting ID, party ownership, active/targetable state, and positive stamina,
before translating an action to its exact preserved Classic key record. Combat
Items additionally requires that the same member still exists and remains the
live selected member. Delay
is available only before movement (`movement == movement_maximum`), revalidates
that condition at delivery, and becomes the exact Classic `d` message
`0x00000264`. Center becomes the exact Classic `c` message `0x00000863`, and
Switch Weapon becomes the exact lowercase `w` message `0x00000D77`. Weapon
Switch intentionally carries no desired set; the live relative toggle remains
inside Classic combat. Center Previous becomes the exact Classic `p` message
`0x00002370`, and Center Next becomes the exact Classic `n` message
`0x00002D6E`. Combat Items becomes the exact Classic `i` message `0x00002269`.
Auto becomes the exact Classic `a` message `0x00000061`, Range becomes the
exact Classic `r` message `0x00000F72`, and Bandage becomes the exact Classic
`b` message `0x00000B62` only while the fresh combat snapshot reports that
Classic's current-turn `canundo` gate remains open. Undo has an independent
presentation capability derived from that same fresh gate and becomes the exact
Classic `u` message `0x00002075`.
Combat Cast is late-gated by a read-only projection of the current Classic
`cancast` prerequisites and becomes the exact Classic `s` message
`0x00000173`.
Combat Target is late-gated by a read-only mirror of Classic's visible Target
control: the active queued actor must still be displayed, no spell flow may be
open, and the toggled base item must still encode a charged targeting spell.
It becomes the exact Classic `t` message `0x00001174`.
Combat Escape is exposed through the same fresh active-party-actor gate and
becomes the exact Classic `e` message `0x00000E65`. Presentation deliberately
does not call or mirror Classic's mutating `getrange` helper or its condition
and warning rules.
Use Scroll is late-gated by a read-only mirror of Classic's visible equipped
scroll-case control: the queued party actor must remain current, outside a spell
flow, alive, and equipped in armor slot 13. It becomes the exact Classic `l`
message `0x0000256C`; an empty equipped case still opens Classic's chooser.
Center Cursor exists only after a hover sample inside the fresh combat viewport
has been bound to the current party actor, field origin, and dimensions. It
becomes the exact Classic `m` message `0x00002E6D`; the absolute 0–89 cell is
delivered through a one-shot typed handoff rather than `EventRecord.where` or
Classic's global `point`. A stale actor, viewport, surface, or sample is inert.
Validation fails closed, so stale or newly ineligible actions become inert.
The original Classic guard/finish/delay mutations and turn
advance remain authoritative, as do Center's existing non-turn-ending camera
sequence, Weapon's toggle and feedback, and Classic's queue-relative focus
destination. Classic also owns the entire Items modal, its further character
selection, every item use or mutation, targeting, and any resulting attack,
movement, or turn effect. The semantic checks are not a camera, combat, modal,
or save replay. Classic also owns Auto's automation, RNG, animation/movement
and attack mutations, and turn effects; this route establishes no automation,
RNG, mutation, or turn equivalence. Classic owns Range's overlay drawing,
event flush, raw mouse/key dismissal wait, recentering, and redraw path. The
contract does not execute or replay that modal range flow. Classic likewise
owns Bandage's raw `getchoice` picker, abort handling, selected-member bleeding
mutation, redraws, and turn advance; the semantic route selects no target and
claims no mutation or turn equivalence. Classic owns Undo's further condition
checks and every position, field, queue, redraw, and turn-state mutation; the
semantic route claims no undo-mutation equivalence. Classic remains
authoritative for Combat Cast's repeated `cancast` check, spell/power chooser,
target loops, spell-point charges and refunds, RNG, resolution, redraws,
movement/attack costs, and turn handling; the semantic route claims none of
that equivalence.
Classic remains authoritative for Target's equipment and quiver resolution,
target type, charge consumption and item drops, RNG, raw target loops,
abort/launch costs, spell effects, redraws, and turn handling. A charge or RNG
decision may occur before a manual target is chosen, and an aborted target flow
does not imply a semantic refund.
Classic remains authoritative for Escape's range calculation, warning 81 versus
83 precedence, raw `question3` confirmation, confirmed body/field/queue and
position removal, `inbattle` and prestige mutation, light update,
last-loyal-member coward/kill handling, and `getup` turn or post-combat flow.
The semantic route chooses no confirmation response and claims no flee or turn
equivalence.
Classic remains authoritative for Use Scroll's `getscroll` modal, character and
slot browsing, selection warnings, spell/power load, immediate accepted-scroll
consumption, combat-spell validation, target modes and raw input, RNG, spell
effects, movement/attack costs, and turn handling. A later target abort costs
three movement and does not restore the scroll or award a spell-point refund;
the semantic route claims no scroll-use equivalence.
Classic remains authoritative for Center Cursor's `centerfield` clamping,
camera mutation, redraw, overlay, and button feedback. The semantic route only
converts the queued absolute cell back to Classic's current field-relative
coordinates; the unchanged physical `m` branch still uses `point / 32`.
All other combat commands remain in the interactive Classic frame.

The seventeen code-native combat commands are organized into four persistent,
direct-selection tabs: Turn, Gear, Tactics, and Special. During an eligible
live-party turn, every tab remains visible and keyboard-focusable on every
combat command page, including the selected tab; selecting the current tab is
an idempotent presentation action. A double border and underline expose the
active page without relying on color. This replaces relative More/Back paging
without changing action payloads, combat-command availability checks, or
legacy handoffs.

Panels and controls use the approved native material set: panel `ppat` 131,
normal 129, selected 128, and pressed/inactive 130, with inactive taking
precedence over interactive states. Selected tabs retain their double border
and underline, active drawers retain the explicit `OPEN` label, and selected
party cards retain a second border. Deterministic dark/light scrims preserve
the tile texture while the exhaustive pixel contract holds every production
shell text/state pairing above 4.5:1 contrast.

Party-card layouts reserve a contained 44×44 portrait slot and reflow name,
level, stamina, and state text around it across compact/wide layouts and one
through six members. The four reviewed native portraits are used only after a
fresh proof of the actual Resource Manager winner; every other portrait remains
a code-native monogram. The exact approved Tutorial title picture receives its
reviewed title typography at render time rather than from baked generated text.

Details and log surfaces stay informational, and the complete Classic frame
remains interactive. The wide Details panel and compact Details drawer use the
same pure selected-member layout and rendering path. That path exposes detached
stamina and spell-point values, consciousness, armor, movement, and the complete
normalized state-token sequence, with condition identities sorted and
deduplicated. Every rendered state has an explicit marker or label independent
of color. Compact visual elision ends with `+N`, where `N` is the exact count
omitted from the visible summary while the complete semantic token sequence
remains available to the renderer.
Compact Details/Event Log drawer tabs are local presentation actions with
pointer and wrapping keyboard operation; they never cross the legacy mutation
bridge. The Details inspector itself dispatches no action and mutates neither
the snapshot nor legacy state. This is a bounded renderer milestone; it does
not enable the cropped gameplay route or claim Event Log, action, or screen
migration completeness. Popup anchors, text-input rectangles, cursor warps,
and pointer capture use the same central Classic-to-window transform.

A narrow legacy-context adapter keeps title/no-gameplay state and nested legacy
flows safe. Title, inventory, shop, encounter, and other modal screens retain
an intact full-frame 800×600 fallback. Both compatibility routes scale the
existing framebuffer uniformly: window growth changes presentation space, not
the number of visible map or combat tiles. Complete semantic screen migration
and complete action dispatch remain acceptance work.

## Gameplay-chrome coverage and crop readiness

Cropping the Classic gameplay frame has a bounded, fail-closed coverage
contract. A versioned static inventory must account for every Classic
interaction and essential-information role on each of the outdoor, dungeon,
and combat surfaces. Validation and completeness are global: an invalid,
unknown, duplicate, omitted, or missing role anywhere rejects cropping for
every surface. Controls, status fields, messages, inspectors, and other roles
whose visible pixels or hit areas fall outside the proposed crop may not be
inferred from an action count or omitted because a nearby native surface looks
similar. Each surface/role pair has exactly one status:

| Status | Required evidence |
| --- | --- |
| `retained_in_crop` | The complete authoritative Classic rendering and hit area remain inside the crop in every applicable state, with no dependency on hidden chrome. |
| `semantic_complete` | The replacement has a complete detached-model and layout path; interactions additionally have a typed payload, semantic tag, availability rules, guarded production consumer, late validation, and authoritative Classic handoff or bounded native behavior; information additionally has authoritative capture, any required ordered retention, and complete accessible rendering. |
| `missing` | Any part of the retained or semantic proof is absent, provisional, placeholder-only, stale, or untested. This status always rejects cropping. |

The inventory revision covers the complete reviewed contract: every stable ID,
surface, role kind, status, evidence statement, and cited Classic
UI/control-map source anchor. Any change to any field or cited anchor requires
a new revision and review. The inventory and runtime predicate serve different
purposes. Static coverage proves that the reviewed global role census contains
no missing, unknown, duplicate, or unaccounted entry. A crop request may proceed
only if all of the following independently sourced evidence succeeds for the
current frame:

| Check | Fail-closed requirement |
| --- | --- |
| Context | Typed expected and live surface/context-variant values are equal. Only the ordinary `standard` gameplay variant is currently supported; camp, nested/modal, spell, targeting, and text-entry variants reject cropping. |
| Legacy window | Granular evidence identifies the expected gameplay window, verifies the required front-window relationship, rejects any nested or conflicting legacy window, and proves that no full-frame safeguard applies. A single optimistic eligibility flag is insufficient. |
| Inventory revision | The runtime expectation equals the nonzero reviewed inventory revision covering every manifest field and cited Classic UI/control-map anchor; an unknown, zero, or mismatched revision rejects the crop. |
| Model | Every required detached snapshot field, identity, and capability is present and internally consistent; snapshot and shell-model revisions are equal and nonzero; and shell-model, snapshot, and live contexts match. |
| Font | Required code-native fonts and accepted metrics exist for the selected text scale; substitution or metric failure cannot silently clip or remove a role. |
| Layout | The current window, backing scale, text scale, and responsive branch produce finite, contained, non-overlapping information and controls, including minimum 44×44-point targets where applicable. |
| Control/handler | Every available native interaction has exactly one typed route and registered guarded production consumer, with late context and payload validation; unsupported, stale, ambiguous, or unhandled actions reject the crop. |
| Information | Every essential-information role has an authoritative source and complete visible or accessible presentation; ordered/history-bearing data is retained without placeholder substitution, loss, duplication, or reordering. |

The model, font, layout, expected-control, live-handler, and information values
accepted by the current evaluator are fail-closed evidence inputs. The
evaluator conjoins them but does not independently prove them. No authoritative
production evidence builder currently derives all of those values from live
runtime objects, so the evaluator alone is not authorized to set
`semantic_controls_ready`. Readiness is never inferred from screen dimensions,
the presence of a `UIAction` alternative, a successful layout, or one completed
control group. Any false, unknown, absent, zero-revision, or mismatched
component selects the intact full-frame compatibility route. Production
continues to pass a hardcoded `semantic_controls_ready = false`; no cropped
Classic gameplay frame is enabled and this section does not claim runtime
readiness.

The 95-row inventory currently contains six `retained_in_crop`, 54
`semantic_complete`, and 35 `missing` roles: 15 interactions and 20
essential-information roles. Cropping remains disabled. The
known incomplete roles include at least the following; the source-derived
inventory remains authoritative and must reject an omitted role:

| Surface | Known `missing` interaction roles |
| --- | --- |
| Outdoor | Heal, Trade, selected-member condition drilldown, and per-member Auto. Money management, context-sensitive Shop/Temple/seamless-encounter entry, Character Sheet, and the distinct quick Equipment popup are covered interaction rows, not evidence for any broader inspection or information role. |
| Dungeon | The corresponding dungeon Heal, Trade, condition drilldown, and per-member Auto, including dungeon-specific availability and input semantics. Money management, context-sensitive Shop/Temple/seamless-encounter entry, Character Sheet, and quick Equipment remain distinct covered rows. |
| Combat | Conditional Turn Undead, per-member Auto, and distinct focused-combatant character/monster inspection, items, conditions, and monster-attack actions. |

Known `missing` essential-information roles across these surfaces include
ordered capture and retention of the Classic message/flash stream for Event
Log; pooled-money and fatigue status; party-wide condition indicators;
authoritative coordinates, calendar/clock, and
complete combined Search/Torch state (the persistent Torch-state presentation
remains absent); focused-combatant
information; complete combat conditions and attacks; and combat round and
enemies-remaining counts. The common rail now covers the three all-member
party-vitals rows, but that claim is deliberately bounded: it does not promote
party conditions, pooled money, fatigue, focused combatants, or combat
conditions/attacks. Snapshot fields or the current placeholder Event Log
do not satisfy the information-completeness check merely by existing. The
selected-member Details inspector remains a distinct bounded renderer and does
not provide the common rail's all-member proof or establish equivalence for
the remaining party-wide or combat roles.

## Required automated coverage

Release-candidate tests must include:

- resource precedence, scenario-local ID collisions, duplicate reuse, manifest validation, missing coverage, dimensions, masks, anchors, cursor hotspots, and atlas order;
- shared verified-raster loading for every public approved-raster consumer,
  including native non-ASCII paths, bounded exact-byte reads and PNG dimensions,
  digest/substitution/missing/malformed/oversized rejection, sanitized failures,
  and exact physical-to-logical scale checks;
- exact key-only `ppat` 128–131 native-material bindings, isolated public-only
  loading, headless software-renderer realization/drawing, atomic four-texture
  publication/fallback, renderer mismatch rejection, replacement and reset
  recovery, Classic-mode non-loading, state-role mapping, and exhaustive per-
  pixel contrast for every shell text/state color;
- exact ordered `cicn` 257/267/297/337 portrait bindings, 44×44 layout at
  compact/wide sizes for one through six members, absence of ID-only draw
  authorization, fresh actual-winner proof matching every bound field,
  custom/other-pack/passthrough/coverage-failure/writable-edit rejection,
  monogram fallback, headless all-or-nothing texture realization, renderer
  ownership/mismatch, and device-reset recovery;
- exact Tutorial `PICT` 32128 key/digest/dimension gating, deterministic
  code-rendered title pixels confined to the reviewed plaque inset, source
  isolation, font-state restoration, and complete Classic fallback on failure;
- selected-member Details projection and shared wide/compact layout for absent
  or stale selection, bounded stamina and spell-point meters, consciousness,
  armor, movement, sorted/deduplicated condition identities, marker/label cues
  that do not rely on color, deterministic compact `+N` elision, complete
  retained semantic tokens, finite contained non-overlapping bounds, practical
  text-size floors, and input immutability; structural coverage must also prove
  both rendering branches use the shared path, the pure model/layout has no
  SDL, Resource Manager, action, or legacy-state dependency, and the renderer
  has no dispatch path;
- all-member party-rail capture, model, layout, rendering, and semantic
  accessibility text on exploration, dungeon, and combat: exact AC;
  `spellpointsmax != 0` as the sole
  caster discriminator even at zero current SP; noncaster raw
  `normattacks + attackbonus`, Speedy 23 before Slow 6, integer halving, reduced
  `0..19` half-unit fractions, and `> 10` for every other value; row ordering,
  finite contained non-overlapping geometry, one common renderer, complete
  unelided layout accessibility state text, exploration/dungeon control
  metadata without an OS-publication claim, input immutability, and absence of
  any new action, tag, input, Classic-source, or replay path;
- logical/physical coordinate transforms, hit testing, stable semantic focus,
  Tab/Shift-Tab wrapping, Return/Space release activation, repeat suppression,
  cancelled key-up ownership, 1024×768 through ultrawide layouts, and 1×/2×
  backing scales;
- versioned gameplay-chrome role inventories for outdoor, dungeon, and combat,
  with exact global role/order/field/source-anchor and status validation;
  deterministic crop-readiness checks for typed expected/live standard context,
  granular legacy window/front/full-frame facts, nonzero inventory and
  snapshot/model revisions, matching model context, detached-model completeness,
  fonts/metrics, responsive layout, control-to-handler completeness, and
  information capture/retention/rendering; and negative tests proving every
  missing, unknown, unsupported variant, absent window fact, zero/stale revision,
  mismatched context, unhandled control, or incomplete information component
  retains the full 800×600 compatibility frame;
- every semantic `UIAction`, including confirmation and cancellation paths;
- identical scripted Classic and Remastered action streams with snapshot and save-byte comparisons;
- retail Mac, current-port, and early-port save fixtures, including the recognized `Data I1` sizes `0x398B`, `0x3979`, and `0x398D`;
- migration copy, SHA-256 verification, pre-import backup, idempotency, failure rollback, and proof that the original Fantasoft directory is unchanged;
- title/start, party creation/selection, exploration, dungeon, combat, inventory, shop/trade, spells, encounters, maps, save/load, and endings for Tutorial and City;
- SFX decode/playback and music start, loop, stop, switch, volume, missing-file, corrupt-module, and shutdown behavior without audio-thread leaks;
- Classic 800×600 framebuffer goldens and Remastered goldens at 1024×768, 1440×900, 1920×1080, ultrawide, and both backing scales.

Private retail or user-owned fixtures follow [CONTENT_PROVENANCE.md](CONTENT_PROVENANCE.md). Test output must never overwrite fixture input.

## Content blockers

The following are hard stops, not warnings:

1. City of Bywater requires an authorized, hashed Mac 7.1.2 baseline record at `provenance/content/city-of-bywater-mac-7.1.2.json`. Repository City data cannot silently substitute for it.
2. Each bundled music track requires separately reviewed composition and sample provenance in `provenance/content/phase1-music.json`. Unresolved tracks are excluded and may be supplied only by a user-owned-installation importer.
3. The phase-one census, placeholder manifest, and deterministic mixed runtime
   manifest must validate. Human approval applies per output and does not turn
   rejected or unattempted resources into release-ready artwork.
4. A release remaster manifest must be approved and have zero coverage failures. Placeholder artwork and Classic fallbacks are unacceptable inside the selected slice.
5. Third-party scenarios beyond the phase-one scope require their own rights gate before their assets are used as generation references.

## Artifact verification

Verify an expanded local app before signing:

```sh
scripts/verify-macos-artifact.sh --mode development \
  "build/Realmz Remastered — Unofficial.app"
```

The development gate checks:

- distinct reverse-DNS bundle identifier outside the `com.fantasoft` namespace and an explicit “Realmz Remastered”/“Unofficial” display name;
- `APPL`, high-resolution capability, and macOS 13.3 minimum in the plist and every Mach-O slice;
- x86_64 and arm64 slices in the executable and all packaged Mach-O files;
- only system or bundle-relative dylib loads and bundle-relative `LC_RPATH` values;
- core data, Tutorial, City, a final or placeholder phase-one remaster manifest,
  the bound runtime manifest and census, and the exact paths and SHA-256
  digests of all 11 approved public runtime PNGs, including the four native
  shell materials; every other style-proof input/evidence path and every
  symlink must be absent;
- complete license, attribution, modification, and provenance notices.

Canonical notice paths are:

```text
Contents/Resources/Notices/LICENSE
Contents/Resources/Notices/ATTRIBUTION.md
Contents/Resources/Notices/MODIFICATIONS.md
Contents/Resources/Notices/CONTENT_PROVENANCE.md
```

The attribution names Tim Phillips and clearly says the fork is unofficial. The modification notice describes the fork’s changes. The full CC BY-NC-SA 4.0 license is included, not merely linked.

After signing:

```sh
scripts/verify-macos-artifact.sh --mode release \
  "dist/Realmz Remastered — Unofficial.app"
```

Release mode requires a final `Remastered/phase1.manifest.json`, zero coverage failures, a valid deep signature, a Developer ID Application authority, a TeamIdentifier, and hardened runtime. It is expected to fail on ad-hoc or unsigned development builds.

After Apple notarization and stapling, and again after copying the app from the mounted DMG:

```sh
scripts/verify-macos-artifact.sh --mode release --require-notarization \
  "/Volumes/Realmz Remastered/Realmz Remastered — Unofficial.app"
```

This adds staple validation and Gatekeeper assessment. Keep notarization request ID, source commit, dependency pins, signing identity fingerprint, app/DMG SHA-256 digests, and verifier logs with the release record.

`CPack` uses `hdiutil` for the final DMG. A managed or virtualized macOS runner can reject disk-image creation with `Device not configured` even when the staged `.app` is valid; treat that as a runner capability failure, move the same reproducible build to a disk-image-capable macOS runner, and rerun the complete mounted-DMG verification rather than bypassing the gate.

## Manual release acceptance

Automated checks do not replace these release decisions:

- two human start-to-finish playthroughs of Tutorial and City, with no unresolved Classic fallback;
- complete keyboard operation, remappable shortcuts, scalable UI/text, reduced motion, contrast-safe focus/state styling, and non-color state cues;
- native panel, normal, selected, pressed, and inactive materials at compact
  and wide layouts at both 1× and 2× backing scales; deliberately remove or
  corrupt each of the four packaged PNGs in a disposable copy and verify that
  no partial texture cache appears and the complete shell remains legible on
  its flat-color fallback;
- native party portraits at compact and wide layouts, one through six members,
  and both backing scales; in a disposable private fixture, verify all four
  exact approved winners, then a custom ID, colliding other-pack ID, unapproved
  resource, and user-edited resource each fall back to the correct monogram;
  remove or corrupt each packaged portrait PNG and verify that no partial
  portrait cache appears; trigger renderer replacement and SDL render-device
  reset and verify both native caches recover without stale textures;
- the exact approved Tutorial title picture with readable, centered code-
  rendered typography at both backing scales; in a disposable package, change
  its PNG digest or remove the bundled title font and verify complete Classic
  fallback rather than a blank or partially composed approved picture;
- selected-member Details at compact and wide layouts, both backing scales,
  minimum and enlarged text scales, conscious and unconscious members, zero and
  clamped stamina/spell-point ranges, and long condition sets; verify explicit
  non-color markers, exact compact `+N` elision, no clipped/overlapping fields,
  and that opening, closing, or viewing Details changes no gameplay state and
  dispatches no legacy action;
- the common all-member party rail on disposable outdoor, dungeon-map,
  dungeon-first-person, and combat fixtures with one through six members,
  compact and wide layouts, minimum and enlarged text, and both backing scales.
  Verify row 1 name / level / AC, row 2 stamina plus SP for maximum-SP casters
  (including current SP zero) or ATK for noncasters, and row 3 state summary.
  Exercise unmodified, Speedy-only, Slow-only, and simultaneous Speedy-then-
  Slow cadence, every adjusted half-unit from 0 through 19, plus negative and
  above-range values; compare exact reduced fractions and the `> 10` fallback
  with Classic `updatechar`. Inspect layout accessibility text with long names
  and multiple states to confirm the complete unelided three-row meaning
  remains available even when visible state text is compact; separately verify
  that outdoor/dungeon selection controls carry it in metadata without
  asserting an OS-accessibility publisher on any surface. Confirm rendering
  changes no gameplay state and creates no semantic action, tag, input,
  Classic-source, or replay path;
- before accepting any future crop-enabling change, review the globally complete
  versioned outdoor, dungeon, and combat chrome inventory row by row against an
  uncropped Classic reference, including every manifest field and cited source
  anchor. Exercise the supported typed `standard` variant and prove immediate
  full-frame fallback for camp, nested/modal, spell, targeting, and text-entry
  variants; wrong or non-front gameplay windows; outdoor and dungeon
  availability differences; live and stale party selection; player and
  non-player combat turns; conditional Turn Undead; and focused party and
  monster combatants. Compare the uncropped reference with the candidate
  cropped shell at compact and wide layouts, minimum and enlarged text, and
  both 1× and 2× backing scales;
  confirm every role is either wholly retained in the crop or semantically
  complete, every interactive target remains visible and keyboard/pointer
  operable, messages preserve order without loss or duplication, and money,
  fatigue, and focused-combatant state remain complete and legible. Deliberately
  invalidate each readiness input—global coverage, typed context variant,
  granular legacy window/front/full-frame state, inventory revision, nonzero
  snapshot/model revision and model context, model completeness, font, layout,
  control, handler, and information—and verify immediate full-frame fallback
  rather than a partial crop. Archive the inventory, captures, failure evidence,
  and reviewer sign-off with the release record;
- pointer and Tab/Shift-Tab plus Return/Space activation for code-native Items,
  Equipment,
  Spells, non-combat Use Scroll, Character, Save, Load,
  Rest/Shop/Temple/Encounter, Camp/Break Camp,
  Search/Stop Search, Torch, Area Search/Make Scroll,
  Guard, Finish, Delay, Center,
  Switch Weapon, Center Previous/Next, Combat Items, Auto, Range, Bandage, Undo,
  Combat Cast, Combat Target, Combat Escape, Use Scroll, and Center Cursor
  controls at compact and wide layouts,
  including direct and idempotent selection of the persistent Travel, Party,
  and Game world tabs and Turn, Gear, Tactics, and Special combat tabs; stable
  focus across page recomposition;
  unique non-overlapping targets of at least 44×44 points; and a visible
  non-color selected-tab indicator; and an inert stale Spells action after
  selection, consciousness, spell points, or surface state changes; verify
  non-combat Use Scroll emits outdoor `l` and dungeon `p`, remains available
  for an equipped case whose five entries are empty, and becomes inert after
  selection, stamina, equipment, or surface state changes; inert stale Save and
  Load actions after leaving their gameplay surface; verify Character is inert
  after selection, member, or surface changes and otherwise reaches only the
  existing Classic character-sheet path through the neutral one-shot handoff;
  close the private manual-QA gap for EQUIPMENT on disposable outdoor and
  dungeon fixtures, including dungeon map and first-person presentations.
  Confirm it is adjacent to and distinct from full ITEMS, binds the selected
  member in `OpenSelectedItemDrilldownAction`, and accepts only strict
  `0x5349SSMM` tags with outdoor `SS=0x01` or dungeon `SS=0x02` and
  `MM=0x00..0x05`. Queue actions made stale by scope, surface, adaptive
  eligibility, presentation, selection, or member changes and confirm they are
  inert. For each accepted pointer and keyboard activation, verify exactly one
  neutral `app1Evt` with zero modifiers stages the same member without forged
  key or pointer input and without a held-mouse gate. Confirm the matching
  outer loop selects the real `showitembut` and the preserved `buttonchoice` /
  `showcondition` popup alone owns browsing and every `wear` or `removeitem`
  mutation. Confirm replay schemas and decoders expose no Equipment vocabulary,
  keep all fixture data and resulting evidence private, and do not redistribute
  them;
  close the private manual-QA gap for the sixth PARTY-page MONEY control on
  disposable outdoor and dungeon fixtures, including dungeon map and
  first-person presentations and both camp and noncamp states. Confirm there is
  exactly one member-free `OpenMoneyManagementAction`, with no bound member
  identity, and accept only strict `0x574DSS00` tags with outdoor `SS=0x01` or
  dungeon `SS=0x02` and a zero reserved low byte. Queue actions made stale by
  scope, surface, adaptive eligibility, presentation, empty or oversized party,
  missing or out-of-range current selection, or a cleared selected flag and
  confirm they are inert. For each accepted pointer and keyboard activation,
  verify exactly one neutral `app1Evt` becomes lowercase `m` keyDown
  `0x00002E6D` with zero pointer, modifier, and window state and no held-mouse
  gate. Exercise zero and nonzero funds plus shop, temple, bank, and serialized
  `swapavail` values and confirm none gates opening. Confirm the real
  `swapbut`/CNTL 157 and preserved `checkkeypad`/`threed`, `buttonchoice`,
  `swap`, `pool`, and `share` routes alone own modal cancellation, bank
  transfer, pool/share, availability changes, and every mutation. Confirm no
  pooled-money information coverage is implied, Classic sources and replay
  schemas/decoders expose no Money vocabulary, keep all fixtures and resulting
  evidence private, and do not redistribute them;
  close the private manual-QA gap for the mutually exclusive
  Rest/Shop/Temple/Encounter GAME slot on disposable outdoor and dungeon
  fixtures, exercising both dungeon map and first-person presentations. Confirm
  camp shows REST and hides world entry, while leaving camp hides REST and shows
  exactly one of SHOP, TEMPLE, or ENCOUNTER in the same position; verify the
  snapshot and label priority is `shopavail > templeavail > encounter` when
  both availability flags are true, and that toggling saved visual-only
  `canshop` cannot change the action mode. Confirm the typed
  `ContextualWorldEntryAction` always carries that explicit mode; only strict
  `0x5745SSMM` tags with outdoor `SS=0x01` or dungeon `SS=0x02` and explicit
  `MM=0/1/2` are accepted. Queue actions made stale by scope, surface, adaptive
  eligibility, presentation, camp, or mode changes and confirm they are inert
  rather than changing meaning. For each accepted pointer and keyboard
  activation, verify one neutral zero-modifier lowercase `g` keyDown
  `0x00000567` reaches Shop or Temple and one lowercase `e` keyDown
  `0x00000E65` reaches Encounter, with no forged pointer/control/window state
  and no held-mouse gate. Exercise shop and temple global macros, modal entry
  and cancellation, encounter RNG including consumed positive door chances,
  outdoor and dungeon save selection, seamless transitions, and the door-item
  handoff; confirm Classic alone owns every effect. Confirm Classic sources and
  replay schemas/decoders contain no contextual-world-entry vocabulary, keep
  all fixtures and resulting evidence private, and do not redistribute them;
  verify Rest is unavailable outside camp; becomes inert after its semantic
  scope, surface, adaptive eligibility, presentation, or fresh camp state
  changes; yields the exact Classic `r` message only after every late gate;
  rejects delivery while the cached SDL/Classic mouse state is held; and begins
  with one mandatory Classic rest quantum per accepted pointer-release or
  keyboard activation, absent a distinct later physical press, while Classic
  alone owns sound, fatigue, time, encounters, and revert handling;
  verify Camp carries an explicit desired state; is unavailable outside normal
  world navigation or during an encounter; becomes inert after its semantic
  scope, surface, adaptive eligibility, presentation, or live camp state
  changes; and yields exactly one Classic `c` keyDown only while the live state
  remains opposite to the request. Queue Camp, enter camp by another path, and
  confirm the stale action cannot break camp; repeat symmetrically for Break
  Camp. Exercise both values of Classic's inverted `cancamp` permission and
  confirm Classic alone owns denial feedback, music, sound, state and movement
  updates, time, control refresh, and revert handling. Confirm replay schemas
  and decoders expose no Camp vocabulary;
  close the remaining private manual-QA gap for Search on both outdoor and
  dungeon fixtures, including while camped: confirm the GAME order is SAVE,
  LOAD, REST, CAMP/BREAK CAMP, SEARCH/STOP SEARCH; verify the visible
  `SEARCH`/`STOP SEARCH` labels and `Start searching`/`Stop searching`
  accessibility text for both states; and prove pointer and keyboard activation
  request an absolute desired state. Queue both same-state stale tags and tags
  made stale by scope, surface, adaptive eligibility, presentation, real
  Classic-control, or live-state changes and confirm they are inert. Exercise
  zero and multiple nonzero Classic search values and confirm every nonzero
  value is treated as searching. For each accepted action, verify exactly one
  neutral `app1Evt` sideband reaches the real `search` control and existing
  `buttonchoice` path with `theControl == search`, with no forged key, pointer,
  or direct mutation;
  confirm Classic alone owns sound, state, and icon changes, and that
  `checkforsecret` and its time cost occur only during subsequent Classic
  behavior. Confirm replay schemas and decoders expose no Search vocabulary;
  close the private manual-QA gap for Torch on disposable outdoor and dungeon
  fixtures, including dungeon map and first-person presentations and while
  camped, searching, and already lit. Confirm the GAME order ends in
  SEARCH/STOP SEARCH, TORCH; that no usable first exact item 805 disables Torch;
  and that the first zero- or negative-charge match blocks any later charged
  match. Queue locators made stale by scope, surface, adaptive eligibility,
  presentation, member/slot movement, depletion, or a missing real Classic
  control and confirm they are inert. For each accepted pointer and keyboard
  activation, verify exactly one neutral `app1Evt` reaches the existing
  `buttonchoice` path with `theControl == torch`, consumes exactly one charge
  from the bound first source, and preserves Classic's drop and slot-shift
  behavior. Confirm there is no forged key or pointer, direct inventory or Light
  mutation, or `timeclick`; Classic alone owns item/spell loading, RNG, sound,
  Light duration, darkness, and icon updates. Confirm replay schemas and
  decoders expose no Torch vocabulary, and do not redistribute private fixture
  data;
- close the private manual-QA gap for the contextual Overview control on
  disposable outdoor and dungeon fixtures, including dungeon map and
  first-person presentations. Confirm the GAME order ends in
  SEARCH/STOP SEARCH, TORCH, then AREA SEARCH outside camp or MAKE SCROLL in
  camp; that the one `ContextualOverviewAction` always carries an explicit
  mode; Area Search carries no member; and enabled Make Scroll carries the
  selected member. Queue actions made stale by scope, surface, adaptive
  eligibility, presentation, camp-mode, selection, stamina, case, or spell-flow
  changes and confirm they are inert and never change mode or retarget. Verify
  malformed `0x574F` mode/member encodings are rejected. For each accepted
  activation, confirm EventManager yields only the exact Classic lowercase `a`
  message `0x00000061` or lowercase `k` message `0x0000286B`. Press, hold, and
  release AREA SEARCH and verify it fires only after release and begins exactly
  one forced-search pass; separately verify a pre-held cached SDL or Classic
  mouse state burns and rejects only Area Search, while Make Scroll does not
  acquire that extra held-mouse gate. Exercise a known secret and the fatigue
  warning, and verify Classic alone owns discovery, sound, one location-scaled
  time quantum (`timeclick(1, TRUE)`) per mandatory pass, encounters, and revert
  handling. Open and cancel
  Make Scroll; then exercise noncaster, no-parchment, full-case, and successful
  scribing paths, verifying Classic alone owns warnings, browsing, spell-point
  cost, first-free-slot contents, first matching parchment charge/load/drop,
  and cleanup. Confirm no pointer or control is forged, no Classic source is
  changed, and replay schemas and decoders expose no contextual Overview
  vocabulary. Keep all fixtures and resulting evidence private and do not
  redistribute them;
  and inert stale Guard,
  Finish, Delay, and
  Center, Switch Weapon, and Center Previous/Next actions after the acting
  combatant, eligibility, or combat surface changes; verify Combat Items is
  inert after either the acting combatant or selected party member changes and
  otherwise opens the preserved Classic modal for that selected member; verify
  Auto is inert after the acting combatant or combat eligibility changes and
  otherwise reaches the preserved Classic automation handoff; verify Range is
  inert after the acting combatant or combat eligibility changes and otherwise
  reaches the preserved Classic overlay, dismisses from a fresh keyboard key
  or click inside the Classic battlefield, and does not dispatch another shell
  command when shell chrome is clicked during that raw dismissal wait; verify
  Bandage is inert after the acting combatant, combat eligibility, or Classic
  current-turn gate changes and otherwise reaches the preserved party-member
  picker, accepts a Classic-frame portrait or `a` abort, does not dispatch shell
  commands while the raw picker is active, and leaves targeting, mutation, and
  turn advance entirely to Classic; verify Undo is inert after the acting
  combatant, combat eligibility, or Classic current-turn gate changes and
  otherwise reaches the preserved Classic condition checks and mutation path;
  verify Combat Cast is inert after the actor or any projected casting
  prerequisite changes and otherwise reaches the preserved Classic chooser,
  including cancel, target, cost/refund, resolution, and turn paths entirely
  inside the Classic frame;
  verify Combat Target is inert after the actor, displayed/queued body, spell
  flow, toggled source, encoded spell, or source charge changes and otherwise
  reaches Classic's equipment/quiver resolution, target modes, raw target loop,
  charge/drop and RNG paths, abort/launch costs, resolution, and turn handling;
  verify Combat Escape is inert after the acting combatant or combat surface
  changes and otherwise reaches Classic's distance and condition warnings;
  verify range 9 versus 10, no enemies, warning precedence, cancel/Stay versus
  Embrace, individual versus last-loyal escape, a fresh confirmation input,
  and Classic-owned field/queue/position/prestige/coward/turn effects;
  verify Use Scroll is inert after the actor, queue slot, spell-flow state,
  stamina, or equipped case changes and otherwise opens Classic's chooser;
  verify empty cases, other-character browsing, cancel, accepted-scroll
  consumption, combat-spell rejection, raw targeting and abort, movement/attack
  costs, RNG/effects, and turn handling remain entirely in the Classic frame;
  verify Center Cursor samples pixel 31 versus 32 correctly, retains a valid
  sample while moving across in-window chrome to the shell button, clears it
  for outside-window or non-finite pointer input, rejects
  actor/surface/origin/viewport changes, survives a camera move after queueing
  by using its absolute cell, never writes `EventRecord.where` or global
  `point`, and leaves physical `m` behavior unchanged;
  verify
  Save and Load open the Classic chooser without selecting a slot, writing a
  save, or replacing live state;
- clean install on macOS 13.3 and the current macOS release, plus upgrade/import from an existing Realmz installation;
- crash-free soak sessions and zero P0/P1 defects;
- verification that imported saves were copied, hashed, and backed up, and the old installation remained byte-for-byte unchanged;
- mounted-DMG drag/install/launch and quarantine/Gatekeeper checks on a machine without developer tools;
- source archive availability at the exact release tag, attribution/modification notices, no App Store DRM, and noncommercial distribution only;
- trademark/no-endorsement review of the final “Realmz Remastered — Unofficial” branding.

## Release sequence

1. Freeze a clean commit and run `verify-source-baseline.sh --mode release`.
2. Build sanitizer configurations and complete automated and manual slice testing.
3. Build the universal Release app from hash-verified dependencies.
4. Run artifact verification in development mode.
5. Sign nested code and the app with hardened runtime, then run release-mode verification.
6. Create the DMG, submit it for notarization, staple the app/DMG as appropriate, and validate the ticket.
7. Mount the final DMG, copy or address the contained app directly, and rerun release verification with `--require-notarization`.
8. Hash final artifacts, perform clean-machine acceptance, archive evidence, publish binaries and corresponding source.
