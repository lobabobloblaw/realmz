#!/usr/bin/env python3
"""Verify and stage provenance-declared Realmz semantic replay fixtures.

This utility deliberately does not know a default save directory.  Both the
manifest and every filesystem root must be supplied by the caller.

Manifest v1 records an exact, canonically ordered file census.  Its tree hash
is SHA-256 over this binary stream::

    b"realmz-semantic-replay-fixture-tree-v1\n"
    + repeated(
        uint64_be(len(path_utf8)) + path_utf8
        + uint64_be(size) + sha256_bytes
      )

The stager makes two independent byte copies.  It establishes fixture
identity only; it never evaluates or claims behavioral equivalence.

Destination parents are a trust boundary: they must be owned by the current
user and not group- or world-writable.  Staging occurs in random mode-0700
siblings and publishes with the operating system's atomic no-replace rename.
On failure, private staging roots and any completed partial publish are
reported and retained; these may contain fixture bytes and require deliberate
cleanup by the caller.  This tool never attempts race-prone rollback deletion.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import secrets
import stat
import sys
from dataclasses import dataclass
import unicodedata
from typing import Any, Callable, Iterable, NoReturn, Sequence


MANIFEST_VERSION = 1
TREE_DOMAIN = b"realmz-semantic-replay-fixture-tree-v1\n"
SHA256_HEX_LENGTH = 64
MAX_FILE_SIZE = (1 << 63) - 1
MAX_FILES = 100_000
MAX_DIRECTORIES = 100_000
MAX_TOTAL_FILE_BYTES = 1024 * 1024 * 1024
MAX_RELATIVE_PATH_BYTES = 4096
MAX_PATH_COMPONENTS = 128
MAX_MANIFEST_BYTES = 64 * 1024 * 1024
SOURCE_CLASS_MAX_LENGTH = 64
AUTHORIZATION_BASIS_MAX_LENGTH = 4096
COPY_BLOCK_SIZE = 1024 * 1024
_HAS_REQUIRED_DIR_FD = all(
    function in os.supports_dir_fd for function in (os.open, os.stat, os.mkdir)
)
_HAS_REQUIRED_NOFOLLOW = os.stat in os.supports_follow_symlinks

EXIT_USAGE = 2
EXIT_MANIFEST = 3
EXIT_VERIFICATION = 4
EXIT_STAGING = 5

TOP_LEVEL_FIELDS = frozenset(
    {
        "manifest_version",
        "source_class",
        "authorization_basis",
        "redistribution_allowed",
        "slot",
        "files",
        "tree_sha256",
    }
)
FILE_FIELDS = frozenset({"path", "size", "sha256"})


class FixtureError(Exception):
    """A deterministic, user-facing fixture failure."""

    def __init__(self, code: str, message: str, exit_code: int) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.exit_code = exit_code


class _DuplicateJsonKey(ValueError):
    pass


@dataclass(frozen=True)
class FileRecord:
    path: str
    size: int
    sha256: str


@dataclass(frozen=True)
class FixtureManifest:
    manifest_version: int
    source_class: str
    authorization_basis: str
    redistribution_allowed: bool
    slot: str
    files: tuple[FileRecord, ...]
    tree_sha256: str


@dataclass(frozen=True)
class VerifiedFixture:
    manifest: FixtureManifest
    manifest_path: Path
    source_root: Path


@dataclass(frozen=True)
class FixtureSetEvidence:
    """Non-content evidence for one continuously pinned fixture set."""

    manifest_sha256: str
    tree_sha256: str
    slot: str
    file_count: int
    total_file_bytes: int

    def as_json(self) -> dict[str, object]:
        return {
            "manifest_sha256": self.manifest_sha256,
            "tree_sha256": self.tree_sha256,
            "slot": self.slot,
            "file_count": self.file_count,
            "total_file_bytes": self.total_file_bytes,
        }


def _manifest_error(message: str) -> NoReturn:
    raise FixtureError("manifest.invalid", message, EXIT_MANIFEST)


def _verification_error(message: str) -> NoReturn:
    raise FixtureError("fixture.verification_failed", message, EXIT_VERIFICATION)


def _staging_error(message: str) -> NoReturn:
    raise FixtureError("fixture.staging_failed", message, EXIT_STAGING)


def _json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise _DuplicateJsonKey(f"duplicate JSON key: {key}")
        value[key] = item
    return value


def _is_plain_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_lower_sha256(value: object) -> bool:
    if not isinstance(value, str) or len(value) != SHA256_HEX_LENGTH:
        return False
    return all(character in "0123456789abcdef" for character in value)


def _contains_control(value: str) -> bool:
    return any(unicodedata.category(character) == "Cc" for character in value)


def _require_exact_fields(
    value: dict[str, Any], expected: frozenset[str], context: str
) -> None:
    actual = frozenset(value)
    unknown = sorted(actual - expected)
    missing = sorted(expected - actual)
    if unknown:
        _manifest_error(f"{context} has unknown field: {unknown[0]}")
    if missing:
        _manifest_error(f"{context} is missing field: {missing[0]}")


def _validate_source_class(value: object) -> str:
    if not isinstance(value, str):
        _manifest_error("source_class must be a string")
    if not (1 <= len(value) <= SOURCE_CLASS_MAX_LENGTH):
        _manifest_error("source_class length is invalid")
    if not ("a" <= value[0] <= "z"):
        _manifest_error("source_class must be a lowercase ASCII identifier")
    if any(
        not ("a" <= character <= "z" or "0" <= character <= "9" or character == "-")
        for character in value
    ):
        _manifest_error("source_class must be a lowercase ASCII identifier")
    return value


def _validate_authorization_basis(value: object) -> str:
    if not isinstance(value, str):
        _manifest_error("authorization_basis must be a string")
    if not (1 <= len(value) <= AUTHORIZATION_BASIS_MAX_LENGTH):
        _manifest_error("authorization_basis length is invalid")
    if value != value.strip():
        _manifest_error("authorization_basis must not have leading or trailing whitespace")
    if _contains_control(value):
        _manifest_error("authorization_basis contains a control character")
    if unicodedata.normalize("NFC", value) != value:
        _manifest_error("authorization_basis must use NFC Unicode normalization")
    return value


def _validate_slot(value: object) -> str:
    if not isinstance(value, str) or len(value) != 1 or value < "A" or value > "J":
        _manifest_error("slot must be one uppercase Classic save slot letter A through J")
    return value


def _canonical_path_key(value: str) -> bytes:
    return value.encode("utf-8")


def _validate_relative_path(value: object, context: str = "file path") -> str:
    if not isinstance(value, str) or not value:
        _manifest_error(f"{context} must be a non-empty string")
    if _contains_control(value):
        _manifest_error(f"{context} contains a control character")
    if "\\" in value:
        _manifest_error(f"{context} must use forward slashes")
    if unicodedata.normalize("NFC", value) != value:
        _manifest_error(f"{context} must use NFC Unicode normalization")
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError:
        _manifest_error(f"{context} is not valid UTF-8 text")
    if len(encoded) > MAX_RELATIVE_PATH_BYTES:
        _manifest_error(f"{context} exceeds {MAX_RELATIVE_PATH_BYTES} UTF-8 bytes")

    posix_path = PurePosixPath(value)
    windows_path = PureWindowsPath(value)
    if posix_path.is_absolute() or windows_path.is_absolute() or windows_path.drive:
        _manifest_error(f"{context} must be relative")
    components = value.split("/")
    if len(components) > MAX_PATH_COMPONENTS:
        _manifest_error(f"{context} has more than {MAX_PATH_COMPONENTS} components")
    if any(component in {"", ".", ".."} for component in components):
        _manifest_error(f"{context} is not a normalized relative path")
    if any(component != component.strip() for component in components):
        _manifest_error(f"{context} components must not have edge whitespace")
    if posix_path.as_posix() != value:
        _manifest_error(f"{context} is not canonical")
    return value


def compute_tree_sha256(files: Iterable[FileRecord]) -> str:
    """Return the v1 tree digest for an already canonical file census."""

    digest = hashlib.sha256()
    digest.update(TREE_DOMAIN)
    for record in files:
        path_bytes = record.path.encode("utf-8")
        digest.update(len(path_bytes).to_bytes(8, "big"))
        digest.update(path_bytes)
        digest.update(record.size.to_bytes(8, "big"))
        digest.update(bytes.fromhex(record.sha256))
    return digest.hexdigest()


def _parse_manifest(value: object) -> FixtureManifest:
    if not isinstance(value, dict):
        _manifest_error("manifest must contain a JSON object")
    _require_exact_fields(value, TOP_LEVEL_FIELDS, "manifest")

    version = value["manifest_version"]
    if not _is_plain_int(version) or version != MANIFEST_VERSION:
        _manifest_error(f"manifest_version must be {MANIFEST_VERSION}")
    source_class = _validate_source_class(value["source_class"])
    authorization_basis = _validate_authorization_basis(value["authorization_basis"])
    redistribution_allowed = value["redistribution_allowed"]
    if not isinstance(redistribution_allowed, bool):
        _manifest_error("redistribution_allowed must be a boolean")
    slot = _validate_slot(value["slot"])

    raw_files = value["files"]
    if not isinstance(raw_files, list) or not raw_files:
        _manifest_error("files must be a non-empty array")
    if len(raw_files) > MAX_FILES:
        _manifest_error(f"files exceeds the limit of {MAX_FILES}")

    records: list[FileRecord] = []
    seen: set[str] = set()
    previous_key: bytes | None = None
    total_file_bytes = 0
    for index, raw_record in enumerate(raw_files):
        context = f"files[{index}]"
        if not isinstance(raw_record, dict):
            _manifest_error(f"{context} must be an object")
        _require_exact_fields(raw_record, FILE_FIELDS, context)
        path = _validate_relative_path(raw_record["path"], f"{context}.path")
        if path in seen:
            _manifest_error(f"duplicate file path: {path}")
        path_key = _canonical_path_key(path)
        if previous_key is not None and path_key <= previous_key:
            _manifest_error("files must be in canonical UTF-8 byte order")
        seen.add(path)
        previous_key = path_key

        size = raw_record["size"]
        if not _is_plain_int(size) or size < 0 or size > MAX_FILE_SIZE:
            _manifest_error(f"{context}.size must be an integer from 0 through {MAX_FILE_SIZE}")
        total_file_bytes += size
        if total_file_bytes > MAX_TOTAL_FILE_BYTES:
            _manifest_error(
                f"files declare more than {MAX_TOTAL_FILE_BYTES} total bytes"
            )
        file_sha256 = raw_record["sha256"]
        if not _is_lower_sha256(file_sha256):
            _manifest_error(f"{context}.sha256 must be 64 lowercase hexadecimal characters")
        records.append(FileRecord(path=path, size=size, sha256=file_sha256))

    file_paths = {record.path for record in records}
    directory_paths = _collect_expected_directories(records, _manifest_error)
    collisions = sorted(file_paths.intersection(directory_paths), key=_canonical_path_key)
    if collisions:
        _manifest_error(
            f"file path is also required as a directory: {collisions[0]}"
        )

    tree_sha256 = value["tree_sha256"]
    if not _is_lower_sha256(tree_sha256):
        _manifest_error("tree_sha256 must be 64 lowercase hexadecimal characters")
    expected_tree_sha256 = compute_tree_sha256(records)
    if tree_sha256 != expected_tree_sha256:
        _manifest_error(
            f"tree_sha256 mismatch: expected {expected_tree_sha256}, got {tree_sha256}"
        )

    return FixtureManifest(
        manifest_version=version,
        source_class=source_class,
        authorization_basis=authorization_basis,
        redistribution_allowed=redistribution_allowed,
        slot=slot,
        files=tuple(records),
        tree_sha256=tree_sha256,
    )


Failure = Callable[[str], NoReturn]
StatIdentity = tuple[int, int, int]
StatStability = tuple[int, int, int, int, int, int, int]


def _require_descriptor_support(fail: Failure) -> None:
    if (
        not hasattr(os, "O_NOFOLLOW")
        or not hasattr(os, "O_DIRECTORY")
        or not hasattr(os, "O_CLOEXEC")
        or os.scandir not in os.supports_fd
        or not _HAS_REQUIRED_DIR_FD
        or not _HAS_REQUIRED_NOFOLLOW
    ):
        fail("this platform does not provide descriptor-anchored filesystem access")


def _read_flags(*, directory: bool = False) -> int:
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW
    if directory:
        flags |= os.O_DIRECTORY
    else:
        flags |= getattr(os, "O_NONBLOCK", 0)
    return flags


def _stat_identity(metadata: os.stat_result) -> StatIdentity:
    return (metadata.st_dev, metadata.st_ino, stat.S_IFMT(metadata.st_mode))


def _stat_stability(metadata: os.stat_result) -> StatStability:
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_mode,
        metadata.st_size,
        metadata.st_nlink,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def _close_descriptor(descriptor: int) -> None:
    if descriptor >= 0:
        try:
            os.close(descriptor)
        except OSError:
            pass


def _manifest_display_path(path: Path | str) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def load_manifest(path: Path | str) -> FixtureManifest:
    """Open once, read through the pinned descriptor, and validate manifest v1."""

    pinned = _open_pinned_manifest(path)
    try:
        return pinned.manifest
    finally:
        pinned.close()


@dataclass
class _PathNode:
    descriptor: int
    identity: StatIdentity
    parent_descriptor: int | None
    name: str | None


@dataclass
class _PathChain:
    display_path: Path
    nodes: list[_PathNode]

    @property
    def descriptor(self) -> int:
        return self.nodes[-1].descriptor

    def close(self) -> None:
        for node in reversed(self.nodes):
            _close_descriptor(node.descriptor)
            node.descriptor = -1


@dataclass
class _PinnedManifest:
    manifest: FixtureManifest
    path: Path
    descriptor: int
    parent_chain: _PathChain
    name: str
    stability: StatStability
    byte_size: int
    sha256: str

    def close(self) -> None:
        _close_descriptor(self.descriptor)
        self.descriptor = -1
        self.parent_chain.close()


@dataclass(frozen=True)
class _PathSpec:
    display_path: Path
    parent_path: Path
    name: str


@dataclass(frozen=True)
class _EntryState:
    name: str
    device: int
    inode: int
    mode: int
    size: int
    link_count: int
    mtime_ns: int
    ctime_ns: int


@dataclass
class _DirectoryAnchor:
    relative: str
    descriptor: int
    identity: StatIdentity
    link_parent_descriptor: int
    link_name: str
    entries: tuple[_EntryState, ...] | None = None


@dataclass
class _PinnedTree:
    label: str
    path: Path
    parent_chain: _PathChain
    root: _DirectoryAnchor
    directories: dict[str, _DirectoryAnchor]
    private_name: str
    published: bool = False
    publish_attempted: bool = False
    tainted: bool = False

    def close_nested(self) -> None:
        nested = sorted(
            (relative for relative in self.directories if relative),
            key=lambda value: (value.count("/"), _canonical_path_key(value)),
            reverse=True,
        )
        for relative in nested:
            anchor = self.directories[relative]
            _close_descriptor(anchor.descriptor)
            anchor.descriptor = -1

    def close(self) -> None:
        self.close_nested()
        _close_descriptor(self.root.descriptor)
        self.root.descriptor = -1
        self.parent_chain.close()


@dataclass
class _DestinationPlan:
    label: str
    spec: _PathSpec
    parent_chain: _PathChain | None
    private_name: str | None = None
    root_identity: StatIdentity | None = None
    retention_reported: bool = False

    def close(self) -> None:
        if self.parent_chain is not None:
            self.parent_chain.close()
            self.parent_chain = None


@dataclass
class _DirectoryNameObservation:
    name: str
    status: str = "unchecked"
    descriptor: int = -1
    metadata: os.stat_result | None = None

    def close(self) -> None:
        _close_descriptor(self.descriptor)
        self.descriptor = -1


def _path_spec(path: Path | str, label: str, fail: Failure) -> _PathSpec:
    raw_path = os.fspath(path)
    if not isinstance(raw_path, str) or not raw_path:
        fail(f"{label} path must be non-empty filesystem text")
    if _contains_control(raw_path) or "\\" in raw_path:
        fail(f"{label} path contains a forbidden character")
    try:
        if os.path.isabs(raw_path):
            absolute_text = raw_path
        else:
            absolute_text = os.path.join(os.getcwd(), raw_path)
    except OSError as error:
        fail(f"cannot resolve the current directory for {label}: {error.strerror or error}")
    components = absolute_text.split(os.sep)
    if not components or components[0] != "" or any(
        component in {"", ".", ".."} for component in components[1:]
    ):
        fail(f"{label} path must be lexically normalized")
    absolute = Path(absolute_text)
    if absolute == Path(absolute.anchor) or not absolute.name:
        fail(f"{label} must name a directory below the filesystem root")
    parent = absolute.parent
    return _PathSpec(display_path=parent / absolute.name, parent_path=parent, name=absolute.name)


def _open_directory_chain(path: Path, label: str, fail: Failure) -> _PathChain:
    _require_descriptor_support(fail)
    if not path.is_absolute():
        fail(f"internal error: {label} parent path is not absolute")
    nodes: list[_PathNode] = []
    pending_descriptor = -1
    try:
        pending_descriptor = os.open(path.anchor, _read_flags(directory=True))
        root_metadata = os.fstat(pending_descriptor)
        if not stat.S_ISDIR(root_metadata.st_mode):
            fail(f"filesystem root for {label} is not a directory")
        nodes.append(
            _PathNode(
                descriptor=pending_descriptor,
                identity=_stat_identity(root_metadata),
                parent_descriptor=None,
                name=None,
            )
        )
        pending_descriptor = -1
        for component in path.parts[1:]:
            parent_descriptor = nodes[-1].descriptor
            link_metadata = os.stat(
                component,
                dir_fd=parent_descriptor,
                follow_symlinks=False,
            )
            if not stat.S_ISDIR(link_metadata.st_mode):
                fail(f"path component for {label} is not a directory: {component}")
            pending_descriptor = os.open(
                component,
                _read_flags(directory=True),
                dir_fd=parent_descriptor,
            )
            opened_metadata = os.fstat(pending_descriptor)
            if _stat_identity(link_metadata) != _stat_identity(opened_metadata):
                _close_descriptor(pending_descriptor)
                pending_descriptor = -1
                fail(f"path component changed while opening {label}: {component}")
            nodes.append(
                _PathNode(
                    descriptor=pending_descriptor,
                    identity=_stat_identity(opened_metadata),
                    parent_descriptor=parent_descriptor,
                    name=component,
                )
            )
            pending_descriptor = -1
    except FixtureError:
        _close_descriptor(pending_descriptor)
        for node in reversed(nodes):
            _close_descriptor(node.descriptor)
        raise
    except OSError as error:
        _close_descriptor(pending_descriptor)
        for node in reversed(nodes):
            _close_descriptor(node.descriptor)
        fail(f"cannot open descriptor path for {label}: {error.strerror or error}")
    return _PathChain(display_path=path, nodes=nodes)


def _assert_path_chain_stable(chain: _PathChain, label: str, fail: Failure) -> None:
    for node in chain.nodes:
        try:
            opened_metadata = os.fstat(node.descriptor)
            if _stat_identity(opened_metadata) != node.identity:
                fail(f"descriptor path changed for {label}")
            if node.parent_descriptor is not None and node.name is not None:
                link_metadata = os.stat(
                    node.name,
                    dir_fd=node.parent_descriptor,
                    follow_symlinks=False,
                )
                if _stat_identity(link_metadata) != node.identity:
                    fail(f"descriptor path link changed for {label}: {node.name}")
        except FixtureError:
            raise
        except OSError as error:
            fail(f"cannot recheck descriptor path for {label}: {error.strerror or error}")


def _read_manifest_bytes(
    descriptor: int,
    display_path: Path,
    fail: Failure,
) -> bytes:
    try:
        os.lseek(descriptor, 0, os.SEEK_SET)
        chunks: list[bytes] = []
        total = 0
        while True:
            block = os.read(
                descriptor,
                min(COPY_BLOCK_SIZE, MAX_MANIFEST_BYTES + 1 - total),
            )
            if not block:
                break
            chunks.append(block)
            total += len(block)
            if total > MAX_MANIFEST_BYTES:
                fail(f"manifest exceeds {MAX_MANIFEST_BYTES} bytes: {display_path}")
    except FixtureError:
        raise
    except OSError as error:
        fail(f"cannot read manifest {display_path}: {error.strerror or error}")
    return b"".join(chunks)


def _open_pinned_manifest(path: Path | str) -> _PinnedManifest:
    _require_descriptor_support(_manifest_error)
    spec = _path_spec(path, "manifest", _manifest_error)
    parent_chain = _open_directory_chain(spec.parent_path, "manifest", _manifest_error)
    try:
        descriptor = os.open(
            spec.name,
            _read_flags(),
            dir_fd=parent_chain.descriptor,
        )
    except OSError as error:
        parent_chain.close()
        _manifest_error(
            f"cannot open manifest {spec.display_path}: {error.strerror or error}"
        )

    try:
        _assert_path_chain_stable(parent_chain, "manifest", _manifest_error)
        before = os.fstat(descriptor)
        link_before = os.stat(
            spec.name,
            dir_fd=parent_chain.descriptor,
            follow_symlinks=False,
        )
        if not stat.S_ISREG(before.st_mode):
            _manifest_error(f"manifest must be a regular file: {spec.display_path}")
        if _stat_identity(before) != _stat_identity(link_before):
            _manifest_error(f"manifest link changed while opening: {spec.display_path}")
        if before.st_size > MAX_MANIFEST_BYTES:
            _manifest_error(
                f"manifest exceeds {MAX_MANIFEST_BYTES} bytes: {spec.display_path}"
            )

        raw = _read_manifest_bytes(descriptor, spec.display_path, _manifest_error)
        after = os.fstat(descriptor)
        link_after = os.stat(
            spec.name,
            dir_fd=parent_chain.descriptor,
            follow_symlinks=False,
        )
        if (
            _stat_stability(before) != _stat_stability(after)
            or _stat_identity(after) != _stat_identity(link_after)
            or len(raw) != after.st_size
        ):
            _manifest_error(f"manifest changed while it was being read: {spec.display_path}")
        _assert_path_chain_stable(parent_chain, "manifest", _manifest_error)

        try:
            text = raw.decode("utf-8")
            value = json.loads(text, object_pairs_hook=_json_object)
        except UnicodeDecodeError:
            _manifest_error(f"manifest is not valid UTF-8: {spec.display_path}")
        except (ValueError, RecursionError) as error:
            _manifest_error(f"manifest is not valid JSON: {error}")

        return _PinnedManifest(
            manifest=_parse_manifest(value),
            path=spec.display_path,
            descriptor=descriptor,
            parent_chain=parent_chain,
            name=spec.name,
            stability=_stat_stability(after),
            byte_size=len(raw),
            sha256=hashlib.sha256(raw).hexdigest(),
        )
    except FixtureError:
        _close_descriptor(descriptor)
        parent_chain.close()
        raise
    except OSError as error:
        _close_descriptor(descriptor)
        parent_chain.close()
        _manifest_error(
            f"cannot read manifest {spec.display_path}: {error.strerror or error}"
        )
    except BaseException:
        _close_descriptor(descriptor)
        parent_chain.close()
        raise


def _verify_pinned_manifest(
    pinned: _PinnedManifest,
    fail: Failure,
) -> None:
    try:
        _assert_path_chain_stable(pinned.parent_chain, "manifest", fail)
        before = os.fstat(pinned.descriptor)
        linked = os.stat(
            pinned.name,
            dir_fd=pinned.parent_chain.descriptor,
            follow_symlinks=False,
        )
        if (
            _stat_stability(before) != pinned.stability
            or _stat_identity(before) != _stat_identity(linked)
            or before.st_size != pinned.byte_size
        ):
            fail(f"manifest changed while fixture trees were leased: {pinned.path}")
        raw = _read_manifest_bytes(pinned.descriptor, pinned.path, fail)
        after = os.fstat(pinned.descriptor)
        linked_after = os.stat(
            pinned.name,
            dir_fd=pinned.parent_chain.descriptor,
            follow_symlinks=False,
        )
        if (
            _stat_stability(after) != pinned.stability
            or _stat_identity(after) != _stat_identity(linked_after)
            or len(raw) != pinned.byte_size
            or hashlib.sha256(raw).hexdigest() != pinned.sha256
        ):
            fail(f"manifest changed while fixture trees were leased: {pinned.path}")
        _assert_path_chain_stable(pinned.parent_chain, "manifest", fail)
    except FixtureError:
        raise
    except OSError as error:
        fail(f"cannot recheck leased manifest {pinned.path}: {error.strerror or error}")


def _open_existing_tree(path: Path | str, label: str, fail: Failure) -> _PinnedTree:
    spec = _path_spec(path, label, fail)
    parent_chain = _open_directory_chain(spec.parent_path, label, fail)
    descriptor = -1
    try:
        link_metadata = os.stat(
            spec.name,
            dir_fd=parent_chain.descriptor,
            follow_symlinks=False,
        )
        if not stat.S_ISDIR(link_metadata.st_mode):
            fail(f"{label} must be a directory and not a symbolic link: {spec.display_path}")
        descriptor = os.open(
            spec.name,
            _read_flags(directory=True),
            dir_fd=parent_chain.descriptor,
        )
        opened_metadata = os.fstat(descriptor)
        if _stat_identity(link_metadata) != _stat_identity(opened_metadata):
            fail(f"{label} changed while it was being opened: {spec.display_path}")
    except FixtureError:
        _close_descriptor(descriptor)
        parent_chain.close()
        raise
    except OSError as error:
        _close_descriptor(descriptor)
        parent_chain.close()
        fail(f"cannot open {label} {spec.display_path}: {error.strerror or error}")

    root = _DirectoryAnchor(
        relative="",
        descriptor=descriptor,
        identity=_stat_identity(opened_metadata),
        link_parent_descriptor=parent_chain.descriptor,
        link_name=spec.name,
    )
    return _PinnedTree(
        label=label,
        path=spec.display_path,
        parent_chain=parent_chain,
        root=root,
        directories={"": root},
        private_name=spec.name,
    )


def _entry_state(
    directory_descriptor: int,
    name: str,
    relative: str,
    fail: Failure,
) -> _EntryState:
    try:
        metadata = os.stat(name, dir_fd=directory_descriptor, follow_symlinks=False)
    except OSError as error:
        fail(f"cannot inspect fixture entry {relative}: {error.strerror or error}")
    return _EntryState(
        name=name,
        device=metadata.st_dev,
        inode=metadata.st_ino,
        mode=metadata.st_mode,
        size=metadata.st_size,
        link_count=metadata.st_nlink,
        mtime_ns=metadata.st_mtime_ns,
        ctime_ns=metadata.st_ctime_ns,
    )


def _state_identity(entry: _EntryState) -> StatIdentity:
    return (entry.device, entry.inode, stat.S_IFMT(entry.mode))


def _list_directory(
    anchor: _DirectoryAnchor,
    fail: Failure,
) -> tuple[_EntryState, ...]:
    try:
        with os.scandir(anchor.descriptor) as iterator:
            names: list[str] = []
            for entry in iterator:
                if len(names) >= MAX_FILES + MAX_DIRECTORIES:
                    fail(
                        f"fixture directory census exceeds "
                        f"{MAX_FILES + MAX_DIRECTORIES} entries: "
                        f"{anchor.relative or '.'}"
                    )
                names.append(entry.name)
    except OSError as error:
        fail(f"cannot scan fixture directory {anchor.relative or '.'}: {error.strerror or error}")

    validated: list[tuple[bytes, str, str]] = []
    for name in names:
        relative = f"{anchor.relative}/{name}" if anchor.relative else name
        try:
            canonical = _validate_relative_path(relative, "filesystem path")
        except FixtureError as error:
            fail(error.message)
        validated.append((_canonical_path_key(canonical), name, canonical))
    return tuple(
        _entry_state(anchor.descriptor, name, relative, fail)
        for _, name, relative in sorted(validated, key=lambda item: item[0])
    )


def _open_child_directory(
    parent: _DirectoryAnchor,
    entry: _EntryState,
    relative: str,
    fail: Failure,
) -> _DirectoryAnchor:
    descriptor = -1
    try:
        descriptor = os.open(
            entry.name,
            _read_flags(directory=True),
            dir_fd=parent.descriptor,
        )
        metadata = os.fstat(descriptor)
        if _state_identity(entry) != _stat_identity(metadata):
            _close_descriptor(descriptor)
            fail(f"fixture directory changed while opening it: {relative}")
    except FixtureError:
        raise
    except OSError as error:
        _close_descriptor(descriptor)
        fail(f"cannot open fixture directory {relative}: {error.strerror or error}")
    return _DirectoryAnchor(
        relative=relative,
        descriptor=descriptor,
        identity=_stat_identity(metadata),
        link_parent_descriptor=parent.descriptor,
        link_name=entry.name,
    )


def _discover_tree(
    tree: _PinnedTree,
    fail: Failure,
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    files: list[str] = []
    directories: list[str] = []
    pending = [tree.root]
    while pending:
        anchor = pending.pop()
        entries = _list_directory(anchor, fail)
        child_directories: list[_DirectoryAnchor] = []
        for entry in entries:
            relative = f"{anchor.relative}/{entry.name}" if anchor.relative else entry.name
            if stat.S_ISLNK(entry.mode):
                fail(f"symbolic links are forbidden in a fixture: {relative}")
            if stat.S_ISDIR(entry.mode):
                if len(directories) >= MAX_DIRECTORIES:
                    fail(f"fixture has more than {MAX_DIRECTORIES} directories")
                child = _open_child_directory(anchor, entry, relative, fail)
                tree.directories[relative] = child
                directories.append(relative)
                child_directories.append(child)
            elif stat.S_ISREG(entry.mode):
                if len(files) >= MAX_FILES:
                    fail(f"fixture has more than {MAX_FILES} files")
                files.append(relative)
            else:
                fail(f"special files are forbidden in a fixture: {relative}")
        pending.extend(reversed(child_directories))
    return (
        tuple(sorted(files, key=_canonical_path_key)),
        tuple(sorted(directories, key=_canonical_path_key)),
    )


def _capture_tree(
    tree: _PinnedTree,
    fail: Failure,
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    files: list[str] = []
    directories: list[str] = []
    for relative in sorted(tree.directories, key=_canonical_path_key):
        anchor = tree.directories[relative]
        entries = _list_directory(anchor, fail)
        anchor.entries = entries
        for entry in entries:
            child_relative = f"{relative}/{entry.name}" if relative else entry.name
            if stat.S_ISLNK(entry.mode):
                fail(f"symbolic links are forbidden in a fixture: {child_relative}")
            if stat.S_ISDIR(entry.mode):
                if len(directories) >= MAX_DIRECTORIES:
                    fail(f"fixture has more than {MAX_DIRECTORIES} directories")
                directories.append(child_relative)
            elif stat.S_ISREG(entry.mode):
                if len(files) >= MAX_FILES:
                    fail(f"fixture has more than {MAX_FILES} files")
                files.append(child_relative)
            else:
                fail(f"special files are forbidden in a fixture: {child_relative}")
    return (
        tuple(sorted(files, key=_canonical_path_key)),
        tuple(sorted(set(directories), key=_canonical_path_key)),
    )


def _assert_tree_links_stable(tree: _PinnedTree, fail: Failure) -> None:
    try:
        _assert_path_chain_stable(tree.parent_chain, tree.label, fail)
    except FixtureError:
        tree.tainted = True
        raise
    for relative in sorted(tree.directories, key=_canonical_path_key):
        anchor = tree.directories[relative]
        try:
            opened_metadata = os.fstat(anchor.descriptor)
            link_metadata = os.stat(
                anchor.link_name,
                dir_fd=anchor.link_parent_descriptor,
                follow_symlinks=False,
            )
        except OSError as error:
            tree.tainted = True
            fail(
                f"cannot recheck fixture directory {relative or tree.path}: "
                f"{error.strerror or error}"
            )
        if (
            _stat_identity(opened_metadata) != anchor.identity
            or _stat_identity(link_metadata) != anchor.identity
        ):
            tree.tainted = True
            fail(f"fixture directory link changed: {relative or tree.path}")


def _assert_tree_stable(tree: _PinnedTree, fail: Failure) -> None:
    _assert_tree_links_stable(tree, fail)
    for relative in sorted(tree.directories, key=_canonical_path_key):
        anchor = tree.directories[relative]
        if anchor.entries is None:
            fail(f"internal error: fixture directory was not captured: {relative or '.'}")
        current = _list_directory(anchor, fail)
        if current != anchor.entries:
            fail(f"fixture directory contents changed: {relative or tree.path}")


def _collect_expected_directories(
    records: Sequence[FileRecord],
    fail: Failure,
) -> set[str]:
    values: set[str] = set()
    for record in records:
        parts = record.path.split("/")
        for end in range(len(parts) - 1, 0, -1):
            parent = "/".join(parts[:end])
            if parent in values:
                break
            values.add(parent)
            if len(values) > MAX_DIRECTORIES:
                fail(f"fixture requires more than {MAX_DIRECTORIES} directories")
    return values


def _expected_directories(records: Sequence[FileRecord]) -> tuple[str, ...]:
    values = _collect_expected_directories(records, _manifest_error)
    return tuple(sorted(values, key=_canonical_path_key))


def _compare_census(
    actual_files: Sequence[str],
    actual_directories: Sequence[str],
    manifest: FixtureManifest,
    label: str,
    fail: Failure,
) -> None:
    expected_files = tuple(record.path for record in manifest.files)
    if tuple(actual_files) != expected_files:
        missing = sorted(set(expected_files) - set(actual_files), key=_canonical_path_key)
        extra = sorted(set(actual_files) - set(expected_files), key=_canonical_path_key)
        if missing:
            fail(f"{label} is missing file: {missing[0]}")
        fail(f"{label} has extra file: {extra[0]}")

    expected_directories = _expected_directories(manifest.files)
    if tuple(actual_directories) != expected_directories:
        missing = sorted(
            set(expected_directories) - set(actual_directories), key=_canonical_path_key
        )
        extra = sorted(
            set(actual_directories) - set(expected_directories), key=_canonical_path_key
        )
        if missing:
            fail(f"{label} is missing directory: {missing[0]}")
        fail(f"{label} has extra directory: {extra[0]}")


def _record_parent(tree: _PinnedTree, record: FileRecord, fail: Failure) -> tuple[int, str]:
    parts = record.path.split("/")
    parent_relative = "/".join(parts[:-1])
    parent = tree.directories.get(parent_relative)
    if parent is None:
        fail(f"fixture parent directory is not pinned: {parent_relative or '.'}")
    return parent.descriptor, parts[-1]


def _open_regular_leaf(
    tree: _PinnedTree,
    record: FileRecord,
    fail: Failure,
) -> tuple[int, os.stat_result]:
    parent_descriptor, name = _record_parent(tree, record, fail)
    descriptor = -1
    try:
        descriptor = os.open(name, _read_flags(), dir_fd=parent_descriptor)
        opened_metadata = os.fstat(descriptor)
        link_metadata = os.stat(name, dir_fd=parent_descriptor, follow_symlinks=False)
        if not stat.S_ISREG(opened_metadata.st_mode):
            _close_descriptor(descriptor)
            fail(f"fixture entry is not a regular file: {record.path}")
        if _stat_identity(opened_metadata) != _stat_identity(link_metadata):
            _close_descriptor(descriptor)
            fail(f"fixture file changed while opening it: {record.path}")
    except FixtureError:
        raise
    except OSError as error:
        _close_descriptor(descriptor)
        fail(f"cannot open fixture file {record.path}: {error.strerror or error}")
    return descriptor, opened_metadata


def _hash_record(
    tree: _PinnedTree,
    record: FileRecord,
    fail: Failure,
) -> tuple[int, str, tuple[int, int], int]:
    descriptor, before = _open_regular_leaf(tree, record, fail)
    digest = hashlib.sha256()
    total = 0
    try:
        if before.st_size != record.size:
            fail(
                f"fixture size mismatch for {record.path}: "
                f"expected {record.size}, got {before.st_size}"
            )
        while True:
            block = os.read(
                descriptor,
                min(COPY_BLOCK_SIZE, record.size + 1 - total),
            )
            if not block:
                break
            total += len(block)
            if total > record.size:
                fail(f"fixture file exceeded its declared size: {record.path}")
            digest.update(block)
        after = os.fstat(descriptor)
        parent_descriptor, name = _record_parent(tree, record, fail)
        link_metadata = os.stat(name, dir_fd=parent_descriptor, follow_symlinks=False)
        if (
            _stat_stability(before) != _stat_stability(after)
            or _stat_identity(after) != _stat_identity(link_metadata)
            or total != after.st_size
        ):
            fail(f"fixture file changed while it was being read: {record.path}")
    except FixtureError:
        raise
    except OSError as error:
        fail(f"cannot read fixture file {record.path}: {error.strerror or error}")
    finally:
        _close_descriptor(descriptor)
    return total, digest.hexdigest(), (after.st_dev, after.st_ino), after.st_nlink


def _verify_hashes(
    tree: _PinnedTree,
    manifest: FixtureManifest,
    label: str,
    fail: Failure,
) -> dict[str, tuple[int, int, int]]:
    identities: dict[str, tuple[int, int, int]] = {}
    for record in manifest.files:
        size, file_sha256, identity, link_count = _hash_record(tree, record, fail)
        if size != record.size:
            fail(f"{label} size mismatch for {record.path}: expected {record.size}, got {size}")
        if file_sha256 != record.sha256:
            fail(
                f"{label} SHA-256 mismatch for {record.path}: "
                f"expected {record.sha256}, got {file_sha256}"
            )
        identities[record.path] = (identity[0], identity[1], link_count)
    return identities


def _verify_tree_initial(
    tree: _PinnedTree,
    manifest: FixtureManifest,
    fail: Failure,
) -> dict[str, tuple[int, int, int]]:
    actual_files, actual_directories = _discover_tree(tree, fail)
    _compare_census(
        actual_files,
        actual_directories,
        manifest,
        tree.label,
        fail,
    )
    captured_files, captured_directories = _capture_tree(tree, fail)
    _compare_census(
        captured_files,
        captured_directories,
        manifest,
        tree.label,
        fail,
    )
    _assert_tree_stable(tree, fail)
    identities = _verify_hashes(tree, manifest, tree.label, fail)
    _assert_tree_stable(tree, fail)
    return identities


def _verify_tree_again(
    tree: _PinnedTree,
    manifest: FixtureManifest,
    label: str,
    fail: Failure,
) -> dict[str, tuple[int, int, int]]:
    _assert_tree_stable(tree, fail)
    identities = _verify_hashes(tree, manifest, label, fail)
    _assert_tree_stable(tree, fail)
    return identities


def _verify_source_initial(tree: _PinnedTree, manifest: FixtureManifest) -> None:
    _verify_tree_initial(tree, manifest, _verification_error)


def _verify_source_again(tree: _PinnedTree, manifest: FixtureManifest) -> None:
    _verify_tree_again(
        tree,
        manifest,
        "source root after staging",
        _verification_error,
    )


def verify_fixture(
    manifest_path: Path | str, source_root: Path | str
) -> VerifiedFixture:
    """Verify an explicitly supplied source root through pinned descriptors."""

    manifest = load_manifest(manifest_path)
    tree = _open_existing_tree(source_root, "source root", _verification_error)
    try:
        _verify_source_initial(tree, manifest)
        canonical_source = tree.path
    finally:
        tree.close()
    return VerifiedFixture(
        manifest=manifest,
        manifest_path=_manifest_display_path(manifest_path),
        source_root=canonical_source,
    )


def _is_nested(first: Path, second: Path) -> bool:
    return first == second or first in second.parents or second in first.parents


def _check_distinct_roots(
    source: Path,
    classic: Path,
    semantic: Path,
    fail: Failure = _staging_error,
) -> None:
    for left_label, left, right_label, right in (
        ("source root", source, "Classic root", classic),
        ("source root", source, "semantic root", semantic),
        ("Classic root", classic, "semantic root", semantic),
    ):
        if _is_nested(left, right):
            fail(
                f"{left_label} and {right_label} must be distinct, non-nested roots: "
                f"{left} ; {right}"
            )


def _open_destination_plan(spec: _PathSpec, label: str) -> _DestinationPlan:
    chain = _open_directory_chain(spec.parent_path, label, _staging_error)
    try:
        parent_metadata = os.fstat(chain.descriptor)
        # POSIX mkdir does not return a descriptor, so no portable sequence can
        # exclude a hostile same-UID writer between mkdirat and openat.  The
        # immediate parent is therefore an explicit trusted-writer boundary.
        if parent_metadata.st_uid != os.geteuid():
            _staging_error(f"parent of {label} must be owned by the current user")
        if parent_metadata.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
            _staging_error(
                f"parent of {label} must not be group- or world-writable"
            )
        _assert_path_chain_stable(chain, label, _staging_error)
        os.stat(spec.name, dir_fd=chain.descriptor, follow_symlinks=False)
    except FixtureError:
        chain.close()
        raise
    except FileNotFoundError:
        return _DestinationPlan(label=label, spec=spec, parent_chain=chain)
    except OSError as error:
        chain.close()
        _staging_error(f"cannot inspect {label} {spec.display_path}: {error.strerror or error}")
    chain.close()
    _staging_error(f"{label} already exists: {spec.display_path}")


def _check_descriptor_aliases(
    source: _PinnedTree,
    plans: Sequence[_DestinationPlan],
) -> None:
    source_directories = {anchor.identity for anchor in source.directories.values()}
    destination_names: set[tuple[StatIdentity, str]] = set()
    for plan in plans:
        if plan.parent_chain is None:
            _staging_error(f"internal error: {plan.label} plan is not open")
        parent_identity = _stat_identity(os.fstat(plan.parent_chain.descriptor))
        if parent_identity in source_directories:
            _staging_error(
                f"parent of {plan.label} aliases a directory inside the source root"
            )
        destination = (parent_identity, plan.spec.name)
        if destination in destination_names:
            _staging_error("Classic root and semantic root resolve to the same directory name")
        destination_names.add(destination)


_LIBC = ctypes.CDLL(None, use_errno=True)


def _native_rename_noreplace(
    source_parent: int,
    source_name: str,
    destination_parent: int,
    destination_name: str,
) -> None:
    source_bytes = os.fsencode(source_name)
    destination_bytes = os.fsencode(destination_name)
    ctypes.set_errno(0)
    if sys.platform == "darwin":
        function = _LIBC.renameatx_np
        function.argtypes = [
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        ]
        function.restype = ctypes.c_int
        result = function(
            source_parent,
            source_bytes,
            destination_parent,
            destination_bytes,
            0x00000004,
        )
    elif sys.platform.startswith("linux") and hasattr(_LIBC, "renameat2"):
        function = _LIBC.renameat2
        function.argtypes = [
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        ]
        function.restype = ctypes.c_int
        result = function(
            source_parent,
            source_bytes,
            destination_parent,
            destination_bytes,
            1,
        )
    else:
        raise OSError("atomic no-replace rename is unavailable on this platform")
    if result != 0:
        error_number = ctypes.get_errno()
        raise OSError(error_number, os.strerror(error_number))


def _unused_private_name(parent_descriptor: int, purpose: str) -> str:
    for _ in range(32):
        candidate = f".realmz-{purpose}-{secrets.token_hex(24)}"
        try:
            os.stat(candidate, dir_fd=parent_descriptor, follow_symlinks=False)
        except FileNotFoundError:
            return candidate
        except OSError as error:
            _staging_error(
                f"cannot inspect private {purpose} name: {error.strerror or error}"
            )
    _staging_error(f"cannot allocate an unused private {purpose} name")


def _observe_directory_name(
    parent_descriptor: int,
    observation: _DirectoryNameObservation,
) -> None:
    observation.close()
    observation.metadata = None
    try:
        linked = os.stat(
            observation.name,
            dir_fd=parent_descriptor,
            follow_symlinks=False,
        )
    except FileNotFoundError:
        observation.status = "missing"
        return
    except OSError:
        observation.status = "uninspectable"
        return
    if not stat.S_ISDIR(linked.st_mode):
        observation.status = "non-directory"
        return
    try:
        observation.descriptor = os.open(
            observation.name,
            _read_flags(directory=True),
            dir_fd=parent_descriptor,
        )
        opened = os.fstat(observation.descriptor)
    except OSError:
        observation.close()
        observation.status = "uninspectable"
        return
    if _stat_identity(linked) != _stat_identity(opened):
        observation.close()
        observation.status = "unstable"
        return
    observation.metadata = opened
    observation.status = "pinned"


def _unadopted_private_report(
    parent_chain: _PathChain,
    private_name: str,
    observation: _DirectoryNameObservation,
) -> str:
    attempted = parent_chain.display_path / private_name
    return (
        f"retained namespace-uncertain private staging root allocation if created; "
        f"last-observed attempted name status={observation.status} (it may now be "
        f"missing or refer to a replacement): {attempted}; no deletion attempted"
    )


def _create_destination_tree(
    plan: _DestinationPlan,
    ownership: list[_PinnedTree] | None = None,
) -> _PinnedTree:
    if plan.parent_chain is None:
        _staging_error(f"internal error: {plan.label} plan was already consumed")
    parent_chain = plan.parent_chain
    private_name = _unused_private_name(parent_chain.descriptor, "stage")
    # Register the candidate before mkdir so outer failure handling can report
    # it even if this helper is interrupted at a later Python bytecode boundary.
    plan.private_name = private_name
    observation = _DirectoryNameObservation(private_name)
    try:
        _assert_path_chain_stable(parent_chain, plan.label, _staging_error)
        parent_before = os.fstat(parent_chain.descriptor)
        try:
            os.mkdir(private_name, mode=0o700, dir_fd=parent_chain.descriptor)
        finally:
            # Reconcile even when an asynchronous exception arrives after the
            # syscall succeeds but before normal Python bookkeeping can run.
            _observe_directory_name(parent_chain.descriptor, observation)
        if observation.status != "pinned" or observation.metadata is None:
            _staging_error(
                f"new {plan.label} could not be pinned after creation: "
                f"{observation.status}"
            )
        opened_metadata = observation.metadata
        plan.root_identity = _stat_identity(opened_metadata)
        parent_after = os.fstat(parent_chain.descriptor)
        if stat.S_IMODE(opened_metadata.st_mode) != 0o700:
            _staging_error(f"new {plan.label} private root does not have mode 0700")
        if parent_after.st_nlink != parent_before.st_nlink + 1:
            _staging_error(
                f"new {plan.label} was replaced before its descriptor was pinned; "
                f"cleanup refused for private name {private_name}"
            )
    except BaseException as error:
        if observation.metadata is not None:
            plan.root_identity = _stat_identity(observation.metadata)
        observation.close()
        report = _unadopted_private_report(
            parent_chain,
            private_name,
            observation,
        )
        if isinstance(error, FixtureError):
            plan.retention_reported = True
            raise FixtureError(
                error.code,
                f"{error.message}; {report}",
                error.exit_code,
            ) from error
        if isinstance(error, OSError):
            detail = (
                f"cannot create {plan.label} {plan.spec.display_path}: "
                f"{error.strerror or error}; {report}"
            )
        else:
            detail = (
                f"interrupted while creating {plan.label}: "
                f"{type(error).__name__}; {report}"
            )
        plan.retention_reported = True
        raise FixtureError("fixture.staging_failed", detail, EXIT_STAGING) from error

    descriptor = observation.descriptor
    observation.descriptor = -1
    root = _DirectoryAnchor(
        relative="",
        descriptor=descriptor,
        identity=_stat_identity(opened_metadata),
        link_parent_descriptor=parent_chain.descriptor,
        link_name=private_name,
    )
    tree = _PinnedTree(
        label=plan.label,
        path=plan.spec.display_path,
        parent_chain=parent_chain,
        root=root,
        directories={"": root},
        private_name=private_name,
    )
    if ownership is not None:
        # Transfer ownership before returning so an asynchronous exception in a
        # caller wrapper cannot strand an unreported, fully created tree.
        ownership.append(tree)
    plan.parent_chain = None
    return tree


def _create_fixture_directories(tree: _PinnedTree, records: Sequence[FileRecord]) -> None:
    directories = sorted(
        _expected_directories(records),
        key=lambda value: (value.count("/"), _canonical_path_key(value)),
    )
    for relative in directories:
        parts = relative.split("/")
        parent_relative = "/".join(parts[:-1])
        parent = tree.directories[parent_relative]
        name = parts[-1]
        descriptor = -1
        directory_created = False
        try:
            parent_before = os.fstat(parent.descriptor)
            os.mkdir(name, mode=0o700, dir_fd=parent.descriptor)
            directory_created = True
            link_metadata = os.stat(name, dir_fd=parent.descriptor, follow_symlinks=False)
            descriptor = os.open(
                name,
                _read_flags(directory=True),
                dir_fd=parent.descriptor,
            )
            opened_metadata = os.fstat(descriptor)
            parent_after = os.fstat(parent.descriptor)
            if _stat_identity(link_metadata) != _stat_identity(opened_metadata):
                tree.tainted = True
                _close_descriptor(descriptor)
                _staging_error(f"staged directory changed while opening it: {relative}")
            if parent_after.st_nlink != parent_before.st_nlink + 1:
                tree.tainted = True
                _close_descriptor(descriptor)
                descriptor = -1
                _staging_error(
                    f"staged directory was replaced before its descriptor was pinned: "
                    f"{relative}"
                )
        except FixtureError:
            if directory_created:
                tree.tainted = True
            raise
        except OSError as error:
            if directory_created:
                tree.tainted = True
            _close_descriptor(descriptor)
            _staging_error(f"cannot create staged directory {relative}: {error.strerror or error}")
        tree.directories[relative] = _DirectoryAnchor(
            relative=relative,
            descriptor=descriptor,
            identity=_stat_identity(opened_metadata),
            link_parent_descriptor=parent.descriptor,
            link_name=name,
        )


def _open_new_leaf(tree: _PinnedTree, record: FileRecord) -> int:
    parent_descriptor, name = _record_parent(tree, record, _staging_error)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW
    try:
        return os.open(name, flags, 0o600, dir_fd=parent_descriptor)
    except OSError as error:
        _staging_error(f"cannot create staged file {record.path}: {error.strerror or error}")


def _write_all(descriptor: int, block: bytes) -> None:
    remaining = memoryview(block)
    while remaining:
        written = os.write(descriptor, remaining)
        if written <= 0:
            _staging_error("short write while staging fixture")
        remaining = remaining[written:]


def _copy_record_to_trees(
    source: _PinnedTree,
    classic: _PinnedTree,
    semantic: _PinnedTree,
    record: FileRecord,
) -> None:
    _assert_tree_stable(source, _staging_error)
    _assert_tree_links_stable(classic, _staging_error)
    _assert_tree_links_stable(semantic, _staging_error)
    source_descriptor, source_before = _open_regular_leaf(source, record, _staging_error)
    classic_descriptor = -1
    semantic_descriptor = -1
    digest = hashlib.sha256()
    total = 0
    try:
        if source_before.st_size != record.size:
            _staging_error(
                f"source size mismatch for {record.path}: "
                f"expected {record.size}, got {source_before.st_size}"
            )
        classic_descriptor = _open_new_leaf(classic, record)
        semantic_descriptor = _open_new_leaf(semantic, record)
        while True:
            block = os.read(
                source_descriptor,
                min(COPY_BLOCK_SIZE, record.size + 1 - total),
            )
            if not block:
                break
            total += len(block)
            if total > record.size:
                _staging_error(f"source file exceeded its declared size: {record.path}")
            digest.update(block)
            _write_all(classic_descriptor, block)
            _write_all(semantic_descriptor, block)
        source_after = os.fstat(source_descriptor)
        classic_metadata = os.fstat(classic_descriptor)
        semantic_metadata = os.fstat(semantic_descriptor)
    except FixtureError:
        raise
    except OSError as error:
        _staging_error(f"cannot stage {record.path}: {error.strerror or error}")
    finally:
        _close_descriptor(source_descriptor)
        _close_descriptor(classic_descriptor)
        _close_descriptor(semantic_descriptor)

    if _stat_stability(source_before) != _stat_stability(source_after):
        _staging_error(f"source file changed while it was being staged: {record.path}")
    if total != record.size or digest.hexdigest() != record.sha256:
        _staging_error(f"source file no longer matches the manifest: {record.path}")
    for label, metadata in (
        ("Classic", classic_metadata),
        ("semantic", semantic_metadata),
    ):
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_size != record.size:
            _staging_error(f"{label} staged file is not byte complete: {record.path}")
    _assert_tree_links_stable(classic, _staging_error)
    _assert_tree_links_stable(semantic, _staging_error)


def _verify_destination(
    tree: _PinnedTree,
    manifest: FixtureManifest,
) -> dict[str, tuple[int, int, int]]:
    actual_files, actual_directories = _capture_tree(tree, _staging_error)
    _compare_census(
        actual_files,
        actual_directories,
        manifest,
        tree.label,
        _staging_error,
    )
    if set(actual_directories) != set(tree.directories) - {""}:
        _staging_error(f"{tree.label} contains an unpinned directory")
    _assert_tree_stable(tree, _staging_error)
    identities = _verify_hashes(tree, manifest, tree.label, _staging_error)
    _assert_tree_stable(tree, _staging_error)
    return identities


def _prove_independent_files(
    source: dict[str, tuple[int, int, int]],
    classic: dict[str, tuple[int, int, int]],
    semantic: dict[str, tuple[int, int, int]],
    fail: Failure = _staging_error,
) -> None:
    seen_destinations: set[tuple[int, int]] = set()
    for relative in sorted(source, key=_canonical_path_key):
        identities = (
            source[relative][:2],
            classic[relative][:2],
            semantic[relative][:2],
        )
        if len(set(identities)) != 3:
            fail(f"staged files must not be hard links: {relative}")
        for identity, link_count in (
            (classic[relative][:2], classic[relative][2]),
            (semantic[relative][:2], semantic[relative][2]),
        ):
            if link_count != 1:
                fail(f"staged file has an unexpected hard link: {relative}")
            if identity in seen_destinations:
                fail(f"staged files alias one another: {relative}")
            seen_destinations.add(identity)


def _prove_independent_roots(
    source: _PinnedTree,
    classic: _PinnedTree,
    semantic: _PinnedTree,
    fail: Failure = _staging_error,
) -> None:
    identities = {
        source.root.identity,
        classic.root.identity,
        semantic.root.identity,
    }
    if len(identities) != 3:
        fail("source, Classic, and semantic roots must not alias")


class FixtureSetLease:
    """Hold one verified source and two independent staged inputs stable."""

    def __init__(
        self,
        manifest: _PinnedManifest,
        source: _PinnedTree,
        classic: _PinnedTree,
        semantic: _PinnedTree,
        evidence: FixtureSetEvidence,
    ) -> None:
        self._manifest = manifest
        self._source = source
        self._classic = classic
        self._semantic = semantic
        self._evidence = evidence
        self._closed = False
        self._finalization_error: BaseException | None = None

    @property
    def evidence(self) -> FixtureSetEvidence:
        return self._evidence

    def verify_runner_input_identities(
        self,
        classic_identity: tuple[int, int],
        semantic_identity: tuple[int, int],
    ) -> None:
        """Bind runner input-directory pins to the continuously held trees."""

        if self._closed:
            _verification_error("fixture set lease is already closed")
        _assert_tree_links_stable(self._classic, _verification_error)
        _assert_tree_links_stable(self._semantic, _verification_error)
        expected_classic = self._classic.root.identity[:2]
        expected_semantic = self._semantic.root.identity[:2]
        if classic_identity != expected_classic:
            _verification_error(
                "runner Classic input identity does not match the leased fixture"
            )
        if semantic_identity != expected_semantic:
            _verification_error(
                "runner semantic input identity does not match the leased fixture"
            )

    def _close_descriptors(self) -> None:
        for tree in (self._semantic, self._classic, self._source):
            tree.close()
        self._manifest.close()

    def finalize(self) -> FixtureSetEvidence:
        """Reverify every pinned object, then close descriptors without deletion."""

        if self._finalization_error is not None:
            raise self._finalization_error
        if self._closed:
            return self._evidence

        failures: list[str] = []
        identities: dict[str, dict[str, tuple[int, int, int]]] = {}

        def record_failure(label: str, operation: Callable[[], object]) -> None:
            try:
                value = operation()
                if isinstance(value, dict):
                    identities[label] = value
            except FixtureError as error:
                failures.append(f"{label}: {error.message}")

        try:
            record_failure(
                "manifest",
                lambda: _verify_pinned_manifest(
                    self._manifest,
                    _verification_error,
                ),
            )
            for label, tree in (
                ("source", self._source),
                ("classic_input", self._classic),
                ("semantic_input", self._semantic),
            ):
                record_failure(
                    label,
                    lambda tree=tree: _verify_tree_again(
                        tree,
                        self._manifest.manifest,
                        f"{tree.label} at fixture lease finalization",
                        _verification_error,
                    ),
                )

            record_failure(
                "root_independence",
                lambda: _prove_independent_roots(
                    self._source,
                    self._classic,
                    self._semantic,
                    _verification_error,
                ),
            )
            if all(
                label in identities
                for label in ("source", "classic_input", "semantic_input")
            ):
                record_failure(
                    "file_independence",
                    lambda: _prove_independent_files(
                        identities["source"],
                        identities["classic_input"],
                        identities["semantic_input"],
                        _verification_error,
                    ),
                )

            if failures:
                _verification_error(
                    "fixture set changed while leased: " + "; ".join(failures)
                )
            return self._evidence
        except BaseException as error:
            self._finalization_error = error
            raise
        finally:
            self._closed = True
            self._close_descriptors()

    def close(self) -> FixtureSetEvidence:
        return self.finalize()

    def __enter__(self) -> FixtureSetLease:
        if self._closed:
            raise RuntimeError("fixture set lease is already closed")
        return self

    def __exit__(
        self,
        exception_type: object,
        exception: object,
        traceback: object,
    ) -> bool:
        del exception_type, exception, traceback
        self.finalize()
        return False


def acquire_fixture_set_lease(
    manifest_path: Path | str,
    source_root: Path | str,
    classic_root: Path | str,
    semantic_root: Path | str,
) -> FixtureSetLease:
    """Verify and continuously pin one source plus two staged input copies."""

    pinned_manifest = _open_pinned_manifest(manifest_path)
    trees: list[_PinnedTree] = []
    try:
        for path, label in (
            (source_root, "source root"),
            (classic_root, "Classic staged input root"),
            (semantic_root, "semantic staged input root"),
        ):
            trees.append(_open_existing_tree(path, label, _verification_error))
        source, classic, semantic = trees
        _check_distinct_roots(
            source.path,
            classic.path,
            semantic.path,
            _verification_error,
        )
        _prove_independent_roots(
            source,
            classic,
            semantic,
            _verification_error,
        )
        _verify_pinned_manifest(pinned_manifest, _verification_error)
        source_identities = _verify_tree_initial(
            source,
            pinned_manifest.manifest,
            _verification_error,
        )
        classic_identities = _verify_tree_initial(
            classic,
            pinned_manifest.manifest,
            _verification_error,
        )
        semantic_identities = _verify_tree_initial(
            semantic,
            pinned_manifest.manifest,
            _verification_error,
        )
        _prove_independent_files(
            source_identities,
            classic_identities,
            semantic_identities,
            _verification_error,
        )
        _verify_pinned_manifest(pinned_manifest, _verification_error)

        manifest = pinned_manifest.manifest
        evidence = FixtureSetEvidence(
            manifest_sha256=pinned_manifest.sha256,
            tree_sha256=manifest.tree_sha256,
            slot=manifest.slot,
            file_count=len(manifest.files),
            total_file_bytes=sum(record.size for record in manifest.files),
        )
        return FixtureSetLease(
            pinned_manifest,
            source,
            classic,
            semantic,
            evidence,
        )
    except BaseException:
        for tree in reversed(trees):
            tree.close()
        pinned_manifest.close()
        raise


def _path_chain_is_stable_nonthrowing(chain: _PathChain) -> bool:
    for node in chain.nodes:
        try:
            opened = os.fstat(node.descriptor)
            if _stat_identity(opened) != node.identity:
                return False
            if node.parent_descriptor is not None and node.name is not None:
                linked = os.stat(
                    node.name,
                    dir_fd=node.parent_descriptor,
                    follow_symlinks=False,
                )
                if _stat_identity(linked) != node.identity:
                    return False
        except BaseException:
            return False
    return True


def _reconcile_tree_namespace(tree: _PinnedTree) -> tuple[str, ...]:
    matching: list[str] = []
    if not _path_chain_is_stable_nonthrowing(tree.parent_chain):
        tree.tainted = True
    try:
        if _stat_identity(os.fstat(tree.root.descriptor)) != tree.root.identity:
            tree.tainted = True
    except BaseException:
        tree.tainted = True
    for name in dict.fromkeys((tree.private_name, tree.path.name)):
        try:
            linked = os.stat(
                name,
                dir_fd=tree.root.link_parent_descriptor,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            continue
        except BaseException:
            tree.tainted = True
            continue
        if _stat_identity(linked) == tree.root.identity:
            matching.append(name)
    if len(matching) == 1:
        tree.root.link_name = matching[0]
        tree.published = matching[0] == tree.path.name
    else:
        tree.tainted = True
    return tuple(matching)


def _publish_destination(tree: _PinnedTree) -> None:
    _assert_tree_stable(tree, _staging_error)
    final_name = tree.path.name
    # Both candidate names live on the tree before the syscall; cleanup can
    # reconcile either one even if control never reaches the next assignment.
    tree.publish_attempted = True
    rename_returned = False
    try:
        try:
            _native_rename_noreplace(
                tree.root.link_parent_descriptor,
                tree.private_name,
                tree.root.link_parent_descriptor,
                final_name,
            )
            rename_returned = True
        finally:
            # Reconcile both candidate names even when an asynchronous
            # exception arrives after the native rename has already committed.
            matching = _reconcile_tree_namespace(tree)
    except OSError as error:
        _staging_error(
            f"cannot atomically publish {tree.label} {tree.path}: "
            f"{error.strerror or error}"
        )

    if rename_returned and matching != (final_name,):
        tree.root.link_name = final_name
        tree.published = True
        tree.tainted = True
        _staging_error(
            f"cannot verify published {tree.label} namespace after rename: "
            f"{tree.path}"
        )
    # Until all post-rename checks pass, the final path is only a last-known
    # name: another writer could already have moved or replaced the directory.
    tree.tainted = True
    try:
        published = os.stat(
            final_name,
            dir_fd=tree.root.link_parent_descriptor,
            follow_symlinks=False,
        )
    except OSError as error:
        _staging_error(
            f"cannot verify published {tree.label} {tree.path}: "
            f"{error.strerror or error}"
        )
    if _stat_identity(published) != tree.root.identity:
        _staging_error(f"published {tree.label} identity changed: {tree.path}")
    _assert_tree_stable(tree, _staging_error)
    tree.tainted = False


def _cleanup_created_tree(tree: _PinnedTree) -> str | None:
    matching = _reconcile_tree_namespace(tree)
    if tree.published:
        state = "published"
    elif tree.publish_attempted and not matching:
        state = "publication-attempted"
    else:
        state = "private staging"
    if tree.tainted or len(matching) != 1:
        private_path = tree.parent_chain.display_path / tree.private_name
        final_path = tree.parent_chain.display_path / tree.path.name
        return (
            f"retained namespace-tainted {state} root allocation; last-known "
            f"names (each may now be missing or refer to a replacement): "
            f"private={private_path}, final={final_path}; no deletion attempted"
        )
    retained = tree.parent_chain.display_path / matching[0]
    return (
        f"retained {state} root allocation; last-observed matching name: "
        f"{retained}; no deletion attempted"
    )


def _cleanup_created_trees(trees: Sequence[_PinnedTree]) -> str | None:
    failures = [
        failure
        for tree in reversed(trees)
        if (failure := _cleanup_created_tree(tree)) is not None
    ]
    return "; ".join(failures) if failures else None


def _cleanup_pending_plans(
    plans: Sequence[_DestinationPlan],
    owned_trees: Sequence[_PinnedTree],
) -> str | None:
    reports: list[str] = []
    for plan in plans:
        if plan.private_name is None or plan.retention_reported:
            continue
        if any(
            tree.label == plan.label and tree.private_name == plan.private_name
            for tree in owned_trees
        ):
            continue
        status = "uninspectable"
        if plan.parent_chain is not None:
            try:
                linked = os.stat(
                    plan.private_name,
                    dir_fd=plan.parent_chain.descriptor,
                    follow_symlinks=False,
                )
            except FileNotFoundError:
                status = "missing"
            except BaseException:
                pass
            else:
                identity = _stat_identity(linked)
                if plan.root_identity is None:
                    status = "present-unattributed"
                elif identity == plan.root_identity:
                    status = "matching-pinned-identity"
                else:
                    status = "replacement"
        attempted = plan.spec.parent_path / plan.private_name
        reports.append(
            f"retained namespace-uncertain private staging root allocation if "
            f"created; last-observed attempted name status={status} (it may now "
            f"be missing or refer to a replacement): {attempted}; no deletion "
            f"attempted"
        )
    return "; ".join(reports) if reports else None


def stage_fixture(
    manifest_path: Path | str,
    source_root: Path | str,
    classic_root: Path | str,
    semantic_root: Path | str,
) -> dict[str, object]:
    """Verify and descriptor-copy one source fixture into independent twin roots."""

    manifest = load_manifest(manifest_path)
    source = _open_existing_tree(source_root, "source root", _verification_error)
    plans: list[_DestinationPlan] = []
    created: list[_PinnedTree] = []
    completed = False
    result: dict[str, object] | None = None
    try:
        classic_spec = _path_spec(classic_root, "Classic root", _staging_error)
        semantic_spec = _path_spec(semantic_root, "semantic root", _staging_error)
        _check_distinct_roots(
            source.path,
            classic_spec.display_path,
            semantic_spec.display_path,
        )
        _verify_source_initial(source, manifest)
        plans.append(_open_destination_plan(classic_spec, "Classic root"))
        plans.append(_open_destination_plan(semantic_spec, "semantic root"))
        _check_descriptor_aliases(source, plans)
        classic = _create_destination_tree(plans[0], created)
        semantic = _create_destination_tree(plans[1], created)
        _prove_independent_roots(source, classic, semantic)
        _create_fixture_directories(classic, manifest.files)
        _create_fixture_directories(semantic, manifest.files)
        for record in manifest.files:
            _copy_record_to_trees(source, classic, semantic, record)
        classic_identities = _verify_destination(classic, manifest)
        semantic_identities = _verify_destination(semantic, manifest)
        source_identities = _verify_hashes(
            source,
            manifest,
            "source root after staging",
            _verification_error,
        )
        _prove_independent_files(
            source_identities,
            classic_identities,
            semantic_identities,
        )
        _verify_source_again(source, manifest)
        _publish_destination(classic)
        _publish_destination(semantic)
        completed = True
        result = {
            "status": "staged",
            "semantic_equivalence": "not_evaluated",
            "manifest": str(_manifest_display_path(manifest_path)),
            "source_root": str(source.path),
            "classic_root": str(classic.path),
            "semantic_root": str(semantic.path),
            "source_unchanged": True,
            "slot": manifest.slot,
            "file_count": len(manifest.files),
            "tree_sha256": manifest.tree_sha256,
        }
    except BaseException as error:
        retention_reports = tuple(
            report
            for report in (
                _cleanup_created_trees(created),
                _cleanup_pending_plans(plans, created),
            )
            if report
        )
        retained_roots = "; ".join(retention_reports) or None
        if isinstance(error, FixtureError):
            if retained_roots:
                raise FixtureError(
                    error.code,
                    f"{error.message}; {retained_roots}",
                    error.exit_code,
                ) from error
            raise
        detail = f"unexpected staging failure: {type(error).__name__}"
        if retained_roots:
            detail += f"; {retained_roots}"
        raise FixtureError("fixture.staging_failed", detail, EXIT_STAGING) from error
    finally:
        for tree in reversed(created):
            tree.close()
        for plan in plans:
            plan.close()
        source.close()

    if not completed or result is None:
        _staging_error("internal error: staging did not produce a result")
    return result


def verification_result(verified: VerifiedFixture) -> dict[str, object]:
    return {
        "status": "verified",
        "semantic_equivalence": "not_evaluated",
        "manifest": str(verified.manifest_path),
        "source_root": str(verified.source_root),
        "slot": verified.manifest.slot,
        "file_count": len(verified.manifest.files),
        "tree_sha256": verified.manifest.tree_sha256,
    }


def _emit_json(value: object, stream: Any = sys.stdout) -> None:
    stream.write(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    stream.write("\n")


class _JsonArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> NoReturn:
        _emit_json(
            {"error": {"code": "usage.invalid", "message": message}, "status": "error"},
            sys.stderr,
        )
        raise SystemExit(EXIT_USAGE)


def build_parser() -> argparse.ArgumentParser:
    parser = _JsonArgumentParser(
        description=(
            "Verify a provenance-declared Realmz replay fixture or stage it into "
            "independent Classic and semantic roots. No save path is implied."
        ),
        epilog=(
            "Manifest v1 requires source_class, authorization_basis, "
            "redistribution_allowed, slot A-J, a canonical files census, and its "
            "tree_sha256. Verification establishes byte identity only; it does "
            "not establish semantic equivalence. Destination parents must be "
            "current-user-owned and not group- or world-writable. Failed "
            "staging roots are retained, may contain fixture bytes, and are "
            "reported for deliberate cleanup by the caller."
        ),
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    verify_parser = subparsers.add_parser(
        "verify", help="verify one explicitly supplied source root"
    )
    verify_parser.add_argument(
        "--manifest", required=True, type=Path, help="path to a v1 manifest JSON file"
    )
    verify_parser.add_argument(
        "--source-root",
        required=True,
        type=Path,
        help="existing fixture directory to verify (there is no default)",
    )

    stage_parser = subparsers.add_parser(
        "stage", help="verify and copy into two new, independent roots"
    )
    stage_parser.add_argument(
        "--manifest", required=True, type=Path, help="path to a v1 manifest JSON file"
    )
    stage_parser.add_argument(
        "--source-root",
        required=True,
        type=Path,
        help="existing fixture directory to verify and copy (there is no default)",
    )
    stage_parser.add_argument(
        "--classic-root",
        required=True,
        type=Path,
        help="new, nonexistent destination for the Classic run",
    )
    stage_parser.add_argument(
        "--semantic-root",
        required=True,
        type=Path,
        help="new, nonexistent destination for the semantic run",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "verify":
            result = verification_result(verify_fixture(args.manifest, args.source_root))
        else:
            result = stage_fixture(
                args.manifest,
                args.source_root,
                args.classic_root,
                args.semantic_root,
            )
    except FixtureError as error:
        _emit_json(
            {"error": {"code": error.code, "message": error.message}, "status": "error"},
            sys.stderr,
        )
        return error.exit_code
    except Exception as error:
        exit_code = EXIT_VERIFICATION if args.command == "verify" else EXIT_STAGING
        _emit_json(
            {
                "error": {
                    "code": "fixture.unexpected_error",
                    "message": f"unexpected {type(error).__name__}",
                },
                "status": "error",
            },
            sys.stderr,
        )
        return exit_code
    _emit_json(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
