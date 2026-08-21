#!/usr/bin/env python3
"""Synthetic-only tests for the process-isolated semantic replay parent."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts/semantic_replay_runner.py"
SCHEMA_NAMES = (
    "semantic-replay-run-request.schema.json",
    "semantic-replay-child-config.schema.json",
    "semantic-replay-child-result.schema.json",
    "semantic-replay-run-envelope.schema.json",
)

SPEC = importlib.util.spec_from_file_location("semantic_replay_runner", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:  # pragma: no cover - import guard
    raise RuntimeError(f"cannot import {SCRIPT_PATH}")
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


FAKE_CHILD = r'''#!__PYTHON__
import json
import os
from pathlib import Path
import sys
import time

if len(sys.argv) != 3 or sys.argv[1] != "--semantic-replay-child":
    raise SystemExit(97)
config_path = Path(sys.argv[2])
config = json.loads(config_path.read_text(encoding="utf-8"))
log_path = os.environ.get("REALMZ_REPLAY_FAKE_LOG")
if log_path:
    with open(log_path, "a", encoding="utf-8") as stream:
        stream.write(json.dumps({
            "pid": os.getpid(),
            "argv": sys.argv,
            "executable": str(Path(sys.argv[0]).resolve()),
            "cwd": str(Path.cwd()),
            "config_path": str(config_path),
            "config": config,
        }, sort_keys=True) + "\n")

behavior = os.environ.get("REALMZ_REPLAY_FAKE_BEHAVIOR", "ok")
behavior_route = os.environ.get("REALMZ_REPLAY_FAKE_ROUTE")
if behavior_route and behavior_route != config["replay_route"]:
    behavior = "ok"

if behavior == "timeout":
    time.sleep(10)
if behavior == "nonzero":
    print("synthetic child failure", file=sys.stderr)
    raise SystemExit(23)
if behavior == "oversized_output":
    os.write(1, b"x" * 70000)
if behavior == "alias_semantic_root":
    target = Path(os.environ["REALMZ_REPLAY_FAKE_ALIAS_TARGET"])
    target.rename(target.with_name(target.name + "-moved"))
    target.symlink_to(Path(config["user_data_root"]), target_is_directory=True)
if behavior == "create_root_working_directory":
    (Path(config["user_data_root"]) / "Data Files").mkdir()
if behavior == "mutate_shared_ancestor":
    shared_sibling = Path(os.environ["REALMZ_REPLAY_FAKE_SHARED_SIBLING"])
    (shared_sibling / config["replay_route"]).mkdir(parents=True)

input_path = Path(config["user_data_root"]) / "Save" / f"Game {config['input_slot']}"
output_path = Path(config["user_data_root"]) / "Save" / f"Game {config['output_slot']}"
if behavior == "symlink_output":
    output_path.symlink_to(input_path, target_is_directory=True)
elif behavior != "missing_output":
    output_path.mkdir()
if behavior == "missing":
    raise SystemExit(0)

result_path = Path(config["result_path"])
if behavior == "malformed":
    result_path.write_text("{", encoding="utf-8")
    os.chmod(result_path, 0o600)
    raise SystemExit(0)
if behavior == "nested_result":
    result_path.write_text("[" * 500000 + "]" * 500000, encoding="utf-8")
    os.chmod(result_path, 0o600)
    raise SystemExit(0)
if behavior == "fifo_result":
    os.mkfifo(result_path, 0o600)
    raise SystemExit(0)
if behavior == "symlink_result":
    payload = result_path.with_name("payload.json")
    payload.write_text("{}", encoding="utf-8")
    result_path.symlink_to(payload)
    raise SystemExit(0)

result = {
    "schema_version": 1,
    "run_id": config["run_id"],
    "child_nonce": config["child_nonce"],
    "replay_route": config["replay_route"],
    "presentation_mode": config["presentation_mode"],
    "process_id": os.getpid(),
    "status": "completed",
    "engine_identity": "synthetic-fake-child",
    "settled_action_count": len(config["actions"]),
    "state_sha256": "1" * 64,
    "save_tree_sha256": "2" * 64,
    "rng_draw_count": 7,
    "rng_seed": config["rng_seed"],
    "rng_stream": config["rng_stream"],
}
if behavior == "stale_nonce":
    result["child_nonce"] = "0" * 32
if behavior == "stale_run":
    result["run_id"] = "0" * 32
if behavior == "wrong_pid":
    result["process_id"] = os.getpid() + 1
if behavior == "wrong_route":
    result["replay_route"] = (
        "semantic" if config["replay_route"] == "classic" else "classic"
    )
if behavior == "wrong_presentation":
    result["presentation_mode"] = "remastered" if config["presentation_mode"] == "classic" else "classic"
if behavior == "unknown_result_field":
    result["equivalent"] = True
if behavior == "engine_mismatch":
    result["engine_identity"] = "synthetic-different-engine"
result_path.write_text(json.dumps(result, sort_keys=True) + "\n", encoding="utf-8")
os.chmod(result_path, 0o600)
if behavior == "public_result":
    os.chmod(result_path, 0o644)
if behavior == "hardlink_config":
    os.link(config_path, config_path.with_name("config-hardlink.json"))
if behavior == "public_workspace":
    os.chmod(config_path.parent, 0o755)
if behavior == "mutate_config":
    config_path.write_text("{}\n", encoding="utf-8")
if behavior == "mutate_executable":
    os.utime(sys.argv[0], None)
if behavior == "swap_workspace":
    workspace = config_path.parent.parent
    moved = workspace.with_name(workspace.name + "-moved")
    victim = Path(os.environ["REALMZ_REPLAY_FAKE_VICTIM"])
    workspace.rename(moved)
    victim.rename(workspace)
raise SystemExit(0)
'''


class ReplayRunnerTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name).resolve()
        self.classic_root = self.root / "classic-user-root"
        self.semantic_root = self.root / "semantic-user-root"
        self.classic_root.mkdir()
        self.semantic_root.mkdir()
        (self.classic_root / "Save" / "Game A").mkdir(parents=True)
        (self.semantic_root / "Save" / "Game A").mkdir(parents=True)
        self.executable_directory = self.root / "bin"
        self.executable_directory.mkdir()
        self.fake_executable = self.executable_directory / "realmz"
        self.write_fake_executable(self.fake_executable)
        self.log_path = self.root / "invocations.jsonl"
        self.log_path.write_text("", encoding="utf-8")
        self.request_path = self.root / "request.json"
        self.request = {
            "schema_version": 1,
            "executable": str(self.fake_executable),
            "classic_user_data_root": str(self.classic_root),
            "semantic_user_data_root": str(self.semantic_root),
            "input_slot": "A",
            "output_slot": "B",
            "actions": [
                {
                    "ordinal": 0,
                    "kind": "synthetic_probe",
                    "arguments": {
                        "actor_id": 4,
                        "enabled": True,
                        "label": "fixture",
                    },
                }
            ],
            "timeout_seconds": 2,
            "rng_seed": "0123456789abcdef",
            "rng_stream": "fedcba9876543210",
        }
        self.write_request()

    def write_fake_executable(
        self, path: Path, *, startup_marker: Path | None = None
    ) -> None:
        source = FAKE_CHILD.replace("__PYTHON__", sys.executable)
        if startup_marker is not None:
            needle = 'config = json.loads(config_path.read_text(encoding="utf-8"))\n'
            source = source.replace(
                needle,
                needle
                + f"Path({str(startup_marker)!r}).write_text("
                '"started\\n", encoding="utf-8")\n',
                1,
            )
        path.write_text(textwrap.dedent(source), encoding="utf-8")
        path.chmod(0o700)

    def write_request(self, value: object | None = None) -> None:
        if value is None:
            value = self.request
        self.request_path.write_text(
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def run_parent(self) -> dict[str, object]:
        with mock.patch.dict(
            os.environ,
            {"REALMZ_REPLAY_FAKE_LOG": str(self.log_path)},
            clear=False,
        ):
            envelope = runner.run_request_file(self.request_path)
        self.track_workspace(envelope)
        return envelope

    def track_workspace(self, source: object) -> None:
        if isinstance(source, dict):
            retention = source.get("workspace_retention")
        else:
            retention = getattr(source, "workspace_retention", None)
        if isinstance(retention, dict) and retention.get("path_authoritative") is True:
            self.addCleanup(
                shutil.rmtree,
                Path(str(retention["candidate_path"])),
                True,
            )

    def invocations(self) -> list[dict[str, object]]:
        return [
            json.loads(line)
            for line in self.log_path.read_text(encoding="utf-8").splitlines()
        ]

    def remove_output_slots(self) -> None:
        for user_root in (self.classic_root, self.semantic_root):
            output = user_root / "Save" / "Game B"
            if output.is_symlink():
                output.unlink()
            elif output.exists():
                shutil.rmtree(output)

    def assert_error(
        self, code: str, expected_message: str, *, behavior: str | None = None
    ) -> runner.ReplayRunnerError:
        environment = {"REALMZ_REPLAY_FAKE_LOG": str(self.log_path)}
        if behavior is not None:
            environment["REALMZ_REPLAY_FAKE_BEHAVIOR"] = behavior
        with mock.patch.dict(os.environ, environment, clear=False):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, code)
        self.assertIn(expected_message, raised.exception.message)
        self.track_workspace(raised.exception)
        return raised.exception


class ProcessIsolationTests(ReplayRunnerTestCase):
    def test_runs_same_executable_in_classic_then_semantic_processes(self) -> None:
        envelope = self.run_parent()
        invocations = self.invocations()

        self.assertEqual(len(invocations), 2)
        self.assertEqual(
            [entry["config"]["replay_route"] for entry in invocations],
            ["classic", "semantic"],
        )
        self.assertNotEqual(invocations[0]["pid"], invocations[1]["pid"])
        self.assertEqual(
            {entry["executable"] for entry in invocations},
            {str(self.fake_executable)},
        )
        for entry in invocations:
            self.assertEqual(
                entry["argv"][:2],
                [str(self.fake_executable), "--semantic-replay-child"],
            )
            self.assertEqual(entry["argv"][2], entry["config_path"])

        self.assertEqual(envelope["schema_version"], 1)
        self.assertEqual(envelope["runner_scope"], "process_isolation_only")
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["executable"], str(self.fake_executable))
        results = envelope["child_results"]
        self.assertEqual(
            [result["replay_route"] for result in results],
            ["classic", "semantic"],
        )
        self.assertEqual(
            [result["presentation_mode"] for result in results],
            ["classic", "remastered"],
        )
        self.assertEqual([result["process_id"] for result in results], [entry["pid"] for entry in invocations])
        self.assertEqual({result["run_id"] for result in results}, {envelope["run_id"]})
        self.assertEqual(results[0]["state_sha256"], results[1]["state_sha256"])
        self.assertEqual(results[0]["save_tree_sha256"], results[1]["save_tree_sha256"])
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(envelope["rng_seed"], "0123456789abcdef")
        self.assertEqual(envelope["rng_stream"], "fedcba9876543210")
        retention = envelope["workspace_retention"]
        self.assertIs(retention["cleanup_attempted"], False)
        self.assertIs(retention["path_authoritative"], True)
        self.assertTrue(Path(retention["candidate_path"]).is_dir())

    def test_children_may_create_root_level_working_directories(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"REALMZ_REPLAY_FAKE_BEHAVIOR": "create_root_working_directory"},
            clear=False,
        ):
            envelope = self.run_parent()

        self.assertEqual(len(envelope["child_results"]), 2)
        self.assertTrue((self.classic_root / "Data Files").is_dir())
        self.assertTrue((self.semantic_root / "Data Files").is_dir())

    def test_unrelated_shared_ancestor_activity_does_not_fail_replay(self) -> None:
        shared_sibling = self.root.parent / f"{self.root.name}-unrelated"
        self.addCleanup(shutil.rmtree, shared_sibling, True)
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "mutate_shared_ancestor",
                "REALMZ_REPLAY_FAKE_SHARED_SIBLING": str(shared_sibling),
            },
            clear=False,
        ):
            envelope = self.run_parent()

        self.assertEqual(len(envelope["child_results"]), 2)
        self.assertTrue((shared_sibling / "classic").is_dir())
        self.assertTrue((shared_sibling / "semantic").is_dir())

    def test_child_configs_are_private_distinct_and_route_scoped(self) -> None:
        self.run_parent()
        classic, semantic = self.invocations()
        classic_config = classic["config"]
        semantic_config = semantic["config"]

        self.assertNotEqual(classic["config_path"], semantic["config_path"])
        self.assertNotEqual(classic_config["result_path"], semantic_config["result_path"])
        self.assertNotEqual(classic_config["child_nonce"], semantic_config["child_nonce"])
        self.assertEqual(classic_config["run_id"], semantic_config["run_id"])
        self.assertEqual(classic_config["user_data_root"], str(self.classic_root))
        self.assertEqual(semantic_config["user_data_root"], str(self.semantic_root))
        self.assertEqual(classic_config["replay_route"], "classic")
        self.assertEqual(semantic_config["replay_route"], "semantic")
        self.assertEqual(classic_config["presentation_mode"], "classic")
        self.assertEqual(semantic_config["presentation_mode"], "remastered")
        self.assertEqual(
            classic_config["user_data_root_policy"],
            "set_once_before_toolbox_init",
        )
        self.assertEqual(
            semantic_config["settlement_barrier"],
            "next_semantic_gameplay_poll",
        )
        self.assertEqual(classic_config["preferences_write_policy"], "disabled")
        self.assertEqual(
            semantic_config["input_slot_policy"],
            "user_data_root_only_no_bundled_fallback",
        )
        self.assertEqual(
            semantic_config["output_slot_policy"], "fresh_nonexistent"
        )
        self.assertEqual(classic_config["rng_seed"], semantic_config["rng_seed"])
        self.assertEqual(classic_config["rng_stream"], semantic_config["rng_stream"])
        self.assertEqual(classic_config["actions"], semantic_config["actions"])
        self.assertEqual(classic_config["input_slot"], "A")
        self.assertEqual(classic_config["output_slot"], "B")
        self.assertNotEqual(classic_config["input_slot"], classic_config["output_slot"])
        for entry in (classic, semantic):
            config_path = Path(entry["config_path"])
            result_path = Path(entry["config"]["result_path"])
            self.assertEqual(config_path.parent, result_path.parent)
            self.assertEqual(entry["cwd"], str(config_path.parent))
            self.assertNotEqual(config_path, result_path)

    def test_shell_metacharacters_are_only_argv_and_data(self) -> None:
        sentinel = self.root / "shell-injection-sentinel"
        executable = self.root / "fake realmz; harmless name"
        self.write_fake_executable(executable)
        self.request["executable"] = str(executable)
        self.request["actions"][0]["arguments"]["label"] = f"$(touch {sentinel})"
        self.write_request()

        envelope = self.run_parent()

        self.assertEqual(envelope["executable"], str(executable))
        self.assertFalse(sentinel.exists())
        self.assertEqual(
            {entry["executable"] for entry in self.invocations()}, {str(executable)}
        )

    def test_cli_emits_one_machine_readable_non_equivalence_envelope(self) -> None:
        environment = os.environ.copy()
        environment["REALMZ_REPLAY_FAKE_LOG"] = str(self.log_path)
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_PATH), "--request", str(self.request_path)],
            cwd=self.root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        envelope = json.loads(completed.stdout)
        self.track_workspace(envelope)
        self.assertEqual(envelope["semantic_equivalence"], "not_evaluated")
        self.assertEqual(len(envelope["child_results"]), 2)

    def test_cli_interrupt_reports_retained_workspace_and_exits_130(self) -> None:
        self.request["timeout_seconds"] = 30
        self.write_request()
        environment = os.environ.copy()
        environment.update(
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "timeout",
            }
        )
        stdout = ""
        stderr = ""
        with subprocess.Popen(
            [sys.executable, str(SCRIPT_PATH), "--request", str(self.request_path)],
            cwd=self.root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ) as process:
            try:
                deadline = time.monotonic() + 5
                while self.log_path.stat().st_size == 0 and time.monotonic() < deadline:
                    if process.poll() is not None:
                        break
                    time.sleep(0.01)
                self.assertGreater(self.log_path.stat().st_size, 0)
                process.send_signal(signal.SIGINT)
                stdout, stderr = process.communicate(timeout=5)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

        self.assertEqual(process.returncode, runner.EXIT_INTERRUPTED)
        self.assertEqual(stdout, "")
        error = json.loads(stderr)
        self.track_workspace(error)
        self.assertEqual(error["error"]["code"], "runner.interrupted")
        self.assertEqual(error["semantic_equivalence"], "not_evaluated")
        retention = error["workspace_retention"]
        self.assertIs(retention["cleanup_attempted"], False)
        self.assertIs(retention["path_authoritative"], True)
        self.assertTrue(Path(retention["candidate_path"]).is_dir())


class ChildFailureTests(ReplayRunnerTestCase):
    def test_timeout_fails_closed_before_semantic_launch(self) -> None:
        self.request["timeout_seconds"] = 0.1
        self.write_request()
        self.assert_error("child.timeout", "classic child exceeded", behavior="timeout")
        self.assertEqual(len(self.invocations()), 1)

    def test_nonzero_exit_includes_bounded_diagnostic(self) -> None:
        error = self.assert_error(
            "child.nonzero_exit", "status 23", behavior="nonzero"
        )
        self.assertIn("synthetic child failure", error.message)
        self.assertLess(len(error.message), 1024)

    def test_missing_result_fails_closed(self) -> None:
        self.assert_error(
            "child.result_invalid", "cannot open child result", behavior="missing"
        )

    def test_malformed_result_fails_closed(self) -> None:
        self.assert_error(
            "child.result_invalid", "not valid strict JSON", behavior="malformed"
        )

    def test_deeply_nested_result_fails_as_strict_json(self) -> None:
        self.assert_error(
            "child.result_invalid", "not valid strict JSON", behavior="nested_result"
        )

    def test_stale_nonce_and_run_identity_fail_closed(self) -> None:
        self.assert_error(
            "child.result_invalid", "stale child_nonce", behavior="stale_nonce"
        )
        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.result_invalid", "stale run_id", behavior="stale_run"
        )

    def test_wrong_process_or_route_identity_fails_closed(self) -> None:
        self.assert_error(
            "child.result_invalid", "wrong process_id", behavior="wrong_pid"
        )
        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.result_invalid", "wrong replay_route", behavior="wrong_route"
        )

        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.result_invalid",
            "wrong presentation_mode",
            behavior="wrong_presentation",
        )

    def test_unknown_result_field_fails_closed(self) -> None:
        self.assert_error(
            "child.result_invalid", "unknown field: equivalent", behavior="unknown_result_field"
        )

    def test_result_symlink_fails_closed(self) -> None:
        self.assert_error(
            "child.result_invalid",
            "private current-user-owned",
            behavior="symlink_result",
        )

    def test_fifo_result_is_rejected_without_blocking(self) -> None:
        self.assert_error(
            "child.result_invalid", "regular file", behavior="fifo_result"
        )

    def test_missing_or_symlinked_output_slot_fails_closed(self) -> None:
        self.assert_error(
            "child.launch_failed", "output slot was not created", behavior="missing_output"
        )
        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.launch_failed", "output slot must be a physical directory", behavior="symlink_output"
        )

    def test_child_cannot_mutate_its_config_unnoticed(self) -> None:
        self.assert_error(
            "child.config_changed", "config identity changed", behavior="mutate_config"
        )

    def test_private_protocol_paths_remain_private_and_unaliased(self) -> None:
        self.assert_error(
            "child.result_invalid", "private current-user-owned", behavior="public_result"
        )
        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.config_changed", "config identity changed", behavior="hardlink_config"
        )
        self.log_path.write_text("", encoding="utf-8")
        self.remove_output_slots()
        self.assert_error(
            "child.workspace_changed", "no longer private", behavior="public_workspace"
        )

    def test_oversized_process_output_fails_closed(self) -> None:
        self.assert_error(
            "child.output_limit", "output limit", behavior="oversized_output"
        )

    def test_same_executable_identity_is_required_for_both_launches(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "mutate_executable",
                "REALMZ_REPLAY_FAKE_ROUTE": "classic",
            },
            clear=False,
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, "child.launch_failed")
        self.track_workspace(raised.exception)
        self.assertIn("identity changed before/during classic completion", raised.exception.message)
        self.assertEqual(len(self.invocations()), 1)

    def test_transient_executable_ancestor_swap_fails_closed(self) -> None:
        alternate_marker = self.root / "alternate-started.txt"
        alternate_marker.write_text("", encoding="utf-8")
        alternate_directory = self.root / "bin-alternate"
        alternate_directory.mkdir()
        self.write_fake_executable(
            alternate_directory / self.fake_executable.name,
            startup_marker=alternate_marker,
        )
        moved_original = self.root / "bin-original"
        real_popen = subprocess.Popen

        def swapping_popen(
            argv: list[str], *arguments: object, **keywords: object
        ) -> subprocess.Popen[bytes]:
            config = json.loads(Path(argv[2]).read_text(encoding="utf-8"))
            if config["replay_route"] != "semantic":
                return real_popen(argv, *arguments, **keywords)
            self.executable_directory.rename(moved_original)
            alternate_directory.rename(self.executable_directory)
            try:
                process = real_popen(argv, *arguments, **keywords)
                deadline = time.monotonic() + 2
                while (
                    alternate_marker.read_text(encoding="utf-8") != "started\n"
                    and time.monotonic() < deadline
                    and process.poll() is None
                ):
                    time.sleep(0.005)
                return process
            finally:
                self.executable_directory.rename(alternate_directory)
                moved_original.rename(self.executable_directory)

        with mock.patch.dict(
            os.environ,
            {"REALMZ_REPLAY_FAKE_LOG": str(self.log_path)},
            clear=False,
        ), mock.patch.object(
            runner.subprocess, "Popen", side_effect=swapping_popen
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)

        self.track_workspace(raised.exception)
        self.assertEqual(raised.exception.code, "child.launch_failed")
        self.assertIn("namespace ancestor changed", raised.exception.message)
        self.assertEqual(alternate_marker.read_text(encoding="utf-8"), "started\n")

    def test_transient_user_root_ancestor_swap_fails_closed(self) -> None:
        alternate_root = self.root / "semantic-user-root-alternate"
        (alternate_root / "Save" / "Game A").mkdir(parents=True)
        moved_original = self.root / "semantic-user-root-original"
        real_popen = subprocess.Popen

        def swapping_popen(
            argv: list[str], *arguments: object, **keywords: object
        ) -> subprocess.Popen[bytes]:
            config = json.loads(Path(argv[2]).read_text(encoding="utf-8"))
            if config["replay_route"] != "semantic":
                return real_popen(argv, *arguments, **keywords)
            self.semantic_root.rename(moved_original)
            alternate_root.rename(self.semantic_root)
            try:
                process = real_popen(argv, *arguments, **keywords)
                redirected_output = self.semantic_root / "Save" / "Game B"
                deadline = time.monotonic() + 2
                while (
                    not redirected_output.is_dir()
                    and time.monotonic() < deadline
                    and process.poll() is None
                ):
                    time.sleep(0.005)
                return process
            finally:
                self.semantic_root.rename(alternate_root)
                moved_original.rename(self.semantic_root)

        with mock.patch.dict(
            os.environ,
            {"REALMZ_REPLAY_FAKE_LOG": str(self.log_path)},
            clear=False,
        ), mock.patch.object(
            runner.subprocess, "Popen", side_effect=swapping_popen
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)

        self.track_workspace(raised.exception)
        self.assertEqual(raised.exception.code, "child.launch_failed")
        self.assertIn("namespace ancestor changed", raised.exception.message)
        self.assertTrue((alternate_root / "Save" / "Game B").is_dir())
        self.assertFalse((self.semantic_root / "Save" / "Game B").exists())

    def test_transient_input_slot_swap_fails_closed_without_pinning_save_ctime(self) -> None:
        save_root = self.semantic_root / "Save"
        alternate_input = save_root / "Game C"
        alternate_input.mkdir()
        moved_original = save_root / "Game A-original"
        real_popen = subprocess.Popen

        def swapping_popen(
            argv: list[str], *arguments: object, **keywords: object
        ) -> subprocess.Popen[bytes]:
            config = json.loads(Path(argv[2]).read_text(encoding="utf-8"))
            if config["replay_route"] != "semantic":
                return real_popen(argv, *arguments, **keywords)
            (save_root / "Game A").rename(moved_original)
            alternate_input.rename(save_root / "Game A")
            try:
                process = real_popen(argv, *arguments, **keywords)
                output = save_root / "Game B"
                deadline = time.monotonic() + 2
                while (
                    not output.is_dir()
                    and time.monotonic() < deadline
                    and process.poll() is None
                ):
                    time.sleep(0.005)
                return process
            finally:
                (save_root / "Game A").rename(alternate_input)
                moved_original.rename(save_root / "Game A")

        with mock.patch.dict(
            os.environ,
            {"REALMZ_REPLAY_FAKE_LOG": str(self.log_path)},
            clear=False,
        ), mock.patch.object(
            runner.subprocess, "Popen", side_effect=swapping_popen
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)

        self.track_workspace(raised.exception)
        self.assertEqual(raised.exception.code, "child.launch_failed")
        self.assertIn("namespace ancestor changed", raised.exception.message)
        self.assertIn("Game A", raised.exception.message)

    def test_classic_cannot_alias_semantic_root_before_second_launch(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "alias_semantic_root",
                "REALMZ_REPLAY_FAKE_ROUTE": "classic",
                "REALMZ_REPLAY_FAKE_ALIAS_TARGET": str(self.semantic_root),
            },
            clear=False,
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, "child.launch_failed")
        self.track_workspace(raised.exception)
        self.assertIn("semantic user-data root identity changed", raised.exception.message)
        self.assertEqual(len(self.invocations()), 1)

    def test_workspace_swap_never_deletes_victim_and_reports_taint(self) -> None:
        victim = self.root / "victim"
        victim.mkdir()
        (victim / "KEEP_ME").write_text("preserve\n", encoding="utf-8")
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "swap_workspace",
                "REALMZ_REPLAY_FAKE_ROUTE": "classic",
                "REALMZ_REPLAY_FAKE_VICTIM": str(victim),
            },
            clear=False,
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, "child.workspace_changed")
        retention = raised.exception.workspace_retention
        self.assertIsNotNone(retention)
        self.assertIs(retention["cleanup_attempted"], False)
        self.assertIs(retention["path_authoritative"], False)
        self.assertIn("unknown renamed location", retention["notice"])
        candidate = Path(retention["candidate_path"])
        moved_protocol_workspace = candidate.with_name(candidate.name + "-moved")
        self.addCleanup(shutil.rmtree, candidate, True)
        self.addCleanup(shutil.rmtree, moved_protocol_workspace, True)
        self.assertEqual((candidate / "KEEP_ME").read_text(encoding="utf-8"), "preserve\n")
        self.assertTrue((moved_protocol_workspace / "classic" / "config.json").is_file())

    def test_keyboard_interrupt_terminates_the_launched_process_group(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "timeout",
            },
            clear=False,
        ), mock.patch.object(
            runner, "_wait_for_child", side_effect=KeyboardInterrupt
        ), mock.patch.object(
            runner, "_terminate_process", wraps=runner._terminate_process
        ) as terminate:
            with self.assertRaises(KeyboardInterrupt) as raised:
                runner.run_request_file(self.request_path)
        terminate.assert_called_once()
        self.track_workspace(raised.exception)

    def test_only_requested_route_behavior_is_applied(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "nonzero",
                "REALMZ_REPLAY_FAKE_ROUTE": "semantic",
            },
            clear=False,
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, "child.nonzero_exit")
        self.track_workspace(raised.exception)
        self.assertIn("semantic child", raised.exception.message)
        self.assertEqual(
            [entry["config"]["replay_route"] for entry in self.invocations()],
            ["classic", "semantic"],
        )

    def test_children_must_report_the_same_engine_identity(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "REALMZ_REPLAY_FAKE_LOG": str(self.log_path),
                "REALMZ_REPLAY_FAKE_BEHAVIOR": "engine_mismatch",
                "REALMZ_REPLAY_FAKE_ROUTE": "semantic",
            },
            clear=False,
        ):
            with self.assertRaises(runner.ReplayRunnerError) as raised:
                runner.run_request_file(self.request_path)
        self.assertEqual(raised.exception.code, "child.engine_identity_mismatch")
        self.track_workspace(raised.exception)
        self.assertIn("different engine identities", raised.exception.message)


class RequestValidationTests(ReplayRunnerTestCase):
    def assert_request_error(self, expected: str) -> None:
        with self.assertRaises(runner.ReplayRunnerError) as raised:
            runner.load_request(self.request_path)
        self.assertEqual(raised.exception.code, "request.invalid")
        self.assertIn(expected, raised.exception.message)

    def test_unknown_missing_and_duplicate_request_fields_are_rejected(self) -> None:
        unknown = copy.deepcopy(self.request)
        unknown["compare"] = True
        self.write_request(unknown)
        self.assert_request_error("unknown field: compare")

        missing = copy.deepcopy(self.request)
        del missing["actions"]
        self.write_request(missing)
        self.assert_request_error("missing field: actions")

        self.request_path.write_text(
            '{"schema_version":1,"schema_version":1}', encoding="utf-8"
        )
        self.assert_request_error("duplicate JSON key: schema_version")

    def test_deep_json_nesting_is_a_bounded_request_error(self) -> None:
        self.request_path.write_text("[" * 600000 + "]" * 600000, encoding="utf-8")
        self.assert_request_error("not valid strict JSON")

    def test_fifo_request_is_rejected_without_blocking(self) -> None:
        fifo = self.root / "request.fifo"
        try:
            os.mkfifo(fifo, 0o600)
        except (AttributeError, NotImplementedError, OSError) as error:
            self.skipTest(f"FIFOs unavailable: {error}")
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_PATH), "--request", str(fifo)],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
        self.assertEqual(completed.returncode, runner.EXIT_REQUEST)
        error = json.loads(completed.stderr)
        self.assertEqual(error["error"]["code"], "request.invalid")
        self.assertIn("regular file", error["error"]["message"])

    def test_executable_must_be_explicit_canonical_regular_and_executable(self) -> None:
        for replacement, expected in (
            ("fake-realmz", "absolute normalized"),
            (str(self.root / "missing"), "does not exist"),
        ):
            with self.subTest(replacement=replacement):
                value = copy.deepcopy(self.request)
                value["executable"] = replacement
                self.write_request(value)
                self.assert_request_error(expected)

        non_executable = self.root / "non-executable"
        non_executable.write_text("x", encoding="utf-8")
        non_executable.chmod(0o600)
        self.request["executable"] = str(non_executable)
        self.write_request()
        self.assert_request_error("must be executable")

    def test_executable_fifo_replacement_is_rejected_without_blocking(self) -> None:
        probe = f"""
