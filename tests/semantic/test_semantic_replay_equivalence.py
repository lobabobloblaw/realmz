#!/usr/bin/env python3
"""Synthetic-only tests for the provenance-bound live equivalence gate."""

from __future__ import annotations

import copy
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
    "schema_version": 1,
    "run_id": config["run_id"],
    "child_nonce": config["child_nonce"],
    "replay_route": route,
    "presentation_mode": config["presentation_mode"],
    "process_id": os.getpid(),
    "status": "completed",
    "engine_identity": "synthetic-live-gate-child-v1",
    "settled_action_count": len(config["actions"]),
    "state_sha256": "1" * 64,
    "save_tree_sha256": "2" * 64,
    "rng_draw_count": 7,
    "rng_seed": config["rng_seed"],
    "rng_stream": config["rng_stream"],
}
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
            "fixture_tree_sha256": self.manifest_value["tree_sha256"],
            "executable": str(self.executable),
            "output_slot": "B",
            "actions": [
                {
                    "ordinal": 0,
                    "kind": "move_party",
                    "arguments": {"direction": "north"},
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
        self.assertEqual(fixture["tree_sha256"], self.request["fixture_tree_sha256"])
        self.assertEqual(fixture["slot"], "A")
        self.assertEqual(fixture["file_count"], len(self.files))
        self.assertEqual(
            fixture["total_file_bytes"], sum(map(len, self.files.values()))
        )
        self.assertEqual(len(fixture["manifest_sha256"]), 64)
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

        fixture_retention = envelope["fixture_workspace_retention"]
        runner_retention = runner["workspace_retention"]
        for retention in (fixture_retention, runner_retention):
            self.assertIs(retention["cleanup_attempted"], False)
            self.assertIs(retention["path_authoritative"], True)
            path = Path(retention["candidate_path"])
            self.assertTrue(path.is_dir())
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)

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

        def replace_before_stage(*arguments: object) -> dict[str, object]:
            for relative, contents in replacement_files.items():
                (self.source / relative).write_bytes(contents)
            self.write_json(self.manifest, replacement_manifest)
            return stage_fixture(*arguments)

        with mock.patch.object(
            gate.fixture_tool,
            "stage_fixture",
            side_effect=replace_before_stage,
        ), mock.patch.object(
            gate.replay_runner,
            "run_request",
            side_effect=AssertionError("children must not run for a changed fixture"),
        ) as run_children:
            error = self.assert_gate_error("fixture.attestation_failed")
        run_children.assert_not_called()
        self.assertIn("before child execution", error.message)
        self.assertIsNotNone(error.fixture_workspace_retention)
        self.assertIsNone(error.runner_workspace_retention)

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
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["status"], "error")

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

    def test_zero_and_maximum_action_profiles_parse_without_staging(self) -> None:
        empty = copy.deepcopy(self.request)
        empty["actions"] = []
        self.write_request(empty)
        self.assertEqual(gate.load_request(self.request_path).actions, ())

        maximum = copy.deepcopy(self.request)
        maximum["actions"] = [
            {"ordinal": index, "kind": "probe", "arguments": {}}
            for index in range(gate.replay_runner.MAX_ACTIONS)
        ]
        self.write_request(maximum)
        parsed = gate.load_request(self.request_path)
        self.assertEqual(len(parsed.actions), gate.replay_runner.MAX_ACTIONS)

    def test_action_profile_digest_has_a_pinned_canonical_protocol_vector(self) -> None:
        self.write_request()
        actions = gate.load_request(self.request_path).actions
        canonical = (
            b'[{"arguments":{"direction":"north"},"kind":"move_party","ordinal":0}]'
        )
        expected = hashlib.sha256(
            b"realmz-semantic-replay-actions-v1\n" + canonical
        ).hexdigest()
        self.assertEqual(gate._actions_sha256(actions), expected)

        envelope_schema = json.loads(
            ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8")
        )
        description = envelope_schema["$defs"]["replay_profile"]["properties"][
            "actions_sha256"
        ]["description"]
        self.assertIn("realmz-semantic-replay-actions-v1", description)
        self.assertIn("RFC 8785", description)

    def test_new_schemas_are_v1_closed_and_keep_runner_v1_nested(self) -> None:
        request_schema = json.loads(REQUEST_SCHEMA_PATH.read_text(encoding="utf-8"))
        envelope_schema = json.loads(ENVELOPE_SCHEMA_PATH.read_text(encoding="utf-8"))
        for schema in (request_schema, envelope_schema):
            self.assertEqual(
                schema["$schema"], "https://json-schema.org/draft/2020-12/schema"
            )
            self.assertIs(schema["additionalProperties"], False)
            self.assertEqual(schema["properties"]["schema_version"]["const"], 1)
        self.assertEqual(set(request_schema["required"]), gate.REQUEST_FIELDS)
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
