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

`run-core-tests.sh` compiles dependency-free tests directly with `${CXX:-c++}` in C++23 mode and warning-as-error flags. Temporary executables are created under `mktemp` and removed on exit. It covers presentation routing, adaptive-shell geometry, input transforms and pointer capture, legacy screen-context classification, party/action accessibility models, 128 state-invariant mode switches, deterministic resource failures, live legacy-snapshot copying, asset validation, and user-data safety. It deliberately does not configure the full SDL application.

A configured full build also registers `ResourceForkSelectionIntegrationTest`.
That test parses the five real phase-one resource forks and proves that all
1,520 immutable selected payloads resolve through the live pack-aware hook as
11 hash-verified approved replacements or 1,509 exact Classic passthroughs.
Classic runtime selection never opens the manifest or hashes payloads;
Remastered selection runs only after the legacy resource chain has selected the
owning fork. Switching Presentation mode reloads the cached UI patterns and
background pictures, so Classic and Remastered can be compared in one running
session without retaining stale raster handles.

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

CI mirrors the dependency-free checks on Linux and macOS. `scripts/bootstrap-macos-dependencies.sh` supplies the hash-pinned universal phosg/resource_file bootstrap for a full macOS 13.3 application build. The bootstrap has passed locally; keep a full artifact job non-required until it also completes reliably on the selected runner image and its disk-image service supports `hdiutil`.

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
On eligible exploration and dungeon screens, party cards expose typed
`SelectPartyMemberAction` payloads. Their distinct tagged event is
late-validated against a fresh party snapshot, then a narrow
legacy adapter changes `charselectnew` exactly once or performs an idempotent
no-op for the selected member; it never synthesizes a portrait click or opens a
modal. The Items and Spells actions carry that selected member in distinct typed
and tagged commands; Spells is available only while the member is conscious and
has spell points. The same guarded top-level loop revalidates the member, live
selection, eligibility, and screen before returning the exact Classic `i` or
`s` key record. Stale queued actions therefore become inert. The nested
inventory and spell-selection screens remain unmodified and full-frame.
The adjacent typed Save action has its own member-free tagged event. It is
late-validated against the current exploration or dungeon surface and only then
translated to the preserved Game menu ID 129, item 3 route. That route opens
the unmodified Classic slot chooser; no slot selection or save write occurs at
the semantic boundary. The neighboring typed Load action follows an independent
member-free tag and translates only to Game menu ID 129, item 2 (Revert To A
Previous Game). It opens the preserved in-game chooser without identifying a
slot; selection and live-state replacement remain inside the Classic flow.
Combat exposes fourteen bounded semantic controls: typed `GuardCombatantAction`,
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
All other combat commands remain in the interactive Classic frame.

Details and log surfaces stay informational, and the complete
Classic frame remains interactive. Compact Details/Event Log drawer tabs are
local presentation actions with pointer and wrapping keyboard operation; they
never cross the legacy mutation bridge. Popup anchors, text-input rectangles, cursor
warps, and pointer capture use the same central Classic-to-window transform.

A narrow legacy-context adapter keeps title/no-gameplay state and nested legacy
flows safe. Title, inventory, shop, encounter, and other modal screens retain
an intact full-frame 800×600 fallback. Both compatibility routes scale the
existing framebuffer uniformly: window growth changes presentation space, not
the number of visible map or combat tiles. Complete semantic screen migration
and complete action dispatch remain acceptance work.

## Required automated coverage

Release-candidate tests must include:

- resource precedence, scenario-local ID collisions, duplicate reuse, manifest validation, missing coverage, dimensions, masks, anchors, cursor hotspots, and atlas order;
- logical/physical coordinate transforms, hit testing, stable semantic focus,
  Tab/Shift-Tab wrapping, Return/Space release activation, repeat suppression,
  cancelled key-up ownership, 1024×768 through ultrawide layouts, and 1×/2×
  backing scales;
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
- core data, Tutorial, City, and a final or placeholder phase-one remaster manifest;
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
- pointer and Tab/Shift-Tab plus Return/Space activation for code-native Items,
  Spells, Save, Load, Guard, Finish, Delay, Center, Switch Weapon, Center
  Previous/Next, Combat Items, Auto, Range, Bandage, Undo, Combat Cast, and
  Combat Target
  controls at compact and wide layouts,
  including an inert stale Spells action after selection, consciousness, spell
  points, or surface state changes; inert stale Save and Load actions after
  leaving their gameplay surface; and inert stale Guard, Finish, Delay, and
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
