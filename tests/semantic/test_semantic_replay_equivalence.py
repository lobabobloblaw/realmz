#!/usr/bin/env python3
"""Synthetic-only tests for the provenance-bound live equivalence gate."""

from __future__ import annotations

import copy
from dataclasses import replace
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts/semantic_replay_equivalence.py"
REQUEST_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-request.schema.json"
)
ENVELOPE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-envelope.schema.json"
)
PROFILE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-profile.schema.json"
)
V2_REQUEST_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-request-v2.schema.json"
)
V2_ENVELOPE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-envelope-v2.schema.json"
)
V2_PROFILE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-profile-v2.schema.json"
)
V3_REQUEST_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-request-v3.schema.json"
)
V3_ENVELOPE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-envelope-v3.schema.json"
)
V3_PROFILE_SCHEMA_PATH = Path(__file__).with_name(
    "semantic-replay-equivalence-profile-v3.schema.json"
)

SPEC = importlib.util.spec_from_file_location(
    "semantic_replay_equivalence", SCRIPT_PATH
)
if SPEC is None or SPEC.loader is None:  # pragma: no cover - import guard
    raise RuntimeError(f"cannot import {SCRIPT_PATH}")
gate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gate
SPEC.loader.exec_module(gate)


FAKE_CHILD = r'''#!__PYTHON__
import json
import os
from pathlib import Path
import sys

if len(sys.argv) != 3 or sys.argv[1] != "--semantic-replay-child":
    raise SystemExit(97)
config_path = Path(sys.argv[2])
config = json.loads(config_path.read_text(encoding="utf-8"))
flags = {
    value for value in os.environ.get("REALMZ_EQ_FAKE_FLAGS", "").split(",") if value
}
route = config["replay_route"]

if "mutate_input" in flags and route == "classic":
    input_file = (
        Path(config["user_data_root"])
        / "Save"
        / f"Game {config['input_slot']}"
        / "Data I1"
    )
    original = input_file.read_bytes()
    input_file.write_bytes(b"X" * len(original))
    input_file.write_bytes(original)

if "mutate_semantic_input" in flags and route == "semantic":
    input_file = (
        Path(config["user_data_root"])
        / "Save"
        / f"Game {config['input_slot']}"
        / "Data I1"
    )
    original = input_file.read_bytes()
    input_file.write_bytes(b"Y" * len(original))
    input_file.write_bytes(original)

if "mutate_source" in flags and route == "classic":
    source_file = Path(os.environ["REALMZ_EQ_FAKE_SOURCE"]) / "Data I1"
    original = source_file.read_bytes()
    source_file.write_bytes(b"Z" * len(original))
    source_file.write_bytes(original)

if "mutate_manifest" in flags and route == "classic":
    manifest = Path(os.environ["REALMZ_EQ_FAKE_MANIFEST"])
    original = manifest.read_bytes()
    manifest.write_bytes(original + b" ")
    manifest.write_bytes(original)

if "nonzero" in flags and route == "classic":
    print("synthetic live-gate child failure", file=sys.stderr)
    raise SystemExit(29)
if "nonzero_leak" in flags and route == "classic":
    private_value = (
        Path(config["user_data_root"])
        / "Save"
        / f"Game {config['input_slot']}"
        / "Data I1"
    ).read_text(encoding="utf-8")
    print(private_value, file=sys.stderr)
    raise SystemExit(30)

output_path = (
    Path(config["user_data_root"])
    / "Save"
    / f"Game {config['output_slot']}"
)
output_path.mkdir()

result = {
    "schema_version": config["schema_version"],
    "run_id": config["run_id"],
    "child_nonce": config["child_nonce"],
    "replay_route": route,
    "presentation_mode": config["presentation_mode"],
    "process_id": os.getpid(),
    "status": "completed",
    "engine_identity": (
        "synthetic-live-gate-child-v1"
        if config["schema_version"] == 1
        else (
            "synthetic-live-gate-child-v2"
            if config["schema_version"] == 2
            else "synthetic-live-gate-child-v3"
        )
    ),
    "settled_action_count": len(config["actions"]),
    "state_sha256": "1" * 64,
    "save_tree_sha256": "2" * 64,
    "rng_draw_count": 7,
    "rng_seed": config["rng_seed"],
    "rng_stream": config["rng_stream"],
}
if "wrong_schema" in flags:
    result["schema_version"] = 2 if config["schema_version"] == 1 else 1
if route == "semantic":
    if "state_sha256" in flags:
        result["state_sha256"] = "3" * 64
    if "save_tree_sha256" in flags:
        result["save_tree_sha256"] = "4" * 64
    if "rng_draw_count" in flags:
        result["rng_draw_count"] = 8
    if "engine_identity" in flags:
        result["engine_identity"] = "synthetic-other-engine"

result_path = Path(config["result_path"])
result_path.write_text(json.dumps(result, sort_keys=True) + "\n", encoding="utf-8")
os.chmod(result_path, 0o600)
raise SystemExit(0)
'''


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


class EquivalenceGateTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.files = {
            "Data I1": b"private-fixture-byte-sentinel",
            "Nested/State": b"synthetic-state\x00\x01",
        }
        for relative, contents in self.files.items():
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)

        records = [
            gate.fixture_tool.FileRecord(relative, len(contents), _sha256(contents))
            for relative, contents in sorted(
                self.files.items(), key=lambda item: item[0].encode("utf-8")
            )
        ]
        self.authorization_basis = (
            "PRIVATE-AUTHORIZATION-SENTINEL supplied for local synthetic testing."
        )
        self.manifest_value = {
            "manifest_version": 1,
            "source_class": "synthetic-test-save",
            "authorization_basis": self.authorization_basis,
            "redistribution_allowed": False,
            "slot": "A",
            "files": [
                {
                    "path": record.path,
                    "size": record.size,
                    "sha256": record.sha256,
                }
                for record in records
            ],
            "tree_sha256": gate.fixture_tool.compute_tree_sha256(records),
        }
        self.manifest = self.root / "fixture.json"
        self.write_json(self.manifest, self.manifest_value)

        executable_directory = self.root / "bin"
        executable_directory.mkdir()
        self.executable = executable_directory / "realmz"
        source = FAKE_CHILD.replace("__PYTHON__", sys.executable)
        self.executable.write_text(textwrap.dedent(source), encoding="utf-8")
        self.executable.chmod(0o700)

        self.request = {
            "schema_version": 1,
            "manifest": str(self.manifest),
            "source_root": str(self.source),
            "fixture_manifest_sha256": _sha256(self.manifest.read_bytes()),
            "fixture_tree_sha256": self.manifest_value["tree_sha256"],
            "executable": str(self.executable),
            "output_slot": "B",
            "actions": [
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"command": "north"},
                }
            ],
            "timeout_seconds": 2,
            "rng_seed": "0123456789abcdef",
            "rng_stream": "fedcba9876543210",
        }
        self.request_path = self.root / "request.json"
        self.write_request()

    def write_json(self, path: Path, value: object) -> None:
        path.write_text(
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def write_request(self, value: object | None = None) -> None:
        self.write_json(self.request_path, self.request if value is None else value)

    def track_retention(self, value: object) -> None:
        if not isinstance(value, dict):
            return
        candidates: list[object] = [
            value.get("fixture_workspace_retention"),
            value.get("runner_workspace_retention"),
            value.get("workspace_retention"),
        ]
        runner = value.get("runner_envelope")
        if isinstance(runner, dict):
            candidates.append(runner.get("workspace_retention"))
        for candidate in candidates:
            if (
                isinstance(candidate, dict)
                and candidate.get("path_authoritative") is True
            ):
                path = Path(str(candidate["candidate_path"]))
                self.addCleanup(shutil.rmtree, path, True)

    def run_gate(self, flags: str = "") -> dict[str, object]:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": flags,
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        with mock.patch.dict(os.environ, environment, clear=False):
            result = gate.run_request_file(self.request_path)
        self.track_retention(result)
        return result

    def assert_gate_error(
        self, code: str, *, flags: str = ""
    ) -> gate.EquivalenceGateError:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": flags,
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        with mock.patch.dict(os.environ, environment, clear=False):
            with self.assertRaises(gate.EquivalenceGateError) as raised:
                gate.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, code)
        error_value = {
            "fixture_workspace_retention": (
                raised.exception.fixture_workspace_retention
            ),
            "runner_workspace_retention": raised.exception.runner_workspace_retention,
        }
        self.track_retention(error_value)
        return raised.exception


