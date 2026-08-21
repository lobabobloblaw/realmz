#!/usr/bin/env python3
"""Synthetic-only tests for the semantic replay fixture verifier/stager."""

from __future__ import annotations

import copy
import errno
import gc
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts/semantic_replay_fixture.py"
SCHEMA_PATH = Path(__file__).with_name("replay-fixture-manifest.schema.json")
CENSUS_SCHEMA_PATH = Path(__file__).with_name("replay-fixture-census.schema.json")

SPEC = importlib.util.spec_from_file_location("semantic_replay_fixture", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:  # pragma: no cover - import machinery guard
    raise RuntimeError(f"cannot import {SCRIPT_PATH}")
fixture_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = fixture_tool
SPEC.loader.exec_module(fixture_tool)


def digest_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def make_manifest(
    files: dict[str, bytes],
    *,
    canonical_order: bool = True,
    source_class: str = "user-owned-save",
    authorization_basis: str = "Owner supplied these bytes for local replay testing.",
    redistribution_allowed: bool = False,
    slot: str = "A",
) -> dict[str, object]:
    records = [
        {"path": path, "size": len(data), "sha256": digest_bytes(data)}
        for path, data in files.items()
    ]
    if canonical_order:
        records.sort(key=lambda record: str(record["path"]).encode("utf-8"))
    typed_records = tuple(
        fixture_tool.FileRecord(
            path=str(record["path"]),
            size=int(record["size"]),
            sha256=str(record["sha256"]),
        )
        for record in records
    )
    return {
        "manifest_version": 1,
        "source_class": source_class,
        "authorization_basis": authorization_basis,
        "redistribution_allowed": redistribution_allowed,
        "slot": slot,
        "files": records,
        "tree_sha256": fixture_tool.compute_tree_sha256(typed_records),
    }


class ReplayFixtureTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.files = {
            "Data I1": b"synthetic-main-state\x00\x01",
            "Journal/State": b"synthetic-journal-state\n",
            "empty.bin": b"",
        }
        self.write_source(self.files)
        self.manifest = self.root / "fixture.json"
        self.manifest_value = make_manifest(self.files)
        self.write_manifest(self.manifest_value)

    def write_source(self, files: dict[str, bytes]) -> None:
        for relative, data in files.items():
            destination = self.source / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)

    def write_manifest(self, value: object) -> None:
        self.manifest.write_text(
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def assert_manifest_error(self, value: object, expected: str) -> None:
        self.write_manifest(value)
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(self.manifest)
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_MANIFEST)
        self.assertIn(expected, raised.exception.message)

    def run_cli(self, *arguments: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT_PATH), *(str(argument) for argument in arguments)],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
        )

    def assert_descriptor_closed(self, descriptor: int) -> None:
        try:
            os.fstat(descriptor)
        except OSError as error:
            self.assertEqual(error.errno, errno.EBADF)
            return
        os.close(descriptor)
        self.fail(f"descriptor {descriptor} remained open")


class ManifestValidationTests(ReplayFixtureTestCase):
    def test_valid_manifest_and_source_verify(self) -> None:
        verified = fixture_tool.verify_fixture(self.manifest, self.source)
        self.assertEqual(
            verified.manifest_sha256,
            digest_bytes(self.manifest.read_bytes()),
        )
        self.assertEqual(verified.manifest.manifest_version, 1)
        self.assertEqual(verified.manifest.source_class, "user-owned-save")
        self.assertEqual(
            verified.manifest.authorization_basis,
            "Owner supplied these bytes for local replay testing.",
        )
        self.assertIs(verified.manifest.redistribution_allowed, False)
        self.assertEqual(verified.manifest.slot, "A")
        self.assertEqual(len(verified.manifest.files), 3)

    def test_tree_hash_has_a_domain_separator_and_binary_records(self) -> None:
        one = fixture_tool.FileRecord("a", 2, digest_bytes(b"bc"))
        two = fixture_tool.FileRecord("ab", 1, digest_bytes(b"c"))
        self.assertNotEqual(
            fixture_tool.compute_tree_sha256((one,)),
            fixture_tool.compute_tree_sha256((two,)),
        )
        digest = hashlib.sha256()
        digest.update(b"realmz-semantic-replay-fixture-tree-v1\n")
        digest.update((1).to_bytes(8, "big"))
        digest.update(b"a")
        digest.update((2).to_bytes(8, "big"))
        digest.update(bytes.fromhex(one.sha256))
        self.assertEqual(fixture_tool.compute_tree_sha256((one,)), digest.hexdigest())

    def test_schema_pins_v1_fields_and_refuses_extensions(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertIs(schema["additionalProperties"], False)
        self.assertEqual(set(schema["required"]), fixture_tool.TOP_LEVEL_FIELDS)
        self.assertEqual(schema["properties"]["manifest_version"]["const"], 1)
        file_schema = schema["properties"]["files"]["items"]
        self.assertIs(file_schema["additionalProperties"], False)
        self.assertEqual(set(file_schema["required"]), fixture_tool.FILE_FIELDS)
        self.assertEqual(
            schema["properties"]["slot"]["pattern"],
            r"^[A-J](?![\s\S])",
        )

    def test_unknown_and_missing_fields_are_rejected_at_every_level(self) -> None:
        top_unknown = copy.deepcopy(self.manifest_value)
        top_unknown["comment"] = "not part of v1"
        self.assert_manifest_error(top_unknown, "unknown field: comment")

        top_missing = copy.deepcopy(self.manifest_value)
        del top_missing["authorization_basis"]
        self.assert_manifest_error(top_missing, "missing field: authorization_basis")

        file_unknown = copy.deepcopy(self.manifest_value)
        file_unknown["files"][0]["mode"] = 0o600
        self.assert_manifest_error(file_unknown, "unknown field: mode")

        file_missing = copy.deepcopy(self.manifest_value)
        del file_missing["files"][0]["size"]
        self.assert_manifest_error(file_missing, "missing field: size")

    def test_duplicate_json_keys_are_rejected(self) -> None:
        self.manifest.write_text(
            '{"manifest_version":1,"manifest_version":1}', encoding="utf-8"
        )
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(self.manifest)
        self.assertIn("duplicate JSON key: manifest_version", raised.exception.message)

    def test_manifest_must_be_utf8_regular_file_not_a_symlink(self) -> None:
        invalid_utf8 = self.root / "invalid.json"
        invalid_utf8.write_bytes(b"\xff")
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(invalid_utf8)
        self.assertIn("not valid UTF-8", raised.exception.message)

        directory = self.root / "manifest-directory"
        directory.mkdir()
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(directory)
        self.assertIn("regular file", raised.exception.message)

        link = self.root / "manifest-link.json"
        try:
            link.symlink_to(self.manifest)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(link)
        self.assertIn("symbolic link", raised.exception.message)

    def test_provenance_and_slot_are_explicit_and_strictly_typed(self) -> None:
        invalid_values = (
            ("manifest_version", True, "manifest_version"),
            ("manifest_version", 2, "manifest_version"),
            ("source_class", "", "source_class length"),
            ("source_class", "User_Save", "lowercase ASCII identifier"),
            ("source_class", 4, "must be a string"),
            ("authorization_basis", "", "length is invalid"),
            ("authorization_basis", " padded ", "leading or trailing"),
            ("authorization_basis", "line\nbreak", "control character"),
            ("authorization_basis", "next\u0085line", "control character"),
            ("redistribution_allowed", 0, "must be a boolean"),
            ("slot", "a", "slot must be"),
            ("slot", "K", "slot must be"),
            ("slot", 1, "slot must be"),
        )
        for field, replacement, expected in invalid_values:
            with self.subTest(field=field, replacement=replacement):
                value = copy.deepcopy(self.manifest_value)
                value[field] = replacement
                self.assert_manifest_error(value, expected)

    def test_empty_or_oversized_file_census_is_rejected(self) -> None:
        empty = copy.deepcopy(self.manifest_value)
        empty["files"] = []
        self.assert_manifest_error(empty, "non-empty array")

        with mock.patch.object(fixture_tool, "MAX_FILES", 2):
            self.assert_manifest_error(self.manifest_value, "exceeds the limit")

    def test_manifest_directory_expansion_is_bounded(self) -> None:
        value = make_manifest(
            {
                "shared/one/value": b"one",
                "shared/two/value": b"two",
            }
        )
        with mock.patch.object(fixture_tool, "MAX_DIRECTORIES", 2):
            self.assert_manifest_error(value, "requires more than 2 directories")

    def test_manifest_aggregate_declared_bytes_are_bounded(self) -> None:
        value = copy.deepcopy(self.manifest_value)
        value["files"][0]["size"] = 11
        value["files"][1]["size"] = 10
        records = tuple(
            fixture_tool.FileRecord(item["path"], item["size"], item["sha256"])
            for item in value["files"]
        )
        value["tree_sha256"] = fixture_tool.compute_tree_sha256(records)
        with mock.patch.object(fixture_tool, "MAX_TOTAL_FILE_BYTES", 20):
            self.assert_manifest_error(value, "more than 20 total bytes")

    def test_unsafe_and_noncanonical_paths_are_rejected(self) -> None:
        invalid_paths = (
            "",
            "/absolute",
            "../escape",
            "dir/../escape",
            ".",
            "dir/./file",
            "dir//file",
            "dir/",
            "dir\\file",
            "C:/windows",
            "\\\\server\\share",
            "tab\tfile",
            "line\nfile",
            "nul\x00file",
            " leading/file",
            "trailing /file",
            "Cafe\u0301/file",
            "a" * 4097,
        )
        for invalid_path in invalid_paths:
            with self.subTest(path=repr(invalid_path)):
                value = copy.deepcopy(self.manifest_value)
                value["files"][0]["path"] = invalid_path
                self.assert_manifest_error(value, "path")

    def test_duplicate_paths_and_noncanonical_order_are_rejected(self) -> None:
        duplicate = copy.deepcopy(self.manifest_value)
        duplicate["files"][1]["path"] = duplicate["files"][0]["path"]
        self.assert_manifest_error(duplicate, "duplicate file path")

        reversed_files = copy.deepcopy(self.manifest_value)
        reversed_files["files"].reverse()
        typed = tuple(
            fixture_tool.FileRecord(item["path"], item["size"], item["sha256"])
            for item in reversed_files["files"]
        )
        reversed_files["tree_sha256"] = fixture_tool.compute_tree_sha256(typed)
        self.assert_manifest_error(reversed_files, "canonical UTF-8 byte order")

        prefix_collision = make_manifest({"node": b"file", "node/child": b"child"})
        self.assert_manifest_error(prefix_collision, "also required as a directory")

    def test_sizes_and_hashes_require_canonical_exact_values(self) -> None:
        cases = (
            ("size", True, "size must be an integer"),
            ("size", -1, "size must be an integer"),
            ("size", 1 << 63, "size must be an integer"),
            ("sha256", "0" * 63, "64 lowercase"),
            ("sha256", "A" * 64, "64 lowercase"),
            ("sha256", 0, "64 lowercase"),
        )
        for field, replacement, expected in cases:
            with self.subTest(field=field, replacement=replacement):
                value = copy.deepcopy(self.manifest_value)
                value["files"][0][field] = replacement
                self.assert_manifest_error(value, expected)

        bad_tree = copy.deepcopy(self.manifest_value)
        bad_tree["tree_sha256"] = "0" * 64
        self.assert_manifest_error(bad_tree, "tree_sha256 mismatch")


class SourceVerificationTests(ReplayFixtureTestCase):
    def assert_verification_error(self, expected: str) -> None:
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.verify_fixture(self.manifest, self.source)
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_VERIFICATION)
        self.assertIn(expected, raised.exception.message)

    def test_missing_and_extra_files_are_rejected(self) -> None:
        (self.source / "empty.bin").unlink()
        self.assert_verification_error("missing file: empty.bin")

        (self.source / "empty.bin").write_bytes(b"")
        (self.source / "extra.bin").write_bytes(b"synthetic-extra")
        self.assert_verification_error("extra file: extra.bin")

    def test_extra_empty_directories_are_rejected(self) -> None:
        (self.source / "unlisted-empty-directory").mkdir()
        self.assert_verification_error("extra directory: unlisted-empty-directory")

    def test_bad_size_and_bad_hash_are_rejected(self) -> None:
        target = self.source / "Data I1"
        target.write_bytes(b"short")
        self.assert_verification_error("size mismatch for Data I1")

        replacement = b"x" * len(self.files["Data I1"])
        target.write_bytes(replacement)
        self.assert_verification_error("SHA-256 mismatch for Data I1")

    def test_source_root_must_exist_and_must_not_be_a_symlink(self) -> None:
        missing = self.root / "missing"
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.verify_fixture(self.manifest, missing)
        self.assertIn("cannot open source root", raised.exception.message)

        link = self.root / "source-link"
        try:
            link.symlink_to(self.source, target_is_directory=True)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.verify_fixture(self.manifest, link)
        self.assertIn("not a symbolic link", raised.exception.message)

    def test_symlinks_in_the_fixture_are_rejected_without_following_them(self) -> None:
        target = self.source / "empty.bin"
        target.unlink()
        try:
            target.symlink_to(self.root / "outside")
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        self.assert_verification_error("symbolic links are forbidden")

    def test_symlinked_directories_are_rejected_without_traversal(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "secret").write_bytes(b"not fixture data")
        link = self.source / "linked-directory"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        self.assert_verification_error("symbolic links are forbidden")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation is unavailable")
    def test_special_files_are_rejected(self) -> None:
        fifo = self.source / "fifo"
        os.mkfifo(fifo)
        self.assert_verification_error("special files are forbidden")

    def test_filesystem_names_must_obey_manifest_path_rules(self) -> None:
        invalid = self.source / "edge "
        invalid.write_bytes(b"synthetic")
        self.assert_verification_error("components must not have edge whitespace")

    def test_raw_directory_census_is_bounded_before_entry_metadata(self) -> None:
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._verification_error,
        )
        try:
            with mock.patch.object(
                fixture_tool,
                "MAX_FILES",
                1,
            ), mock.patch.object(
                fixture_tool,
                "MAX_DIRECTORIES",
                1,
            ), mock.patch.object(
                fixture_tool,
                "_entry_state",
                side_effect=AssertionError("entry metadata must not be materialized"),
            ) as entry_state:
                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    fixture_tool._discover_tree(tree, fixture_tool._verification_error)
            self.assertIn("directory census exceeds 2 entries", raised.exception.message)
            entry_state.assert_not_called()
        finally:
            tree.close()

    def test_size_mismatch_is_rejected_before_hash_read(self) -> None:
        manifest = fixture_tool.load_manifest(self.manifest)
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._verification_error,
        )
        record = manifest.files[0]
        undersized_record = fixture_tool.FileRecord(
            path=record.path,
            size=record.size - 1,
            sha256=record.sha256,
        )
        try:
            with mock.patch.object(
                fixture_tool.os,
                "read",
                side_effect=AssertionError("a size-mismatched file must not be read"),
            ) as read_call:
                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    fixture_tool._hash_record(
                        tree,
                        undersized_record,
                        fixture_tool._verification_error,
                    )
            self.assertIn("fixture size mismatch", raised.exception.message)
            read_call.assert_not_called()
        finally:
            tree.close()

    def test_hashing_aborts_if_stream_exceeds_declared_size(self) -> None:
        manifest = fixture_tool.load_manifest(self.manifest)
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._verification_error,
        )
        record = manifest.files[0]
        requested_sizes: list[int] = []

        def oversized_read(descriptor: int, requested: int) -> bytes:
            del descriptor
            requested_sizes.append(requested)
            return b"x" * requested

        try:
            with mock.patch.object(
                fixture_tool.os,
                "read",
                side_effect=oversized_read,
            ):
                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    fixture_tool._hash_record(
                        tree,
                        record,
                        fixture_tool._verification_error,
                    )
            self.assertIn("exceeded its declared size", raised.exception.message)
            self.assertEqual(requested_sizes, [record.size + 1])
        finally:
            tree.close()