import importlib.util
import os
from pathlib import Path
import sys

script = Path({str(SCRIPT_PATH)!r})
spec = importlib.util.spec_from_file_location("fifo_probe_runner", script)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
request = module.load_request(Path({str(self.request_path)!r}))
os.unlink(request.executable)
os.mkfifo(request.executable, 0o700)
try:
    module.run_request(request)
except module.ReplayRunnerError as error:
    print(error.code)
else:
    raise SystemExit("replacement FIFO was accepted")
"""
        completed = subprocess.run(
            [sys.executable, "-c", textwrap.dedent(probe)],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stdout.strip(), "child.launch_failed")

    def test_user_roots_must_be_distinct_non_nested_physical_directories(self) -> None:
        same = copy.deepcopy(self.request)
        same["semantic_user_data_root"] = same["classic_user_data_root"]
        self.write_request(same)
        self.assert_request_error("must be distinct")

        nested_root = self.classic_root / "nested"
        nested_root.mkdir()
        nested = copy.deepcopy(self.request)
        nested["semantic_user_data_root"] = str(nested_root)
        self.write_request(nested)
        self.assert_request_error("must not be nested")

        link = self.root / "root-link"
        try:
            link.symlink_to(self.semantic_root, target_is_directory=True)
        except (NotImplementedError, OSError) as error:
            self.skipTest(f"symbolic links unavailable: {error}")
        linked = copy.deepcopy(self.request)
        linked["semantic_user_data_root"] = str(link)
        self.write_request(linked)
        self.assert_request_error("symbolic-link or noncanonical")

    def test_slots_are_strict_and_distinct(self) -> None:
        for field, replacement, expected in (
            ("input_slot", "a", "uppercase Classic slot"),
            ("output_slot", "K", "uppercase Classic slot"),
            ("output_slot", "A", "must be distinct"),
        ):
            with self.subTest(field=field, replacement=replacement):
                value = copy.deepcopy(self.request)
                value[field] = replacement
                self.write_request(value)
                self.assert_request_error(expected)

    def test_input_slots_exist_and_output_slots_start_fresh(self) -> None:
        missing_input = copy.deepcopy(self.request)
        missing_input["input_slot"] = "C"
        missing_input["output_slot"] = "D"
        self.write_request(missing_input)
        self.assert_request_error("input slot does not exist")

        (self.classic_root / "Save" / "Game B").mkdir()
        self.write_request()
        self.assert_request_error("output slot must not exist")

    def test_rng_seed_and_stream_are_explicit_fixed_width_hex(self) -> None:
        for field, replacement in (
            ("rng_seed", "0" * 15),
            ("rng_seed", "G" * 16),
            ("rng_stream", 1),
        ):
            with self.subTest(field=field, replacement=replacement):
                value = copy.deepcopy(self.request)
                value[field] = replacement
                self.write_request(value)
                self.assert_request_error("lowercase hexadecimal")

    def test_actions_are_exact_bounded_and_normalized(self) -> None:
        invalid_actions = (
            ("not-an-array", "actions must be an array"),
            ([{"ordinal": 1, "kind": "probe", "arguments": {}}], "zero-based"),
            ([{"ordinal": 0, "kind": "NotNormalized", "arguments": {}}], "snake_case"),
            ([{"ordinal": 0, "kind": "probe", "arguments": {}, "extra": 1}], "unknown field"),
            ([{"ordinal": 0, "kind": "probe", "arguments": {"value": 1.5}}], "string, integer, or boolean"),
            ([{"ordinal": 0, "kind": "probe", "arguments": {"bad__name": 1}}], "normalized"),
            ([{"ordinal": 0, "kind": "probe", "arguments": {"value": "café"}}], "printable ASCII"),
            ([{"ordinal": 0, "kind": "probe", "arguments": {"value": "line\nbreak"}}], "printable ASCII"),
        )
        for actions, expected in invalid_actions:
            with self.subTest(actions=actions):
                value = copy.deepcopy(self.request)
                value["actions"] = actions
                self.write_request(value)
                self.assert_request_error(expected)

    def test_action_and_json_size_limits_are_enforced(self) -> None:
        with mock.patch.object(runner, "MAX_ACTIONS", 0):
            self.assert_request_error("action limit")

        with mock.patch.object(runner, "MAX_JSON_BYTES", 16):
            self.assert_request_error("exceeds 16 bytes")

    def test_timeout_is_finite_and_bounded(self) -> None:
        for replacement in (True, 0, 0.01, 301, 10**400):
            with self.subTest(replacement=replacement):
                value = copy.deepcopy(self.request)
                value["timeout_seconds"] = replacement
                self.write_request(value)
                self.assert_request_error("timeout_seconds")

        self.request_path.write_text(
            json.dumps(self.request).replace('"timeout_seconds": 2', '"timeout_seconds": NaN'),
            encoding="utf-8",
        )
        self.assert_request_error("non-finite JSON number")


class SchemaContractTests(ReplayRunnerTestCase):
    def test_schema_patterns_require_the_absolute_end_of_the_string(self) -> None:
        valid_samples = {
            r"^[ -~]*(?![\s\S])": "printable ASCII",
            r"^[a-z](?:[a-z0-9]|_(?=[a-z0-9]))*(?![\s\S])": "probe_value",
            r"^[a-z][a-z0-9-]*(?![\s\S])": "fixture-id",
            r"^[A-J](?![\s\S])": "A",
            r"^[0-9a-f]{16}(?![\s\S])": "0" * 16,
            r"^[0-9a-f]{32}(?![\s\S])": "0" * 32,
            r"^[0-9a-f]{64}(?![\s\S])": "0" * 64,
        }

        def collect_patterns(value: object) -> list[str]:
            if isinstance(value, dict):
                found = []
                if isinstance(value.get("pattern"), str):
                    found.append(value["pattern"])
                for child in value.values():
                    found.extend(collect_patterns(child))
                return found
            if isinstance(value, list):
                found = []
                for child in value:
                    found.extend(collect_patterns(child))
                return found
            return []

        for name in (*SCHEMA_NAMES, "replay-fixture-manifest.schema.json"):
            schema = json.loads(
                (Path(__file__).with_name(name)).read_text(encoding="utf-8")
            )
            patterns = collect_patterns(schema)
            self.assertTrue(patterns)
            for pattern in patterns:
                with self.subTest(name=name, pattern=pattern):
                    self.assertIn(pattern, valid_samples)
                    sample = valid_samples[pattern]
                    self.assertIsNotNone(re.search(pattern, sample))
                    self.assertIsNone(re.search(pattern, sample + "\n"))
                    self.assertIsNone(re.search(pattern, sample + "\r"))

    def test_all_protocol_schemas_are_v1_and_closed(self) -> None:
        schemas = {
            name: json.loads((Path(__file__).with_name(name)).read_text(encoding="utf-8"))
            for name in SCHEMA_NAMES
        }
        for name, schema in schemas.items():
            with self.subTest(name=name):
                self.assertEqual(
                    schema["$schema"], "https://json-schema.org/draft/2020-12/schema"
                )
                self.assertIs(schema["additionalProperties"], False)
                self.assertEqual(schema["properties"]["schema_version"]["const"], 1)

        request_schema = schemas["semantic-replay-run-request.schema.json"]
        config_schema = schemas["semantic-replay-child-config.schema.json"]
        result_schema = schemas["semantic-replay-child-result.schema.json"]
        envelope_schema = schemas["semantic-replay-run-envelope.schema.json"]
        self.assertEqual(set(request_schema["required"]), runner.REQUEST_FIELDS)
        self.assertEqual(set(config_schema["required"]), runner.CONFIG_FIELDS)
        self.assertEqual(set(result_schema["required"]), runner.RESULT_FIELDS)
        self.assertEqual(set(envelope_schema["required"]), runner.ENVELOPE_FIELDS)
        self.assertEqual(
            envelope_schema["properties"]["semantic_equivalence"]["const"],
            "not_evaluated",
        )
        self.assertEqual(
            envelope_schema["properties"]["runner_scope"]["const"],
            "process_isolation_only",
        )
        self.assertEqual(
            envelope_schema["properties"]["child_results"]["items"]["$ref"],
            result_schema["$id"],
        )
        route_pair = config_schema["allOf"][0]
        self.assertEqual(
            route_pair["then"]["properties"]["presentation_mode"]["const"],
            "classic",
        )
        self.assertEqual(
            route_pair["else"]["properties"]["presentation_mode"]["const"],
            "remastered",
        )

    def test_action_schemas_match_the_runtime_action_fields(self) -> None:
        for name in (
            "semantic-replay-run-request.schema.json",
            "semantic-replay-child-config.schema.json",
        ):
            schema = json.loads(
                (Path(__file__).with_name(name)).read_text(encoding="utf-8")
            )
            action = schema["$defs"]["action"]
            self.assertIs(action["additionalProperties"], False)
            self.assertEqual(set(action["required"]), runner.ACTION_FIELDS)
            self.assertEqual(set(action["properties"]), runner.ACTION_FIELDS)


if __name__ == "__main__":
    unittest.main()