class CompletedGateTests(EquivalenceGateTestCase):
    def test_equal_live_results_produce_a_profile_bound_equivalent_verdict(self) -> None:
        envelope = self.run_gate()

        self.assertEqual(envelope["schema_version"], 1)
        self.assertEqual(envelope["status"], "completed")
        self.assertEqual(envelope["semantic_equivalence"], "equivalent")
        comparison = envelope["comparison"]
        self.assertEqual(comparison["contract"], "realmz.semantic-replay.exact.v1")
        self.assertEqual(comparison["mismatched_fields"], [])
        for field in gate.COMPARED_FIELDS:
            self.assertEqual(
                comparison[field]["classic"], comparison[field]["semantic"]
            )

        fixture = envelope["fixture_evidence"]
        self.assertEqual(
            fixture["manifest_sha256"],
            self.request["fixture_manifest_sha256"],
        )
        self.assertEqual(fixture["tree_sha256"], self.request["fixture_tree_sha256"])
        self.assertEqual(fixture["slot"], "A")
        self.assertEqual(fixture["file_count"], len(self.files))
        self.assertEqual(
            fixture["total_file_bytes"], sum(map(len, self.files.values()))
        )
        self.assertIs(fixture["independent_copies"], True)
        for role in ("source", "classic_input", "semantic_input"):
            self.assertEqual(
                fixture["trees"][role],
                {"verified_before": True, "verified_after": True},
            )

        profile = envelope["replay_profile"]
        self.assertEqual(profile["action_count"], 1)
        self.assertEqual(profile["input_slot"], "A")
        self.assertEqual(profile["output_slot"], "B")
        self.assertEqual(
            profile["settlement_barrier"], "next_semantic_gameplay_poll"
        )
        self.assertEqual(profile["rng_seed"], self.request["rng_seed"])
        self.assertEqual(profile["rng_stream"], self.request["rng_stream"])
        self.assertEqual(len(profile["actions_sha256"]), 64)

        runner = envelope["runner_envelope"]
        self.assertEqual(runner["runner_scope"], "process_isolation_only")
        self.assertEqual(runner["semantic_equivalence"], "not_evaluated")
        classic, semantic = runner["child_results"]
        self.assertEqual(classic["replay_route"], "classic")
        self.assertEqual(classic["presentation_mode"], "classic")
        self.assertEqual(semantic["replay_route"], "semantic")
        self.assertEqual(semantic["presentation_mode"], "remastered")
        self.assertNotEqual(classic["process_id"], semantic["process_id"])
        self.assertEqual(runner["schema_version"], 1)
        self.assertEqual(classic["schema_version"], 1)
        self.assertEqual(semantic["schema_version"], 1)
        self.assertEqual(
            {classic["engine_identity"], semantic["engine_identity"]},
            {"synthetic-live-gate-child-v1"},
        )
        encoded = json.dumps(
            envelope, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertIn(b'"schema_version":1', encoded)

        fixture_retention = envelope["fixture_workspace_retention"]
        runner_retention = runner["workspace_retention"]
        for retention in (fixture_retention, runner_retention):
            self.assertIs(retention["cleanup_attempted"], False)
            self.assertIs(retention["path_authoritative"], True)
            path = Path(retention["candidate_path"])
            self.assertTrue(path.is_dir())
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)

    def test_v2_mixed_actions_propagate_to_an_exact_v2_envelope(self) -> None:
        self.request["schema_version"] = 2
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "move_party",
                "arguments": {"command": "north"},
            },
            {
                "ordinal": 1,
                "kind": "select_party_member",
                "arguments": {"member": 2},
            },
        ]
        self.write_request()

        envelope = self.run_gate()

        self.assertEqual(envelope["schema_version"], 2)
        self.assertEqual(envelope["comparison"]["contract"], "realmz.semantic-replay.exact.v2")
        self.assertEqual(envelope["replay_profile"]["action_count"], 2)
        runner_envelope = envelope["runner_envelope"]
        self.assertEqual(runner_envelope["schema_version"], 2)
        self.assertEqual(
            [result["schema_version"] for result in runner_envelope["child_results"]],
            [2, 2],
        )
        self.assertEqual(
            {
                result["engine_identity"]
                for result in runner_envelope["child_results"]
            },
            {"synthetic-live-gate-child-v2"},
        )

    def test_v3_mixed_actions_propagate_to_an_exact_v3_envelope(self) -> None:
        self.request["schema_version"] = 3
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "move_party",
                "arguments": {"command": "north"},
            },
            {
                "ordinal": 1,
                "kind": "select_party_member",
                "arguments": {"member": 2},
            },
            {
                "ordinal": 2,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 2},
            },
        ]
        self.write_request()

        envelope = self.run_gate()

        self.assertEqual(envelope["schema_version"], 3)
        self.assertEqual(
            envelope["comparison"]["contract"],
            "realmz.semantic-replay.exact.v3",
        )
        self.assertEqual(envelope["replay_profile"]["action_count"], 3)
        self.assertEqual(
            envelope["replay_profile"]["actions_sha256"],
            "8840204b07a824ab0536de2bee8b86f51dcdac0f397f4610d6a578d1990e546c",
        )
        runner_envelope = envelope["runner_envelope"]
        self.assertEqual(runner_envelope["schema_version"], 3)
        self.assertEqual(
            [result["schema_version"] for result in runner_envelope["child_results"]],
            [3, 3],
        )
        self.assertEqual(
            {
                result["engine_identity"]
                for result in runner_envelope["child_results"]
            },
            {"synthetic-live-gate-child-v3"},
        )

    def test_only_declared_observables_control_the_verdict(self) -> None:
        envelope = self.run_gate()
        classic, semantic = envelope["runner_envelope"]["child_results"]
        self.assertNotEqual(classic["process_id"], semantic["process_id"])
        self.assertNotEqual(classic["child_nonce"], semantic["child_nonce"])
        self.assertNotEqual(classic["replay_route"], semantic["replay_route"])
        self.assertNotEqual(
            classic["presentation_mode"], semantic["presentation_mode"]
        )
        self.assertEqual(envelope["semantic_equivalence"], "equivalent")
        for intentionally_different in (
            "process_id",
            "child_nonce",
            "replay_route",
            "presentation_mode",
        ):
            self.assertNotIn(intentionally_different, envelope["comparison"])

    def test_one_and_multiple_measurement_mismatches_are_canonical(self) -> None:
        one = self.run_gate("state_sha256")
        self.assertEqual(one["semantic_equivalence"], "not_equivalent")
        self.assertEqual(
            one["comparison"]["mismatched_fields"], ["state_sha256"]
        )
        self.assertNotEqual(
            one["comparison"]["state_sha256"]["classic"],
            one["comparison"]["state_sha256"]["semantic"],
        )

        many = self.run_gate(
            "state_sha256,save_tree_sha256,rng_draw_count"
        )
        self.assertEqual(many["semantic_equivalence"], "not_equivalent")
        self.assertEqual(
            many["comparison"]["mismatched_fields"],
            ["state_sha256", "save_tree_sha256", "rng_draw_count"],
        )
        self.assertEqual(
            many["comparison"]["settled_action_count"]["classic"],
            many["comparison"]["settled_action_count"]["semantic"],
        )

    def test_settled_count_is_recorded_but_a_divergence_is_not_evaluated(self) -> None:
        classic = {
            "schema_version": 1,
            "state_sha256": "1" * 64,
            "save_tree_sha256": "2" * 64,
            "settled_action_count": 1,
            "rng_draw_count": 7,
            "process_id": 1,
        }
        semantic = dict(classic)
        semantic["settled_action_count"] = 2
        semantic["process_id"] = 99

        with self.assertRaises(gate.EquivalenceGateError) as raised:
            gate.compare_child_results([classic, semantic])
        self.assertEqual(raised.exception.code, "runner.envelope_invalid")
        self.assertIn("settled_action_count", raised.exception.message)

    def test_comparison_rejects_cross_version_child_results(self) -> None:
        result = {
            "schema_version": 1,
            "state_sha256": "1" * 64,
            "save_tree_sha256": "2" * 64,
            "settled_action_count": 1,
            "rng_draw_count": 7,
        }
        with self.assertRaises(gate.EquivalenceGateError) as raised:
            gate.compare_child_results([result, dict(result)], 2)
        self.assertEqual(raised.exception.code, "runner.envelope_invalid")
        self.assertEqual(raised.exception.schema_version, 2)
        self.assertIn("schema_version must be 2", raised.exception.message)

    def test_comparison_requires_plain_exact_child_versions(self) -> None:
        base = {
            "state_sha256": "1" * 64,
            "save_tree_sha256": "2" * 64,
            "settled_action_count": 1,
            "rng_draw_count": 7,
        }
        missing = object()
        for expected, invalid_versions in (
            (1, (missing, True, 1.0, 2, 3)),
            (2, (missing, False, 2.0, 1, 3)),
            (3, (missing, True, 3.0, 1, 2)),
        ):
            for invalid_version in invalid_versions:
                with self.subTest(
                    expected=expected,
                    invalid_version=invalid_version,
                ):
                    result = dict(base)
                    if invalid_version is not missing:
                        result["schema_version"] = invalid_version
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.compare_child_results(
                            [result, dict(result)], expected
                        )
                    self.assertEqual(
                        raised.exception.code, "runner.envelope_invalid"
                    )
                    self.assertEqual(raised.exception.schema_version, expected)

    def test_cli_uses_a_dedicated_non_equivalent_exit_and_stdout_envelope(self) -> None:
        environment = os.environ.copy()
        environment.update(
            {
                "REALMZ_EQ_FAKE_FLAGS": "save_tree_sha256",
                "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
                "REALMZ_EQ_FAKE_SOURCE": str(self.source),
            }
        )
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_PATH), "--request", str(self.request_path)],
            cwd=self.root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, gate.EXIT_NOT_EQUIVALENT)
        self.assertEqual(completed.stderr, "")
        envelope = json.loads(completed.stdout)
        self.track_retention(envelope)
        self.assertEqual(envelope["status"], "completed")
        self.assertEqual(envelope["semantic_equivalence"], "not_equivalent")

    def test_serialized_verdict_never_contains_fixture_or_authorization_bytes(self) -> None:
        envelope = self.run_gate()
        serialized = json.dumps(envelope, ensure_ascii=False, sort_keys=True)
        self.assertNotIn(self.authorization_basis, serialized)
        for contents in self.files.values():
            self.assertNotIn(contents.decode("utf-8", errors="ignore"), serialized)


