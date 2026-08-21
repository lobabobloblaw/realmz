#!/usr/bin/env python3
"""Run two process-isolated semantic replay children under a strict v1 protocol.

This is parent-orchestration infrastructure only.  Realmz recognizes
``--semantic-replay-child``, installs its bounded startup policies, drives the
native action plan, and publishes per-child state and save measurements.  The
runner deliberately does not turn those measurements into an equivalence
verdict.  Its tests use a synthetic child to pin process isolation and protocol
behavior; their output is never evidence of engine or save equivalence.

The supplied executable and its higher same-user filesystem namespace are
trusted.  New process sessions separate legacy globals and let the parent stop
same-session descendants, but this is not a sandbox: a hostile child can
deliberately daemonize outside its session or transiently redirect a trusted
ancestor between identity checkpoints.

The parent invokes the requested absolute executable path twice, without a
shell, in deterministic Classic-then-semantic order.  Direct namespace
mutation guards and repeated identity checks protect the scoped launch paths.
Each child receives a private config path through::

    EXECUTABLE --semantic-replay-child CONFIG.json

The resulting envelope always reports ``semantic_equivalence`` as
``not_evaluated``.  A future live gate must separately compare settled engine
state and independently written save output.

Private protocol workspaces are retained and reported.  The runner never
performs pathname-recursive cleanup because a same-user child can replace a
directory between an identity check and deletion.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import secrets
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unicodedata
from dataclasses import dataclass
from typing import Any, NoReturn, Sequence


SCHEMA_VERSION = 1
CHILD_ARGUMENT = "--semantic-replay-child"
RUNNER_SCOPE = "process_isolation_only"
SEMANTIC_EQUIVALENCE = "not_evaluated"
USER_DATA_ROOT_POLICY = "set_once_before_toolbox_init"
SETTLEMENT_BARRIER = "next_semantic_gameplay_poll"
ROUTES = ("classic", "semantic")
PRESENTATION_MODES = {"classic": "classic", "semantic": "remastered"}
PREFERENCES_WRITE_POLICY = "disabled"
INPUT_SLOT_POLICY = "user_data_root_only_no_bundled_fallback"
OUTPUT_SLOT_POLICY = "fresh_nonexistent"

MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_RESULT_BYTES = 1024 * 1024
MAX_CHILD_OUTPUT_BYTES = 64 * 1024
MAX_ACTIONS = 4096
MAX_ARGUMENTS = 32
MAX_IDENTIFIER_LENGTH = 64
MAX_ARGUMENT_STRING_LENGTH = 1024
MAX_ENGINE_IDENTITY_LENGTH = 256
MAX_PATH_BYTES = 4096
MIN_TIMEOUT_SECONDS = 0.05
MAX_TIMEOUT_SECONDS = 300.0
MAX_SAFE_INTEGER = (1 << 53) - 1
MAX_RNG_DRAW_COUNT = (1 << 63) - 1
TOKEN_HEX_LENGTH = 32
RNG_HEX_LENGTH = 16

EXIT_REQUEST = 3
EXIT_LAUNCH = 4
EXIT_CHILD = 5
EXIT_RESULT = 6
EXIT_INTERNAL = 7
EXIT_INTERRUPTED = 130

REQUEST_FIELDS = frozenset(
    {
        "schema_version",
        "executable",
        "classic_user_data_root",
        "semantic_user_data_root",
        "input_slot",
        "output_slot",
        "actions",
        "timeout_seconds",
        "rng_seed",
        "rng_stream",
    }
)
ACTION_FIELDS = frozenset({"ordinal", "kind", "arguments"})
CONFIG_FIELDS = frozenset(
    {
        "schema_version",
        "run_id",
        "child_nonce",
        "replay_route",
        "presentation_mode",
        "user_data_root",
        "user_data_root_policy",
        "preferences_write_policy",
        "input_slot",
        "input_slot_policy",
        "output_slot",
        "output_slot_policy",
        "actions",
        "settlement_barrier",
        "result_path",
        "rng_seed",
        "rng_stream",
    }
)
RESULT_FIELDS = frozenset(
    {
        "schema_version",
        "run_id",
        "child_nonce",
        "replay_route",
        "presentation_mode",
        "process_id",
        "status",
        "engine_identity",
        "settled_action_count",
        "state_sha256",
        "save_tree_sha256",
        "rng_draw_count",
        "rng_seed",
        "rng_stream",
    }
)
ENVELOPE_FIELDS = frozenset(
    {
        "schema_version",
        "run_id",
        "runner_scope",
        "semantic_equivalence",
        "executable",
        "child_results",
        "rng_seed",
        "rng_stream",
        "workspace_retention",
    }
)


class ReplayRunnerError(Exception):
    """A stable, user-facing replay-runner failure."""

    def __init__(self, code: str, message: str, exit_code: int) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.exit_code = exit_code
        self.workspace_retention: dict[str, object] | None = None


class _DuplicateJsonKey(ValueError):
    pass


@dataclass(frozen=True)
class Action:
    ordinal: int
    kind: str
    arguments: dict[str, str | int | bool]

    def as_json(self) -> dict[str, object]:
        return {
            "ordinal": self.ordinal,
            "kind": self.kind,
            "arguments": dict(self.arguments),
        }


@dataclass(frozen=True)
class RunRequest:
    executable: Path
    executable_identity: tuple[int, int, int, int, int]
    classic_user_data_root: Path
    classic_user_data_identity: tuple[int, int]
    classic_input_slot_identity: tuple[int, int]
    semantic_user_data_root: Path
    semantic_user_data_identity: tuple[int, int]
    semantic_input_slot_identity: tuple[int, int]
    input_slot: str
    output_slot: str
    actions: tuple[Action, ...]
    timeout_seconds: float
    rng_seed: str
    rng_stream: str


@dataclass(frozen=True)
class _PrivateFile:
    path: Path
    device: int
    inode: int
    size: int
    digest: str


@dataclass(frozen=True)
class _DirectoryPin:
    path: Path
    device: int
    inode: int
    ctime_ns: int


@dataclass(frozen=True)
class _NamespacePins:
    executable_ancestors: tuple[_DirectoryPin, ...]
    classic_root_ancestors: tuple[_DirectoryPin, ...]
    semantic_root_ancestors: tuple[_DirectoryPin, ...]
    classic_input: _DirectoryPin
    semantic_input: _DirectoryPin


class _BoundedCollector:
    """Drain one child pipe while retaining only a bounded prefix."""

    def __init__(self, stream: Any, limit: int) -> None:
        self._stream = stream
        self._limit = limit
        self._data = bytearray()
        self.overflow = threading.Event()
        self.done = threading.Event()
        self._thread = threading.Thread(target=self._drain, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def join(self, timeout: float = 2.0) -> None:
        self._thread.join(timeout)

    @property
    def data(self) -> bytes:
        return bytes(self._data[: self._limit])

    def _drain(self) -> None:
        try:
            while True:
                chunk = self._stream.read(8192)
                if not chunk:
                    return
                available = self._limit + 1 - len(self._data)
                if available > 0:
                    self._data.extend(chunk[:available])
                if len(self._data) > self._limit:
                    self.overflow.set()
        finally:
            self._stream.close()
            self.done.set()


def _request_error(message: str) -> NoReturn:
    raise ReplayRunnerError("request.invalid", message, EXIT_REQUEST)


def _launch_error(message: str) -> NoReturn:
    raise ReplayRunnerError("child.launch_failed", message, EXIT_LAUNCH)


def _child_error(code: str, message: str) -> NoReturn:
    raise ReplayRunnerError(code, message, EXIT_CHILD)


def _result_error(message: str) -> NoReturn:
    raise ReplayRunnerError("child.result_invalid", message, EXIT_RESULT)


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


def _contains_control(value: str) -> bool:
    return any(unicodedata.category(character) == "Cc" for character in value)


def _require_fields(
    value: dict[str, Any], expected: frozenset[str], context: str, error: Any
) -> None:
    actual = frozenset(value)
    unknown = sorted(actual - expected)
    missing = sorted(expected - actual)
    if unknown:
        error(f"{context} has unknown field: {unknown[0]}")
    if missing:
        error(f"{context} is missing field: {missing[0]}")


def _read_json(path: Path, maximum_bytes: int, context: str) -> object:
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
        _request_error(f"cannot open {context}: {detail}")

    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode):
            _request_error(f"{context} must be a regular file")
        if before.st_size > maximum_bytes:
            _request_error(f"{context} exceeds {maximum_bytes} bytes")
        chunks: list[bytes] = []
        remaining = maximum_bytes + 1
        while remaining:
            chunk = os.read(descriptor, min(64 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        if remaining == 0:
            _request_error(f"{context} exceeds {maximum_bytes} bytes")
        after = os.fstat(descriptor)
        if (before.st_dev, before.st_ino, before.st_size) != (
            after.st_dev,
            after.st_ino,
            after.st_size,
        ):
            _request_error(f"{context} changed while it was read")
    finally:
        os.close(descriptor)

    try:
        text = b"".join(chunks).decode("utf-8")
    except UnicodeDecodeError:
        _request_error(f"{context} is not valid UTF-8")
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
        _request_error(f"{context} is not valid strict JSON: {error}")


def _validate_identifier(value: object, context: str, error: Any) -> str:
    if not isinstance(value, str) or not (1 <= len(value) <= MAX_IDENTIFIER_LENGTH):
        error(f"{context} must be a non-empty identifier")
    if not ("a" <= value[0] <= "z"):
        error(f"{context} must be lowercase snake_case ASCII")
    if any(
        not ("a" <= character <= "z" or "0" <= character <= "9" or character == "_")
        for character in value
    ):
        error(f"{context} must be lowercase snake_case ASCII")
    if value.endswith("_") or "__" in value:
        error(f"{context} must be normalized lowercase snake_case ASCII")
    return value


def _validate_argument_value(value: object, context: str, error: Any) -> str | int | bool:
    if isinstance(value, bool):
        return value
    if _is_plain_int(value):
        if value < -MAX_SAFE_INTEGER or value > MAX_SAFE_INTEGER:
            error(f"{context} integer exceeds the exact JSON range")
        return value
    if isinstance(value, str):
        if len(value) > MAX_ARGUMENT_STRING_LENGTH:
            error(f"{context} string exceeds {MAX_ARGUMENT_STRING_LENGTH} characters")
        if any(not (" " <= character <= "~") for character in value):
            error(f"{context} string must contain printable ASCII only")
        return value
    error(f"{context} must be a string, integer, or boolean")


def _validate_actions(value: object, error: Any) -> tuple[Action, ...]:
    if not isinstance(value, list):
        error("actions must be an array")
    if len(value) > MAX_ACTIONS:
        error(f"actions exceeds the {MAX_ACTIONS}-action limit")
    normalized: list[Action] = []
    for index, raw_action in enumerate(value):
        context = f"actions[{index}]"
        if not isinstance(raw_action, dict):
            error(f"{context} must be an object")
        _require_fields(raw_action, ACTION_FIELDS, context, error)
        ordinal = raw_action["ordinal"]
        if not _is_plain_int(ordinal) or ordinal != index:
            error(f"{context}.ordinal must equal its zero-based array index")
        kind = _validate_identifier(raw_action["kind"], f"{context}.kind", error)
        raw_arguments = raw_action["arguments"]
        if not isinstance(raw_arguments, dict):
            error(f"{context}.arguments must be an object")
        if len(raw_arguments) > MAX_ARGUMENTS:
            error(f"{context}.arguments exceeds the {MAX_ARGUMENTS}-argument limit")
        arguments: dict[str, str | int | bool] = {}
        for name in sorted(raw_arguments):
            normalized_name = _validate_identifier(
                name, f"{context}.arguments key", error
            )
            arguments[normalized_name] = _validate_argument_value(
                raw_arguments[name], f"{context}.arguments.{name}", error
            )
        normalized.append(Action(ordinal=ordinal, kind=kind, arguments=arguments))
    return tuple(normalized)


def _validate_slot(value: object, context: str, error: Any) -> str:
    if not isinstance(value, str) or len(value) != 1 or value < "A" or value > "J":
        error(f"{context} must be one uppercase Classic slot letter A through J")
    return value


def _validate_rng_hex(value: object, context: str, error: Any) -> str:
    if not isinstance(value, str) or len(value) != RNG_HEX_LENGTH:
        error(f"{context} must be {RNG_HEX_LENGTH} lowercase hexadecimal characters")
    if any(character not in "0123456789abcdef" for character in value):
        error(f"{context} must be {RNG_HEX_LENGTH} lowercase hexadecimal characters")
    return value


def _validate_token(value: object, context: str) -> str:
    if not isinstance(value, str) or len(value) != TOKEN_HEX_LENGTH:
        _result_error(f"{context} must be {TOKEN_HEX_LENGTH} lowercase hexadecimal characters")
    if any(character not in "0123456789abcdef" for character in value):
        _result_error(f"{context} must be {TOKEN_HEX_LENGTH} lowercase hexadecimal characters")
    return value


def _validate_sha256(value: object, context: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        _result_error(f"{context} must be a lowercase SHA-256 digest")
    if any(character not in "0123456789abcdef" for character in value):
        _result_error(f"{context} must be a lowercase SHA-256 digest")
    return value


def _validate_canonical_path(
    value: object, context: str, *, directory: bool, executable: bool = False
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
        _request_error(f"{context} must not contain symbolic-link or noncanonical components")
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


def _validate_distinct_roots(classic: Path, semantic: Path) -> None:
    try:
        classic_status = classic.stat(follow_symlinks=False)
        semantic_status = semantic.stat(follow_symlinks=False)
    except OSError as error:
        _request_error(f"user-data root changed during validation: {error}")
    if (classic_status.st_dev, classic_status.st_ino) == (
        semantic_status.st_dev,
        semantic_status.st_ino,
    ):
        _request_error("Classic and semantic user-data roots must be distinct")
    try:
        if _directory_contains(classic, semantic) or _directory_contains(
            semantic, classic
        ):
            _request_error("Classic and semantic user-data roots must not be nested")
    except OSError as error:
        _request_error(f"user-data root changed during validation: {error}")


def _directory_contains(ancestor: Path, candidate: Path) -> bool:
    """Compare directory identities while walking candidate toward the root."""

    ancestor_status = ancestor.stat(follow_symlinks=False)
    ancestor_identity = (ancestor_status.st_dev, ancestor_status.st_ino)
    current = candidate
    while True:
        current_status = current.stat(follow_symlinks=False)
        if (current_status.st_dev, current_status.st_ino) == ancestor_identity:
            return True
        parent = current.parent
        if parent == current:
            return False
        current = parent


def _directory_identity(path: Path) -> tuple[int, int]:
    status = path.stat(follow_symlinks=False)
    return (status.st_dev, status.st_ino)


def _slot_directory(user_root: Path, slot: str) -> Path:
    return user_root / "Save" / f"Game {slot}"


def _capture_directory_pin(path: Path) -> _DirectoryPin:
    status = path.stat(follow_symlinks=False)
    if not stat.S_ISDIR(status.st_mode):
        _launch_error(f"namespace component is no longer a directory: {path}")
    return _DirectoryPin(
        path=path,
        device=status.st_dev,
        inode=status.st_ino,
        ctime_ns=status.st_ctime_ns,
    )


def _capture_directory_chain(path: Path) -> tuple[_DirectoryPin, ...]:
    try:
        return tuple(_capture_directory_pin(item) for item in (path, *path.parents))
    except OSError as error:
        _launch_error(f"cannot pin namespace ancestor chain for {path}: {error}")


def _capture_namespace_pins(request: RunRequest) -> _NamespacePins:
    classic_input = _slot_directory(
        request.classic_user_data_root, request.input_slot
    )
    semantic_input = _slot_directory(
        request.semantic_user_data_root, request.input_slot
    )
    try:
        return _NamespacePins(
            executable_ancestors=_capture_directory_chain(request.executable.parent),
            classic_root_ancestors=_capture_directory_chain(
                request.classic_user_data_root
            ),
            semantic_root_ancestors=_capture_directory_chain(
                request.semantic_user_data_root
            ),
            classic_input=_capture_directory_pin(classic_input),
            semantic_input=_capture_directory_pin(semantic_input),
        )
    except OSError as error:
        _launch_error(f"cannot pin replay namespace: {error}")


def _verify_directory_pin(
    pin: _DirectoryPin, phase: str, *, verify_ctime: bool = True
) -> None:
    try:
        status = pin.path.stat(follow_symlinks=False)
    except OSError as error:
        _launch_error(f"namespace changed before/during {phase}: {pin.path}: {error}")
    if (
        not stat.S_ISDIR(status.st_mode)
        or (status.st_dev, status.st_ino) != (pin.device, pin.inode)
        or (verify_ctime and status.st_ctime_ns != pin.ctime_ns)
    ):
        _launch_error(f"namespace ancestor changed before/during {phase}: {pin.path}")


def _verify_namespace_pins(pins: _NamespacePins, phase: str) -> None:
    for index, pin in enumerate(pins.executable_ancestors):
        # The executable directory catches transient executable replacement;
        # its direct parent catches a rename/replace/restore of that directory.
        # Higher shared ancestors are identity-pinned without ctime checks so
        # unrelated activity elsewhere under /tmp, a home directory, or a
        # volume does not make a replay fail nondeterministically. Transient
        # mutation there is part of the documented same-user trust boundary.
        _verify_directory_pin(pin, phase, verify_ctime=index <= 1)
    for chain in (pins.classic_root_ancestors, pins.semantic_root_ancestors):
        for index, pin in enumerate(chain):
            # A live load creates and updates root-level working directories.
            # Preserve the root's identity while allowing that legitimate
            # self-ctime churn. Its direct parent remains ctime-pinned, which
            # catches a root rename/replace/restore. Higher shared ancestors
            # remain identity-pinned without treating unrelated sibling
            # creation as a replay mutation; transient mutation there is part
            # of the documented same-user trust boundary.
            _verify_directory_pin(pin, phase, verify_ctime=index == 1)
    _verify_directory_pin(pins.classic_input, phase)
    _verify_directory_pin(pins.semantic_input, phase)


def _validate_slot_layout(
    user_root: Path, input_slot: str, output_slot: str, context: str
) -> tuple[int, int]:
    save_root = user_root / "Save"
    input_path = _slot_directory(user_root, input_slot)
    output_path = _slot_directory(user_root, output_slot)
    try:
        resolved_save = save_root.resolve(strict=True)
        resolved_input = input_path.resolve(strict=True)
        save_status = save_root.stat(follow_symlinks=False)
        input_status = input_path.stat(follow_symlinks=False)
    except (OSError, RuntimeError) as error:
        _request_error(f"{context} input slot does not exist under its user-data root: {error}")
    if str(resolved_save) != str(save_root) or str(resolved_input) != str(input_path):
        _request_error(f"{context} input slot must not contain symbolic-link components")
    if not stat.S_ISDIR(save_status.st_mode) or not stat.S_ISDIR(input_status.st_mode):
        _request_error(f"{context} input slot must be an existing directory")
    if os.path.lexists(output_path):
        _request_error(f"{context} output slot must not exist before replay")
    return (input_status.st_dev, input_status.st_ino)


def _verify_user_roots(request: RunRequest, route: str) -> None:
    roots = (
        (
            "classic",
            request.classic_user_data_root,
            request.classic_user_data_identity,
        ),
        (
            "semantic",
            request.semantic_user_data_root,
            request.semantic_user_data_identity,
        ),
    )
    for name, path, expected_identity in roots:
        try:
            resolved = path.resolve(strict=True)
            status = path.stat(follow_symlinks=False)
        except (OSError, RuntimeError) as error:
            _launch_error(f"{name} user-data root changed before/during {route}: {error}")
        if (
            str(resolved) != str(path)
            or not stat.S_ISDIR(status.st_mode)
            or (status.st_dev, status.st_ino) != expected_identity
        ):
            _launch_error(f"{name} user-data root identity changed before/during {route}")
    try:
        if _directory_contains(
            request.classic_user_data_root, request.semantic_user_data_root
        ) or _directory_contains(
            request.semantic_user_data_root, request.classic_user_data_root
        ):
            _launch_error(f"user-data roots became aliased or nested before/during {route}")
    except OSError as error:
        _launch_error(f"user-data roots changed before/during {route}: {error}")


def _verify_route_slots(
    request: RunRequest,
    route: str,
    *,
    before_launch: bool,
    expected_output_identity: tuple[int, int] | None = None,
) -> tuple[int, int] | None:
    if route == "classic":
        user_root = request.classic_user_data_root
        expected_input_identity = request.classic_input_slot_identity
    else:
        user_root = request.semantic_user_data_root
        expected_input_identity = request.semantic_input_slot_identity
    input_path = _slot_directory(user_root, request.input_slot)
    output_path = _slot_directory(user_root, request.output_slot)
    try:
        resolved_input = input_path.resolve(strict=True)
        input_status = input_path.stat(follow_symlinks=False)
    except (OSError, RuntimeError) as error:
        _launch_error(f"{route} input slot changed before/during replay: {error}")
    if (
        str(resolved_input) != str(input_path)
        or not stat.S_ISDIR(input_status.st_mode)
        or (input_status.st_dev, input_status.st_ino) != expected_input_identity
    ):
        _launch_error(f"{route} input slot identity changed before/during replay")
    if before_launch and os.path.lexists(output_path):
        _launch_error(f"{route} output slot must not exist before replay")
    if before_launch:
        return None
    try:
        resolved_output = output_path.resolve(strict=True)
        output_status = output_path.stat(follow_symlinks=False)
    except (OSError, RuntimeError) as error:
        _launch_error(f"{route} output slot was not created as a directory: {error}")
    output_identity = (output_status.st_dev, output_status.st_ino)
    if str(resolved_output) != str(output_path) or not stat.S_ISDIR(
        output_status.st_mode
    ):
        _launch_error(f"{route} output slot must be a physical directory")
    if output_identity == expected_input_identity:
        _launch_error(f"{route} output slot must not alias its input slot")
    if expected_output_identity is not None and output_identity != expected_output_identity:
        _launch_error(f"{route} output slot identity changed while reading its result")
    return output_identity


def _parse_request(value: object) -> RunRequest:
    if not isinstance(value, dict):
        _request_error("run request must be a JSON object")
    _require_fields(value, REQUEST_FIELDS, "run request", _request_error)
    version = value["schema_version"]
    if not _is_plain_int(version) or version != SCHEMA_VERSION:
        _request_error(f"schema_version must be {SCHEMA_VERSION}")
    executable = _validate_canonical_path(
        value["executable"], "executable", directory=False, executable=True
    )
    executable_status = executable.stat(follow_symlinks=False)
    executable_identity = (
        executable_status.st_dev,
        executable_status.st_ino,
        executable_status.st_size,
        executable_status.st_mtime_ns,
        executable_status.st_ctime_ns,
    )
    classic_root = _validate_canonical_path(
        value["classic_user_data_root"],
        "classic_user_data_root",
        directory=True,
    )
    semantic_root = _validate_canonical_path(
        value["semantic_user_data_root"],
        "semantic_user_data_root",
        directory=True,
    )
    _validate_distinct_roots(classic_root, semantic_root)
    try:
        classic_root_identity = _directory_identity(classic_root)
        semantic_root_identity = _directory_identity(semantic_root)
    except OSError as error:
        _request_error(f"user-data root changed during validation: {error}")
    input_slot = _validate_slot(value["input_slot"], "input_slot", _request_error)
    output_slot = _validate_slot(value["output_slot"], "output_slot", _request_error)
    if input_slot == output_slot:
        _request_error("input_slot and output_slot must be distinct")
    classic_input_identity = _validate_slot_layout(
        classic_root, input_slot, output_slot, "classic"
    )
    semantic_input_identity = _validate_slot_layout(
        semantic_root, input_slot, output_slot, "semantic"
    )
    actions = _validate_actions(value["actions"], _request_error)
    timeout = value["timeout_seconds"]
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)):
        _request_error("timeout_seconds must be a finite number")
    try:
        timeout_float = float(timeout)
    except OverflowError:
        _request_error("timeout_seconds must be a finite number")
    if not math.isfinite(timeout_float):
        _request_error("timeout_seconds must be a finite number")
    if not MIN_TIMEOUT_SECONDS <= timeout_float <= MAX_TIMEOUT_SECONDS:
        _request_error(
            f"timeout_seconds must be between {MIN_TIMEOUT_SECONDS} and {MAX_TIMEOUT_SECONDS}"
        )
    rng_seed = _validate_rng_hex(value["rng_seed"], "rng_seed", _request_error)
    rng_stream = _validate_rng_hex(value["rng_stream"], "rng_stream", _request_error)
    return RunRequest(
        executable=executable,
        executable_identity=executable_identity,
        classic_user_data_root=classic_root,
        classic_user_data_identity=classic_root_identity,
        classic_input_slot_identity=classic_input_identity,
        semantic_user_data_root=semantic_root,
        semantic_user_data_identity=semantic_root_identity,
        semantic_input_slot_identity=semantic_input_identity,
        input_slot=input_slot,
        output_slot=output_slot,
        actions=actions,
        timeout_seconds=timeout_float,
        rng_seed=rng_seed,
        rng_stream=rng_stream,
    )


def load_request(path: Path) -> RunRequest:
    """Load and strictly validate a v1 replay run request."""

    return _parse_request(_read_json(path, MAX_JSON_BYTES, "run request"))


def _private_json_bytes(value: object) -> bytes:
    encoded = (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    if len(encoded) > MAX_JSON_BYTES:
        _request_error(f"generated child config exceeds {MAX_JSON_BYTES} bytes")
    return encoded


def _write_private_json(path: Path, value: object) -> _PrivateFile:
    data = _private_json_bytes(value)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    if hasattr(os, "O_NONBLOCK"):
        flags |= os.O_NONBLOCK
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as error:
        _launch_error(f"cannot create private child config: {error}")
    try:
        offset = 0
        while offset < len(data):
            written = os.write(descriptor, data[offset:])
            if written <= 0:
                _launch_error("short write while creating private child config")
            offset += written
        status = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    return _PrivateFile(
        path=path,
        device=status.st_dev,
        inode=status.st_ino,
        size=status.st_size,
        digest=hashlib.sha256(data).hexdigest(),
    )


def _verify_private_file(record: _PrivateFile) -> None:
    try:
        status = record.path.stat(follow_symlinks=False)
    except OSError as error:
        _child_error("child.config_changed", f"child config disappeared: {error}")
    if (
        not stat.S_ISREG(status.st_mode)
        or (status.st_dev, status.st_ino) != (record.device, record.inode)
        or status.st_size != record.size
        or status.st_uid != os.getuid()
        or stat.S_IMODE(status.st_mode) != 0o600
        or status.st_nlink != 1
    ):
        _child_error("child.config_changed", "child config identity changed")
    flags = os.O_RDONLY
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    if hasattr(os, "O_NONBLOCK"):
        flags |= os.O_NONBLOCK
    try:
        descriptor = os.open(record.path, flags)
    except OSError as error:
        _child_error("child.config_changed", f"cannot re-read child config: {error}")
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (record.device, record.inode, record.size)
            or opened.st_uid != os.getuid()
            or stat.S_IMODE(opened.st_mode) != 0o600
            or opened.st_nlink != 1
        ):
            _child_error("child.config_changed", "child config identity changed")
        chunks: list[bytes] = []
        remaining = record.size + 1
        while remaining:
            chunk = os.read(descriptor, min(64 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        data = b"".join(chunks)
        if len(data) != record.size:
            _child_error("child.config_changed", "child config size changed")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size)
            != (record.device, record.inode, record.size)
            or after.st_uid != os.getuid()
            or stat.S_IMODE(after.st_mode) != 0o600
            or after.st_nlink != 1
        ):
            _child_error("child.config_changed", "child config changed while re-read")
    finally:
        os.close(descriptor)
    if hashlib.sha256(data).hexdigest() != record.digest:
        _child_error("child.config_changed", "child config contents changed")


def _verify_private_directory(
    path: Path, expected_identity: tuple[int, int], context: str
) -> None:
    try:
        status = path.stat(follow_symlinks=False)
    except OSError as error:
        _child_error("child.workspace_changed", f"{context} disappeared: {error}")
    if (
        not stat.S_ISDIR(status.st_mode)
        or (status.st_dev, status.st_ino) != expected_identity
        or status.st_uid != os.getuid()
        or stat.S_IMODE(status.st_mode) != 0o700
    ):
        _child_error("child.workspace_changed", f"{context} is no longer private")


def _private_result_identity(path: Path) -> tuple[int, int, int]:
    try:
        status = path.stat(follow_symlinks=False)
    except OSError as error:
        _result_error(f"cannot open child result: {error}")
    if (
        not stat.S_ISREG(status.st_mode)
        or status.st_uid != os.getuid()
        or stat.S_IMODE(status.st_mode) != 0o600
        or status.st_nlink != 1
    ):
        _result_error("child result must be one private current-user-owned regular file")
    return (status.st_dev, status.st_ino, status.st_size)


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
            )
    if authoritative:
        notice = (
            f"private protocol workspace retained at {workspace}; cleanup was not attempted"
        )
    else:
        notice = (
            f"workspace namespace changed; {workspace} is only a non-authoritative "
            "candidate path, and the original private protocol workspace may remain "
            "at an unknown renamed location; cleanup was not attempted"
        )
    return {
        "cleanup_attempted": False,
        "candidate_path": str(workspace),
        "path_authoritative": authoritative,
        "notice": notice,
    }


def _attach_workspace_retention(
    error: BaseException, retention: dict[str, object]
) -> None:
    setattr(error, "workspace_retention", retention)
    if isinstance(error, ReplayRunnerError):
        error.message = f"{error.message}; {retention['notice']}"
        error.args = (error.message,)


def _terminate_process(process: subprocess.Popen[bytes]) -> None:
    group_was_signaled = False
    try:
        os.killpg(process.pid, signal.SIGTERM)
        group_was_signaled = True
    except (OSError, ProcessLookupError):
        if process.poll() is None:
            process.terminate()
    if process.poll() is None:
        try:
            process.wait(timeout=0.5)
        except subprocess.TimeoutExpired:
            pass
    if group_was_signaled:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass
    elif process.poll() is None:
        process.kill()
    if process.poll() is None:
        try:
            process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            pass


def _process_group_is_alive(process: subprocess.Popen[bytes]) -> bool:
    try:
        os.killpg(process.pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _output_excerpt(data: bytes) -> str:
    text = data.decode("utf-8", errors="replace")
    return text[:512].replace("\x00", "\\0")


def _wait_for_child(
    process: subprocess.Popen[bytes], route: str, timeout_seconds: float
) -> tuple[bytes, bytes]:
    assert process.stdout is not None
    assert process.stderr is not None
    stdout = _BoundedCollector(process.stdout, MAX_CHILD_OUTPUT_BYTES)
    stderr = _BoundedCollector(process.stderr, MAX_CHILD_OUTPUT_BYTES)
    stdout.start()
    stderr.start()
    deadline = time.monotonic() + timeout_seconds
    timed_out = False
    while True:
        if stdout.overflow.is_set() or stderr.overflow.is_set():
            _terminate_process(process)
            break
        if process.poll() is not None and stdout.done.is_set() and stderr.done.is_set():
            break
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            timed_out = True
            _terminate_process(process)
            break
        if process.poll() is None:
            try:
                process.wait(timeout=min(0.05, remaining))
            except subprocess.TimeoutExpired:
                pass
        else:
            stdout.done.wait(min(0.05, remaining))
    stdout.join()
    stderr.join()
    if timed_out:
        _child_error(
            "child.timeout",
            f"{route} child exceeded its {timeout_seconds:g}-second timeout",
        )
    if stdout.overflow.is_set() or stderr.overflow.is_set():
        _child_error(
            "child.output_limit",
            f"{route} child exceeded the {MAX_CHILD_OUTPUT_BYTES}-byte output limit",
        )
    if process.returncode != 0:
        excerpt = _output_excerpt(stderr.data or stdout.data)
        suffix = f": {excerpt}" if excerpt else ""
        _child_error(
            "child.nonzero_exit",
            f"{route} child exited with status {process.returncode}{suffix}",
        )
    if _process_group_is_alive(process):
        _terminate_process(process)
        _child_error(
            "child.descendant_process",
            f"{route} child left a descendant process running",
        )
    return stdout.data, stderr.data


def _read_child_result(path: Path) -> object:
    try:
        return _read_json(path, MAX_RESULT_BYTES, "child result")
    except ReplayRunnerError as error:
        if error.exit_code == EXIT_REQUEST:
            _result_error(error.message)
        raise


def _parse_child_result(
    value: object,
    *,
    run_id: str,
    nonce: str,
    route: str,
    process_id: int,
    action_count: int,
    request_rng_seed: str,
    request_rng_stream: str,
) -> dict[str, object]:
    if not isinstance(value, dict):
        _result_error(f"{route} child result must be a JSON object")
    _require_fields(value, RESULT_FIELDS, f"{route} child result", _result_error)
    version = value["schema_version"]
    if not _is_plain_int(version) or version != SCHEMA_VERSION:
        _result_error(f"{route} child result schema_version must be {SCHEMA_VERSION}")
    if _validate_token(value["run_id"], "child result run_id") != run_id:
        _result_error(f"{route} child result echoed a stale run_id")
    if _validate_token(value["child_nonce"], "child result child_nonce") != nonce:
        _result_error(f"{route} child result echoed a stale child_nonce")
    if value["replay_route"] != route:
        _result_error(f"{route} child result echoed the wrong replay_route")
    expected_presentation = PRESENTATION_MODES[route]
    if value["presentation_mode"] != expected_presentation:
        _result_error(f"{route} child result echoed the wrong presentation_mode")
    if not _is_plain_int(value["process_id"]) or value["process_id"] != process_id:
        _result_error(f"{route} child result echoed the wrong process_id")
    if value["status"] != "completed":
        _result_error(f"{route} child result status must be completed")
    engine_identity = value["engine_identity"]
    if not isinstance(engine_identity, str) or not (
        1 <= len(engine_identity) <= MAX_ENGINE_IDENTITY_LENGTH
    ):
        _result_error(f"{route} child result engine_identity length is invalid")
    if (
        engine_identity != engine_identity.strip()
        or _contains_control(engine_identity)
        or unicodedata.normalize("NFC", engine_identity) != engine_identity
    ):
        _result_error(f"{route} child result engine_identity is not normalized text")
    settled = value["settled_action_count"]
    if not _is_plain_int(settled) or settled != action_count:
        _result_error(f"{route} child result settled_action_count is wrong")
    _validate_sha256(value["state_sha256"], f"{route} child result state_sha256")
    _validate_sha256(
        value["save_tree_sha256"], f"{route} child result save_tree_sha256"
    )
    rng_draw_count = value["rng_draw_count"]
    if (
        not _is_plain_int(rng_draw_count)
        or rng_draw_count < 0
        or rng_draw_count > MAX_RNG_DRAW_COUNT
    ):
        _result_error(f"{route} child result rng_draw_count is invalid")
    if _validate_rng_hex(
        value["rng_seed"], f"{route} child result rng_seed", _result_error
    ) != request_rng_seed:
        _result_error(f"{route} child result echoed the wrong rng_seed")
    if _validate_rng_hex(
        value["rng_stream"], f"{route} child result rng_stream", _result_error
    ) != request_rng_stream:
        _result_error(f"{route} child result echoed the wrong rng_stream")
    return dict(value)


def _child_config(
    request: RunRequest,
    *,
    run_id: str,
    nonce: str,
    route: str,
    result_path: Path,
) -> dict[str, object]:
    user_root = (
        request.classic_user_data_root
        if route == "classic"
        else request.semantic_user_data_root
    )
    config: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "run_id": run_id,
        "child_nonce": nonce,
        "replay_route": route,
        "presentation_mode": PRESENTATION_MODES[route],
        "user_data_root": str(user_root),
        "user_data_root_policy": USER_DATA_ROOT_POLICY,
        "preferences_write_policy": PREFERENCES_WRITE_POLICY,
        "input_slot": request.input_slot,
        "input_slot_policy": INPUT_SLOT_POLICY,
        "output_slot": request.output_slot,
        "output_slot_policy": OUTPUT_SLOT_POLICY,
        "actions": [action.as_json() for action in request.actions],
        "settlement_barrier": SETTLEMENT_BARRIER,
        "result_path": str(result_path),
        "rng_seed": request.rng_seed,
        "rng_stream": request.rng_stream,
    }
    if frozenset(config) != CONFIG_FIELDS:  # pragma: no cover - internal guard
        raise AssertionError("child config fields drifted from the v1 contract")
    return config


def _verify_executable_identity(request: RunRequest, phase: str) -> None:
    try:
        status = request.executable.stat(follow_symlinks=False)
    except OSError as error:
        _launch_error(f"executable disappeared before/during {phase}: {error}")
    identity = (
        status.st_dev,
        status.st_ino,
        status.st_size,
        status.st_mtime_ns,
        status.st_ctime_ns,
    )
    if not stat.S_ISREG(status.st_mode) or identity != request.executable_identity:
        _launch_error(f"executable identity changed before/during {phase}")
    if not os.access(request.executable, os.X_OK):
        _launch_error(f"executable permission changed before/during {phase}")


def _verify_pinned_executable(
    request: RunRequest, descriptor: int, phase: str
) -> None:
    try:
        status = os.fstat(descriptor)
    except OSError as error:
        _launch_error(f"pinned executable became unavailable before/during {phase}: {error}")
    identity = (
        status.st_dev,
        status.st_ino,
        status.st_size,
        status.st_mtime_ns,
        status.st_ctime_ns,
    )
    if not stat.S_ISREG(status.st_mode) or identity != request.executable_identity:
        _launch_error(f"pinned executable changed before/during {phase}")


def _open_pinned_executable(request: RunRequest) -> int:
    flags = os.O_RDONLY
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    if hasattr(os, "O_NONBLOCK"):
        flags |= os.O_NONBLOCK
    try:
        descriptor = os.open(request.executable, flags)
    except OSError as error:
        _launch_error(f"cannot pin the requested executable: {error}")
    status = os.fstat(descriptor)
    identity = (
        status.st_dev,
        status.st_ino,
        status.st_size,
        status.st_mtime_ns,
        status.st_ctime_ns,
    )
    if not stat.S_ISREG(status.st_mode) or identity != request.executable_identity:
        os.close(descriptor)
        _launch_error("executable identity changed before it could be pinned")
    return descriptor


def _run_one_child(
    request: RunRequest,
    workspace: Path,
    run_id: str,
    route: str,
    nonce: str,
    executable_descriptor: int,
    workspace_identity: tuple[int, int],
    namespace_pins: _NamespacePins,
) -> dict[str, object]:
    _verify_private_directory(workspace, workspace_identity, "parent workspace")
    _verify_user_roots(request, f"{route} launch")
    _verify_route_slots(request, route, before_launch=True)
    _verify_namespace_pins(namespace_pins, f"{route} launch")
    route_directory = workspace / route
    route_directory.mkdir(mode=0o700)
    route_status = route_directory.stat(follow_symlinks=False)
    if route_status.st_uid != os.getuid() or stat.S_IMODE(route_status.st_mode) != 0o700:
        _launch_error(f"{route} child workspace is not private and current-user-owned")
    route_identity = (route_status.st_dev, route_status.st_ino)
    config_path = route_directory / "config.json"
    result_path = route_directory / "result.json"
    for internal_path in (config_path, result_path):
        if len(str(internal_path).encode("utf-8")) > MAX_PATH_BYTES:
            _launch_error(f"{route} child workspace path exceeds {MAX_PATH_BYTES} bytes")
    config = _child_config(
        request,
        run_id=run_id,
        nonce=nonce,
        route=route,
        result_path=result_path,
    )
    config_record = _write_private_json(config_path, config)
    argv = [str(request.executable), CHILD_ARGUMENT, str(config_path)]
    _verify_namespace_pins(namespace_pins, f"{route} launch")
    _verify_executable_identity(request, f"{route} launch")
    _verify_pinned_executable(request, executable_descriptor, f"{route} launch")
    try:
        process = subprocess.Popen(
            argv,
            cwd=route_directory,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            start_new_session=True,
        )
    except OSError as error:
        _launch_error(f"cannot launch {route} child: {error}")
    process_id = process.pid
    with process:
        try:
            _verify_namespace_pins(namespace_pins, f"{route} process start")
            _verify_executable_identity(request, f"{route} process start")
            _verify_pinned_executable(
                request, executable_descriptor, f"{route} process start"
            )
            _wait_for_child(process, route, request.timeout_seconds)
        except BaseException:
            _terminate_process(process)
            raise
    _verify_user_roots(request, f"{route} completion")
    _verify_private_directory(workspace, workspace_identity, "parent workspace")
    _verify_private_directory(route_directory, route_identity, f"{route} workspace")
    _verify_namespace_pins(namespace_pins, f"{route} completion")
    _verify_executable_identity(request, f"{route} completion")
    _verify_pinned_executable(request, executable_descriptor, f"{route} completion")
    output_identity = _verify_route_slots(request, route, before_launch=False)
    assert output_identity is not None
    _verify_private_file(config_record)
    result_identity = _private_result_identity(result_path)
    value = _read_child_result(result_path)
    if _private_result_identity(result_path) != result_identity:
        _result_error(f"{route} child result identity changed while it was read")
    result = _parse_child_result(
        value,
        run_id=run_id,
        nonce=nonce,
        route=route,
        process_id=process_id,
        action_count=len(request.actions),
        request_rng_seed=request.rng_seed,
        request_rng_stream=request.rng_stream,
    )
    _verify_user_roots(request, f"{route} result read")
    _verify_private_directory(workspace, workspace_identity, "parent workspace")
    _verify_private_directory(route_directory, route_identity, f"{route} workspace")
    _verify_namespace_pins(namespace_pins, f"{route} result read")
    _verify_executable_identity(request, f"{route} result read")
    _verify_pinned_executable(request, executable_descriptor, f"{route} result read")
    _verify_route_slots(
        request,
        route,
        before_launch=False,
        expected_output_identity=output_identity,
    )
    return result


def run_request(request: RunRequest) -> dict[str, object]:
    """Run the v1 parent protocol and return a non-equivalence envelope."""

    run_id = secrets.token_hex(TOKEN_HEX_LENGTH // 2)
    nonces = [secrets.token_hex(TOKEN_HEX_LENGTH // 2) for _ in ROUTES]
    if len(set(nonces)) != len(nonces):  # cryptographically negligible, fail closed
        _launch_error("could not generate distinct child nonces")
    executable_descriptor = _open_pinned_executable(request)
    workspace: Path | None = None
    workspace_identity: tuple[int, int] | None = None
    try:
        workspace = Path(tempfile.mkdtemp(prefix="realmz-semantic-replay-"))
        workspace = workspace.resolve(strict=True)
        workspace_status = workspace.stat(follow_symlinks=False)
        if workspace_status.st_uid != os.getuid() or stat.S_IMODE(workspace_status.st_mode) != 0o700:
            _launch_error("temporary workspace is not private and owned by the current user")
        workspace_identity = (workspace_status.st_dev, workspace_status.st_ino)
        for user_root in (
            request.classic_user_data_root,
            request.semantic_user_data_root,
        ):
            if _directory_contains(workspace, user_root) or _directory_contains(
                user_root, workspace
            ):
                _launch_error("temporary workspace must not alias a user-data root")
        _verify_executable_identity(request, "namespace pinning")
        _verify_user_roots(request, "namespace pinning")
        _verify_route_slots(request, "classic", before_launch=True)
        _verify_route_slots(request, "semantic", before_launch=True)
        namespace_pins = _capture_namespace_pins(request)
        _verify_namespace_pins(namespace_pins, "namespace pinning")
        child_results = [
            _run_one_child(
                request,
                workspace,
                run_id,
                route,
                nonce,
                executable_descriptor,
                workspace_identity,
                namespace_pins,
            )
            for route, nonce in zip(ROUTES, nonces, strict=True)
        ]
        if child_results[0]["process_id"] == child_results[1]["process_id"]:
            _child_error(
                "child.process_not_isolated",
                "Classic and semantic children did not have distinct process IDs",
            )
        if child_results[0]["engine_identity"] != child_results[1]["engine_identity"]:
            _child_error(
                "child.engine_identity_mismatch",
                "Classic and semantic children reported different engine identities",
            )
        _verify_private_directory(workspace, workspace_identity, "parent workspace")
        retention = _workspace_retention_record(workspace, workspace_identity)
    except BaseException as error:
        if workspace is not None:
            retention = _workspace_retention_record(workspace, workspace_identity)
            _attach_workspace_retention(error, retention)
        raise
    finally:
        os.close(executable_descriptor)
    envelope: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "run_id": run_id,
        "runner_scope": RUNNER_SCOPE,
        "semantic_equivalence": SEMANTIC_EQUIVALENCE,
        "executable": str(request.executable),
        "child_results": child_results,
        "rng_seed": request.rng_seed,
        "rng_stream": request.rng_stream,
        "workspace_retention": retention,
    }
    if frozenset(envelope) != ENVELOPE_FIELDS:  # pragma: no cover - internal guard
        raise AssertionError("run envelope fields drifted from the v1 contract")
    return envelope


def run_request_file(path: Path) -> dict[str, object]:
    """Load a request and execute both isolated child routes."""

    return run_request(load_request(path))


def _error_envelope(error: ReplayRunnerError) -> dict[str, object]:
    envelope: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "status": "error",
        "semantic_equivalence": SEMANTIC_EQUIVALENCE,
        "error": {"code": error.code, "message": error.message},
    }
    if error.workspace_retention is not None:
        envelope["workspace_retention"] = error.workspace_retention
    return envelope


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Launch one explicit executable as isolated Classic and semantic replay "
            "children. The executable is trusted code, not sandboxed; private protocol "
            "workspaces are retained and reported. Native Realmz children load explicit "
            "slots, drive guarded gameplay polls, and publish verified results."
        )
    )
    parser.add_argument(
        "--request",
        required=True,
        type=Path,
        help="v1 semantic replay run request JSON",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _argument_parser().parse_args(argv)
    try:
        envelope = run_request_file(arguments.request)
    except ReplayRunnerError as error:
        print(
            json.dumps(_error_envelope(error), ensure_ascii=False, sort_keys=True),
            file=sys.stderr,
        )
        return error.exit_code
    except KeyboardInterrupt as error:
        wrapped = ReplayRunnerError(
            "runner.interrupted",
            "semantic replay runner interrupted",
            EXIT_INTERRUPTED,
        )
        retention = getattr(error, "workspace_retention", None)
        if isinstance(retention, dict):
            _attach_workspace_retention(wrapped, retention)
        print(
            json.dumps(_error_envelope(wrapped), ensure_ascii=False, sort_keys=True),
            file=sys.stderr,
        )
        return wrapped.exit_code
    except SystemExit:
        raise
    except BaseException as error:  # pragma: no cover - fail-closed CLI guard
        wrapped = ReplayRunnerError(
            "runner.internal_error",
            f"unexpected runner failure: {type(error).__name__}",
            EXIT_INTERNAL,
        )
        retention = getattr(error, "workspace_retention", None)
        if isinstance(retention, dict):
            _attach_workspace_retention(wrapped, retention)
        print(
            json.dumps(_error_envelope(wrapped), ensure_ascii=False, sort_keys=True),
            file=sys.stderr,
        )
        return wrapped.exit_code
    print(json.dumps(envelope, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
