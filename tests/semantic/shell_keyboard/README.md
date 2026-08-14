# Semantic shell keyboard interaction contract

This dependency-free contract exercises the real production
`ShellKeyboardInteraction` and composes every returned invocation with a
recording `LegacyCommandBridge`. It does not maintain a parallel keyboard state
machine.

Covered behavior:

- forward and reverse Tab traversal, wrapping, equal-order stability, and
  skipping disabled, omitted, or duplicate-identity controls;
- preserving focus by semantic identifier when a recomposition changes only
  bounds, labels, tab order, vector order, and backing storage;
- Tab traversal on initial key-down and ownership of the matching physical
  key-up;
- Enter and Space activation on initial down and exactly one bridge dispatch
  on the matching physical release;
- consuming repeats without traversing or invoking again;
- treating `(keyboard, scancode)` as the authoritative physical token even if
  the logical keycode changes before release;
- first-activation-wins behavior for simultaneous Enter/Space candidates:
  every accepted physical key is independently owned and released, but only
  the original capture invokes;
- copying the complete control descriptor at key-down while requiring the same
  enabled semantic identity, region, kind, and `UIActionPayload` on release;
- sticky cancellation when a control disappears, becomes disabled, changes
  identity, region, kind, or payload, loses focus, or leaves the semantic route;
- retaining cancelled ownership solely to consume each paired release;
- fail-closed handling for repeats without an owned initial down and duplicate
  semantic identifiers.

An omitted placement represents a hidden control because
`ShellControlPlacement` contains only controls present in the current layout.
Modal transitions use the same fail-closed `route_enabled = false` boundary as
other runtime route changes.

Run the strict standalone test from the repository root:

```sh
tests/semantic/shell_keyboard/run-shell-keyboard-interaction-contract.sh
```

Run with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
REALMZ_ENABLE_SANITIZERS=1 \
  tests/semantic/shell_keyboard/run-shell-keyboard-interaction-contract.sh
```

This test validates the production dependency-free state machine and its
semantic dispatch boundary. It does not replace an application integration test
covering SDL translation, `WindowManager`, or the Classic event queue.