class FailClosedGateTests(EquivalenceGateTestCase):
    def test_v2_child_result_version_mismatch_is_a_v2_error(self) -> None:
        self.request["schema_version"] = 2
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "select_party_member",
                "arguments": {"member": 2},
            }
        ]
        self.write_request()

        error = self.assert_gate_error("child.result_invalid", flags="wrong_schema")

        self.assertEqual(error.schema_version, 2)
        self.assertEqual(gate._error_envelope(error)["schema_version"], 2)

    def test_v3_child_result_version_mismatch_is_a_v3_error(self) -> None:
        self.request["schema_version"] = 3
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 2},
            }
        ]
        self.write_request()

        error = self.assert_gate_error("child.result_invalid", flags="wrong_schema")

        self.assertEqual(error.schema_version, 3)
        self.assertEqual(gate._error_envelope(error)["schema_version"], 3)

    def test_v2_gate_rejects_non_plain_or_downgraded_runner_envelope(self) -> None:
        self.request["schema_version"] = 2
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "select_party_member",
                "arguments": {"member": 2},
            }
        ]
        self.write_request()
        run_runner = gate.replay_runner.run_request

        for invalid_version in (1, 2.0):
            with self.subTest(invalid_version=invalid_version):
                def invalid_envelope(
                    *args: object, **kwargs: object
                ) -> dict[str, object]:
                    envelope = run_runner(*args, **kwargs)
                    envelope["schema_version"] = invalid_version
                    return envelope

                with mock.patch.object(
                    gate.replay_runner,
                    "run_request",
                    side_effect=invalid_envelope,
                ):
                    error = self.assert_gate_error("runner.envelope_invalid")

                self.assertEqual(error.schema_version, 2)
                self.assertIn("must match the equivalence request", error.message)

    def test_v3_gate_rejects_missing_non_plain_or_cross_version_runner_envelope(
        self,
    ) -> None:
        self.request["schema_version"] = 3
        self.request["actions"] = [
            {
                "ordinal": 0,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 2},
            }
        ]
        self.write_request()
        run_runner = gate.replay_runner.run_request

        for invalid_version in (None, True, 3.0, 1, 2):
            with self.subTest(invalid_version=invalid_version):
                def invalid_envelope(
                    *args: object, **kwargs: object
                ) -> dict[str, object]:
                    envelope = run_runner(*args, **kwargs)
                    if invalid_version is None:
                        envelope.pop("schema_version")
                    else:
                        envelope["schema_version"] = invalid_version
                    return envelope

                with mock.patch.object(
                    gate.replay_runner,
                    "run_request",
                    side_effect=invalid_envelope,
                ):
                    error = self.assert_gate_error("runner.envelope_invalid")

                self.assertEqual(error.schema_version, 3)
                self.assertIn("must match the equivalence request", error.message)

    def test_v1_gate_rejects_missing_or_boolean_runner_envelope_version(self) -> None:
        run_runner = gate.replay_runner.run_request
        for invalid_version in (None, True):
            with self.subTest(invalid_version=invalid_version):
                def invalid_envelope(
                    *args: object, **kwargs: object
                ) -> dict[str, object]:
                    envelope = run_runner(*args, **kwargs)
                    if invalid_version is None:
                        envelope.pop("schema_version")
                    else:
                        envelope["schema_version"] = invalid_version
                    return envelope

                with mock.patch.object(
                    gate.replay_runner,
                    "run_request",
                    side_effect=invalid_envelope,
                ):
                    error = self.assert_gate_error("runner.envelope_invalid")

                self.assertEqual(error.schema_version, 1)
                self.assertIn("must match the equivalence request", error.message)

    def test_partial_workspace_creation_is_retained_and_reported(self) -> None:
        require_private = gate._require_private_directory

        def fail_after_workspace(path: Path, context: str) -> tuple[int, int]:
            if context == "staged user-data root":
                raise gate.EquivalenceGateError(
                    "gate.workspace_invalid",
                    "synthetic child-root inspection failure",
                    gate.EXIT_STAGING,
                )
            return require_private(path, context)

        with mock.patch.object(
            gate,
            "_require_private_directory",
            side_effect=fail_after_workspace,
        ):
            with self.assertRaises(gate.EquivalenceGateError) as raised:
                gate._make_gate_workspace()
        retention = raised.exception.fixture_workspace_retention
        self.assertIsNotNone(retention)
        self.track_retention({"fixture_workspace_retention": retention})
        self.assertIs(retention["cleanup_attempted"], False)
        self.assertIs(retention["path_authoritative"], True)
        self.assertIs(retention["contains_fixture_bytes"], True)
        self.assertTrue(Path(retention["candidate_path"]).is_dir())

    def test_fixture_digest_and_slot_are_checked_before_workspace_creation(self) -> None:
        self.request["fixture_manifest_sha256"] = "0" * 64
        self.write_request()
        with mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("workspace must not be created"),
        ) as make_workspace:
            error = self.assert_gate_error("fixture.digest_mismatch")
        make_workspace.assert_not_called()
        self.assertIn("fixture_manifest_sha256", error.message)
        self.assertIsNone(error.fixture_workspace_retention)

        self.request["fixture_manifest_sha256"] = _sha256(
            self.manifest.read_bytes()
        )
        self.request["fixture_tree_sha256"] = "0" * 64
        self.write_request()
        with mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("workspace must not be created"),
        ) as make_workspace:
            error = self.assert_gate_error("fixture.digest_mismatch")
        make_workspace.assert_not_called()
        self.assertIsNone(error.fixture_workspace_retention)

        self.request["fixture_tree_sha256"] = self.manifest_value["tree_sha256"]
        self.request["output_slot"] = "A"
        self.write_request()
        with mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("workspace must not be created"),
        ) as make_workspace:
            self.assert_gate_error("request.invalid")
        make_workspace.assert_not_called()

    def test_staged_lease_is_bound_before_either_child_can_run(self) -> None:
        replacement_files = {
            "Data I1": b"different-private-fixture-data",
            "Nested/State": b"different-state---",
        }
        replacement_records = [
            gate.fixture_tool.FileRecord(relative, len(contents), _sha256(contents))
            for relative, contents in sorted(
                replacement_files.items(), key=lambda item: item[0].encode("utf-8")
            )
        ]
        replacement_manifest = copy.deepcopy(self.manifest_value)
        replacement_manifest["files"] = [
            {
                "path": record.path,
                "size": record.size,
                "sha256": record.sha256,
            }
            for record in replacement_records
        ]
        replacement_manifest["tree_sha256"] = (
            gate.fixture_tool.compute_tree_sha256(replacement_records)
        )
        stage_fixture = gate.fixture_tool.stage_fixture

        def replace_before_stage(
            *arguments: object, **keywords: object
        ) -> dict[str, object]:
            for relative, contents in replacement_files.items():
                (self.source / relative).write_bytes(contents)
            self.write_json(self.manifest, replacement_manifest)
            return stage_fixture(*arguments, **keywords)

        with mock.patch.object(
            gate.fixture_tool,
            "stage_fixture",
            side_effect=replace_before_stage,
        ), mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=AssertionError("children must not run for a changed fixture"),
        ) as run_children:
            error = self.assert_gate_error("fixture.digest_mismatch")
        run_children.assert_not_called()
        self.assertIn("expected_manifest_sha256", error.message)
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNone(error.runner_workspace_retention)
        workspace = Path(error.fixture_workspace_retention["candidate_path"])
        self.assertFalse((workspace / "classic-user" / "Save" / "Game A").exists())
        self.assertFalse((workspace / "semantic-user" / "Save" / "Game A").exists())

    def test_reviewed_manifest_bytes_are_bound_before_either_child_can_run(self) -> None:
        changed_manifest = copy.deepcopy(self.manifest_value)
        changed_manifest["authorization_basis"] = (
            "A different declaration over the same exact fixture bytes."
        )
        stage_fixture = gate.fixture_tool.stage_fixture

        def replace_manifest_before_stage(
            *arguments: object, **keywords: object
        ) -> dict[str, object]:
            self.write_json(self.manifest, changed_manifest)
            return stage_fixture(*arguments, **keywords)

        with mock.patch.object(
            gate.fixture_tool,
            "stage_fixture",
            side_effect=replace_manifest_before_stage,
        ), mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=AssertionError("children must not run for changed provenance"),
        ) as run_children:
            error = self.assert_gate_error("fixture.digest_mismatch")
        run_children.assert_not_called()
        self.assertIn("expected_manifest_sha256", error.message)
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNone(error.runner_workspace_retention)
        workspace = Path(error.fixture_workspace_retention["candidate_path"])
        self.assertFalse((workspace / "classic-user" / "Save" / "Game A").exists())
        self.assertFalse((workspace / "semantic-user" / "Save" / "Game A").exists())

    def test_input_mutation_prevents_an_equivalence_verdict(self) -> None:
        for flag in ("mutate_source", "mutate_input", "mutate_semantic_input"):
            with self.subTest(flag=flag):
                error = self.assert_gate_error(
                    "fixture.attestation_failed", flags=flag
                )
                self.assertIn("fixture", error.message)
                self.assertIsNotNone(error.fixture_workspace_retention)
                self.assertIsNotNone(error.runner_workspace_retention)

    def test_manifest_mutation_is_detected_by_the_held_lease(self) -> None:
        error = self.assert_gate_error(
            "fixture.attestation_failed", flags="mutate_manifest"
        )
        self.assertIn("manifest", error.message)

    def test_runner_failure_is_not_an_equivalence_mismatch(self) -> None:
        error = self.assert_gate_error("child.nonzero_exit", flags="nonzero")
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNotNone(error.runner_workspace_retention)
        envelope = gate._error_envelope(error)
        self.assertEqual(envelope["schema_version"], 1)
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["status"], "error")
        encoded = json.dumps(
            envelope, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertIn(b'"schema_version":1', encoded)

    def test_child_output_cannot_leak_fixture_bytes_through_gate_errors(self) -> None:
        error = self.assert_gate_error(
            "child.nonzero_exit", flags="nonzero_leak"
        )
        serialized = json.dumps(gate._error_envelope(error), sort_keys=True)
        self.assertNotIn("private-fixture-byte-sentinel", serialized)
        self.assertEqual(
            gate._error_envelope(error)["error"]["message"],
            "semantic replay runner failed with code child.nonzero_exit",
        )

    def test_fixture_failure_wins_but_preserves_a_runner_failure(self) -> None:
        error = self.assert_gate_error(
            "fixture.attestation_failed", flags="mutate_input,nonzero"
        )
        self.assertEqual(error.secondary_error["code"], "child.nonzero_exit")
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNotNone(error.runner_workspace_retention)
        envelope = gate._error_envelope(error)
        self.assertEqual(
            envelope["secondary_error"]["code"], "child.nonzero_exit"
        )

    def test_engine_identity_mismatch_remains_not_evaluated(self) -> None:
        error = self.assert_gate_error(
            "child.engine_identity_mismatch", flags="engine_identity"
        )
        self.assertEqual(
            gate._error_envelope(error)["semantic_equivalence"], "not_evaluated"
        )

    def test_post_runner_error_reports_both_retained_workspaces(self) -> None:
        with mock.patch.object(
            gate,
            "compare_child_results",
            side_effect=gate.EquivalenceGateError(
                "runner.envelope_invalid",
                "synthetic post-run comparison failure",
                gate.EXIT_RUNNER,
            ),
        ):
            error = self.assert_gate_error("runner.envelope_invalid")
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNotNone(error.runner_workspace_retention)
        self.assertTrue(
            Path(error.runner_workspace_retention["candidate_path"]).is_dir()
        )

    def test_generic_post_runner_error_keeps_runner_retention_for_cli_wrapping(self) -> None:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate,
            "compare_child_results",
            side_effect=AssertionError("synthetic unexpected comparison failure"),
        ):
            with self.assertRaises(AssertionError) as raised:
                gate.run_request_file(self.request_path)
        fixture_retention = getattr(
            raised.exception, "fixture_workspace_retention", None
        )
        runner_retention = gate._runner_retention(raised.exception)
        self.track_retention(
            {
                "fixture_workspace_retention": fixture_retention,
                "runner_workspace_retention": runner_retention,
            }
        )
        self.assertIsNotNone(fixture_retention)
        self.assertIsNotNone(runner_retention)
        self.assertTrue(Path(runner_retention["candidate_path"]).is_dir())

    def test_interrupt_during_post_run_attestation_exits_130_with_retention(self) -> None:
        finalize = gate.fixture_tool.FixtureSetLease.finalize

        def finalize_then_interrupt(
            lease: gate.fixture_tool.FixtureSetLease,
        ) -> gate.fixture_tool.FixtureSetEvidence:
            finalize(lease)
            raise KeyboardInterrupt

        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate.fixture_tool.FixtureSetLease,
            "finalize",
            finalize_then_interrupt,
        ), mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = gate.main(["--request", str(self.request_path)])

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "")
        envelope = json.loads(stderr.getvalue())
        self.track_retention(envelope)
        self.assertEqual(envelope["status"], "error")
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")
        self.assertIn("fixture_workspace_retention", envelope)
        self.assertIn("runner_workspace_retention", envelope)

    def test_interrupt_during_staging_exits_130_without_running_children(self) -> None:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate.fixture_tool,
            "stage_fixture",
            side_effect=KeyboardInterrupt,
        ), mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=AssertionError("children must not run after staging interrupt"),
        ) as run_children, mock.patch.object(
            sys, "stdout", stdout
        ), mock.patch.object(sys, "stderr", stderr):
            exit_code = gate.main(["--request", str(self.request_path)])

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "")
        envelope = json.loads(stderr.getvalue())
        self.track_retention(envelope)
        self.assertEqual(envelope["status"], "error")
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")
        self.assertIn("fixture_workspace_retention", envelope)
        self.assertNotIn("runner_workspace_retention", envelope)
        workspace = Path(
            envelope["fixture_workspace_retention"]["candidate_path"]
        )
        self.assertFalse((workspace / "classic-user" / "Save" / "Game A").exists())
        self.assertFalse((workspace / "semantic-user" / "Save" / "Game A").exists())
        run_children.assert_not_called()

    def test_completed_gate_emission_interrupt_preserves_both_retentions(self) -> None:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        real_emit = gate._emit_json
        stdout = io.StringIO()
        stderr = io.StringIO()

        def interrupt_stdout(value: object, stream: object) -> None:
            if stream is sys.stdout:
                self.assertEqual(value["status"], "completed")
                stream.write("partial-completed-gate")
                raise KeyboardInterrupt
            real_emit(value, stream)

        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate,
            "_emit_json",
            side_effect=interrupt_stdout,
        ), mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = gate.main(["--request", str(self.request_path)])

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "partial-completed-gate")
        envelope = json.loads(stderr.getvalue())
        self.track_retention(envelope)
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")
        self.assertIn("fixture_workspace_retention", envelope)
        self.assertIn("runner_workspace_retention", envelope)
        self.assertTrue(
            Path(envelope["fixture_workspace_retention"]["candidate_path"]).is_dir()
        )
        self.assertTrue(
            Path(envelope["runner_workspace_retention"]["candidate_path"]).is_dir()
        )

    def test_runner_return_interrupt_preserves_both_retentions(self) -> None:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        run_request = gate.replay_runner.run_request

        def run_then_interrupt(
            *arguments: object,
            **keywords: object,
        ) -> dict[str, object]:
            run_request(*arguments, **keywords)
            raise KeyboardInterrupt

        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=run_then_interrupt,
        ), mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = gate.main(["--request", str(self.request_path)])

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "")
        envelope = json.loads(stderr.getvalue())
        self.track_retention(envelope)
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")
        self.assertIn("fixture_workspace_retention", envelope)
        self.assertIn("runner_workspace_retention", envelope)

    def test_completed_gate_return_interrupt_preserves_both_retentions(self) -> None:
        environment = {
            "REALMZ_EQ_FAKE_FLAGS": "",
            "REALMZ_EQ_FAKE_MANIFEST": str(self.manifest),
            "REALMZ_EQ_FAKE_SOURCE": str(self.source),
        }
        run_request_file = gate.run_request_file

        def run_then_interrupt(
            *arguments: object,
            **keywords: object,
        ) -> dict[str, object]:
            run_request_file(*arguments, **keywords)
            raise KeyboardInterrupt

        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, environment, clear=False), mock.patch.object(
            gate,
            "run_request_file",
            side_effect=run_then_interrupt,
        ), mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = gate.main(["--request", str(self.request_path)])

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "")
        envelope = json.loads(stderr.getvalue())
        self.track_retention(envelope)
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")
        self.assertIn("fixture_workspace_retention", envelope)
        self.assertIn("runner_workspace_retention", envelope)

    def test_retained_fixture_workspace_is_not_automatically_removed(self) -> None:
        envelope = self.run_gate()
        retention = envelope["fixture_workspace_retention"]
        path = Path(retention["candidate_path"])
        self.assertTrue(path.is_dir())
        self.assertTrue((path / "classic-user" / "Save" / "Game A").is_dir())
        self.assertTrue((path / "semantic-user" / "Save" / "Game B").is_dir())


