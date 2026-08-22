#!/usr/bin/env python3
"""Run the provenance-bound Realmz semantic replay equivalence gate.

The existing fixture utility proves byte identity and the existing runner
proves process isolation.  This opt-in gate composes those contracts without
changing either v1 result: it stages one explicitly pinned fixture into a
private retained workspace, holds descriptor-backed attestations over the
source and both input copies while the children run, and only then compares
the four declared replay observables.

The gate is an equivalence comparison, not a correctness oracle.  A successful
result is scoped to the exact manifest, fixture tree, and canonical action-plan
digests in its envelope.  Fixture bytes are never emitted.  The fixture and
protocol workspaces are deliberately retained and reported; this tool never
performs pathname-recursive cleanup.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import stat
import sys
import tempfile
from dataclasses import dataclass
import unicodedata
from typing import Any, NoReturn, Sequence


# Make sibling modules importable both when this file is executed and when a
# test imports it through importlib.util.spec_from_file_location.
SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

import semantic_replay_fixture as fixture_tool  # noqa: E402
import semantic_replay_runner as replay_runner  # noqa: E402


SCHEMA_VERSION = 1
SCHEMA_VERSION_V2 = 2
SCHEMA_VERSION_V3 = 3
SUPPORTED_SCHEMA_VERSIONS = frozenset(
    {SCHEMA_VERSION, SCHEMA_VERSION_V2, SCHEMA_VERSION_V3}
)
COMPARISON_CONTRACT = "realmz.semantic-replay.exact.v1"
COMPARISON_CONTRACT_V2 = "realmz.semantic-replay.exact.v2"
COMPARISON_CONTRACT_V3 = "realmz.semantic-replay.exact.v3"
SETTLEMENT_BARRIER = replay_runner.SETTLEMENT_BARRIER
SEMANTIC_NOT_EVALUATED = "not_evaluated"
SEMANTIC_EQUIVALENT = "equivalent"
SEMANTIC_NOT_EQUIVALENT = "not_equivalent"

MAX_JSON_BYTES = replay_runner.MAX_JSON_BYTES
MAX_PATH_BYTES = replay_runner.MAX_PATH_BYTES
SHA256_HEX_LENGTH = 64
MAX_ERROR_MESSAGE_LENGTH = 4096
NATIVE_V1_ACTION_KIND = "move_party"
NATIVE_V1_ACTION_ARGUMENT = "command"
NATIVE_V2_SELECT_ACTION_KIND = "select_party_member"
NATIVE_V2_SELECT_ACTION_ARGUMENT = "member"
NATIVE_V3_SWITCH_ACTION_KIND = "switch_weapon_set"
NATIVE_V3_SWITCH_ACTION_ARGUMENT = "combatant"
NATIVE_V1_MOVEMENT_COMMANDS = (
    "step_forward",
    "step_backward",
    "turn_left",
    "turn_right",
    "north",
    "northeast",
    "east",
    "southeast",
    "south",
    "southwest",
    "west",
    "northwest",
)
_NATIVE_V1_MOVEMENT_COMMAND_SET = frozenset(NATIVE_V1_MOVEMENT_COMMANDS)

EXIT_NOT_EQUIVALENT = 1
EXIT_USAGE = 2
EXIT_REQUEST = 3
EXIT_FIXTURE = 4
EXIT_STAGING = 5
EXIT_RUNNER = 6
EXIT_ATTESTATION = 7
EXIT_INTERNAL = 8
EXIT_INTERRUPTED = 130

REQUEST_FIELDS = frozenset(
    {
        "schema_version",
        "manifest",
        "source_root",
        "fixture_manifest_sha256",
        "fixture_tree_sha256",
        "executable",
        "output_slot",
        "actions",
        "timeout_seconds",
        "rng_seed",
        "rng_stream",
    }
)
COMPARED_FIELDS = (
    "state_sha256",
    "save_tree_sha256",
    "settled_action_count",
    "rng_draw_count",
)
MISMATCHABLE_FIELDS = (
    "state_sha256",
    "save_tree_sha256",
    "rng_draw_count",
)
COMPLETED_ENVELOPE_FIELDS = frozenset(
    {
        "schema_version",
        "status",
        "semantic_equivalence",
        "comparison",
        "fixture_evidence",
        "replay_profile",
        "runner_envelope",
        "fixture_workspace_retention",
    }
)
PROFILE_INSPECTION_FIELDS = frozenset(
    {
        "schema_version",
        "status",
        "fixture_manifest_sha256",
        "fixture_tree_sha256",
        "actions_sha256",
        "action_count",
        "input_slot",
        "output_slot",
        "settlement_barrier",
        "rng_seed",
        "rng_stream",
    }
)


class EquivalenceGateError(Exception):
    """A stable, user-facing live-gate failure."""

    def __init__(
        self,
        code: str,
        message: str,
        exit_code: int,
        *,
        schema_version: int = SCHEMA_VERSION,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.exit_code = exit_code
        self.schema_version = schema_version
        self.fixture_workspace_retention: dict[str, object] | None = None
        self.runner_workspace_retention: dict[str, object] | None = None
        self.secondary_error: dict[str, str] | None = None


class _DuplicateJsonKey(ValueError):
    pass


@dataclass(frozen=True)
class EquivalenceRequest:
    schema_version: int
    manifest: Path
    source_root: Path
    fixture_manifest_sha256: str
    fixture_tree_sha256: str
    executable: Path
    output_slot: str
    actions: tuple[replay_runner.Action, ...]
    timeout_seconds: float
    rng_seed: str
    rng_stream: str


def _request_error(message: str) -> NoReturn:
    raise EquivalenceGateError("request.invalid", message, EXIT_REQUEST)


def _json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateJsonKey(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> NoReturn:
    raise ValueError(f"non-finite JSON number: {value}")


def _is_plain_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_supported_schema_version(value: object) -> bool:
    return _is_plain_int(value) and value in SUPPORTED_SCHEMA_VERSIONS


def _schema_version_hint(value: object) -> int:
    if isinstance(value, dict):
        candidate = value.get("schema_version")
        if _is_supported_schema_version(candidate):
            return candidate
    if isinstance(value, EquivalenceRequest):
        candidate = value.schema_version
        if _is_supported_schema_version(candidate):
            return candidate
    return SCHEMA_VERSION


def _attach_schema_version(error: BaseException, schema_version: int) -> None:
    try:
        setattr(error, "schema_version", schema_version)
    except BaseException:  # pragma: no cover - defensive exception annotation
        pass


def _validate_schema_version(value: object) -> int:
    if not _is_supported_schema_version(value):
        _request_error("schema_version must be 1, 2, or 3")
    return value


def _comparison_contract(schema_version: int) -> str:
    if not _is_supported_schema_version(schema_version):
        _request_error("schema_version must be 1, 2, or 3")
    if schema_version == SCHEMA_VERSION:
        return COMPARISON_CONTRACT
    if schema_version == SCHEMA_VERSION_V2:
        return COMPARISON_CONTRACT_V2
    if schema_version == SCHEMA_VERSION_V3:
        return COMPARISON_CONTRACT_V3
    _request_error("schema_version must be 1, 2, or 3")


def _contains_control(value: str) -> bool:
    return any(unicodedata.category(character) == "Cc" for character in value)


def _require_fields(value: dict[str, Any], expected: frozenset[str], context: str) -> None:
    actual = frozenset(value)
    unknown = sorted(actual - expected)
    missing = sorted(expected - actual)
    if unknown:
        _request_error(f"{context} has unknown field: {unknown[0]}")
    if missing:
        _request_error(f"{context} is missing field: {missing[0]}")


def _read_json(path: Path) -> object:
    flags = os.O_RDONLY
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    if hasattr(os, "O_NONBLOCK"):
        flags |= os.O_NONBLOCK
    try:
        descriptor = os.open(path, flags)
    except (OSError, ValueError) as error:
        detail = getattr(error, "strerror", None) or str(error)
        _request_error(f"cannot open equivalence request: {detail}")

    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode):
            _request_error("equivalence request must be a regular file")
        if before.st_size > MAX_JSON_BYTES:
            _request_error(f"equivalence request exceeds {MAX_JSON_BYTES} bytes")
        chunks: list[bytes] = []
        remaining = MAX_JSON_BYTES + 1
        while remaining:
            chunk = os.read(descriptor, min(64 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        if remaining == 0:
            _request_error(f"equivalence request exceeds {MAX_JSON_BYTES} bytes")
        after = os.fstat(descriptor)
        before_identity = (
            before.st_dev,
            before.st_ino,
            before.st_size,
            before.st_mtime_ns,
            before.st_ctime_ns,
        )
        after_identity = (
            after.st_dev,
            after.st_ino,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        if before_identity != after_identity or sum(map(len, chunks)) != after.st_size:
            _request_error("equivalence request changed while it was read")
    finally:
        os.close(descriptor)

    try:
        text = b"".join(chunks).decode("utf-8")
    except UnicodeDecodeError:
        _request_error("equivalence request is not valid UTF-8")
    try:
        return json.loads(
            text,
            object_pairs_hook=_json_object,
            parse_constant=_reject_json_constant,
        )
    except (
        _DuplicateJsonKey,
        ValueError,
        json.JSONDecodeError,
        RecursionError,
    ) as error:
        _request_error(f"equivalence request is not valid strict JSON: {error}")


def _validate_canonical_path(
    value: object,
    context: str,
    *,
    directory: bool,
    executable: bool = False,
) -> Path:
    if not isinstance(value, str) or not value:
        _request_error(f"{context} must be a non-empty string")
    if _contains_control(value) or unicodedata.normalize("NFC", value) != value:
        _request_error(f"{context} must be NFC text without control characters")
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError:
        _request_error(f"{context} is not valid UTF-8 text")
    if len(encoded) > MAX_PATH_BYTES:
        _request_error(f"{context} exceeds {MAX_PATH_BYTES} UTF-8 bytes")
    path = Path(value)
    if not path.is_absolute() or os.path.normpath(value) != value:
        _request_error(f"{context} must be an absolute normalized path")
    try:
        resolved = path.resolve(strict=True)
    except (OSError, RuntimeError) as error:
        _request_error(f"{context} does not exist: {error}")
    if str(resolved) != value:
        _request_error(
            f"{context} must not contain symbolic-link or noncanonical components"
        )
    try:
        status = path.stat(follow_symlinks=False)
    except OSError as error:
        _request_error(f"cannot inspect {context}: {error}")
    if directory:
        if not stat.S_ISDIR(status.st_mode):
            _request_error(f"{context} must be an existing directory")
    elif not stat.S_ISREG(status.st_mode):
        _request_error(f"{context} must be an existing regular file")
    if executable and not os.access(path, os.X_OK):
        _request_error(f"{context} must be executable")
    return path


def _validate_slot(value: object, context: str) -> str:
    if not isinstance(value, str) or len(value) != 1 or not "A" <= value <= "J":
        _request_error(f"{context} must be one uppercase Classic slot letter A through J")
    return value


def _validate_sha256(value: object, context: str) -> str:
    if not isinstance(value, str) or len(value) != SHA256_HEX_LENGTH:
        _request_error(f"{context} must be a lowercase SHA-256 digest")
    if any(character not in "0123456789abcdef" for character in value):
        _request_error(f"{context} must be a lowercase SHA-256 digest")
    return value


def _validate_timeout(value: object) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        _request_error("timeout_seconds must be a finite number")
    try:
        timeout = float(value)
    except OverflowError:
        _request_error("timeout_seconds must be a finite number")
    if not math.isfinite(timeout):
        _request_error("timeout_seconds must be a finite number")
    if not replay_runner.MIN_TIMEOUT_SECONDS <= timeout <= replay_runner.MAX_TIMEOUT_SECONDS:
        _request_error(
            "timeout_seconds must be between "
            f"{replay_runner.MIN_TIMEOUT_SECONDS} and "
            f"{replay_runner.MAX_TIMEOUT_SECONDS}"
        )
    return timeout


def _validate_native_v1_actions(
    actions: Sequence[replay_runner.Action],
) -> tuple[replay_runner.Action, ...]:
    """Enforce the deliberately movement-only native v1 action vocabulary."""

    if len(actions) > replay_runner.MAX_ACTIONS:
        _request_error(
            f"actions exceeds the {replay_runner.MAX_ACTIONS}-action limit"
        )
    validated: list[replay_runner.Action] = []
    for index, action in enumerate(actions):
        context = f"actions[{index}]"
        if not isinstance(action, replay_runner.Action):
            _request_error(f"{context} is not a normalized replay action")
        if not replay_runner._is_plain_int(action.ordinal) or action.ordinal != index:
            _request_error(f"{context}.ordinal must equal its zero-based array index")
        if not isinstance(action.kind, str) or action.kind != NATIVE_V1_ACTION_KIND:
            _request_error(
                f"{context}.kind must be {NATIVE_V1_ACTION_KIND} for native v1"
            )
        if not isinstance(action.arguments, dict) or set(action.arguments) != {
            NATIVE_V1_ACTION_ARGUMENT
        }:
            _request_error(
                f"{context}.arguments must contain exactly the "
                f"{NATIVE_V1_ACTION_ARGUMENT} field"
            )
        command = action.arguments[NATIVE_V1_ACTION_ARGUMENT]
        if not isinstance(command, str):
            _request_error(f"{context}.arguments.command must be a string")
        if command not in _NATIVE_V1_MOVEMENT_COMMAND_SET:
            _request_error(
                f"{context}.arguments.command is not a supported native v1 "
                "movement command"
            )
        validated.append(action)
    return tuple(validated)


def _validate_native_v2_actions(
    actions: Sequence[replay_runner.Action],
) -> tuple[replay_runner.Action, ...]:
    """Enforce the closed native v2 movement-and-selection vocabulary."""

    if len(actions) > replay_runner.MAX_ACTIONS:
        _request_error(
            f"actions exceeds the {replay_runner.MAX_ACTIONS}-action limit"
        )
    validated: list[replay_runner.Action] = []
    for index, action in enumerate(actions):
        context = f"actions[{index}]"
        if not isinstance(action, replay_runner.Action):
            _request_error(f"{context} is not a normalized replay action")
        if not replay_runner._is_plain_int(action.ordinal) or action.ordinal != index:
            _request_error(f"{context}.ordinal must equal its zero-based array index")
        if not isinstance(action.kind, str):
            _request_error(
                f"{context}.kind must be move_party or select_party_member for native v2"
            )
        if action.kind == NATIVE_V1_ACTION_KIND:
            if not isinstance(action.arguments, dict) or set(action.arguments) != {
                NATIVE_V1_ACTION_ARGUMENT
            }:
                _request_error(
                    f"{context}.arguments must contain exactly the "
                    f"{NATIVE_V1_ACTION_ARGUMENT} field for native v2 move_party"
                )
            command = action.arguments[NATIVE_V1_ACTION_ARGUMENT]
            if not isinstance(command, str):
                _request_error(f"{context}.arguments.command must be a string")
            if command not in _NATIVE_V1_MOVEMENT_COMMAND_SET:
                _request_error(
                    f"{context}.arguments.command is not a supported native v2 "
                    "movement command"
                )
        elif action.kind == NATIVE_V2_SELECT_ACTION_KIND:
            if not isinstance(action.arguments, dict) or set(action.arguments) != {
                NATIVE_V2_SELECT_ACTION_ARGUMENT
            }:
                _request_error(
                    f"{context}.arguments must contain exactly the "
                    f"{NATIVE_V2_SELECT_ACTION_ARGUMENT} field for native v2 "
                    "select_party_member"
                )
            member = action.arguments[NATIVE_V2_SELECT_ACTION_ARGUMENT]
            if not replay_runner._is_plain_int(member) or not 0 <= member <= 5:
                _request_error(
                    f"{context}.arguments.member must be a plain integer from 0 through 5"
                )
        else:
            _request_error(
                f"{context}.kind must be move_party or select_party_member for native v2"
            )
        validated.append(action)
    return tuple(validated)


def _validate_native_v3_actions(
    actions: Sequence[replay_runner.Action],
) -> tuple[replay_runner.Action, ...]:
    """Enforce the closed native v3 movement, selection, and weapon-set vocabulary."""

    if len(actions) > replay_runner.MAX_ACTIONS:
        _request_error(
            f"actions exceeds the {replay_runner.MAX_ACTIONS}-action limit"
        )
    validated: list[replay_runner.Action] = []
    for index, action in enumerate(actions):
        context = f"actions[{index}]"
        if not isinstance(action, replay_runner.Action):
            _request_error(f"{context} is not a normalized replay action")
        if not replay_runner._is_plain_int(action.ordinal) or action.ordinal != index:
            _request_error(f"{context}.ordinal must equal its zero-based array index")
        if not isinstance(action.kind, str):
            _request_error(
                f"{context}.kind must be move_party, select_party_member, or "
                "switch_weapon_set for native v3"
            )
        if action.kind == NATIVE_V1_ACTION_KIND:
            if not isinstance(action.arguments, dict) or set(action.arguments) != {
                NATIVE_V1_ACTION_ARGUMENT
            }:
                _request_error(
                    f"{context}.arguments must contain exactly the "
                    f"{NATIVE_V1_ACTION_ARGUMENT} field for native v3 move_party"
                )
            command = action.arguments[NATIVE_V1_ACTION_ARGUMENT]
            if not isinstance(command, str):
                _request_error(f"{context}.arguments.command must be a string")
            if command not in _NATIVE_V1_MOVEMENT_COMMAND_SET:
                _request_error(
                    f"{context}.arguments.command is not a supported native v3 "
                    "movement command"
                )
        elif action.kind == NATIVE_V2_SELECT_ACTION_KIND:
            if not isinstance(action.arguments, dict) or set(action.arguments) != {
                NATIVE_V2_SELECT_ACTION_ARGUMENT
            }:
                _request_error(
                    f"{context}.arguments must contain exactly the "
                    f"{NATIVE_V2_SELECT_ACTION_ARGUMENT} field for native v3 "
                    "select_party_member"
                )
            member = action.arguments[NATIVE_V2_SELECT_ACTION_ARGUMENT]
            if not replay_runner._is_plain_int(member) or not 0 <= member <= 5:
                _request_error(
                    f"{context}.arguments.member must be a plain integer from 0 through 5"
                )
        elif action.kind == NATIVE_V3_SWITCH_ACTION_KIND:
            if not isinstance(action.arguments, dict) or set(action.arguments) != {
                NATIVE_V3_SWITCH_ACTION_ARGUMENT
            }:
                _request_error(
                    f"{context}.arguments must contain exactly the "
                    f"{NATIVE_V3_SWITCH_ACTION_ARGUMENT} field for native v3 "
                    "switch_weapon_set"
                )
            combatant = action.arguments[NATIVE_V3_SWITCH_ACTION_ARGUMENT]
            if not replay_runner._is_plain_int(combatant) or not 0 <= combatant <= 5:
                _request_error(
                    f"{context}.arguments.combatant must be a plain integer from 0 through 5"
                )
        else:
            _request_error(
                f"{context}.kind must be move_party, select_party_member, or "
                "switch_weapon_set for native v3"
            )
        validated.append(action)
    return tuple(validated)


def _validate_native_actions(
    actions: Sequence[replay_runner.Action], schema_version: int
) -> tuple[replay_runner.Action, ...]:
    if not _is_supported_schema_version(schema_version):
        _request_error("schema_version must be 1, 2, or 3")
    if schema_version == SCHEMA_VERSION:
        return _validate_native_v1_actions(actions)
    if schema_version == SCHEMA_VERSION_V2:
        return _validate_native_v2_actions(actions)
    if schema_version == SCHEMA_VERSION_V3:
        return _validate_native_v3_actions(actions)
    _request_error("schema_version must be 1, 2, or 3")


def _validate_normalized_request(request: EquivalenceRequest) -> None:
    """Revalidate every field on the public normalized request value type."""

    if not isinstance(request, EquivalenceRequest):
        _request_error("request must be a normalized EquivalenceRequest")
    schema_version = _validate_schema_version(request.schema_version)
    for value, context, directory, executable in (
        (request.manifest, "manifest", False, False),
        (request.source_root, "source_root", True, False),
        (request.executable, "executable", False, True),
    ):
        if not isinstance(value, Path):
            _request_error(f"{context} must be a normalized Path value")
        _validate_canonical_path(
            str(value),
            context,
            directory=directory,
            executable=executable,
        )
    _validate_sha256(
        request.fixture_manifest_sha256,
        "fixture_manifest_sha256",
    )
    _validate_sha256(request.fixture_tree_sha256, "fixture_tree_sha256")
    _validate_slot(request.output_slot, "output_slot")
    if not isinstance(request.actions, tuple):
        _request_error("actions must be a normalized tuple")
    _validate_native_actions(request.actions, schema_version)
    _validate_timeout(request.timeout_seconds)
    replay_runner._validate_rng_hex(request.rng_seed, "rng_seed", _request_error)
    replay_runner._validate_rng_hex(
        request.rng_stream,
        "rng_stream",
        _request_error,
    )


def _parse_request_impl(value: object) -> EquivalenceRequest:
    """Strictly validate all non-fixture-mutation inputs."""

    if not isinstance(value, dict):
        _request_error("equivalence request must be a JSON object")
    _require_fields(value, REQUEST_FIELDS, "equivalence request")
    version = _validate_schema_version(value["schema_version"])
    manifest = _validate_canonical_path(
        value["manifest"], "manifest", directory=False
    )
    source_root = _validate_canonical_path(
        value["source_root"], "source_root", directory=True
    )
    fixture_manifest_sha256 = _validate_sha256(
        value["fixture_manifest_sha256"], "fixture_manifest_sha256"
    )
    fixture_tree_sha256 = _validate_sha256(
        value["fixture_tree_sha256"], "fixture_tree_sha256"
    )
    executable = _validate_canonical_path(
        value["executable"], "executable", directory=False, executable=True
    )
    output_slot = _validate_slot(value["output_slot"], "output_slot")
    actions = _validate_native_actions(
        replay_runner._validate_actions(value["actions"], _request_error),
        version,
    )
    timeout_seconds = _validate_timeout(value["timeout_seconds"])
    rng_seed = replay_runner._validate_rng_hex(
        value["rng_seed"], "rng_seed", _request_error
    )
    rng_stream = replay_runner._validate_rng_hex(
        value["rng_stream"], "rng_stream", _request_error
    )
    return EquivalenceRequest(
        schema_version=version,
        manifest=manifest,
        source_root=source_root,
        fixture_manifest_sha256=fixture_manifest_sha256,
        fixture_tree_sha256=fixture_tree_sha256,
        executable=executable,
        output_slot=output_slot,
        actions=actions,
        timeout_seconds=timeout_seconds,
        rng_seed=rng_seed,
        rng_stream=rng_stream,
    )


def parse_request(value: object) -> EquivalenceRequest:
    schema_version = _schema_version_hint(value)
    try:
        return _parse_request_impl(value)
    except BaseException as error:
        _attach_schema_version(error, schema_version)
        raise


def load_request(path: Path) -> EquivalenceRequest:
    """Read and strictly validate a supported equivalence request."""

    return parse_request(_read_json(path))


def _require_private_directory(path: Path, context: str) -> tuple[int, int]:
    try:
        status = path.stat(follow_symlinks=False)
    except OSError as error:
        raise EquivalenceGateError(
            "gate.workspace_invalid",
            f"cannot inspect {context}: {error}",
            EXIT_STAGING,
        ) from error
    if (
        not stat.S_ISDIR(status.st_mode)
        or status.st_uid != os.getuid()
        or stat.S_IMODE(status.st_mode) != 0o700
    ):
        raise EquivalenceGateError(
            "gate.workspace_invalid",
            f"{context} must be a private current-user-owned directory",
            EXIT_STAGING,
        )
    return (status.st_dev, status.st_ino)


def _make_gate_workspace(
    *,
    retention_out: dict[str, object] | None = None,
) -> tuple[Path, tuple[int, int], Path, Path]:
    workspace: Path | None = None
    workspace_identity: tuple[int, int] | None = None
    try:
        workspace = Path(tempfile.mkdtemp(prefix="realmz-semantic-equivalence-"))
        workspace = workspace.resolve(strict=True)
        workspace_identity = _require_private_directory(workspace, "gate workspace")
        classic_root = workspace / "classic-user"
        semantic_root = workspace / "semantic-user"
        for user_root in (classic_root, semantic_root):
            user_root.mkdir(mode=0o700)
            _require_private_directory(user_root, "staged user-data root")
            save_root = user_root / "Save"
            save_root.mkdir(mode=0o700)
            _require_private_directory(save_root, "staged Save root")
        _register_fixture_retention(
            retention_out,
            _workspace_retention_record(workspace, workspace_identity),
        )
    except BaseException as error:
        if isinstance(error, EquivalenceGateError):
            wrapped = error
        elif isinstance(error, (OSError, RuntimeError)):
            wrapped = EquivalenceGateError(
                "gate.workspace_invalid",
                f"cannot create private gate workspace: {error}",
                EXIT_STAGING,
            )
        else:
            wrapped = error
        if workspace is not None:
            retention = _workspace_retention_record(workspace, workspace_identity)
            _register_fixture_retention(retention_out, retention)
            _attach_fixture_retention(wrapped, retention)
        if wrapped is error:
            raise
        raise wrapped from error
    assert workspace is not None and workspace_identity is not None
    return workspace, workspace_identity, classic_root, semantic_root


def _workspace_retention_record(
    workspace: Path, expected_identity: tuple[int, int] | None
) -> dict[str, object]:
    authoritative = False
    if expected_identity is not None:
        try:
            status = workspace.stat(follow_symlinks=False)
        except OSError:
            pass
        else:
            authoritative = (
                stat.S_ISDIR(status.st_mode)
                and (status.st_dev, status.st_ino) == expected_identity
                and status.st_uid == os.getuid()
                and stat.S_IMODE(status.st_mode) == 0o700
            )
    if authoritative:
        notice = (
            f"private fixture workspace retained at {workspace}; cleanup was not attempted"
        )
    else:
        notice = (
            f"fixture workspace namespace changed; {workspace} is only a "
            "non-authoritative candidate path, and the original private workspace "
            "may remain at an unknown renamed location; cleanup was not attempted"
        )
    return {
        "cleanup_attempted": False,
        "candidate_path": str(workspace),
        "path_authoritative": authoritative,
        "contains_fixture_bytes": True,
        "notice": _bounded_message(notice),
    }


def _attach_fixture_retention(
    error: BaseException, retention: dict[str, object]
) -> None:
    setattr(error, "fixture_workspace_retention", retention)
    if isinstance(error, EquivalenceGateError):
        error.fixture_workspace_retention = retention


def _register_fixture_retention(
    retention_out: dict[str, object] | None,
    retention: dict[str, object],
) -> None:
    if retention_out is not None:
        retention_out["fixture_workspace_retention"] = retention


def _registered_retention(
    retention_out: dict[str, object] | None,
    key: str,
) -> dict[str, object] | None:
    if retention_out is None:
        return None
    retention = retention_out.get(key)
    return retention if isinstance(retention, dict) else None


def _attach_registered_retentions(
    error: BaseException,
    retention_out: dict[str, object] | None,
) -> None:
    fixture_retention = _registered_retention(
        retention_out,
        "fixture_workspace_retention",
    )
    if (
        fixture_retention is not None
        and getattr(error, "fixture_workspace_retention", None) is None
    ):
        _attach_fixture_retention(error, fixture_retention)
    runner_retention = _registered_retention(
        retention_out,
        "workspace_retention",
    )
    if (
        runner_retention is not None
        and getattr(error, "runner_workspace_retention", None) is None
    ):
        setattr(error, "runner_workspace_retention", runner_retention)
        if isinstance(error, EquivalenceGateError):
            error.runner_workspace_retention = runner_retention


def _runner_retention(error: BaseException) -> dict[str, object] | None:
    for attribute in ("runner_workspace_retention", "workspace_retention"):
        candidate = getattr(error, attribute, None)
        if isinstance(candidate, dict):
            normalized = dict(candidate)
            notice = normalized.get("notice")
            if isinstance(notice, str):
                normalized["notice"] = _bounded_message(notice)
            return normalized
    return None


def _bounded_message(message: str) -> str:
    if not message:
        return "unspecified equivalence-gate failure"
    return message[:MAX_ERROR_MESSAGE_LENGTH]


def _error_record(code: str, message: str) -> dict[str, str]:
    return {"code": code, "message": _bounded_message(message)}


def _public_runner_message(code: str) -> str:
    # Runner diagnostics may include bounded child stdout/stderr excerpts.  A
    # child is capable of printing private fixture contents, so the equivalence
    # envelope preserves the stable code and retention record but never copies
    # the raw runner message.
    return f"semantic replay runner failed with code {code}"


def _fixture_error(error: fixture_tool.FixtureError) -> EquivalenceGateError:
    return EquivalenceGateError(error.code, error.message, error.exit_code)


def _runner_error(error: replay_runner.ReplayRunnerError) -> EquivalenceGateError:
    wrapped = EquivalenceGateError(
        error.code,
        _public_runner_message(error.code),
        error.exit_code,
        schema_version=getattr(error, "schema_version", SCHEMA_VERSION),
    )
    wrapped.runner_workspace_retention = _runner_retention(error)
    return wrapped


def _actions_sha256(
    actions: Sequence[replay_runner.Action],
    schema_version: int = SCHEMA_VERSION,
) -> str:
    # Action values are limited to exact JSON integers, booleans, and printable
    # ASCII strings.  Sorted keys and the compact separators therefore produce
    # the RFC 8785 representation for the complete admitted action vocabulary.
    encoded = json.dumps(
        [action.as_json() for action in actions],
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    digest = hashlib.sha256()
    if not _is_supported_schema_version(schema_version):
        _request_error("schema_version must be 1, 2, or 3")
    if schema_version == SCHEMA_VERSION:
        digest.update(b"realmz-semantic-replay-actions-v1\n")
    elif schema_version == SCHEMA_VERSION_V2:
        digest.update(b"realmz-semantic-replay-actions-v2\n")
    elif schema_version == SCHEMA_VERSION_V3:
        digest.update(b"realmz-semantic-replay-actions-v3\n")
    else:
        _request_error("schema_version must be 1, 2, or 3")
    digest.update(encoded)
    return digest.hexdigest()


def _generated_runner_request(
    request: EquivalenceRequest,
    *,
    classic_root: Path,
    semantic_root: Path,
    input_slot: str,
) -> replay_runner.RunRequest:
    value: dict[str, object] = {
        "schema_version": request.schema_version,
        "executable": str(request.executable),
        "classic_user_data_root": str(classic_root),
        "semantic_user_data_root": str(semantic_root),
        "input_slot": input_slot,
        "output_slot": request.output_slot,
        "actions": [action.as_json() for action in request.actions],
        "timeout_seconds": request.timeout_seconds,
        "rng_seed": request.rng_seed,
        "rng_stream": request.rng_stream,
    }
    try:
        return replay_runner._parse_request(value)
    except replay_runner.ReplayRunnerError as error:
        raise _runner_error(error) from error


def compare_child_results(
    child_results: object,
    schema_version: int = SCHEMA_VERSION,
) -> tuple[dict[str, object], tuple[str, ...]]:
    """Compare only the four versioned equivalence observables in canonical order."""

    error_schema_version = (
        schema_version
        if _is_supported_schema_version(schema_version)
        else SCHEMA_VERSION
    )

    def invalid(message: str) -> NoReturn:
        raise EquivalenceGateError(
            "runner.envelope_invalid",
            message,
            EXIT_RUNNER,
            schema_version=error_schema_version,
        )

    if not _is_supported_schema_version(schema_version):
        invalid("comparison schema_version must be 1, 2, or 3")

    if not isinstance(child_results, list) or len(child_results) != 2:
        invalid("runner envelope did not contain exactly two child results")
    classic, semantic = child_results
    if not isinstance(classic, dict) or not isinstance(semantic, dict):
        invalid("runner child results must be objects")
    for route, result in (("classic", classic), ("semantic", semantic)):
        result_schema_version = result.get("schema_version")
        if (
            not _is_plain_int(result_schema_version)
            or result_schema_version != schema_version
        ):
            invalid(
                f"runner {route} child result schema_version must be {schema_version}"
            )
    comparison: dict[str, object] = {
        "contract": _comparison_contract(schema_version)
    }
    for field in COMPARED_FIELDS:
        if field not in classic or field not in semantic:
            invalid(f"runner child result is missing comparison field: {field}")
        comparison[field] = {
            "classic": classic[field],
            "semantic": semantic[field],
        }
    settled = comparison["settled_action_count"]
    assert isinstance(settled, dict)
    if settled["classic"] != settled["semantic"]:
        invalid("runner returned inconsistent settled_action_count values")
    mismatched = [
        field
        for field in MISMATCHABLE_FIELDS
        if classic[field] != semantic[field]
    ]
    comparison["mismatched_fields"] = mismatched
    return comparison, tuple(mismatched)


def _lease_and_run(
    request: EquivalenceRequest,
    *,
    classic_root: Path,
    semantic_root: Path,
    input_slot: str,
    expected_file_count: int,
    expected_total_file_bytes: int,
    retention_out: dict[str, object] | None = None,
) -> tuple[dict[str, object], fixture_tool.FixtureSetEvidence]:
    try:
        lease = fixture_tool.acquire_fixture_set_lease(
            request.manifest,
            request.source_root,
            classic_root / "Save" / f"Game {input_slot}",
            semantic_root / "Save" / f"Game {input_slot}",
        )
    except fixture_tool.FixtureError as error:
        raise _fixture_error(error) from error

    initial_evidence = lease.evidence
    if (
        initial_evidence.manifest_sha256 != request.fixture_manifest_sha256
        or initial_evidence.tree_sha256 != request.fixture_tree_sha256
        or initial_evidence.slot != input_slot
        or initial_evidence.file_count != expected_file_count
        or initial_evidence.total_file_bytes != expected_total_file_bytes
    ):
        finalization_detail = ""
        try:
            lease.finalize()
        except fixture_tool.FixtureError as error:
            finalization_detail = f"; fixture finalization also failed: {error.message}"
        raise EquivalenceGateError(
            "fixture.attestation_failed",
            "staged fixture evidence changed before child execution"
            + finalization_detail,
            EXIT_ATTESTATION,
        )
    runner_envelope: dict[str, object] | None = None
    runner_failure: BaseException | None = None
    try:
        bound_request = _generated_runner_request(
            request,
            classic_root=classic_root,
            semantic_root=semantic_root,
            input_slot=input_slot,
        )
        try:
            lease.verify_runner_input_identities(
                bound_request.classic_input_slot_identity,
                bound_request.semantic_input_slot_identity,
            )
        except fixture_tool.FixtureError as error:
            raise _fixture_error(error) from error
        runner_envelope = replay_runner.run_request(
            bound_request,
            retention_out=retention_out,
        )
    except BaseException as error:
        runner_failure = error

    final_evidence: fixture_tool.FixtureSetEvidence | None = None
    attestation_failure: fixture_tool.FixtureError | None = None
    try:
        final_evidence = lease.finalize()
    except fixture_tool.FixtureError as error:
        attestation_failure = error
    except BaseException as error:
        retention = _runner_retention(runner_failure) if runner_failure else None
        if retention is None and runner_envelope is not None:
            candidate = runner_envelope.get("workspace_retention")
            if isinstance(candidate, dict):
                retention = candidate
        if retention is None:
            retention = _registered_retention(
                retention_out,
                "workspace_retention",
            )
        if retention is not None:
            setattr(error, "runner_workspace_retention", retention)
        raise

    if attestation_failure is not None:
        wrapped = EquivalenceGateError(
            "fixture.attestation_failed",
            attestation_failure.message,
            EXIT_ATTESTATION,
        )
        if runner_failure is not None:
            if isinstance(runner_failure, replay_runner.ReplayRunnerError):
                wrapped.secondary_error = {
                    "code": runner_failure.code,
                    "message": _public_runner_message(runner_failure.code),
                }
            elif isinstance(runner_failure, EquivalenceGateError):
                wrapped.secondary_error = _error_record(
                    runner_failure.code,
                    runner_failure.message,
                )
            elif isinstance(runner_failure, KeyboardInterrupt):
                wrapped.secondary_error = {
                    "code": "gate.interrupted",
                    "message": "semantic replay equivalence run was interrupted",
                }
            else:
                wrapped.secondary_error = {
                    "code": "runner.unexpected_error",
                    "message": f"runner failed with {type(runner_failure).__name__}",
                }
            wrapped.runner_workspace_retention = _runner_retention(runner_failure)
            if wrapped.runner_workspace_retention is None:
                wrapped.runner_workspace_retention = _registered_retention(
                    retention_out,
                    "workspace_retention",
                )
        elif runner_envelope is not None:
            candidate = runner_envelope.get("workspace_retention")
            if isinstance(candidate, dict):
                wrapped.runner_workspace_retention = candidate
        if wrapped.runner_workspace_retention is None:
            wrapped.runner_workspace_retention = _registered_retention(
                retention_out,
                "workspace_retention",
            )
        raise wrapped from attestation_failure

    assert final_evidence is not None
    if final_evidence != initial_evidence:  # pragma: no cover - lease contract guard
        raise EquivalenceGateError(
            "fixture.attestation_failed",
            "fixture evidence changed across the held lease",
            EXIT_ATTESTATION,
        )

    if runner_failure is not None:
        registered = _registered_retention(
            retention_out,
            "workspace_retention",
        )
        if registered is not None and _runner_retention(runner_failure) is None:
            setattr(runner_failure, "runner_workspace_retention", registered)
        if isinstance(runner_failure, replay_runner.ReplayRunnerError):
            raise _runner_error(runner_failure) from runner_failure
        raise runner_failure
    if runner_envelope is None:  # pragma: no cover - control-flow guard
        raise EquivalenceGateError(
            "runner.envelope_invalid",
            "runner returned no result",
            EXIT_RUNNER,
        )
    return runner_envelope, final_evidence


def _verify_request_fixture(
    request: EquivalenceRequest,
) -> fixture_tool.VerifiedFixture:
    """Perform the read-only request/fixture preflight shared by inspect and run."""

    # EquivalenceRequest is a public value type. Recheck every normalized field
    # so a direct caller cannot bypass parse_request, emit an invalid inspection
    # record, or create private copies before the runner rejects the value.
    _validate_normalized_request(request)

    # Validate the manifest and source before allocating a workspace containing
    # private copies.  Request parsing has already validated every independent
    # caller-controlled execution field.
    try:
        verified = fixture_tool.verify_fixture(request.manifest, request.source_root)
    except fixture_tool.FixtureError as error:
        raise _fixture_error(error) from error
    manifest = verified.manifest
    if verified.manifest_sha256 != request.fixture_manifest_sha256:
        raise EquivalenceGateError(
            "fixture.digest_mismatch",
            "manifest SHA-256 does not match fixture_manifest_sha256",
            EXIT_FIXTURE,
        )
    if manifest.tree_sha256 != request.fixture_tree_sha256:
        raise EquivalenceGateError(
            "fixture.digest_mismatch",
            "manifest tree_sha256 does not match fixture_tree_sha256",
            EXIT_FIXTURE,
        )
    if manifest.slot == request.output_slot:
        raise EquivalenceGateError(
            "request.invalid",
            "output_slot must differ from the fixture manifest input slot",
            EXIT_REQUEST,
        )
    return verified


def _inspect_profile_impl(request: EquivalenceRequest) -> dict[str, object]:
    """Return a privacy-safe, non-executing record of one validated profile."""

    verified = _verify_request_fixture(request)
    profile: dict[str, object] = {
        "schema_version": request.schema_version,
        "status": "profile_inspected",
        "fixture_manifest_sha256": verified.manifest_sha256,
        "fixture_tree_sha256": verified.manifest.tree_sha256,
        "actions_sha256": _actions_sha256(
            request.actions, request.schema_version
        ),
        "action_count": len(request.actions),
        "input_slot": verified.manifest.slot,
        "output_slot": request.output_slot,
        "settlement_barrier": SETTLEMENT_BARRIER,
        "rng_seed": request.rng_seed,
        "rng_stream": request.rng_stream,
    }
    if frozenset(profile) != PROFILE_INSPECTION_FIELDS:  # pragma: no cover
        raise AssertionError(
            f"profile inspection fields drifted from v{request.schema_version}"
        )
    return profile


def inspect_profile(request: EquivalenceRequest) -> dict[str, object]:
    """Validate and inspect one supported replay profile."""

    schema_version = _schema_version_hint(request)
    try:
        return _inspect_profile_impl(request)
    except BaseException as error:
        _attach_schema_version(error, schema_version)
        raise


def inspect_profile_file(path: Path) -> dict[str, object]:
    """Load and inspect one request without staging or launching children."""

    return inspect_profile(load_request(path))


def _run_request_impl(
    request: EquivalenceRequest,
    *,
    retention_out: dict[str, object] | None = None,
) -> dict[str, object]:
    """Stage, execute, attest, compare, and return a completed gate envelope."""

    verified = _verify_request_fixture(request)
    manifest = verified.manifest

    workspace: Path | None = None
    workspace_identity: tuple[int, int] | None = None
    runner_envelope: dict[str, object] | None = None
    try:
        workspace, workspace_identity, classic_root, semantic_root = (
            _make_gate_workspace(retention_out=retention_out)
        )
        try:
            fixture_tool.stage_fixture(
                request.manifest,
                request.source_root,
                classic_root / "Save" / f"Game {manifest.slot}",
                semantic_root / "Save" / f"Game {manifest.slot}",
                expected_manifest_sha256=request.fixture_manifest_sha256,
            )
        except fixture_tool.FixtureError as error:
            raise _fixture_error(error) from error

        runner_envelope, evidence = _lease_and_run(
            request,
            classic_root=classic_root,
            semantic_root=semantic_root,
            input_slot=manifest.slot,
            expected_file_count=len(manifest.files),
            expected_total_file_bytes=sum(record.size for record in manifest.files),
            retention_out=retention_out,
        )
        runner_schema_version = (
            runner_envelope.get("schema_version")
            if isinstance(runner_envelope, dict)
            else None
        )
        if (
            not _is_plain_int(runner_schema_version)
            or runner_schema_version != request.schema_version
        ):
            raise EquivalenceGateError(
                "runner.envelope_invalid",
                "runner envelope schema_version must match the equivalence request",
                EXIT_RUNNER,
            )
        if (
            evidence.manifest_sha256 != request.fixture_manifest_sha256
            or evidence.tree_sha256 != request.fixture_tree_sha256
            or evidence.slot != manifest.slot
            or evidence.file_count != len(manifest.files)
            or evidence.total_file_bytes
            != sum(record.size for record in manifest.files)
        ):
            raise EquivalenceGateError(
                "fixture.attestation_failed",
                "held fixture evidence does not match the verified manifest",
                EXIT_ATTESTATION,
            )

        comparison, mismatched = compare_child_results(
            runner_envelope.get("child_results"),
            request.schema_version,
        )
        semantic_equivalence = (
            SEMANTIC_EQUIVALENT if not mismatched else SEMANTIC_NOT_EQUIVALENT
        )
        retention = _workspace_retention_record(workspace, workspace_identity)
        _register_fixture_retention(retention_out, retention)
        envelope: dict[str, object] = {
            "schema_version": request.schema_version,
            "status": "completed",
            "semantic_equivalence": semantic_equivalence,
            "comparison": comparison,
            "fixture_evidence": {
                "manifest_sha256": evidence.manifest_sha256,
                "tree_sha256": evidence.tree_sha256,
                "slot": evidence.slot,
                "file_count": evidence.file_count,
                "total_file_bytes": evidence.total_file_bytes,
                "independent_copies": True,
                "trees": {
                    "source": {
                        "verified_before": True,
                        "verified_after": True,
                    },
                    "classic_input": {
                        "verified_before": True,
                        "verified_after": True,
                    },
                    "semantic_input": {
                        "verified_before": True,
                        "verified_after": True,
                    },
                },
            },
            "replay_profile": {
                "actions_sha256": _actions_sha256(
                    request.actions, request.schema_version
                ),
                "action_count": len(request.actions),
                "input_slot": evidence.slot,
                "output_slot": request.output_slot,
                "settlement_barrier": SETTLEMENT_BARRIER,
                "rng_seed": request.rng_seed,
                "rng_stream": request.rng_stream,
            },
            "runner_envelope": runner_envelope,
            "fixture_workspace_retention": retention,
        }
        if frozenset(envelope) != COMPLETED_ENVELOPE_FIELDS:  # pragma: no cover
            raise AssertionError(
                "equivalence envelope fields drifted from "
                f"v{request.schema_version}"
            )
        return envelope
    except BaseException as error:
        if (
            isinstance(runner_envelope, dict)
            and getattr(error, "runner_workspace_retention", None) is None
        ):
            candidate = runner_envelope.get("workspace_retention")
            if isinstance(candidate, dict):
                setattr(error, "runner_workspace_retention", candidate)
                if isinstance(error, EquivalenceGateError):
                    error.runner_workspace_retention = candidate
        if workspace is not None:
            retention = _workspace_retention_record(workspace, workspace_identity)
            _register_fixture_retention(retention_out, retention)
            _attach_fixture_retention(error, retention)
        _attach_registered_retentions(error, retention_out)
        raise


def run_request(
    request: EquivalenceRequest,
    *,
    retention_out: dict[str, object] | None = None,
) -> dict[str, object]:
    """Validate and run a supported provenance-bound equivalence request."""

    schema_version = _schema_version_hint(request)
    try:
        return _run_request_impl(request, retention_out=retention_out)
    except BaseException as error:
        _attach_schema_version(error, schema_version)
        raise


def run_request_file(
    path: Path,
    *,
    retention_out: dict[str, object] | None = None,
) -> dict[str, object]:
    """Load a request and execute the provenance-bound equivalence gate."""

    return run_request(load_request(path), retention_out=retention_out)


def _error_envelope(error: EquivalenceGateError) -> dict[str, object]:
    schema_version = getattr(error, "schema_version", SCHEMA_VERSION)
    if not _is_supported_schema_version(schema_version):
        schema_version = SCHEMA_VERSION
    envelope: dict[str, object] = {
        "schema_version": schema_version,
        "status": "error",
        "semantic_equivalence": SEMANTIC_NOT_EVALUATED,
        "error": _error_record(error.code, error.message),
    }
    if error.secondary_error is not None:
        envelope["secondary_error"] = _error_record(
            error.secondary_error["code"],
            error.secondary_error["message"],
        )
    if error.fixture_workspace_retention is not None:
        envelope["fixture_workspace_retention"] = (
            error.fixture_workspace_retention
        )
    runner_retention = _runner_retention(error)
    if runner_retention is not None:
        envelope["runner_workspace_retention"] = runner_retention
    return envelope


def _attach_completed_envelope_retention(
    error: EquivalenceGateError,
    completed: dict[str, object] | None,
) -> None:
    if not isinstance(completed, dict) or completed.get("status") != "completed":
        return
    if error.fixture_workspace_retention is None:
        fixture_retention = completed.get("fixture_workspace_retention")
        if isinstance(fixture_retention, dict):
            error.fixture_workspace_retention = fixture_retention
    if error.runner_workspace_retention is None:
        runner_envelope = completed.get("runner_envelope")
        if isinstance(runner_envelope, dict):
            runner_retention = runner_envelope.get("workspace_retention")
            if isinstance(runner_retention, dict):
                error.runner_workspace_retention = runner_retention


def _emit_json(value: object, stream: Any) -> None:
    stream.write(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    )
    stream.write("\n")


class _JsonArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> NoReturn:
        _emit_json(
            {
                "schema_version": SCHEMA_VERSION,
                "status": "error",
                "semantic_equivalence": SEMANTIC_NOT_EVALUATED,
                "error": _error_record("usage.invalid", message),
            },
            sys.stderr,
        )
        raise SystemExit(EXIT_USAGE)


def _argument_parser() -> argparse.ArgumentParser:
    parser = _JsonArgumentParser(
        description=(
            "Inspect one provenance-declared native replay profile, or stage its fixture, "
            "run isolated Classic and semantic children, and compare their exact "
            "replay observables. Private run workspaces are retained and reported."
        )
    )
    parser.add_argument(
        "--request",
        required=True,
        type=Path,
        help="v1, v2, or v3 semantic replay equivalence request JSON",
    )
    parser.add_argument(
        "--inspect-profile",
        action="store_true",
        help=(
            "validate the request, exact manifest, source tree, and versioned native "
            "action profile without staging or launching children"
        ),
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _argument_parser().parse_args(argv)
    envelope: dict[str, object] | None = None
    retention_out: dict[str, object] = {}
    try:
        if arguments.inspect_profile:
            envelope = inspect_profile_file(arguments.request)
            exit_code = 0
        else:
            envelope = run_request_file(
                arguments.request,
                retention_out=retention_out,
            )
            exit_code = (
                0
                if envelope["semantic_equivalence"] == SEMANTIC_EQUIVALENT
                else EXIT_NOT_EQUIVALENT
            )
        _emit_json(envelope, sys.stdout)
    except EquivalenceGateError as error:
        _attach_registered_retentions(error, retention_out)
        _attach_completed_envelope_retention(error, envelope)
        _emit_json(_error_envelope(error), sys.stderr)
        return error.exit_code
    except KeyboardInterrupt as error:
        schema_version = getattr(error, "schema_version", None)
        if (
            not _is_supported_schema_version(schema_version)
            and isinstance(envelope, dict)
        ):
            schema_version = envelope.get("schema_version")
        if not _is_supported_schema_version(schema_version):
            schema_version = SCHEMA_VERSION
        wrapped = EquivalenceGateError(
            "gate.interrupted",
            "semantic replay equivalence run was interrupted",
            EXIT_INTERRUPTED,
            schema_version=schema_version,
        )
        _attach_registered_retentions(wrapped, retention_out)
        fixture_retention = getattr(error, "fixture_workspace_retention", None)
        if isinstance(fixture_retention, dict):
            wrapped.fixture_workspace_retention = fixture_retention
        runner_retention = _runner_retention(error)
        if runner_retention is not None:
            wrapped.runner_workspace_retention = runner_retention
        _attach_completed_envelope_retention(wrapped, envelope)
        _emit_json(_error_envelope(wrapped), sys.stderr)
        return EXIT_INTERRUPTED
    except Exception as error:
        schema_version = getattr(error, "schema_version", None)
        if (
            not _is_supported_schema_version(schema_version)
            and isinstance(envelope, dict)
        ):
            schema_version = envelope.get("schema_version")
        if not _is_supported_schema_version(schema_version):
            schema_version = SCHEMA_VERSION
        wrapped = EquivalenceGateError(
            "gate.unexpected_error",
            f"unexpected {type(error).__name__}",
            EXIT_INTERNAL,
            schema_version=schema_version,
        )
        _attach_registered_retentions(wrapped, retention_out)
        fixture_retention = getattr(error, "fixture_workspace_retention", None)
        if isinstance(fixture_retention, dict):
            wrapped.fixture_workspace_retention = fixture_retention
        runner_retention = _runner_retention(error)
        if runner_retention is not None:
            wrapped.runner_workspace_retention = runner_retention
        _attach_completed_envelope_retention(wrapped, envelope)
        _emit_json(_error_envelope(wrapped), sys.stderr)
        return EXIT_INTERNAL

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
