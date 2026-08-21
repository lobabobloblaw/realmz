# Realmz Remastered — Unofficial

This repository is an unofficial, free, noncommercial evolution of Realmz,
forked from upstream commit `4089d550ab606172bac850ac055677c36c6ff547`.
It preserves the native SDL3/Cocoa engine, game rules, scenario/save formats,
and complete Classic presentation while introducing a live Classic/Remastered
renderer boundary and an independently reviewed asset pipeline. Selected
bitmap resources now pass through a pack-aware runtime coverage check after
the legacy search chain chooses their source fork; Classic mode bypasses this
check entirely.

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
the character modal. A code-native Items control carries the selected member in
a typed `OpenInventoryAction`. A neighboring Spells control is available only
for a conscious selected member with spell points and carries that member in a
typed `OpenSpellbookAction`. The guarded top-level route revalidates either
selection before translating it to the preserved Classic `i` or `s` key, so the
inventory and spell-selection screens remain intact compatibility flows. Their
neighboring Save and Load controls carry distinct typed `OpenSaveGameAction`
and `OpenLoadGameAction` commands. After late surface validation, they become
the exact preserved Game > Save Current Game `(129, 3)` and Game > Revert To A
Previous Game `(129, 2)` choices. Each opens the Classic slot chooser; neither
semantic action chooses a slot, writes save data, or replaces engine state.
During combat, code-native Guard, Finish, Delay, Center, Switch Weapon, Center
Previous/Next, Auto, Range, Bandage, and Undo controls each carry the stable
active-party combatant ID in typed
`GuardCombatantAction`, `FinishCombatantAction`, `DelayCombatantAction`,
`CenterActiveCombatantAction`, `SwitchWeaponSetAction`,
`CycleCombatFocusAction`, `AutoCombatantAction`,
`ShowCombatRangeAction`, `BandageCombatantAction`, and `UndoCombatantAction`
commands. The neighboring combat Cast control carries that same stable actor in
a distinct `OpenCombatSpellbookAction`; it does not reuse the exploration
`OpenSpellbookAction` or choose a spell or target. The combat Items
control carries both that acting ID and the stable selected party-member ID in
an `OpenCombatItemsAction`. Their combat-only guarded routes late-validate the
fresh acting combatant and fail closed before returning the exact preserved
Classic key record; Items also revalidates that the same member still exists
and remains selected. Delay is
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
`0x00000173`.
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
redraws, movement/attack costs, and turn handling. These bounded routes are not
a camera, range-overlay, bandage-target, spell-selection, targeting, undo
mutation, combat, modal, automation, RNG, turn, or save replay; other combat
commands remain inside the interactive Classic frame.
The code-native controls share a
keyboard route with wrapping Tab and Shift-Tab focus plus
Return or Space activation, suppresses repeat dispatch, and uses a
high-contrast non-color focus outline. In compact layouts the
read-only Details and Event Log surfaces open and close through pointer or the
same keyboard route; their tabs expose an explicit open-state label in addition
to color. Pointer, popup, text-input, and cursor
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
so switching between Classic and Remastered does not require a restart. This is
an integration milestone, not full-bake or release
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
  -DCMAKE_PREFIX_PATH="$PWD/build/dependencies/install"
cmake --build --preset macOS
```

The bootstrap uses fresh CMake caches, rejects modified or incorrectly pinned
dependency checkouts, and verifies universal `x86_64;arm64` libraries targeting
macOS 13.3 before returning successfully. Its reviewed external commits are
phosg `b2e0c12edb7e274a5e20c460f44eee44f49f57ef` and resource_dasm
`27f64c89a5fed855e68c2a5e97b6c6c389d8eb19` (which provides resource_file).

Run `scripts/run-core-tests.sh` for the dependency-free contracts. A configured
full build additionally registers a resource-fork integration test that parses
all five phase-one forks and verifies all 1,520 immutable selected payloads.

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