class CensusTests(ReplayFixtureTestCase):
    def assert_census_error(self, expected: str, *, slot: object = "A") -> None:
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.census_fixture(self.source, slot)
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_VERIFICATION)
        self.assertEqual(raised.exception.code, "fixture.census_failed")
        self.assertIn(expected, raised.exception.message)

    def test_api_emits_a_canonical_unreviewed_manifest_fragment(self) -> None:
        census = fixture_tool.census_fixture(self.source, "C")
        expected_paths = sorted(self.files, key=lambda value: value.encode("utf-8"))
        self.assertEqual(census.slot, "C")
        self.assertEqual([record.path for record in census.files], expected_paths)
        self.assertEqual(
            [(record.size, record.sha256) for record in census.files],
            [
                (len(self.files[path]), digest_bytes(self.files[path]))
                for path in expected_paths
            ],
        )
        self.assertEqual(
            census.tree_sha256,
            fixture_tool.compute_tree_sha256(census.files),
        )
        self.assertEqual(census.directory_count, 1)
        self.assertEqual(census.total_file_bytes, sum(map(len, self.files.values())))

        result = fixture_tool.census_result(census)
        self.assertEqual(
            set(result),
            {
                "status",
                "slot",
                "files",
                "tree_sha256",
                "file_count",
                "directory_count",
                "total_file_bytes",
            },
        )
        self.assertEqual(result["status"], "census_unreviewed")
        self.assertEqual(result["file_count"], 3)
        for forbidden in (
            "source_class",
            "authorization_basis",
            "redistribution_allowed",
            "semantic_equivalence",
            "equivalent",
            "source_root",
        ):
            self.assertNotIn(forbidden, result)

    def test_census_schema_is_closed_and_declares_structural_bounds(self) -> None:
        schema = json.loads(CENSUS_SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertIs(schema["additionalProperties"], False)
        expected_fields = {
            "status",
            "slot",
            "files",
            "tree_sha256",
            "file_count",
            "directory_count",
            "total_file_bytes",
        }
        self.assertEqual(set(schema["properties"]), expected_fields)
        self.assertEqual(set(schema["required"]), expected_fields)
        self.assertEqual(schema["properties"]["status"]["const"], "census_unreviewed")
        self.assertEqual(schema["properties"]["files"]["maxItems"], fixture_tool.MAX_FILES)
        self.assertEqual(
            schema["properties"]["directory_count"]["maximum"],
            fixture_tool.MAX_DIRECTORIES,
        )
        self.assertEqual(
            schema["properties"]["total_file_bytes"]["maximum"],
            fixture_tool.MAX_TOTAL_FILE_BYTES,
        )
        path_schema = schema["properties"]["files"]["items"]["properties"]["path"]
        self.assertEqual(
            path_schema["maxLength"], fixture_tool.MAX_RELATIVE_PATH_BYTES
        )
        self.assertIn("maxLength counts code points", path_schema["description"])
        self.assertIn("UTF-8 bytes", path_schema["description"])

    def test_paths_are_ordered_by_canonical_utf8_bytes(self) -> None:
        source = self.root / "unicode-source"
        source.mkdir()
        for name in ("é-state", "z-state", "A-state"):
            (source / name).write_bytes(name.encode("utf-8"))
        census = fixture_tool.census_fixture(source, "B")
        self.assertEqual(
            [record.path for record in census.files],
            sorted(
                ("é-state", "z-state", "A-state"),
                key=lambda value: value.encode("utf-8"),
            ),
        )

    def test_census_never_requests_writable_descriptors_or_content_writes(self) -> None:
        real_open = fixture_tool.os.open

        def require_read_only_open(
            path: object,
            flags: int,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> int:
            forbidden = os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND
            if flags & forbidden:
                raise AssertionError(f"census attempted writable open: {path}")
            if dir_fd is None:
                return real_open(path, flags, mode)
            return real_open(path, flags, mode, dir_fd=dir_fd)

        before = {
            relative: (self.source / relative).read_bytes()
            for relative in self.files
        }
        with mock.patch.object(
            fixture_tool.os,
            "open",
            side_effect=require_read_only_open,
        ), mock.patch.object(
            fixture_tool.os,
            "write",
            side_effect=AssertionError("census must not write"),
        ) as write_call:
            census = fixture_tool.census_fixture(self.source, "A")
        self.assertEqual(len(census.files), len(self.files))
        write_call.assert_not_called()
        self.assertEqual(
            {
                relative: (self.source / relative).read_bytes()
                for relative in self.files
            },
            before,
        )

    def test_api_requires_an_explicit_uppercase_slot(self) -> None:
        for invalid in ("", "a", "K", "AA", 1, None):
            with self.subTest(slot=invalid):
                self.assert_census_error("slot must be", slot=invalid)

    def test_root_and_nested_symlinks_are_rejected_without_following(self) -> None:
        root_link = self.root / "source-link"
        try:
            root_link.symlink_to(self.source, target_is_directory=True)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.census_fixture(root_link, "A")
        self.assertIn("not a symbolic link", raised.exception.message)

        target = self.source / "empty.bin"
        target.unlink()
        target.symlink_to(self.root / "outside")
        self.assert_census_error("symbolic links are forbidden")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation is unavailable")
    def test_special_files_are_rejected(self) -> None:
        os.mkfifo(self.source / "fifo")
        self.assert_census_error("special files are forbidden")

    def test_empty_and_unrepresentable_directory_trees_are_rejected(self) -> None:
        (self.source / "empty-directory").mkdir()
        self.assert_census_error("directory not represented by a regular file")

        empty_root = self.root / "empty-root"
        empty_root.mkdir()
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.census_fixture(empty_root, "A")
        self.assertIn("at least one regular file", raised.exception.message)

    def test_file_directory_and_byte_bounds_are_enforced(self) -> None:
        with mock.patch.object(fixture_tool, "MAX_FILES", 2):
            self.assert_census_error("more than 2 files")
        with mock.patch.object(fixture_tool, "MAX_DIRECTORIES", 0):
            self.assert_census_error("more than 0 directories")
        with mock.patch.object(
            fixture_tool,
            "MAX_FILE_SIZE",
            len(self.files["Data I1"]) - 1,
        ):
            self.assert_census_error("file size for Data I1 is outside")
        with mock.patch.object(
            fixture_tool,
            "MAX_TOTAL_FILE_BYTES",
            sum(map(len, self.files.values())) - 1,
        ):
            self.assert_census_error("total bytes")

    def test_mutation_after_hashing_is_detected(self) -> None:
        original_hash = fixture_tool._hash_record
        mutated = False

        def hash_then_mutate(
            tree: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
            fail: object,
        ) -> object:
            nonlocal mutated
            result = original_hash(tree, record, fail)
            if not mutated:
                target = self.source / record.path
                target.write_bytes(b"x" * record.size)
                mutated = True
            return result

        with mock.patch.object(
            fixture_tool,
            "_hash_record",
            side_effect=hash_then_mutate,
        ):
            self.assert_census_error("directory contents changed")
        self.assertTrue(mutated)

    def test_nested_directory_replacement_is_detected_without_hashing_replacement(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        malicious = b"outside bytes must never be hashed"
        (outside / "State").write_bytes(malicious)
        original_hash = fixture_tool._hash_record
        replaced = False

        def replace_before_nested_hash(
            tree: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
            fail: object,
        ) -> object:
            nonlocal replaced
            if not replaced and record.path == "Journal/State":
                (self.source / "Journal").rename(self.source / "Journal-original")
                (self.source / "Journal").symlink_to(outside, target_is_directory=True)
                replaced = True
            return original_hash(tree, record, fail)

        with mock.patch.object(
            fixture_tool,
            "_hash_record",
            side_effect=replace_before_nested_hash,
        ):
            self.assert_census_error("fixture directory link changed: Journal")
        self.assertTrue(replaced)
        self.assertEqual((outside / "State").read_bytes(), malicious)


class StagingTests(ReplayFixtureTestCase):
    def test_stage_creates_two_byte_exact_independent_roots(self) -> None:
        classic = self.root / "classic"
        semantic = self.root / "semantic"
        before = {path: (self.source / path).read_bytes() for path in self.files}

        result = fixture_tool.stage_fixture(
            self.manifest,
            self.source,
            classic,
            semantic,
            expected_manifest_sha256=digest_bytes(self.manifest.read_bytes()),
        )

        self.assertEqual(result["status"], "staged")
        self.assertEqual(result["semantic_equivalence"], "not_evaluated")
        self.assertEqual(
            result["manifest_sha256"],
            digest_bytes(self.manifest.read_bytes()),
        )
        self.assertNotIn("equivalent", result)
        self.assertIs(result["source_unchanged"], True)
        self.assertEqual(result["file_count"], 3)
        self.assertNotEqual(result["classic_root"], result["semantic_root"])
        self.assertEqual(list(self.root.glob(".realmz-stage-*")), [])
        for relative, expected in before.items():
            source_file = self.source / relative
            classic_file = classic / relative
            semantic_file = semantic / relative
            self.assertEqual(source_file.read_bytes(), expected)
            self.assertEqual(classic_file.read_bytes(), expected)
            self.assertEqual(semantic_file.read_bytes(), expected)
            identities = {
                (path.stat().st_dev, path.stat().st_ino)
                for path in (source_file, classic_file, semantic_file)
            }
            self.assertEqual(len(identities), 3)
            self.assertEqual(classic_file.stat().st_nlink, 1)
            self.assertEqual(semantic_file.stat().st_nlink, 1)

    def test_expected_manifest_digest_is_checked_before_source_or_destination_work(
        self,
    ) -> None:
        with mock.patch.object(
            fixture_tool,
            "_open_existing_tree",
            side_effect=AssertionError("source must not be traversed"),
        ) as open_source, mock.patch.object(
            fixture_tool,
            "_open_destination_plan",
            side_effect=AssertionError("destination must not be planned"),
        ) as open_destination, mock.patch.object(
            fixture_tool,
            "_create_destination_tree",
            side_effect=AssertionError("destination must not be allocated"),
        ) as create_destination:
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                    expected_manifest_sha256="0" * 64,
                )

        self.assertEqual(raised.exception.code, "fixture.digest_mismatch")
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_VERIFICATION)
        open_source.assert_not_called()
        open_destination.assert_not_called()
        create_destination.assert_not_called()
        self.assertFalse((self.root / "classic").exists())
        self.assertFalse((self.root / "semantic").exists())
        self.assertEqual(list(self.root.glob(".realmz-stage-*")), [])

    def test_malformed_expected_manifest_digest_precedes_staging_work(self) -> None:
        with mock.patch.object(
            fixture_tool,
            "_open_existing_tree",
            side_effect=AssertionError("source must not be traversed"),
        ) as open_source, mock.patch.object(
            fixture_tool,
            "_open_destination_plan",
            side_effect=AssertionError("destination must not be planned"),
        ) as open_destination, mock.patch.object(
            fixture_tool,
            "_create_destination_tree",
            side_effect=AssertionError("destination must not be allocated"),
        ) as create_destination:
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                    expected_manifest_sha256="not-a-digest",
                )

        self.assertEqual(raised.exception.code, "fixture.staging_failed")
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_STAGING)
        open_source.assert_not_called()
        open_destination.assert_not_called()
        create_destination.assert_not_called()

    def test_stage_refuses_overwrite_and_preserves_existing_content(self) -> None:
        classic = self.root / "classic"
        classic.mkdir()
        sentinel = classic / "sentinel"
        sentinel.write_bytes(b"keep me")
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.stage_fixture(
                self.manifest,
                self.source,
                classic,
                self.root / "semantic",
            )
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_STAGING)
        self.assertIn("already exists", raised.exception.message)
        self.assertEqual(sentinel.read_bytes(), b"keep me")

    def test_stage_refuses_aliases_and_nested_roots_before_creation(self) -> None:
        cases = (
            (self.root / "same", self.root / "same"),
            (self.root / "classic", self.root / "classic" / "semantic"),
            (self.source / "classic", self.root / "semantic"),
        )
        for classic, semantic in cases:
            with self.subTest(classic=classic, semantic=semantic):
                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    fixture_tool.stage_fixture(
                        self.manifest,
                        self.source,
                        classic,
                        semantic,
                    )
                self.assertIn("distinct, non-nested roots", raised.exception.message)
                self.assertFalse(classic.exists())
                self.assertFalse(semantic.exists())

    def test_stage_requires_existing_destination_parents(self) -> None:
        classic = self.root / "absent-parent" / "classic"
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.stage_fixture(
                self.manifest,
                self.source,
                classic,
                self.root / "semantic",
            )
        self.assertIn("cannot open descriptor path", raised.exception.message)
        self.assertFalse(classic.exists())

    def test_stage_rejects_a_writable_destination_parent(self) -> None:
        untrusted = self.root / "untrusted-parent"
        untrusted.mkdir()
        untrusted.chmod(0o777)
        try:
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    untrusted / "classic",
                    self.root / "semantic",
                )
            self.assertIn("must not be group- or world-writable", raised.exception.message)
            self.assertEqual(list(untrusted.glob(".realmz-stage-*")), [])
        finally:
            untrusted.chmod(0o700)

    def test_copy_size_mismatch_is_rejected_before_source_read(self) -> None:
        manifest = fixture_tool.load_manifest(self.manifest)
        source = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._verification_error,
        )
        plans: list[fixture_tool._DestinationPlan] = []
        trees: list[fixture_tool._PinnedTree] = []
        try:
            fixture_tool._verify_source_initial(source, manifest)
            for label, path in (
                ("Classic root", self.root / "classic"),
                ("semantic root", self.root / "semantic"),
            ):
                plan = fixture_tool._open_destination_plan(
                    fixture_tool._path_spec(path, label, fixture_tool._staging_error),
                    label,
                )
                plans.append(plan)
                trees.append(fixture_tool._create_destination_tree(plan))
            for tree in trees:
                fixture_tool._create_fixture_directories(tree, manifest.files)
            record = manifest.files[0]
            wrong_size = fixture_tool.FileRecord(
                path=record.path,
                size=record.size - 1,
                sha256=record.sha256,
            )
            with mock.patch.object(
                fixture_tool.os,
                "read",
                side_effect=AssertionError("a size-mismatched source must not be read"),
            ) as read_call:
                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    fixture_tool._copy_record_to_trees(
                        source,
                        trees[0],
                        trees[1],
                        wrong_size,
                    )
            self.assertIn("source size mismatch", raised.exception.message)
            read_call.assert_not_called()
        finally:
            for tree in reversed(trees):
                tree.close()
            for plan in plans:
                plan.close()
            source.close()

    def test_staging_failure_retains_private_roots_and_preserves_siblings(self) -> None:
        classic = self.root / "classic"
        semantic = self.root / "semantic"
        sibling = self.root / "unrelated"
        sibling.mkdir()
        sentinel = sibling / "sentinel"
        sentinel.write_bytes(b"preserve")
        original_copy = fixture_tool._copy_record_to_trees
        calls = 0

        def fail_after_one_copy(*arguments: object, **keywords: object) -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                raise fixture_tool.FixtureError(
                    "fixture.staging_failed",
                    "injected copy failure",
                    fixture_tool.EXIT_STAGING,
                )
            original_copy(*arguments, **keywords)

        with mock.patch.object(
            fixture_tool,
            "_copy_record_to_trees",
            side_effect=fail_after_one_copy,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
        self.assertIn("injected copy failure", raised.exception.message)
        self.assertIn("retained private staging root", raised.exception.message)
        self.assertFalse(classic.exists())
        self.assertFalse(semantic.exists())
        self.assertEqual(sentinel.read_bytes(), b"preserve")
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 2)

    def test_source_change_after_copy_retains_private_staging_roots(self) -> None:
        classic = self.root / "classic"
        semantic = self.root / "semantic"
        original_copy = fixture_tool._copy_record_to_trees
        calls = 0

        def mutate_after_final_copy(*arguments: object, **keywords: object) -> None:
            nonlocal calls
            original_copy(*arguments, **keywords)
            calls += 1
            if calls == len(self.files):
                (self.source / "Data I1").write_bytes(b"changed after staging")

        with mock.patch.object(
            fixture_tool,
            "_copy_record_to_trees",
            side_effect=mutate_after_final_copy,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
        self.assertIn("fixture size mismatch", raised.exception.message)
        self.assertIn("retained private staging root", raised.exception.message)
        self.assertFalse(classic.exists())
        self.assertFalse(semantic.exists())
        self.assertEqual((self.source / "Data I1").read_bytes(), b"changed after staging")
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 2)

    @unittest.skipUnless(hasattr(os, "link"), "hard links are unavailable")
    def test_hard_linked_stage_is_detected_and_retained_for_review(self) -> None:
        classic = self.root / "classic"
        semantic = self.root / "semantic"

        def hard_link_copy(
            source_root: fixture_tool._PinnedTree,
            classic_root: fixture_tool._PinnedTree,
            semantic_root: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
        ) -> None:
            source_parent, source_name = fixture_tool._record_parent(
                source_root,
                record,
                fixture_tool._staging_error,
            )
            for destination_root in (classic_root, semantic_root):
                destination_parent, destination_name = fixture_tool._record_parent(
                    destination_root,
                    record,
                    fixture_tool._staging_error,
                )
                os.link(
                    source_name,
                    destination_name,
                    src_dir_fd=source_parent,
                    dst_dir_fd=destination_parent,
                    follow_symlinks=False,
                )

        with mock.patch.object(
            fixture_tool,
            "_copy_record_to_trees",
            side_effect=hard_link_copy,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
        self.assertIn("must not be hard links", raised.exception.message)
        self.assertIn("retained private staging root", raised.exception.message)
        self.assertFalse(classic.exists())
        self.assertFalse(semantic.exists())


class FixtureSetLeaseTests(ReplayFixtureTestCase):
    def stage_inputs(self, suffix: str = "") -> tuple[Path, Path]:
        classic = self.root / f"classic{suffix}"
        semantic = self.root / f"semantic{suffix}"
        fixture_tool.stage_fixture(
            self.manifest,
            self.source,
            classic,
            semantic,
        )
        return classic, semantic

    def test_discarded_lease_best_effort_closes_all_descriptors(self) -> None:
        classic, semantic = self.stage_inputs("-discarded")
        real_open = fixture_tool.os.open
        opened_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            opened_descriptors.append(descriptor)
            return descriptor

        with mock.patch.object(
            fixture_tool.os,
            "open",
            side_effect=record_open,
        ):
            fixture_tool.acquire_fixture_set_lease(
                self.manifest,
                self.source,
                classic,
                semantic,
            )
        gc.collect()

        self.assertGreater(len(opened_descriptors), 3)
        for descriptor in opened_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_lease_exposes_only_bounded_non_content_evidence(self) -> None:
        classic, semantic = self.stage_inputs()
        manifest_bytes = self.manifest.read_bytes()
        lease = fixture_tool.acquire_fixture_set_lease(
            self.manifest,
            self.source,
            classic,
            semantic,
        )

        expected = {
            "manifest_sha256": digest_bytes(manifest_bytes),
            "tree_sha256": self.manifest_value["tree_sha256"],
            "slot": "A",
            "file_count": len(self.files),
            "total_file_bytes": sum(len(value) for value in self.files.values()),
        }
        self.assertEqual(lease.evidence.as_json(), expected)
        encoded = json.dumps(lease.evidence.as_json(), sort_keys=True)
        self.assertNotIn(str(self.source), encoded)
        self.assertNotIn(str(classic), encoded)
        self.assertNotIn(str(semantic), encoded)
        self.assertNotIn(
            str(self.manifest_value["authorization_basis"]),
            encoded,
        )

        with mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("lease close must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("lease close must not remove directories"),
        ):
            self.assertEqual(lease.close(), lease.evidence)
        self.assertEqual(lease._manifest.descriptor, -1)
        self.assertEqual(lease._source.root.descriptor, -1)
        self.assertEqual(lease._classic.root.descriptor, -1)
        self.assertEqual(lease._semantic.root.descriptor, -1)
        self.assertEqual(lease.finalize(), lease.evidence)
        self.assertTrue(self.source.is_dir())
        self.assertTrue(classic.is_dir())
        self.assertTrue(semantic.is_dir())

    def test_context_manager_holds_descriptors_and_finalizes_on_exit(self) -> None:
        classic, semantic = self.stage_inputs()
        lease = fixture_tool.acquire_fixture_set_lease(
            self.manifest,
            self.source,
            classic,
            semantic,
        )
        with lease as entered:
            self.assertIs(entered, lease)
            for descriptor in (
                lease._manifest.descriptor,
                lease._source.root.descriptor,
                lease._classic.root.descriptor,
                lease._semantic.root.descriptor,
            ):
                self.assertGreaterEqual(descriptor, 0)
                os.fstat(descriptor)
        self.assertEqual(lease._manifest.descriptor, -1)
        self.assertEqual(lease._source.root.descriptor, -1)
        self.assertEqual(lease._classic.root.descriptor, -1)
        self.assertEqual(lease._semantic.root.descriptor, -1)

    def test_runner_input_identity_binding_matches_the_held_staged_roots(self) -> None:
        classic, semantic = self.stage_inputs()
        lease = fixture_tool.acquire_fixture_set_lease(
            self.manifest,
            self.source,
            classic,
            semantic,
        )
        classic_status = classic.stat(follow_symlinks=False)
        semantic_status = semantic.stat(follow_symlinks=False)
        classic_identity = (classic_status.st_dev, classic_status.st_ino)
        semantic_identity = (semantic_status.st_dev, semantic_status.st_ino)

        lease.verify_runner_input_identities(classic_identity, semantic_identity)
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            lease.verify_runner_input_identities(
                (classic_identity[0], classic_identity[1] + 1),
                semantic_identity,
            )
        self.assertIn("Classic input identity", raised.exception.message)
        self.assertEqual(lease.finalize(), lease.evidence)

    def test_transient_in_place_mutation_of_each_tree_fails_finalization(self) -> None:
        for index, role in enumerate(("source", "classic_input", "semantic_input")):
            with self.subTest(role=role):
                classic, semantic = self.stage_inputs(f"-{index}")
                lease = fixture_tool.acquire_fixture_set_lease(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
                roots = {
                    "source": self.source,
                    "classic_input": classic,
                    "semantic_input": semantic,
                }
                target = roots[role] / "Data I1"
                original = target.read_bytes()
                target.write_bytes(b"x" * len(original))
                target.write_bytes(original)

                with self.assertRaises(fixture_tool.FixtureError) as raised:
                    lease.finalize()
                self.assertEqual(
                    raised.exception.exit_code,
                    fixture_tool.EXIT_VERIFICATION,
                )
                self.assertIn(role, raised.exception.message)
                self.assertEqual(target.read_bytes(), original)
                self.assertTrue(classic.is_dir())
                self.assertTrue(semantic.is_dir())

    def test_finalization_checks_all_three_trees_after_one_failure(self) -> None:
        classic, semantic = self.stage_inputs()
        lease = fixture_tool.acquire_fixture_set_lease(
            self.manifest,
            self.source,
            classic,
            semantic,
        )
        original = (self.source / "Data I1").read_bytes()
        (self.source / "Data I1").write_bytes(b"x" * len(original))
        (self.source / "Data I1").write_bytes(original)
        verify_again = fixture_tool._verify_tree_again
        checked: list[str] = []

        def observe(
            tree: fixture_tool._PinnedTree,
            manifest: fixture_tool.FixtureManifest,
            label: str,
            fail: fixture_tool.Failure,
        ) -> dict[str, tuple[int, int, int]]:
            checked.append(tree.label)
            return verify_again(tree, manifest, label, fail)

        with mock.patch.object(
            fixture_tool,
            "_verify_tree_again",
            side_effect=observe,
        ):
            with self.assertRaises(fixture_tool.FixtureError):
                lease.finalize()
        self.assertEqual(
            checked,
            [
                "source root",
                "Classic staged input root",
                "semantic staged input root",
            ],
        )

    def test_manifest_identity_is_pinned_without_exposing_its_text(self) -> None:
        classic, semantic = self.stage_inputs()
        lease = fixture_tool.acquire_fixture_set_lease(
            self.manifest,
            self.source,
            classic,
            semantic,
        )
        replacement = self.root / "replacement-manifest.json"
        replacement.write_bytes(self.manifest.read_bytes())
        os.replace(replacement, self.manifest)

        with self.assertRaises(fixture_tool.FixtureError) as raised:
            lease.finalize()
        self.assertIn("manifest", raised.exception.message)
        self.assertIn("fixture set changed while leased", raised.exception.message)
        self.assertIn("manifest changed while its descriptor was pinned", raised.exception.message)
        self.assertNotIn(
            str(self.manifest_value["authorization_basis"]),
            raised.exception.message,
        )

    @unittest.skipUnless(hasattr(os, "link"), "hard links are unavailable")
    def test_acquisition_rejects_non_independent_files_without_cleanup(self) -> None:
        classic, semantic = self.stage_inputs()
        classic_file = classic / "Data I1"
        classic_file.unlink()
        os.link(self.source / "Data I1", classic_file)

        with mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("failed lease acquisition must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("failed lease acquisition must not remove roots"),
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.acquire_fixture_set_lease(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
        self.assertEqual(raised.exception.exit_code, fixture_tool.EXIT_VERIFICATION)
        self.assertIn("must not be hard links", raised.exception.message)
        self.assertTrue(self.source.is_dir())
        self.assertTrue(classic.is_dir())
        self.assertTrue(semantic.is_dir())


class DescriptorRaceTests(ReplayFixtureTestCase):
    def test_interrupted_owner_close_cannot_close_a_reused_descriptor(self) -> None:
        descriptor = os.open(os.devnull, os.O_RDONLY)
        anchor = fixture_tool._DirectoryAnchor(
            relative="",
            descriptor=descriptor,
            identity=(0, 0, 0),
            link_parent_descriptor=-1,
            link_name="unused",
        )

        def close_then_interrupt(value: int) -> None:
            os.close(value)
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool,
            "_close_descriptor",
            side_effect=close_then_interrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                anchor.close()

        self.assertEqual(anchor.descriptor, -1)
        replacement = os.open(os.devnull, os.O_RDONLY)
        try:
            self.assertEqual(replacement, descriptor)
            del anchor
            gc.collect()
            os.fstat(replacement)
        finally:
            os.close(replacement)

    def test_discarded_manifest_tree_and_plan_owners_close_descriptors(self) -> None:
        operations = (
            ("manifest", lambda: fixture_tool._open_pinned_manifest(self.manifest)),
            (
                "tree",
                lambda: fixture_tool._open_existing_tree(
                    self.source,
                    "source root",
                    fixture_tool._census_error,
                ),
            ),
            (
                "plan",
                lambda: fixture_tool._open_destination_plan(
                    fixture_tool._path_spec(
                        self.root / "discarded-plan",
                        "Classic root",
                        fixture_tool._staging_error,
                    ),
                    "Classic root",
                ),
            ),
        )
        for name, operation in operations:
            with self.subTest(name=name):
                real_open = fixture_tool.os.open
                opened_descriptors: list[int] = []

                def record_open(*arguments: object, **keywords: object) -> int:
                    descriptor = real_open(*arguments, **keywords)
                    opened_descriptors.append(descriptor)
                    return descriptor

                with mock.patch.object(
                    fixture_tool.os,
                    "open",
                    side_effect=record_open,
                ):
                    operation()
                gc.collect()
                self.assertGreaterEqual(len(opened_descriptors), 1)
                for descriptor in opened_descriptors:
                    self.assert_descriptor_closed(descriptor)

    def test_path_node_registration_interrupt_closes_shared_descriptor_once(
        self,
    ) -> None:
        register = fixture_tool._register_path_node
        replacement = -1
        first_closed = -1

        def register_then_interrupt(
            nodes: list[fixture_tool._PathNode],
            node: fixture_tool._PathNode,
            descriptor: int,
        ) -> None:
            register(nodes, node, descriptor)
            raise KeyboardInterrupt

        def close_and_reuse(descriptor: int) -> None:
            nonlocal first_closed, replacement
            if descriptor < 0:
                return
            os.close(descriptor)
            if replacement < 0:
                first_closed = descriptor
                replacement = os.open(os.devnull, os.O_RDONLY)

        try:
            with mock.patch.object(
                fixture_tool,
                "_register_path_node",
                side_effect=register_then_interrupt,
            ), mock.patch.object(
                fixture_tool,
                "_close_descriptor",
                side_effect=close_and_reuse,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._open_directory_chain(
                        Path("/"),
                        "source root",
                        fixture_tool._census_error,
                    )
            self.assertEqual(replacement, first_closed)
            os.fstat(replacement)
        finally:
            fixture_tool._close_descriptor(replacement)

    def test_partial_path_chain_finalizer_cannot_close_reused_descriptor(
        self,
    ) -> None:
        path_chain_type = fixture_tool._PathChain
        partial_owners: list[fixture_tool._PathChain] = []
        replacement = -1
        first_closed = -1

        def retain_nodes_then_interrupt(
            *,
            display_path: Path,
            nodes: list[fixture_tool._PathNode],
        ) -> fixture_tool._PathChain:
            owner = object.__new__(path_chain_type)
            owner.display_path = display_path
            owner.nodes = nodes
            partial_owners.append(owner)
            raise KeyboardInterrupt

        def close_and_reuse(descriptor: int) -> None:
            nonlocal first_closed, replacement
            if descriptor < 0:
                return
            os.close(descriptor)
            if replacement < 0:
                first_closed = descriptor
                replacement = os.open(os.devnull, os.O_RDONLY)

        try:
            with mock.patch.object(
                fixture_tool,
                "_PathChain",
                side_effect=retain_nodes_then_interrupt,
            ), mock.patch.object(
                fixture_tool,
                "_close_descriptor",
                side_effect=close_and_reuse,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._open_directory_chain(
                        Path("/"),
                        "source root",
                        fixture_tool._census_error,
                    )
            self.assertEqual(replacement, first_closed)
            partial_owners.clear()
            gc.collect()
            os.fstat(replacement)
        finally:
            fixture_tool._close_descriptor(replacement)

    def test_destination_root_constructor_interrupt_closes_descriptor(self) -> None:
        spec = fixture_tool._path_spec(
            self.root / "constructor-interrupt-root",
            "Classic root",
            fixture_tool._staging_error,
        )
        plan = fixture_tool._open_destination_plan(spec, "Classic root")
        owned_trees: list[fixture_tool._PinnedTree] = []
        real_open = fixture_tool.os.open
        opened_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            opened_descriptors.append(descriptor)
            return descriptor

        try:
            with mock.patch.object(
                fixture_tool.os,
                "open",
                side_effect=record_open,
            ), mock.patch.object(
                fixture_tool,
                "_DirectoryAnchor",
                side_effect=KeyboardInterrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._create_destination_tree(plan, owned_trees)
            self.assertGreaterEqual(len(opened_descriptors), 1)
            for descriptor in opened_descriptors:
                self.assert_descriptor_closed(descriptor)
        finally:
            for tree in owned_trees:
                tree.close()
            plan.close()

    def test_destination_tree_append_interrupt_cannot_close_a_reused_descriptor(
        self,
    ) -> None:
        spec = fixture_tool._path_spec(
            self.root / "append-interrupt-root",
            "Classic root",
            fixture_tool._staging_error,
        )
        plan = fixture_tool._open_destination_plan(spec, "Classic root")

        class InterruptBeforeAppend(list[fixture_tool._PinnedTree]):
            def append(self, tree: fixture_tool._PinnedTree) -> None:
                del tree
                raise KeyboardInterrupt

        ownership = InterruptBeforeAppend()
        replacement = -1
        first_closed = -1

        def close_and_reuse(descriptor: int) -> None:
            nonlocal first_closed, replacement
            if descriptor < 0:
                return
            os.close(descriptor)
            if replacement < 0:
                first_closed = descriptor
                replacement = os.open(os.devnull, os.O_RDONLY)

        try:
            with mock.patch.object(
                fixture_tool,
                "_close_descriptor",
                side_effect=close_and_reuse,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._create_destination_tree(plan, ownership)
            self.assertEqual(ownership, [])
            self.assertEqual(replacement, first_closed)
            os.fstat(replacement)
        finally:
            fixture_tool._close_descriptor(replacement)
            plan.close()

    def test_nested_destination_constructor_interrupt_closes_descriptor(self) -> None:
        spec = fixture_tool._path_spec(
            self.root / "nested-constructor-interrupt-root",
            "Classic root",
            fixture_tool._staging_error,
        )
        plan = fixture_tool._open_destination_plan(spec, "Classic root")
        owned_trees: list[fixture_tool._PinnedTree] = []
        tree = fixture_tool._create_destination_tree(plan, owned_trees)
        manifest = fixture_tool.load_manifest(self.manifest)
        real_open = fixture_tool.os.open
        opened_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            opened_descriptors.append(descriptor)
            return descriptor

        try:
            with mock.patch.object(
                fixture_tool.os,
                "open",
                side_effect=record_open,
            ), mock.patch.object(
                fixture_tool,
                "_DirectoryAnchor",
                side_effect=KeyboardInterrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._create_fixture_directories(tree, manifest.files)
            self.assertGreaterEqual(len(opened_descriptors), 1)
            for descriptor in opened_descriptors:
                self.assert_descriptor_closed(descriptor)
        finally:
            for owned_tree in owned_trees:
                owned_tree.close()
            plan.close()

    def test_new_leaf_interrupt_after_registration_is_closed_by_owner(self) -> None:
        spec = fixture_tool._path_spec(
            self.root / "leaf-registration-root",
            "Classic root",
            fixture_tool._staging_error,
        )
        plan = fixture_tool._open_destination_plan(spec, "Classic root")
        owned_trees: list[fixture_tool._PinnedTree] = []
        tree = fixture_tool._create_destination_tree(plan, owned_trees)
        record = fixture_tool.FileRecord("leaf", 0, digest_bytes(b""))

        class InterruptAfterAppend(list[int]):
            def append(self, descriptor: int) -> None:
                super().append(descriptor)
                raise KeyboardInterrupt

        owned_descriptors = InterruptAfterAppend()
        try:
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_new_leaf(tree, record, owned_descriptors)
            self.assertEqual(len(owned_descriptors), 1)
            os.fstat(owned_descriptors[0])
        finally:
            for descriptor in owned_descriptors:
                fixture_tool._close_descriptor(descriptor)
            for descriptor in owned_descriptors:
                self.assert_descriptor_closed(descriptor)
            for owned_tree in owned_trees:
                owned_tree.close()
            plan.close()

    def test_directory_chain_constructor_interrupt_closes_all_descriptors(self) -> None:
        real_open = fixture_tool.os.open
        opened_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            opened_descriptors.append(descriptor)
            return descriptor

        with mock.patch.object(
            fixture_tool.os,
            "open",
            side_effect=record_open,
        ), mock.patch.object(
            fixture_tool,
            "_PathChain",
            side_effect=KeyboardInterrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_directory_chain(
                    Path("/"),
                    "source root",
                    fixture_tool._census_error,
                )

        self.assertGreaterEqual(len(opened_descriptors), 1)
        for descriptor in opened_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_directory_chain_interrupt_closes_every_acquired_descriptor(self) -> None:
        real_fstat = fixture_tool.os.fstat
        interrupted_descriptors: list[int] = []

        def fstat_then_interrupt(descriptor: int) -> os.stat_result:
            metadata = real_fstat(descriptor)
            interrupted_descriptors.append(descriptor)
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool.os,
            "fstat",
            side_effect=fstat_then_interrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_directory_chain(
                    self.source,
                    "source root",
                    fixture_tool._census_error,
                )

        self.assertEqual(len(interrupted_descriptors), 1)
        self.assert_descriptor_closed(interrupted_descriptors[0])

    def test_existing_tree_interrupt_closes_leaf_and_parent_chain(self) -> None:
        chain = fixture_tool._open_directory_chain(
            self.source.parent,
            "source root",
            fixture_tool._census_error,
        )
        chain_descriptors = [node.descriptor for node in chain.nodes]
        real_fstat = fixture_tool.os.fstat
        leaf_descriptors: list[int] = []

        def fstat_then_interrupt(descriptor: int) -> os.stat_result:
            metadata = real_fstat(descriptor)
            leaf_descriptors.append(descriptor)
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool,
            "_open_directory_chain",
            return_value=chain,
        ), mock.patch.object(
            fixture_tool.os,
            "fstat",
            side_effect=fstat_then_interrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_existing_tree(
                    self.source,
                    "source root",
                    fixture_tool._census_error,
                )

        self.assertEqual(len(leaf_descriptors), 1)
        self.assert_descriptor_closed(leaf_descriptors[0])
        for descriptor in chain_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_existing_tree_constructor_interrupt_closes_all_ownership(self) -> None:
        chain = fixture_tool._open_directory_chain(
            self.source.parent,
            "source root",
            fixture_tool._census_error,
        )
        chain_descriptors = [node.descriptor for node in chain.nodes]
        real_open = fixture_tool.os.open
        leaf_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            leaf_descriptors.append(descriptor)
            return descriptor

        with mock.patch.object(
            fixture_tool,
            "_open_directory_chain",
            return_value=chain,
        ), mock.patch.object(
            fixture_tool.os,
            "open",
            side_effect=record_open,
        ), mock.patch.object(
            fixture_tool,
            "_PinnedTree",
            side_effect=KeyboardInterrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_existing_tree(
                    self.source,
                    "source root",
                    fixture_tool._census_error,
                )

        self.assertEqual(len(leaf_descriptors), 1)
        self.assert_descriptor_closed(leaf_descriptors[0])
        for descriptor in chain_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_child_directory_interrupt_closes_the_new_descriptor(self) -> None:
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._census_error,
        )
        entry = fixture_tool._entry_state(
            tree.root.descriptor,
            "Journal",
            "Journal",
            fixture_tool._census_error,
        )
        real_fstat = fixture_tool.os.fstat
        child_descriptors: list[int] = []

        def fstat_then_interrupt(descriptor: int) -> os.stat_result:
            metadata = real_fstat(descriptor)
            child_descriptors.append(descriptor)
            raise KeyboardInterrupt

        try:
            with mock.patch.object(
                fixture_tool.os,
                "fstat",
                side_effect=fstat_then_interrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._open_child_directory(
                        tree.root,
                        entry,
                        "Journal",
                        fixture_tool._census_error,
                    )
            self.assertEqual(len(child_descriptors), 1)
            self.assert_descriptor_closed(child_descriptors[0])
        finally:
            tree.close()

    def test_child_directory_constructor_interrupt_closes_descriptor(self) -> None:
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._census_error,
        )
        entry = fixture_tool._entry_state(
            tree.root.descriptor,
            "Journal",
            "Journal",
            fixture_tool._census_error,
        )
        real_open = fixture_tool.os.open
        child_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            child_descriptors.append(descriptor)
            return descriptor

        try:
            with mock.patch.object(
                fixture_tool.os,
                "open",
                side_effect=record_open,
            ), mock.patch.object(
                fixture_tool,
                "_DirectoryAnchor",
                side_effect=KeyboardInterrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._open_child_directory(
                        tree.root,
                        entry,
                        "Journal",
                        fixture_tool._census_error,
                    )
            self.assertEqual(len(child_descriptors), 1)
            self.assert_descriptor_closed(child_descriptors[0])
        finally:
            tree.close()

    def test_regular_leaf_interrupt_closes_the_file_descriptor(self) -> None:
        tree = fixture_tool._open_existing_tree(
            self.source,
            "source root",
            fixture_tool._census_error,
        )
        record = fixture_tool.FileRecord(
            "Data I1",
            len(self.files["Data I1"]),
            digest_bytes(self.files["Data I1"]),
        )
        real_fstat = fixture_tool.os.fstat
        leaf_descriptors: list[int] = []

        def fstat_then_interrupt(descriptor: int) -> os.stat_result:
            metadata = real_fstat(descriptor)
            leaf_descriptors.append(descriptor)
            raise KeyboardInterrupt

        try:
            with mock.patch.object(
                fixture_tool.os,
                "fstat",
                side_effect=fstat_then_interrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    fixture_tool._open_regular_leaf(
                        tree,
                        record,
                        fixture_tool._census_error,
                    )
            self.assertEqual(len(leaf_descriptors), 1)
            self.assert_descriptor_closed(leaf_descriptors[0])
        finally:
            tree.close()

    def test_manifest_owner_constructor_interrupt_closes_all_descriptors(self) -> None:
        real_open = fixture_tool.os.open
        opened_descriptors: list[int] = []

        def record_open(*arguments: object, **keywords: object) -> int:
            descriptor = real_open(*arguments, **keywords)
            opened_descriptors.append(descriptor)
            return descriptor

        with mock.patch.object(
            fixture_tool.os,
            "open",
            side_effect=record_open,
        ), mock.patch.object(
            fixture_tool,
            "_PinnedManifest",
            side_effect=KeyboardInterrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_pinned_manifest(self.manifest)

        self.assertGreaterEqual(len(opened_descriptors), 2)
        for descriptor in opened_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_destination_plan_constructor_interrupt_closes_parent_chain(self) -> None:
        destination = self.root / "new-destination"
        spec = fixture_tool._path_spec(
            destination,
            "Classic root",
            fixture_tool._staging_error,
        )
        chain = fixture_tool._open_directory_chain(
            destination.parent,
            "Classic root",
            fixture_tool._staging_error,
        )
        chain_descriptors = [node.descriptor for node in chain.nodes]
        with mock.patch.object(
            fixture_tool,
            "_open_directory_chain",
            return_value=chain,
        ), mock.patch.object(
            fixture_tool,
            "_DestinationPlan",
            side_effect=KeyboardInterrupt,
        ):
            with self.assertRaises(KeyboardInterrupt):
                fixture_tool._open_destination_plan(spec, "Classic root")

        for descriptor in chain_descriptors:
            self.assert_descriptor_closed(descriptor)

    def test_supplied_path_with_traversal_is_rejected_before_opening(self) -> None:
        traversing = self.root / "unused" / ".." / self.manifest.name
        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(traversing)
        self.assertIn("path must be lexically normalized", raised.exception.message)

    def test_manifest_leaf_swap_after_open_is_rejected(self) -> None:
        malicious = self.root / "malicious.json"
        malicious_value = copy.deepcopy(self.manifest_value)
        malicious_value["source_class"] = "substituted-save"
        malicious.write_text(json.dumps(malicious_value) + "\n", encoding="utf-8")
        real_open = fixture_tool.os.open
        swapped = False

        def swap_after_leaf_open(
            path: object,
            flags: int,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> int:
            nonlocal swapped
            descriptor = (
                real_open(path, flags, mode)
                if dir_fd is None
                else real_open(path, flags, mode, dir_fd=dir_fd)
            )
            if not swapped and dir_fd is not None and path == self.manifest.name:
                os.rename(
                    self.manifest.name,
                    "fixture-original.json",
                    src_dir_fd=dir_fd,
                    dst_dir_fd=dir_fd,
                )
                os.symlink(str(malicious), self.manifest.name, dir_fd=dir_fd)
                swapped = True
            return descriptor

        with mock.patch.object(fixture_tool.os, "open", side_effect=swap_after_leaf_open):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.load_manifest(self.manifest)
        self.assertTrue(swapped)
        self.assertIn("manifest link changed", raised.exception.message)

    def test_verify_holds_the_exact_manifest_while_hashing_the_source(self) -> None:
        original_verify = fixture_tool._verify_source_initial
        changed = copy.deepcopy(self.manifest_value)
        changed["authorization_basis"] = "Changed after source hashing began."

        def verify_then_change_manifest(*arguments: object) -> object:
            result = original_verify(*arguments)
            self.write_manifest(changed)
            return result

        with mock.patch.object(
            fixture_tool,
            "_verify_source_initial",
            side_effect=verify_then_change_manifest,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.verify_fixture(self.manifest, self.source)
        self.assertEqual(raised.exception.code, "fixture.verification_failed")
        self.assertIn("manifest changed", raised.exception.message)

    def test_stage_holds_the_exact_manifest_through_publication(self) -> None:
        original_verify = fixture_tool._verify_source_initial
        changed = copy.deepcopy(self.manifest_value)
        changed["authorization_basis"] = "Changed while staging was active."

        def verify_then_change_manifest(*arguments: object) -> object:
            result = original_verify(*arguments)
            self.write_manifest(changed)
            return result

        classic = self.root / "classic-manifest-drift"
        semantic = self.root / "semantic-manifest-drift"
        with mock.patch.object(
            fixture_tool,
            "_verify_source_initial",
            side_effect=verify_then_change_manifest,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    classic,
                    semantic,
                )
        self.assertEqual(raised.exception.code, "fixture.staging_failed")
        self.assertIn("manifest changed", raised.exception.message)
        self.assertFalse(classic.exists())
        self.assertFalse(semantic.exists())

    def test_ancestor_symlink_is_rejected_before_manifest_or_root_traversal(self) -> None:
        actual = self.root / "actual-parent"
        actual.mkdir()
        nested = actual / "nested"
        nested.mkdir()
        nested_manifest = nested / "fixture.json"
        nested_manifest.write_bytes(self.manifest.read_bytes())
        link = self.root / "linked-parent"
        link.symlink_to(actual, target_is_directory=True)

        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.load_manifest(link / "nested" / "fixture.json")
        self.assertIn("path component for manifest is not a directory", raised.exception.message)

        with self.assertRaises(fixture_tool.FixtureError) as raised:
            fixture_tool.verify_fixture(self.manifest, link / "nested")
        self.assertIn("path component for source root is not a directory", raised.exception.message)

    def test_manifest_ancestor_swap_uses_pinned_chain_and_is_rejected(self) -> None:
        ancestor = self.root / "ancestor"
        inner = ancestor / "inner"
        inner.mkdir(parents=True)
        original_manifest = inner / "fixture.json"
        original_manifest.write_bytes(self.manifest.read_bytes())
        replacement = self.root / "replacement-ancestor"
        replacement_inner = replacement / "inner"
        replacement_inner.mkdir(parents=True)
        malicious_value = copy.deepcopy(self.manifest_value)
        malicious_value["source_class"] = "substituted-save"
        (replacement_inner / "fixture.json").write_text(
            json.dumps(malicious_value) + "\n",
            encoding="utf-8",
        )
        real_open = fixture_tool.os.open
        swapped = False

        def swap_opened_ancestor(
            path: object,
            flags: int,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> int:
            nonlocal swapped
            descriptor = (
                real_open(path, flags, mode)
                if dir_fd is None
                else real_open(path, flags, mode, dir_fd=dir_fd)
            )
            if not swapped and dir_fd is not None and path == "ancestor":
                os.rename(
                    "ancestor",
                    "ancestor-pinned",
                    src_dir_fd=dir_fd,
                    dst_dir_fd=dir_fd,
                )
                os.rename(
                    "replacement-ancestor",
                    "ancestor",
                    src_dir_fd=dir_fd,
                    dst_dir_fd=dir_fd,
                )
                swapped = True
            return descriptor

        with mock.patch.object(fixture_tool.os, "open", side_effect=swap_opened_ancestor):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.load_manifest(original_manifest)
        self.assertTrue(swapped)
        self.assertIn("descriptor path link changed for manifest", raised.exception.message)

    def test_source_nested_directory_swap_is_detected_without_following_it(self) -> None:
        outside = self.root / "outside-source"
        outside.mkdir()
        malicious = b"outside bytes must never be hashed"
        (outside / "State").write_bytes(malicious)
        original_hash = fixture_tool._hash_record
        swapped = False

        def swap_before_nested_hash(
            tree: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
            fail: object,
        ) -> object:
            nonlocal swapped
            if not swapped and record.path == "Journal/State":
                (self.source / "Journal").rename(self.source / "Journal-original")
                (self.source / "Journal").symlink_to(outside, target_is_directory=True)
                swapped = True
            return original_hash(tree, record, fail)

        with mock.patch.object(fixture_tool, "_hash_record", side_effect=swap_before_nested_hash):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.verify_fixture(self.manifest, self.source)
        self.assertTrue(swapped)
        self.assertIn("fixture directory link changed: Journal", raised.exception.message)
        self.assertEqual((outside / "State").read_bytes(), malicious)

    def test_destination_root_mkdir_swap_is_detected_and_retained(self) -> None:
        real_mkdir = fixture_tool.os.mkdir
        swapped = False

        def replace_new_private_root(
            path: object,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> None:
            nonlocal swapped
            if dir_fd is None:
                real_mkdir(path, mode)
            else:
                real_mkdir(path, mode, dir_fd=dir_fd)
            if not swapped and dir_fd is not None and str(path).startswith(".realmz-stage-"):
                os.rename(
                    path,
                    f"{path}-created",
                    src_dir_fd=dir_fd,
                    dst_dir_fd=dir_fd,
                )
                real_mkdir(path, mode, dir_fd=dir_fd)
                replacement_fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY, dir_fd=dir_fd)
                try:
                    sentinel_fd = os.open(
                        "replacement-sentinel",
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                        0o600,
                        dir_fd=replacement_fd,
                    )
                    os.close(sentinel_fd)
                finally:
                    os.close(replacement_fd)
                swapped = True

        with mock.patch.object(fixture_tool.os, "mkdir", side_effect=replace_new_private_root):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertTrue(swapped)
        self.assertIn("replaced before its descriptor was pinned", raised.exception.message)
        self.assertIn("namespace-uncertain private staging root", raised.exception.message)
        self.assertIn("no deletion attempted", raised.exception.message)
        sentinels = list(self.root.glob(".realmz-stage-*/replacement-sentinel"))
        self.assertEqual(len(sentinels), 1)

    def test_keyboard_interrupt_after_mkdir_reports_observed_private_root(self) -> None:
        real_mkdir = fixture_tool.os.mkdir
        interrupted_names: list[str] = []

        def mkdir_then_interrupt(
            path: object,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> None:
            if dir_fd is None:
                real_mkdir(path, mode)
            else:
                real_mkdir(path, mode, dir_fd=dir_fd)
            if (
                not interrupted_names
                and dir_fd is not None
                and str(path).startswith(".realmz-stage-")
            ):
                interrupted_names.append(str(path))
                raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool.os,
            "mkdir",
            side_effect=mkdir_then_interrupt,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("interruption handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("interruption handling must not rmdir"),
        ):
            with self.assertRaises(KeyboardInterrupt) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertEqual(len(interrupted_names), 1)
        notice = getattr(raised.exception, "fixture_retention_notice", "")
        self.assertIn("last-observed attempted name status=pinned", notice)
        self.assertIn(interrupted_names[0], notice)
        self.assertIn("no deletion attempted", notice)
        self.assertTrue((self.root / interrupted_names[0]).is_dir())

    def test_keyboard_interrupt_creating_second_root_merges_both_retention_notices(
        self,
    ) -> None:
        real_mkdir = fixture_tool.os.mkdir
        staged_names: list[str] = []

        def mkdir_then_interrupt_second_root(
            path: object,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> None:
            if dir_fd is None:
                real_mkdir(path, mode)
            else:
                real_mkdir(path, mode, dir_fd=dir_fd)
            if dir_fd is not None and str(path).startswith(".realmz-stage-"):
                staged_names.append(str(path))
                if len(staged_names) == 2:
                    raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool.os,
            "mkdir",
            side_effect=mkdir_then_interrupt_second_root,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("interruption handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("interruption handling must not rmdir"),
        ):
            with self.assertRaises(KeyboardInterrupt) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )

        self.assertEqual(len(staged_names), 2)
        notice = getattr(raised.exception, "fixture_retention_notice", "")
        for staged_name in staged_names:
            self.assertIn(staged_name, notice)
            self.assertTrue((self.root / staged_name).is_dir())
        self.assertIn("namespace-uncertain private staging root", notice)
        self.assertIn("retained private staging root allocation", notice)
        self.assertGreaterEqual(notice.count("no deletion attempted"), 2)

    def test_keyboard_interrupt_after_helper_return_keeps_owned_tree_reportable(self) -> None:
        create_tree = fixture_tool._create_destination_tree
        interrupted = False

        def create_then_interrupt(
            plan: fixture_tool._DestinationPlan,
            ownership: list[fixture_tool._PinnedTree] | None = None,
        ) -> fixture_tool._PinnedTree:
            nonlocal interrupted
            tree = create_tree(plan, ownership)
            if not interrupted:
                interrupted = True
                raise KeyboardInterrupt
            return tree

        with mock.patch.object(
            fixture_tool,
            "_create_destination_tree",
            side_effect=create_then_interrupt,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("interruption handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("interruption handling must not rmdir"),
        ):
            with self.assertRaises(KeyboardInterrupt) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertTrue(interrupted)
        notice = getattr(raised.exception, "fixture_retention_notice", "")
        self.assertIn("retained private staging root allocation", notice)
        self.assertIn("last-observed matching name", notice)
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 1)

    def test_destination_nested_mkdir_swap_taints_and_retains_private_root(self) -> None:
        real_mkdir = fixture_tool.os.mkdir
        swapped = False

        def replace_nested_directory(
            path: object,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> None:
            nonlocal swapped
            if dir_fd is None:
                real_mkdir(path, mode)
            else:
                real_mkdir(path, mode, dir_fd=dir_fd)
            if not swapped and dir_fd is not None and path == "Journal":
                os.rename(
                    "Journal",
                    "Journal-created",
                    src_dir_fd=dir_fd,
                    dst_dir_fd=dir_fd,
                )
                real_mkdir("Journal", mode, dir_fd=dir_fd)
                replacement_fd = os.open(
                    "Journal",
                    os.O_RDONLY | os.O_DIRECTORY,
                    dir_fd=dir_fd,
                )
                try:
                    sentinel_fd = os.open(
                        "replacement-sentinel",
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                        0o600,
                        dir_fd=replacement_fd,
                    )
                    os.close(sentinel_fd)
                finally:
                    os.close(replacement_fd)
                swapped = True

        with mock.patch.object(fixture_tool.os, "mkdir", side_effect=replace_nested_directory):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertTrue(swapped)
        self.assertIn("staged directory was replaced", raised.exception.message)
        self.assertIn("namespace-tainted private staging root", raised.exception.message)
        self.assertEqual(
            len(list(self.root.glob(".realmz-stage-*/Journal/replacement-sentinel"))),
            1,
        )

    def test_failure_rollback_never_deletes_root_or_leaf_replacements(self) -> None:
        original_copy = fixture_tool._copy_record_to_trees
        replacements: list[Path] = []

        def replace_leaf_after_first_copy(
            source: fixture_tool._PinnedTree,
            classic: fixture_tool._PinnedTree,
            semantic: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
        ) -> None:
            original_copy(source, classic, semantic, record)
            if not replacements:
                private_root = classic.parent_chain.display_path / classic.root.link_name
                leaf = private_root / record.path
                leaf.rename(private_root / f"{record.path}-created")
                leaf.write_bytes(b"replacement leaf")
                replacements.append(leaf)

        with mock.patch.object(
            fixture_tool,
            "_copy_record_to_trees",
            side_effect=replace_leaf_after_first_copy,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("rollback must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("rollback must not rmdir"),
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertIn("retained private staging root", raised.exception.message)
        self.assertEqual(len(replacements), 1)
        self.assertEqual(replacements[0].read_bytes(), b"replacement leaf")

    def test_root_swap_on_failure_is_tainted_and_never_deleted(self) -> None:
        original_copy = fixture_tool._copy_record_to_trees
        moved_roots: list[Path] = []
        replacement_roots: list[Path] = []

        def replace_root_after_first_copy(
            source: fixture_tool._PinnedTree,
            classic: fixture_tool._PinnedTree,
            semantic: fixture_tool._PinnedTree,
            record: fixture_tool.FileRecord,
        ) -> None:
            original_copy(source, classic, semantic, record)
            if not moved_roots:
                private_root = classic.parent_chain.display_path / classic.root.link_name
                moved = private_root.with_name(f"{private_root.name}-created")
                private_root.rename(moved)
                private_root.mkdir(mode=0o700)
                (private_root / "replacement-sentinel").write_bytes(b"do not delete")
                moved_roots.append(moved)
                replacement_roots.append(private_root)

        with mock.patch.object(
            fixture_tool,
            "_copy_record_to_trees",
            side_effect=replace_root_after_first_copy,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("rollback must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("rollback must not rmdir"),
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertIn("namespace-tainted private staging root", raised.exception.message)
        self.assertIn("may now be missing or refer to a replacement", raised.exception.message)
        self.assertEqual(len(moved_roots), 1)
        self.assertTrue(moved_roots[0].is_dir())
        self.assertEqual(
            (replacement_roots[0] / "replacement-sentinel").read_bytes(),
            b"do not delete",
        )

    def test_atomic_publish_race_never_overwrites_new_destination(self) -> None:
        native_publish = fixture_tool._native_rename_noreplace
        inserted = False

        def insert_destination_before_publish(
            source_parent: int,
            source_name: str,
            destination_parent: int,
            destination_name: str,
        ) -> None:
            nonlocal inserted
            if not inserted:
                descriptor = os.open(
                    destination_name,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                    0o600,
                    dir_fd=destination_parent,
                )
                try:
                    os.write(descriptor, b"racing replacement")
                finally:
                    os.close(descriptor)
                inserted = True
            native_publish(
                source_parent,
                source_name,
                destination_parent,
                destination_name,
            )

        with mock.patch.object(
            fixture_tool,
            "_native_rename_noreplace",
            side_effect=insert_destination_before_publish,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertTrue(inserted)
        self.assertIn("cannot atomically publish", raised.exception.message)
        self.assertEqual((self.root / "classic").read_bytes(), b"racing replacement")
        self.assertFalse((self.root / "semantic").exists())
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 2)

    def test_move_after_publish_is_tainted_reported_and_never_deleted(self) -> None:
        native_publish = fixture_tool._native_rename_noreplace
        moved_names: list[str] = []

        def move_just_published_root(
            source_parent: int,
            source_name: str,
            destination_parent: int,
            destination_name: str,
        ) -> None:
            native_publish(
                source_parent,
                source_name,
                destination_parent,
                destination_name,
            )
            moved_name = f"{destination_name}-moved-after-publish"
            os.rename(
                destination_name,
                moved_name,
                src_dir_fd=destination_parent,
                dst_dir_fd=destination_parent,
            )
            moved_names.append(moved_name)

        with mock.patch.object(
            fixture_tool,
            "_native_rename_noreplace",
            side_effect=move_just_published_root,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("failure handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("failure handling must not rmdir"),
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertEqual(moved_names, ["classic-moved-after-publish"])
        self.assertIn("cannot verify published Classic root", raised.exception.message)
        self.assertIn("namespace-tainted published root", raised.exception.message)
        self.assertIn("last-known name", raised.exception.message)
        self.assertIn("may now be missing or refer to a replacement", raised.exception.message)
        moved_root = self.root / moved_names[0]
        self.assertTrue(moved_root.is_dir())
        for relative, expected in self.files.items():
            self.assertEqual((moved_root / relative).read_bytes(), expected)
        self.assertFalse((self.root / "classic").exists())

    def test_keyboard_interrupt_after_native_rename_reports_final_name(self) -> None:
        native_publish = fixture_tool._native_rename_noreplace
        interrupted = False

        def rename_then_interrupt(
            source_parent: int,
            source_name: str,
            destination_parent: int,
            destination_name: str,
        ) -> None:
            nonlocal interrupted
            native_publish(
                source_parent,
                source_name,
                destination_parent,
                destination_name,
            )
            interrupted = True
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool,
            "_native_rename_noreplace",
            side_effect=rename_then_interrupt,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("interruption handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("interruption handling must not rmdir"),
        ):
            with self.assertRaises(KeyboardInterrupt) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertTrue(interrupted)
        notice = getattr(raised.exception, "fixture_retention_notice", "")
        self.assertIn("retained published root allocation", notice)
        self.assertIn(
            f"last-observed matching name: {self.root / 'classic'}",
            notice,
        )
        self.assertTrue((self.root / "classic").is_dir())
        self.assertFalse((self.root / "semantic").exists())
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 1)

    def test_publish_interrupt_after_ancestor_swap_reports_tainted_candidates(self) -> None:
        destination_parent = self.root / "destination-parent"
        destination_parent.mkdir(mode=0o700)
        moved_parent = self.root / "destination-parent-moved"
        native_publish = fixture_tool._native_rename_noreplace
        interrupted = False

        def publish_then_replace_ancestor(
            source_parent: int,
            source_name: str,
            destination_parent_descriptor: int,
            destination_name: str,
        ) -> None:
            nonlocal interrupted
            native_publish(
                source_parent,
                source_name,
                destination_parent_descriptor,
                destination_name,
            )
            destination_parent.rename(moved_parent)
            destination_parent.mkdir(mode=0o700)
            (destination_parent / "replacement-sentinel").write_bytes(b"preserve")
            interrupted = True
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool,
            "_native_rename_noreplace",
            side_effect=publish_then_replace_ancestor,
        ), mock.patch.object(
            fixture_tool.os,
            "unlink",
            side_effect=AssertionError("interruption handling must not unlink"),
        ), mock.patch.object(
            fixture_tool.os,
            "rmdir",
            side_effect=AssertionError("interruption handling must not rmdir"),
        ):
            with self.assertRaises(KeyboardInterrupt) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    destination_parent / "classic",
                    destination_parent / "semantic",
                )
        self.assertTrue(interrupted)
        notice = getattr(raised.exception, "fixture_retention_notice", "")
        self.assertIn("namespace-tainted published root", notice)
        self.assertIn("last-known names", notice)
        self.assertIn(f"final={destination_parent / 'classic'}", notice)
        self.assertIn("may now be missing or refer to a replacement", notice)
        self.assertIn("no deletion attempted", notice)
        self.assertEqual(
            (destination_parent / "replacement-sentinel").read_bytes(),
            b"preserve",
        )
        moved_classic = moved_parent / "classic"
        self.assertTrue(moved_classic.is_dir())
        for relative, expected in self.files.items():
            self.assertEqual((moved_classic / relative).read_bytes(), expected)
        self.assertEqual(len(list(moved_parent.glob(".realmz-stage-*"))), 1)

    def test_partial_publish_is_reported_and_preserved(self) -> None:
        original_publish = fixture_tool._publish_destination
        calls = 0

        def fail_second_publish(tree: fixture_tool._PinnedTree) -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                raise fixture_tool.FixtureError(
                    "fixture.staging_failed",
                    "injected second publish failure",
                    fixture_tool.EXIT_STAGING,
                )
            original_publish(tree)

        with mock.patch.object(
            fixture_tool,
            "_publish_destination",
            side_effect=fail_second_publish,
        ):
            with self.assertRaises(fixture_tool.FixtureError) as raised:
                fixture_tool.stage_fixture(
                    self.manifest,
                    self.source,
                    self.root / "classic",
                    self.root / "semantic",
                )
        self.assertIn("retained published root", raised.exception.message)
        self.assertIn("retained private staging root", raised.exception.message)
        self.assertTrue((self.root / "classic").is_dir())
        self.assertFalse((self.root / "semantic").exists())
        self.assertEqual(len(list(self.root.glob(".realmz-stage-*"))), 1)


class CommandLineTests(ReplayFixtureTestCase):
    def invoke_main(self, arguments: list[str]) -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = fixture_tool.main(arguments)
        return exit_code, stdout.getvalue(), stderr.getvalue()

    def test_census_cli_emits_only_mechanical_unreviewed_evidence(self) -> None:
        completed = self.run_cli(
            "census",
            "--source-root",
            self.source,
            "--slot",
            "H",
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "census_unreviewed")
        self.assertEqual(result["slot"], "H")
        self.assertEqual(result["file_count"], 3)
        self.assertEqual(result["directory_count"], 1)
        self.assertEqual(result["total_file_bytes"], sum(map(len, self.files.values())))
        self.assertEqual(
            [record["path"] for record in result["files"]],
            sorted(self.files, key=lambda value: value.encode("utf-8")),
        )
        for forbidden in (
            "source_class",
            "authorization_basis",
            "redistribution_allowed",
            "semantic_equivalence",
            "equivalent",
            "source_root",
        ):
            self.assertNotIn(forbidden, result)

    def test_verify_emits_machine_readable_non_equivalence_result(self) -> None:
        completed = self.run_cli(
            "verify",
            "--manifest",
            self.manifest,
            "--source-root",
            self.source,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["semantic_equivalence"], "not_evaluated")
        self.assertNotIn("equivalent", result)
        self.assertEqual(
            result["manifest_sha256"],
            digest_bytes(self.manifest.read_bytes()),
        )
        self.assertEqual(result["source_root"], str(self.source.resolve()))

    def test_stage_cli_emits_machine_readable_staged_result(self) -> None:
        classic = self.root / "classic"
        semantic = self.root / "semantic"
        completed = self.run_cli(
            "stage",
            "--manifest",
            self.manifest,
            "--source-root",
            self.source,
            "--classic-root",
            classic,
            "--semantic-root",
            semantic,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["status"], "staged")
        self.assertEqual(result["semantic_equivalence"], "not_evaluated")
        self.assertEqual(
            result["manifest_sha256"],
            digest_bytes(self.manifest.read_bytes()),
        )
        self.assertIs(result["source_unchanged"], True)

    def test_cli_has_help_and_requires_explicit_source_root(self) -> None:
        help_result = self.run_cli("--help")
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("No save path is implied", help_result.stdout)
        self.assertIn("census", help_result.stdout)
        self.assertIn("mechanical and unreviewed", help_result.stdout)
        self.assertIn("verify", help_result.stdout)
        self.assertIn("stage", help_result.stdout)
        self.assertIn("contain fixture bytes", help_result.stdout)

        census_help = self.run_cli("census", "--help")
        self.assertEqual(census_help.returncode, 0)
        self.assertIn("mechanical, unreviewed file census", census_help.stdout)
        self.assertIn("does not establish", census_help.stdout)

        missing_source = self.run_cli("verify", "--manifest", self.manifest)
        self.assertEqual(missing_source.returncode, fixture_tool.EXIT_USAGE)
        error = json.loads(missing_source.stderr)
        self.assertEqual(error["status"], "error")
        self.assertEqual(error["error"]["code"], "usage.invalid")
        self.assertIn("--source-root", error["error"]["message"])

        missing_census_source = self.run_cli("census", "--slot", "A")
        self.assertEqual(missing_census_source.returncode, fixture_tool.EXIT_USAGE)
        error = json.loads(missing_census_source.stderr)
        self.assertEqual(error["error"]["code"], "usage.invalid")
        self.assertIn("--source-root", error["error"]["message"])

        missing_census_slot = self.run_cli(
            "census",
            "--source-root",
            self.source,
        )
        self.assertEqual(missing_census_slot.returncode, fixture_tool.EXIT_USAGE)
        error = json.loads(missing_census_slot.stderr)
        self.assertEqual(error["error"]["code"], "usage.invalid")
        self.assertIn("--slot", error["error"]["message"])

        invalid_census_slot = self.run_cli(
            "census",
            "--source-root",
            self.source,
            "--slot",
            "a",
        )
        self.assertEqual(invalid_census_slot.returncode, fixture_tool.EXIT_USAGE)
        error = json.loads(invalid_census_slot.stderr)
        self.assertEqual(error["error"]["code"], "usage.invalid")
        self.assertIn("uppercase Classic save slot", error["error"]["message"])

    def test_cli_errors_are_json_with_stable_exit_classes(self) -> None:
        invalid_manifest = copy.deepcopy(self.manifest_value)
        invalid_manifest["unknown"] = True
        self.write_manifest(invalid_manifest)
        manifest_failure = self.run_cli(
            "verify",
            "--manifest",
            self.manifest,
            "--source-root",
            self.source,
        )
        self.assertEqual(manifest_failure.returncode, fixture_tool.EXIT_MANIFEST)
        self.assertEqual(manifest_failure.stdout, "")
        manifest_error = json.loads(manifest_failure.stderr)
        self.assertEqual(manifest_error["error"]["code"], "manifest.invalid")

        self.write_manifest(self.manifest_value)
        (self.source / "extra").write_bytes(b"synthetic-extra")
        verification_failure = self.run_cli(
            "verify",
            "--manifest",
            self.manifest,
            "--source-root",
            self.source,
        )
        self.assertEqual(verification_failure.returncode, fixture_tool.EXIT_VERIFICATION)
        verification_error = json.loads(verification_failure.stderr)
        self.assertEqual(
            verification_error["error"]["code"],
            "fixture.verification_failed",
        )

        census_failure = self.run_cli(
            "census",
            "--source-root",
            self.root / "missing-census-root",
            "--slot",
            "A",
        )
        self.assertEqual(census_failure.returncode, fixture_tool.EXIT_VERIFICATION)
        self.assertEqual(census_failure.stdout, "")
        census_error = json.loads(census_failure.stderr)
        self.assertEqual(census_error["status"], "error")
        self.assertEqual(census_error["error"]["code"], "fixture.census_failed")

    def test_cli_interrupts_are_json_exit_130_for_every_operation(self) -> None:
        cases = (
            (
                "census_fixture",
                ["census", "--source-root", str(self.source), "--slot", "A"],
            ),
            (
                "verify_fixture",
                [
                    "verify",
                    "--manifest",
                    str(self.manifest),
                    "--source-root",
                    str(self.source),
                ],
            ),
        )
        for operation, arguments in cases:
            with self.subTest(operation=operation), mock.patch.object(
                fixture_tool,
                operation,
                side_effect=KeyboardInterrupt,
            ):
                exit_code, stdout, stderr = self.invoke_main(arguments)
            self.assertEqual(exit_code, fixture_tool.EXIT_INTERRUPTED)
            self.assertEqual(stdout, "")
            envelope = json.loads(stderr)
            self.assertEqual(
                envelope,
                {
                    "error": {
                        "code": "fixture.interrupted",
                        "message": "fixture operation was interrupted",
                    },
                    "status": "error",
                },
            )

    def test_stage_cli_interrupt_reports_retained_root_notice(self) -> None:
        interrupted = KeyboardInterrupt()
        interrupted.fixture_retention_notice = "retained synthetic private root"
        with mock.patch.object(
            fixture_tool,
            "stage_fixture",
            side_effect=interrupted,
        ):
            exit_code, stdout, stderr = self.invoke_main(
                [
                    "stage",
                    "--manifest",
                    str(self.manifest),
                    "--source-root",
                    str(self.source),
                    "--classic-root",
                    str(self.root / "classic"),
                    "--semantic-root",
                    str(self.root / "semantic"),
                ]
            )
        self.assertEqual(exit_code, fixture_tool.EXIT_INTERRUPTED)
        self.assertEqual(stdout, "")
        envelope = json.loads(stderr)
        self.assertEqual(envelope["error"]["code"], "fixture.interrupted")
        self.assertEqual(
            envelope["retention_notice"], "retained synthetic private root"
        )

    def test_stage_cli_interrupt_after_helper_return_reports_published_roots(
        self,
    ) -> None:
        stage_fixture = fixture_tool.stage_fixture
        classic = self.root / "classic-return-interrupt"
        semantic = self.root / "semantic-return-interrupt"

        def stage_then_interrupt(
            *arguments: object,
            **keywords: object,
        ) -> dict[str, object]:
            stage_fixture(*arguments, **keywords)
            raise KeyboardInterrupt

        with mock.patch.object(
            fixture_tool,
            "stage_fixture",
            side_effect=stage_then_interrupt,
        ):
            exit_code, stdout, stderr = self.invoke_main(
                [
                    "stage",
                    "--manifest",
                    str(self.manifest),
                    "--source-root",
                    str(self.source),
                    "--classic-root",
                    str(classic),
                    "--semantic-root",
                    str(semantic),
                ]
            )

        self.assertEqual(exit_code, fixture_tool.EXIT_INTERRUPTED)
        self.assertEqual(stdout, "")
        envelope = json.loads(stderr)
        self.assertEqual(envelope["error"]["code"], "fixture.interrupted")
        self.assertIn(str(classic), envelope["retention_notice"])
        self.assertIn(str(semantic), envelope["retention_notice"])
        self.assertTrue(classic.is_dir())
        self.assertTrue(semantic.is_dir())

    def test_interrupt_during_success_emission_returns_json_exit_130(self) -> None:
        real_emit = fixture_tool._emit_json

        def interrupt_stdout(value: object, stream: object) -> None:
            if stream is sys.stdout:
                stream.write("partial-success")
                raise KeyboardInterrupt
            real_emit(value, stream)

        with mock.patch.object(
            fixture_tool,
            "_emit_json",
            side_effect=interrupt_stdout,
        ):
            exit_code, stdout, stderr = self.invoke_main(
                [
                    "census",
                    "--source-root",
                    str(self.source),
                    "--slot",
                    "A",
                ]
            )

        self.assertEqual(exit_code, fixture_tool.EXIT_INTERRUPTED)
        self.assertEqual(stdout, "partial-success")
        envelope = json.loads(stderr)
        self.assertEqual(envelope["error"]["code"], "fixture.interrupted")

    def test_stage_emission_interrupt_reports_both_published_roots(self) -> None:
        real_emit = fixture_tool._emit_json
        classic = self.root / "classic"
        semantic = self.root / "semantic"

        def interrupt_stdout(value: object, stream: object) -> None:
            if stream is sys.stdout:
                stream.write("partial-stage")
                raise KeyboardInterrupt
            real_emit(value, stream)

        with mock.patch.object(
            fixture_tool,
            "_emit_json",
            side_effect=interrupt_stdout,
        ):
            exit_code, stdout, stderr = self.invoke_main(
                [
                    "stage",
                    "--manifest",
                    str(self.manifest),
                    "--source-root",
                    str(self.source),
                    "--classic-root",
                    str(classic),
                    "--semantic-root",
                    str(semantic),
                ]
            )

        self.assertEqual(exit_code, fixture_tool.EXIT_INTERRUPTED)
        self.assertEqual(stdout, "partial-stage")
        envelope = json.loads(stderr)
        self.assertEqual(envelope["error"]["code"], "fixture.interrupted")
        self.assertIn(str(classic), envelope["retention_notice"])
        self.assertIn(str(semantic), envelope["retention_notice"])
        self.assertTrue(classic.is_dir())
        self.assertTrue(semantic.is_dir())


if __name__ == "__main__":
    unittest.main()