class RequestAndSchemaTests(EquivalenceGateTestCase):
    def assert_request_error(self, expected: str) -> None:
        with self.assertRaises(gate.EquivalenceGateError) as raised:
            gate.load_request(self.request_path)
        self.assertEqual(raised.exception.code, "request.invalid")
        self.assertIn(expected, raised.exception.message)

    def test_profile_inspection_is_validated_private_and_non_executing(self) -> None:
        with mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("profile inspection must not stage"),
        ) as make_workspace, mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=AssertionError("profile inspection must not launch children"),
        ) as run_children:
            profile = gate.inspect_profile_file(self.request_path)
        make_workspace.assert_not_called()
        run_children.assert_not_called()

        self.assertEqual(set(profile), gate.PROFILE_INSPECTION_FIELDS)
        self.assertEqual(profile["schema_version"], 1)
        self.assertEqual(profile["status"], "profile_inspected")
        self.assertEqual(
            profile["fixture_manifest_sha256"],
            self.request["fixture_manifest_sha256"],
        )
        self.assertEqual(
            profile["fixture_tree_sha256"],
            self.request["fixture_tree_sha256"],
        )
        parsed = gate.load_request(self.request_path)
        self.assertEqual(profile["actions_sha256"], gate._actions_sha256(parsed.actions))
        self.assertEqual(profile["action_count"], 1)
        self.assertEqual(profile["input_slot"], "A")
        self.assertEqual(profile["output_slot"], "B")
        self.assertEqual(profile["settlement_barrier"], gate.SETTLEMENT_BARRIER)
        serialized = json.dumps(profile, sort_keys=True)
        self.assertNotIn(str(self.manifest), serialized)
        self.assertNotIn(str(self.source), serialized)
        self.assertNotIn(str(self.executable), serialized)
        self.assertNotIn(self.authorization_basis, serialized)

    def test_profile_inspection_cli_emits_stdout_and_no_verdict(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ), mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("profile inspection must not stage"),
        ):
            exit_code = gate.main(
                ["--request", str(self.request_path), "--inspect-profile"]
            )
        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        profile = json.loads(stdout.getvalue())
        self.assertEqual(profile["status"], "profile_inspected")
        self.assertNotIn("semantic_equivalence", profile)

    def test_interrupt_during_profile_emission_returns_json_exit_130(self) -> None:
        real_emit = gate._emit_json
        stdout = io.StringIO()
        stderr = io.StringIO()

        def interrupt_stdout(value: object, stream: object) -> None:
            if stream is sys.stdout:
                stream.write("partial-profile")
                raise KeyboardInterrupt
            real_emit(value, stream)

        with mock.patch.object(
            gate,
            "_emit_json",
            side_effect=interrupt_stdout,
        ), mock.patch.object(sys, "stdout", stdout), mock.patch.object(
            sys, "stderr", stderr
        ):
            exit_code = gate.main(
                ["--request", str(self.request_path), "--inspect-profile"]
            )

        self.assertEqual(exit_code, gate.EXIT_INTERRUPTED)
        self.assertEqual(stdout.getvalue(), "partial-profile")
        envelope = json.loads(stderr.getvalue())
        self.assertEqual(envelope["status"], "error")
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["error"]["code"], "gate.interrupted")

    def test_request_is_strict_and_validated_before_fixture_mutation(self) -> None:
        unknown = copy.deepcopy(self.request)
        unknown["claim_equivalent"] = True
        self.write_request(unknown)
        self.assert_request_error("unknown field: claim_equivalent")

        missing = copy.deepcopy(self.request)
        del missing["actions"]
        self.write_request(missing)
        self.assert_request_error("missing field: actions")

        self.request_path.write_text(
            '{"schema_version":1,"schema_version":1}', encoding="utf-8"
        )
        self.assert_request_error("duplicate JSON key: schema_version")

    def test_unknown_schema_version_fails_closed_without_downgrade(self) -> None:
        for unknown_version in (True, 0, 3.0, 4):
            with self.subTest(unknown_version=unknown_version):
                value = copy.deepcopy(self.request)
                value["schema_version"] = unknown_version
                self.write_request(value)

                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.run_request_file(self.request_path)

                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertEqual(raised.exception.schema_version, 1)
                self.assertIn(
                    "schema_version must be 1, 2, or 3",
                    raised.exception.message,
                )
                self.assertEqual(
                    gate._error_envelope(raised.exception)["schema_version"], 1
                )
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_recognized_cli_validation_errors_keep_their_schema_version(self) -> None:
        for schema_version, action in (
            (
                2,
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": True},
                },
            ),
            (
                3,
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": True},
                },
            ),
        ):
            with self.subTest(schema_version=schema_version):
                value = copy.deepcopy(self.request)
                value["schema_version"] = schema_version
                value["actions"] = [action]
                self.write_request(value)
                stdout = io.StringIO()
                stderr = io.StringIO()

                with mock.patch.object(sys, "stdout", stdout), mock.patch.object(
                    sys, "stderr", stderr
                ):
                    exit_code = gate.main(
                        ["--request", str(self.request_path), "--inspect-profile"]
                    )

                self.assertEqual(exit_code, gate.EXIT_REQUEST)
                self.assertEqual(stdout.getvalue(), "")
                envelope = json.loads(stderr.getvalue())
                self.assertEqual(envelope["schema_version"], schema_version)
                self.assertEqual(envelope["error"]["code"], "request.invalid")

    def test_deep_or_nonfinite_json_is_a_bounded_request_error(self) -> None:
        self.request_path.write_text("[" * 600000 + "]" * 600000, encoding="utf-8")
        self.assert_request_error("not valid strict JSON")

        self.write_request()
        raw = self.request_path.read_text(encoding="utf-8")
        self.request_path.write_text(
            raw.replace('"timeout_seconds": 2', '"timeout_seconds": NaN'),
            encoding="utf-8",
        )
        self.assert_request_error("non-finite JSON number")

    def test_tree_digest_output_slot_actions_timeout_and_rng_are_strict(self) -> None:
        cases = (
            ("fixture_manifest_sha256", "A" * 64, "lowercase SHA-256"),
            ("fixture_tree_sha256", "A" * 64, "lowercase SHA-256"),
            ("output_slot", "K", "uppercase Classic slot"),
            ("timeout_seconds", True, "finite number"),
            ("rng_seed", "0" * 15, "lowercase hexadecimal"),
        )
        for field, replacement, expected in cases:
            with self.subTest(field=field):
                value = copy.deepcopy(self.request)
                value[field] = replacement
                self.write_request(value)
                self.assert_request_error(expected)

        invalid_action = copy.deepcopy(self.request)
        invalid_action["actions"][0]["ordinal"] = 1
        self.write_request(invalid_action)
        self.assert_request_error("zero-based array index")

    def test_native_v1_action_rejections_precede_fixture_and_workspace_work(
        self,
    ) -> None:
        cases = (
            (
                "v2 selection kind",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": 2},
                },
                ".kind must be move_party",
            ),
            (
                "unsupported kind",
                {
                    "ordinal": 0,
                    "kind": "probe",
                    "arguments": {"command": "north"},
                },
                ".kind must be move_party",
            ),
            (
                "missing command",
                {"ordinal": 0, "kind": "move_party", "arguments": {}},
                "must contain exactly the command field",
            ),
            (
                "wrong direction argument name",
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"direction": "north"},
                },
                "must contain exactly the command field",
            ),
            (
                "extra argument",
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"command": "north", "repeat": True},
                },
                "must contain exactly the command field",
            ),
            (
                "non-string command",
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"command": 1},
                },
                ".command must be a string",
            ),
            (
                "unsupported command",
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"command": "up"},
                },
                "not a supported native v1 movement command",
            ),
            (
                "noncontiguous ordinal",
                {
                    "ordinal": 1,
                    "kind": "move_party",
                    "arguments": {"command": "north"},
                },
                "zero-based array index",
            ),
        )
        for name, action, expected in cases:
            with self.subTest(name=name):
                value = copy.deepcopy(self.request)
                value["actions"] = [action]
                self.write_request(value)
                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.run_request_file(self.request_path)
                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertIn(expected, raised.exception.message)
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_native_v2_mixed_actions_and_member_boundaries_are_closed(self) -> None:
        valid = copy.deepcopy(self.request)
        valid["schema_version"] = 2
        valid["actions"] = [
            {
                "ordinal": 0,
                "kind": "move_party",
                "arguments": {"command": "southwest"},
            },
            {
                "ordinal": 1,
                "kind": "select_party_member",
                "arguments": {"member": 0},
            },
            {
                "ordinal": 2,
                "kind": "select_party_member",
                "arguments": {"member": 5},
            },
        ]
        self.write_request(valid)
        parsed = gate.load_request(self.request_path)
        self.assertEqual(parsed.schema_version, 2)
        self.assertEqual(
            [action.kind for action in parsed.actions],
            ["move_party", "select_party_member", "select_party_member"],
        )

        invalid_cases = (
            (
                "v3 weapon switch kind",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": 2},
                },
                "must be move_party or select_party_member",
            ),
            (
                "unknown kind",
                {"ordinal": 0, "kind": "probe", "arguments": {"member": 2}},
                "must be move_party or select_party_member",
            ),
            (
                "missing member",
                {"ordinal": 0, "kind": "select_party_member", "arguments": {}},
                "exactly the member field",
            ),
            (
                "wrong member key",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"index": 2},
                },
                "exactly the member field",
            ),
            (
                "extra member argument",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": 2, "repeat": True},
                },
                "exactly the member field",
            ),
            (
                "boolean member",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": True},
                },
                "plain integer from 0 through 5",
            ),
            (
                "string member",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": "2"},
                },
                "plain integer from 0 through 5",
            ),
            (
                "negative member",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": -1},
                },
                "plain integer from 0 through 5",
            ),
            (
                "member above range",
                {
                    "ordinal": 0,
                    "kind": "select_party_member",
                    "arguments": {"member": 6},
                },
                "plain integer from 0 through 5",
            ),
        )
        for name, action, expected in invalid_cases:
            with self.subTest(name=name):
                invalid = copy.deepcopy(self.request)
                invalid["schema_version"] = 2
                invalid["actions"] = [action]
                self.write_request(invalid)
                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.run_request_file(self.request_path)
                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertEqual(raised.exception.schema_version, 2)
                self.assertEqual(
                    gate._error_envelope(raised.exception)["schema_version"], 2
                )
                self.assertIn(expected, raised.exception.message)
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_native_v3_mixed_actions_and_combatant_boundaries_are_closed(self) -> None:
        valid = copy.deepcopy(self.request)
        valid["schema_version"] = 3
        valid["actions"] = [
            {
                "ordinal": 0,
                "kind": "move_party",
                "arguments": {"command": "northeast"},
            },
            {
                "ordinal": 1,
                "kind": "select_party_member",
                "arguments": {"member": 5},
            },
            {
                "ordinal": 2,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 0},
            },
            {
                "ordinal": 3,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 5},
            },
        ]
        self.write_request(valid)
        parsed = gate.load_request(self.request_path)
        self.assertEqual(parsed.schema_version, 3)
        self.assertEqual(
            [action.kind for action in parsed.actions],
            [
                "move_party",
                "select_party_member",
                "switch_weapon_set",
                "switch_weapon_set",
            ],
        )

        invalid_cases = (
            (
                "unknown kind",
                {"ordinal": 0, "kind": "probe", "arguments": {"combatant": 2}},
                "must be move_party, select_party_member, or switch_weapon_set",
            ),
            (
                "missing combatant",
                {"ordinal": 0, "kind": "switch_weapon_set", "arguments": {}},
                "exactly the combatant field",
            ),
            (
                "wrong combatant key",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"member": 2},
                },
                "exactly the combatant field",
            ),
            (
                "extra combatant argument",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": 2, "repeat": True},
                },
                "exactly the combatant field",
            ),
            (
                "boolean combatant",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": True},
                },
                "plain integer from 0 through 5",
            ),
            (
                "floating combatant",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": 2.0},
                },
                "string, integer, or boolean",
            ),
            (
                "string combatant",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": "2"},
                },
                "plain integer from 0 through 5",
            ),
            (
                "negative combatant",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": -1},
                },
                "plain integer from 0 through 5",
            ),
            (
                "combatant above range",
                {
                    "ordinal": 0,
                    "kind": "switch_weapon_set",
                    "arguments": {"combatant": 6},
                },
                "plain integer from 0 through 5",
            ),
        )
        for name, action, expected in invalid_cases:
            with self.subTest(name=name):
                invalid = copy.deepcopy(self.request)
                invalid["schema_version"] = 3
                invalid["actions"] = [action]
                self.write_request(invalid)
                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.run_request_file(self.request_path)
                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertEqual(raised.exception.schema_version, 3)
                self.assertEqual(
                    gate._error_envelope(raised.exception)["schema_version"], 3
                )
                self.assertIn(expected, raised.exception.message)
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_direct_v2_request_cannot_bypass_closed_action_validation(self) -> None:
        value = copy.deepcopy(self.request)
        value["schema_version"] = 2
        value["actions"] = [
            {
                "ordinal": 0,
                "kind": "select_party_member",
                "arguments": {"member": 2},
            }
        ]
        self.write_request(value)
        parsed = gate.load_request(self.request_path)
        invalid = replace(
            parsed,
            actions=(
                gate.replay_runner.Action(
                    ordinal=0,
                    kind="select_party_member",
                    arguments={"member": True},
                ),
            ),
        )

        with mock.patch.object(
            gate.fixture_tool,
            "verify_fixture",
            side_effect=AssertionError("fixture must not be inspected"),
        ) as verify_fixture, mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("workspace must not be created"),
        ) as make_workspace:
            with self.assertRaises(gate.EquivalenceGateError) as raised:
                gate.inspect_profile(invalid)

        self.assertEqual(raised.exception.code, "request.invalid")
        self.assertEqual(raised.exception.schema_version, 2)
        self.assertIn("plain integer", raised.exception.message)
        verify_fixture.assert_not_called()
        make_workspace.assert_not_called()

    def test_direct_v3_request_cannot_bypass_closed_action_validation(self) -> None:
        value = copy.deepcopy(self.request)
        value["schema_version"] = 3
        value["actions"] = [
            {
                "ordinal": 0,
                "kind": "switch_weapon_set",
                "arguments": {"combatant": 2},
            }
        ]
        self.write_request(value)
        parsed = gate.load_request(self.request_path)

        for arguments, expected in (
            ({"combatant": True}, "plain integer"),
            ({"combatant": 2.0}, "plain integer"),
            ({"combatant": 6}, "plain integer"),
            ({"combatant": 2, "member": 2}, "exactly the combatant field"),
        ):
            with self.subTest(arguments=arguments):
                invalid = replace(
                    parsed,
                    actions=(
                        gate.replay_runner.Action(
                            ordinal=0,
                            kind="switch_weapon_set",
                            arguments=arguments,
                        ),
                    ),
                )

                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.inspect_profile(invalid)

                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertEqual(raised.exception.schema_version, 3)
                self.assertIn(expected, raised.exception.message)
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_direct_request_value_cannot_bypass_native_v1_action_check(self) -> None:
        self.write_request()
        parsed = gate.load_request(self.request_path)
        invalid = replace(
            parsed,
            actions=(
                gate.replay_runner.Action(
                    ordinal=0,
                    kind="probe",
                    arguments={"command": "north"},
                ),
            ),
        )
        with mock.patch.object(
            gate.fixture_tool,
            "verify_fixture",
            side_effect=AssertionError("fixture must not be inspected"),
        ) as verify_fixture, mock.patch.object(
            gate,
            "_make_gate_workspace",
            side_effect=AssertionError("workspace must not be created"),
        ) as make_workspace:
            with self.assertRaises(gate.EquivalenceGateError) as raised:
                gate.run_request(invalid)
        self.assertEqual(raised.exception.code, "request.invalid")
        self.assertIn(".kind must be move_party", raised.exception.message)
        verify_fixture.assert_not_called()
        make_workspace.assert_not_called()

    def test_direct_request_value_revalidates_every_independent_field(self) -> None:
        parsed = gate.load_request(self.request_path)
        cases = (
            (replace(parsed, manifest=Path("relative.json")), "manifest must be"),
            (replace(parsed, source_root=str(self.source)), "source_root must be"),
            (
                replace(parsed, fixture_manifest_sha256="A" * 64),
                "fixture_manifest_sha256",
            ),
            (
                replace(parsed, fixture_tree_sha256="A" * 64),
                "fixture_tree_sha256",
            ),
            (
                replace(parsed, executable=self.root / "missing-executable"),
                "executable does not exist",
            ),
            (replace(parsed, output_slot="K"), "output_slot must be"),
            (replace(parsed, actions=list(parsed.actions)), "normalized tuple"),
            (replace(parsed, timeout_seconds=True), "finite number"),
            (replace(parsed, rng_seed="0" * 15), "rng_seed"),
            (replace(parsed, rng_stream="0" * 15), "rng_stream"),
        )
        for invalid, expected in cases:
            with self.subTest(expected=expected):
                with mock.patch.object(
                    gate.fixture_tool,
                    "verify_fixture",
                    side_effect=AssertionError("fixture must not be inspected"),
                ) as verify_fixture, mock.patch.object(
                    gate,
                    "_make_gate_workspace",
                    side_effect=AssertionError("workspace must not be created"),
                ) as make_workspace:
                    with self.assertRaises(gate.EquivalenceGateError) as raised:
                        gate.inspect_profile(invalid)
                self.assertEqual(raised.exception.code, "request.invalid")
                self.assertIn(expected, raised.exception.message)
                verify_fixture.assert_not_called()
                make_workspace.assert_not_called()

    def test_zero_and_maximum_action_profiles_parse_without_staging(self) -> None:
        empty = copy.deepcopy(self.request)
        empty["actions"] = []
        self.write_request(empty)
        self.assertEqual(gate.load_request(self.request_path).actions, ())

        maximum = copy.deepcopy(self.request)
        maximum["actions"] = [
            {
                "ordinal": index,
                "kind": gate.NATIVE_V1_ACTION_KIND,
                "arguments": {
                    gate.NATIVE_V1_ACTION_ARGUMENT: gate.NATIVE_V1_MOVEMENT_COMMANDS[
                        index % len(gate.NATIVE_V1_MOVEMENT_COMMANDS)
                    ]
                },
            }
            for index in range(gate.replay_runner.MAX_ACTIONS)
        ]
        self.write_request(maximum)
        parsed = gate.load_request(self.request_path)
        self.assertEqual(len(parsed.actions), gate.replay_runner.MAX_ACTIONS)

    def test_action_profile_digest_has_a_pinned_canonical_protocol_vector(self) -> None:
        self.write_request()
        actions = gate.load_request(self.request_path).actions
        canonical = (
            b'[{"arguments":{"command":"north"},"kind":"move_party","ordinal":0}]'
        )
        expected = hashlib.sha256(
            b"realmz-semantic-replay-actions-v1\n" + canonical
        ).hexdigest()
        self.assertEqual(
            expected,
            "7dd7082ddb92f2d93c904a23b408c271376e92ebf67e94e2ff366c197a183ae3",
        )
        self.assertEqual(gate._actions_sha256(actions), expected)

        envelope_schema = json.loads(
            ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        description = envelope_schema["$defs"]["replay_profile"]["properties"][
            "actions_sha256"
        ]["description"]
        self.assertIn("realmz-semantic-replay-actions-v1", description)
        self.assertIn("RFC 8785", description)

    def test_v2_member_two_digest_has_a_pinned_canonical_protocol_vector(self) -> None:
        action = gate.replay_runner.Action(
            ordinal=0,
            kind="select_party_member",
            arguments={"member": 2},
        )
        canonical = (
            b'[{"arguments":{"member":2},"kind":"select_party_member","ordinal":0}]'
        )
        expected = hashlib.sha256(
            b"realmz-semantic-replay-actions-v2\n" + canonical
        ).hexdigest()
        self.assertEqual(
            expected,
            "65823e0696865fefb0eee51795f5edcc9d80eeaf4d656e4936b8f8425262c896",
        )
        self.assertEqual(gate._actions_sha256((action,), 2), expected)

        value = copy.deepcopy(self.request)
        value["schema_version"] = 2
        value["actions"] = [action.as_json()]
        self.write_request(value)
        profile = gate.inspect_profile_file(self.request_path)
        self.assertEqual(profile["schema_version"], 2)
        self.assertEqual(profile["actions_sha256"], expected)

    def test_v3_combatant_two_digest_has_a_pinned_canonical_protocol_vector(
        self,
    ) -> None:
        action = gate.replay_runner.Action(
            ordinal=0,
            kind="switch_weapon_set",
            arguments={"combatant": 2},
        )
        canonical = (
            b'[{"arguments":{"combatant":2},"kind":"switch_weapon_set","ordinal":0}]'
        )
        expected = hashlib.sha256(
            b"realmz-semantic-replay-actions-v3\n" + canonical
        ).hexdigest()
        self.assertEqual(
            expected,
            "36db8be0cd9bde18ec4ca33e26bd0343971796d0baf179b36599f1ab58747121",
        )
        self.assertEqual(gate._actions_sha256((action,), 3), expected)

        value = copy.deepcopy(self.request)
        value["schema_version"] = 3
        value["actions"] = [action.as_json()]
        self.write_request(value)
        profile = gate.inspect_profile_file(self.request_path)
        self.assertEqual(profile["schema_version"], 3)
        self.assertEqual(profile["actions_sha256"], expected)

    def test_request_schema_matches_native_v1_runtime_action_contract(self) -> None:
        request_schema = json.loads(REQUEST_SCHEMA_PATH.read_text(encoding="utf-8"))
        actions_schema = request_schema["properties"]["actions"]
        self.assertEqual(actions_schema["maxItems"], gate.replay_runner.MAX_ACTIONS)
        action_schema = request_schema["$defs"]["action"]
        self.assertIs(action_schema["additionalProperties"], False)
        self.assertEqual(
            set(action_schema["required"]), gate.replay_runner.ACTION_FIELDS
        )
        self.assertEqual(
            set(action_schema["properties"]), gate.replay_runner.ACTION_FIELDS
        )

        properties = action_schema["properties"]
        self.assertEqual(
            properties["kind"], {"const": gate.NATIVE_V1_ACTION_KIND}
        )
        self.assertEqual(properties["ordinal"]["minimum"], 0)
        self.assertEqual(properties["ordinal"]["type"], "integer")
        self.assertEqual(
            properties["ordinal"]["maximum"], gate.replay_runner.MAX_ACTIONS - 1
        )

        arguments_schema = properties["arguments"]
        self.assertIs(arguments_schema["additionalProperties"], False)
        self.assertEqual(
            arguments_schema["required"], [gate.NATIVE_V1_ACTION_ARGUMENT]
        )
        self.assertEqual(
            set(arguments_schema["properties"]), {gate.NATIVE_V1_ACTION_ARGUMENT}
        )
        command_schema = arguments_schema["properties"][
            gate.NATIVE_V1_ACTION_ARGUMENT
        ]
        self.assertEqual(command_schema["type"], "string")
        self.assertEqual(
            tuple(command_schema["enum"]), gate.NATIVE_V1_MOVEMENT_COMMANDS
        )

        value = copy.deepcopy(self.request)
        value["actions"] = [
            {
                "ordinal": index,
                "kind": gate.NATIVE_V1_ACTION_KIND,
                "arguments": {gate.NATIVE_V1_ACTION_ARGUMENT: command},
            }
            for index, command in enumerate(gate.NATIVE_V1_MOVEMENT_COMMANDS)
        ]
        self.write_request(value)
        parsed = gate.load_request(self.request_path)
        self.assertEqual(
            tuple(
                action.arguments[gate.NATIVE_V1_ACTION_ARGUMENT]
                for action in parsed.actions
            ),
            gate.NATIVE_V1_MOVEMENT_COMMANDS,
        )

    def test_v2_schemas_bind_the_closed_vocabulary_and_nested_runner_v2(self) -> None:
        request_schema = json.loads(
            V2_REQUEST_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        envelope_schema = json.loads(
            V2_ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        profile_schema = json.loads(
            V2_PROFILE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        for schema in (request_schema, envelope_schema, profile_schema):
            self.assertEqual(schema["properties"]["schema_version"]["const"], 2)
            self.assertIs(schema["additionalProperties"], False)

        action_variants = request_schema["$defs"]["action"]["oneOf"]
        self.assertEqual(
            [variant["properties"]["kind"]["const"] for variant in action_variants],
            ["move_party", "select_party_member"],
        )
        member = action_variants[1]["properties"]["arguments"]["properties"][
            "member"
        ]
        self.assertEqual(member, {"maximum": 5, "minimum": 0, "type": "integer"})
        self.assertEqual(
            envelope_schema["properties"]["runner_envelope"]["$ref"],
            "https://realmz-castle.github.io/schemas/semantic-replay-run-envelope-v2.json",
        )
        self.assertEqual(
            envelope_schema["$defs"]["comparison"]["properties"]["contract"][
                "const"
            ],
            "realmz.semantic-replay.exact.v2",
        )
        digest_description = envelope_schema["$defs"]["replay_profile"][
            "properties"
        ]["actions_sha256"]["description"]
        self.assertIn("realmz-semantic-replay-actions-v2", digest_description)

    def test_v3_schemas_bind_the_closed_vocabulary_and_nested_runner_v3(self) -> None:
        request_schema = json.loads(
            V3_REQUEST_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        envelope_schema = json.loads(
            V3_ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        profile_schema = json.loads(
            V3_PROFILE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        for schema in (request_schema, envelope_schema, profile_schema):
            self.assertEqual(schema["properties"]["schema_version"]["const"], 3)
            self.assertIs(schema["additionalProperties"], False)

        action_variants = request_schema["$defs"]["action"]["oneOf"]
        self.assertEqual(
            [variant["properties"]["kind"]["const"] for variant in action_variants],
            ["move_party", "select_party_member", "switch_weapon_set"],
        )
        switch_arguments = action_variants[2]["properties"]["arguments"]
        self.assertIs(switch_arguments["additionalProperties"], False)
        self.assertEqual(switch_arguments["required"], ["combatant"])
        self.assertEqual(
            switch_arguments["properties"],
            {
                "combatant": {
                    "maximum": 5,
                    "minimum": 0,
                    "type": "integer",
                }
            },
        )
        self.assertEqual(
            envelope_schema["properties"]["runner_envelope"]["$ref"],
            "https://realmz-castle.github.io/schemas/semantic-replay-run-envelope-v3.json",
        )
        self.assertEqual(
            envelope_schema["$defs"]["comparison"]["properties"]["contract"][
                "const"
            ],
            "realmz.semantic-replay.exact.v3",
        )
        digest_description = envelope_schema["$defs"]["replay_profile"][
            "properties"
        ]["actions_sha256"]["description"]
        self.assertIn("realmz-semantic-replay-actions-v3", digest_description)

    def test_all_seven_v3_schema_ids_and_reference_targets_are_exact(self) -> None:
        expected_ids = {
            "semantic-replay-child-config-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-child-config-v3.json"
            ),
            "semantic-replay-child-result-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-child-result-v3.json"
            ),
            "semantic-replay-equivalence-envelope-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-equivalence-envelope-v3.json"
            ),
            "semantic-replay-equivalence-profile-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-equivalence-profile-v3.json"
            ),
            "semantic-replay-equivalence-request-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-equivalence-request-v3.json"
            ),
            "semantic-replay-run-envelope-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-run-envelope-v3.json"
            ),
            "semantic-replay-run-request-v3.schema.json": (
                "https://realmz-castle.github.io/schemas/"
                "semantic-replay-run-request-v3.json"
            ),
        }
        schemas = {
            name: json.loads(
                (Path(__file__).with_name(name)).read_text(encoding="utf-8")
            )
            for name in expected_ids
        }
        draft = "https://json-schema.org/draft/2020-12/schema"
        for name, expected_id in expected_ids.items():
            with self.subTest(name=name):
                self.assertEqual(schemas[name]["$schema"], draft)
                self.assertEqual(schemas[name]["$id"], expected_id)

        def collect_refs(value: object) -> list[str]:
            if isinstance(value, dict):
                refs = [value["$ref"]] if isinstance(value.get("$ref"), str) else []
                for child in value.values():
                    refs.extend(collect_refs(child))
                return refs
            if isinstance(value, list):
                refs = []
                for child in value:
                    refs.extend(collect_refs(child))
                return refs
            return []

        expected_external_refs = {
            "semantic-replay-child-config-v3.schema.json": set(),
            "semantic-replay-child-result-v3.schema.json": set(),
            "semantic-replay-equivalence-envelope-v3.schema.json": {
                expected_ids["semantic-replay-run-envelope-v3.schema.json"]
            },
            "semantic-replay-equivalence-profile-v3.schema.json": set(),
            "semantic-replay-equivalence-request-v3.schema.json": set(),
            "semantic-replay-run-envelope-v3.schema.json": {
                expected_ids["semantic-replay-child-result-v3.schema.json"]
            },
            "semantic-replay-run-request-v3.schema.json": set(),
        }
        known_ids = set(expected_ids.values())
        for name, schema in schemas.items():
            refs = collect_refs(schema)
            external_refs = {ref for ref in refs if not ref.startswith("#/")}
            with self.subTest(name=name, reference_kind="external"):
                self.assertEqual(external_refs, expected_external_refs[name])
                self.assertTrue(external_refs <= known_ids)
            local_refs = {ref for ref in refs if ref.startswith("#/")}
            with self.subTest(name=name, reference_kind="local-shape"):
                self.assertTrue(
                    all(ref.startswith("#/$defs/") for ref in local_refs)
                )
            for ref in local_refs:
                definition = ref[len("#/$defs/") :]
                with self.subTest(
                    name=name,
                    reference_kind="local",
                    reference=ref,
                ):
                    self.assertIn(definition, schema.get("$defs", {}))

    def test_new_schemas_are_v1_closed_and_keep_runner_v1_nested(self) -> None:
        request_schema = json.loads(REQUEST_SCHEMA_PATH.read_text(encoding="utf-8"))
        envelope_schema = json.loads(ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8"))
        profile_schema = json.loads(PROFILE_SCHEMA_PATH.read_text(encoding="utf-8"))
        for schema in (request_schema, envelope_schema, profile_schema):
            self.assertEqual(
                schema["$schema"], "https://json-schema.org/draft/2020-12/schema"
            )
            self.assertIs(schema["additionalProperties"], False)
            self.assertEqual(schema["properties"]["schema_version"]["const"], 1)
        self.assertEqual(set(request_schema["required"]), gate.REQUEST_FIELDS)
        self.assertEqual(
            set(profile_schema["required"]), gate.PROFILE_INSPECTION_FIELDS
        )
        self.assertEqual(
            profile_schema["properties"]["status"]["const"],
            "profile_inspected",
        )
        completed_required = set(envelope_schema["required"]) | set(
            envelope_schema["oneOf"][0]["required"]
        )
        self.assertEqual(completed_required, gate.COMPLETED_ENVELOPE_FIELDS)
        self.assertEqual(
            envelope_schema["properties"]["runner_envelope"]["$ref"],
            "https://realmz-castle.github.io/schemas/semantic-replay-run-envelope-v1.json",
        )
        mismatch_values = envelope_schema["$defs"][
            "canonical_mismatched_fields"
        ]["enum"]
        self.assertTrue(mismatch_values)
        self.assertTrue(
            all("settled_action_count" not in value for value in mismatch_values)
        )

    def test_error_and_retention_messages_are_bounded_to_the_schema_limit(self) -> None:
        error = gate.EquivalenceGateError(
            "gate.synthetic_error",
            "x" * (gate.MAX_ERROR_MESSAGE_LENGTH + 100),
            gate.EXIT_INTERNAL,
        )
        error.runner_workspace_retention = {
            "cleanup_attempted": False,
            "candidate_path": "/tmp/synthetic-runner",
            "path_authoritative": True,
            "notice": "y" * (gate.MAX_ERROR_MESSAGE_LENGTH + 100),
        }
        envelope = gate._error_envelope(error)
        self.assertEqual(
            len(envelope["error"]["message"]), gate.MAX_ERROR_MESSAGE_LENGTH
        )
        self.assertEqual(
            len(envelope["runner_workspace_retention"]["notice"]),
            gate.MAX_ERROR_MESSAGE_LENGTH,
        )


if __name__ == "__main__":
    unittest.main()
