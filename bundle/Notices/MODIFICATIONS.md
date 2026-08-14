# Modifications

Realmz Remastered — Unofficial is derived from upstream commit
`4089d550ab606172bac850ac055677c36c6ff547` and is plainly identified as a
modified, unofficial work.

The fork adds a semantic presentation boundary, responsive fixed-coverage
layout calculations, distinct live Classic/Remastered renderer routes, a
persistent presentation preference, read-only legacy snapshot and screen-context
adapters, centralized high-density input transforms, a pack-aware
remastered-asset manifest and resolver, a deterministic phase-one asset census,
and a post-selection resource hook that validates immutable payloads without
changing legacy search precedence. It also adds a copy-only user-data importer
with SHA-256 verification and backups, isolates all new saves and preferences
from the original Realmz application-support directory, and adds source,
artifact, provenance, and release verification gates.

The current Remastered renderer provides a live responsive compatibility shell.
Exploration, dungeon, and combat screens keep the complete interactive 800×600
Classic framebuffer inside a widescreen layout with reserved party, action,
details, log, or compact-drawer surfaces populated from read-only snapshot
models. Eligible exploration and dungeon screens add typed movement buttons;
their bridge queues the corresponding legacy key event after release-time
context validation. On eligible exploration and dungeon screens, party cards
now dispatch typed, idempotent selection commands through a separately tagged
top-level event route instead of emulating legacy portrait clicks. These
controls support pointer input and a code-native,
repeat-safe keyboard route with wrapping Tab/Shift-Tab focus and Return/Space
activation. A separate high-contrast focus shape avoids relying on color alone.
Details, logs, and other remaining shell surfaces stay informational. Title and
nested legacy flows such as inventory, shops, and
encounters retain an intact full-frame fallback. Both routes preserve the
original visible tile coverage and never reveal extra terrain.

The development bundle includes a hash-validated mixed runtime manifest. In
Remastered mode it substitutes 11 human-approved raster masters at their
original logical dimensions and leaves 1,509 other covered resources as exact
Classic passthroughs. Cached UI patterns and background pictures are reloaded
when Presentation mode changes, permitting live Classic/Remastered comparison
without stale asset handles. It does not claim complete semantic shell migration,
complete remastered artwork, City of Bywater Mac 7.1.2 provenance, music rights
review, signing, or notarization.

The development bundle converts the upstream 32x32 BMP app-image placeholder
into a valid ICNS container. It is not approved remastered artwork and will be
replaced only through the reviewed style-proof pipeline.
