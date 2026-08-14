import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT_DIR = REPO / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))
import prepare_style_proof_references as REFERENCES  # noqa: E402
import validate_style_proof_outputs as TOOL  # noqa: E402


class PromptAmendmentTest(unittest.TestCase):
    def copy_contract(self, root: Path) -> tuple[Path, Path, dict, bytes]:
        relative_paths = (
            TOOL.DEFAULT_JOBS,
            "assets/remastered/style-proof/generation/style-spec.json",
            "assets/remastered/style-proof/classic-selection.json",
            "assets/remastered/style-proof/classic-references/classic-reference-manifest.json",
            TOOL.DEFAULT_AMENDMENTS,
            TOOL.DEFAULT_TOPOLOGY_AMENDMENTS,
            "assets/remastered/style-proof/generation/masked-topology-amendments.schema.json",
        )
        for relative in relative_paths:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / relative, target)
        jobs_path = root / TOOL.DEFAULT_JOBS
        amendment_path = root / TOOL.DEFAULT_AMENDMENTS
        jobs_bytes = jobs_path.read_bytes()
        return jobs_path, amendment_path, json.loads(jobs_bytes), jobs_bytes

    def build_handoff(self, root: Path) -> Path:
        handoff = root / "generation-inputs"
        context = REFERENCES._load_selection(
            REPO,
            REPO / "assets/remastered/style-proof/classic-selection.json",
        )
        REFERENCES.export_generation_inputs(
            context,
            REPO / "assets/remastered/style-proof/classic-references",
            handoff,
        )
        return handoff

    def test_register_is_exactly_scoped_and_hash_bound(self) -> None:
        path = REPO / TOOL.DEFAULT_AMENDMENTS
        data = path.read_bytes()
        self.assertEqual(
            TOOL.PROMPT_AMENDMENT_SET_SHA256, hashlib.sha256(data).hexdigest()
        )
        jobs, jobs_bytes, _ = TOOL._load_job_set(
            REPO, REPO / TOOL.DEFAULT_JOBS
        )
        amendments, digest = TOOL._load_prompt_amendments(
            REPO, jobs, jobs_bytes, path
        )
        self.assertEqual(TOOL.AMENDED_JOB_IDS, tuple(amendments))
        self.assertEqual(TOOL.PROMPT_AMENDMENT_SET_SHA256, digest)
        for amendment in amendments.values():
            self.assertEqual("Codex preflight audit", amendment["reviewer"])
            self.assertEqual(
                amendment["amendment_sha256"],
                hashlib.sha256(amendment["text"].encode()).hexdigest(),
            )
        for job_id in TOOL.AMENDED_JOB_IDS[:3]:
            text = amendments[job_id]["text"].lower()
            for artifact in (
                "checkerboards", "square texels", "pixel grids", "dithering",
                "mosaics", "graph-paper", "diagonal hatching", "contour lines",
                "repeated cells",
            ):
                self.assertIn(artifact, text)
        waterfall = amendments[TOOL.AMENDED_JOB_IDS[3]]["text"].lower()
        for topology in (
            "orthographic edge-to-edge", "foam enters from the left edge",
            "dominates the bottom", "water descends from the top edge",
            "along the right edge", "intrinsic water color",
        ):
            self.assertIn(topology, waterfall)
        for excluded in ("cliff", "rocks", "horizon", "sky", "banks", "isolated object"):
            self.assertIn(excluded, waterfall)

    def test_masked_topology_register_is_hash_bound_to_measured_classic_alpha(self) -> None:
        path = REPO / TOOL.DEFAULT_TOPOLOGY_AMENDMENTS
        data = path.read_bytes()
        self.assertEqual(
            TOOL.MASKED_TOPOLOGY_AMENDMENT_SET_SHA256,
            hashlib.sha256(data).hexdigest(),
        )
        jobs, jobs_bytes, _ = TOOL._load_job_set(
            REPO, REPO / TOOL.DEFAULT_JOBS
        )
        amendments, digest = TOOL._load_masked_topology_amendments(
            REPO, jobs, jobs_bytes, path
        )
        self.assertEqual(TOOL.TOPOLOGY_AMENDED_JOB_IDS, tuple(amendments))
        self.assertEqual(TOOL.MASKED_TOPOLOGY_AMENDMENT_SET_SHA256, digest)
        jobs_by_id = {job["job_id"]: job for job in jobs["jobs"]}
        for job_id, amendment in amendments.items():
            job = jobs_by_id[job_id]
            reference_path = (
                REPO
                / "assets/remastered/style-proof/classic-references"
                / job["input"]["path"]
            )
            reference_data = reference_path.read_bytes()
            classic = REFERENCES.decode_png(reference_data, str(reference_path))
            measured = TOOL._measure_classic_alpha_topology(
                classic, hashlib.sha256(reference_data).hexdigest()
            )
            self.assertEqual(amendment["classic_alpha_evidence"], measured)
            self.assertEqual(
                amendment["amendment_sha256"],
                hashlib.sha256(amendment["text"].encode()).hexdigest(),
            )

        gate = amendments["19_world_dungeon_cicn_m83"]
        evidence = gate["classic_alpha_evidence"]
        self.assertEqual([], evidence["enclosed_transparent_component_sizes_4_connected"])
        self.assertEqual([681], evidence["foreground_component_sizes_4_connected"])
        self.assertEqual(
            {"bottom": 31, "left": 4, "right": 26, "top": 0},
            evidence["foreground_bbox"],
        )
        for phrase in (
            "one connected opaque silhouette",
            "zero enclosed transparent holes",
            "opaque dark-iron and near-black recess paint",
            "#ff00ff may appear only outside the classic mask",
            "never between bars",
        ):
            self.assertIn(phrase, gate["text"].lower())

    def test_job19_pre_call_prompt_overrides_descriptive_opening_with_alpha_fact(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, _, jobs, _ = self.copy_contract(root)
            handoff = self.build_handoff(root)
            job = next(
                item
                for item in jobs["jobs"]
                if item["job_id"] == "19_world_dungeon_cicn_m83"
            )
            prepared = TOOL.prepare_amended_prompt(
                root,
                jobs_path,
                handoff,
                root / TOOL.DEFAULT_RECEIPTS,
                root / TOOL.DEFAULT_OPERATIONAL_LOG,
                job["job_id"],
                amendments_path=root / TOOL.DEFAULT_AMENDMENTS,
                topology_amendments_path=root / TOOL.DEFAULT_TOPOLOGY_AMENDMENTS,
            )
            _, _, style = TOOL._load_job_set(root, jobs_path)
            base = TOOL.assemble_prompt(style, job)
            self.assertIn("visible open gaps", base)
            self.assertEqual(
                base + ". " + prepared["prompt_amendment"]["text"],
                prepared["resolved_prompt"],
            )
            self.assertEqual(
                TOOL.MASKED_TOPOLOGY_AMENDMENT_SET_SHA256,
                prepared["prompt_amendment_provenance"]["amendment_set_sha256"],
            )
            stale = copy.deepcopy(prepared["prompt_amendment"])
            stale["classic_alpha_evidence"]["foreground_pixels"] -= 1
            with self.assertRaisesRegex(
                TOOL.ValidationError, "evidence mismatches the Classic alpha"
            ):
                TOOL._verify_topology_amendment_evidence(handoff, job, stale)

    def test_job19_rejected_receipt_binds_topology_register_without_mutating_jobs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, amendment_path, jobs, jobs_bytes = self.copy_contract(root)
            handoff = self.build_handoff(root)
            job = next(
                item
                for item in jobs["jobs"]
                if item["job_id"] == "19_world_dungeon_cicn_m83"
            )
            raw_path = (
                root
                / "assets/remastered/style-proof/generation/raw"
                / job["job_id"]
                / "attempt-0.png"
            )
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(
                REFERENCES.encode_png(
                    REFERENCES.RGBAImage(2, 2, bytes((40, 42, 45, 255)) * 4)
                )
            )
            inspected = TOOL.inspect_raw_attempt(
                root,
                jobs_path,
                handoff,
                job["job_id"],
                0,
                None,
                amendment_path,
                root / TOOL.DEFAULT_TOPOLOGY_AMENDMENTS,
            )
            entry = {
                "input": inspected["input"],
                "job_id": job["job_id"],
                "model_provenance": {
                    "completed_at": "2026-08-13T04:16:00Z",
                    "model": "test-model",
                    "parameters": {"quality": "test"},
                    "prediction_id": "test-prediction",
                    "provider": "unit-test",
                    "seed": None,
                    "started_at": "2026-08-13T04:15:00Z",
                    "version": "1",
                },
                "output": None,
                "policy_evidence": None,
                "post_processing": [],
                "prior_attempts": [],
                "prompt_amendment_provenance": inspected[
                    "prompt_amendment_provenance"
                ],
                "prompt_sha256": inspected["prompt_sha256"],
                "raw_output": inspected["raw_output"],
                "regeneration_ordinal": 0,
                "rejection_reason": "Synthetic topology-contract rejection.",
                "resolved_prompt": inspected["resolved_prompt"],
                "retry_prompt_supplement": None,
                "review_status": "rejected",
                "reviewer": "Test Reviewer",
                "shared_style_sha256": inspected["shared_style_sha256"],
            }
            receipts = TOOL.empty_receipt_manifest(jobs, jobs_bytes)
            receipts["entries"].append(entry)
            receipts_path = root / TOOL.DEFAULT_RECEIPTS
            receipts_path.parent.mkdir(parents=True, exist_ok=True)
            receipts_path.write_bytes(TOOL._canonical_json(receipts))
            result = TOOL.validate(
                root,
                jobs_path,
                receipts_path,
                handoff,
                amendment_path,
                root / TOOL.DEFAULT_TOPOLOGY_AMENDMENTS,
            )
            self.assertEqual((job["job_id"],), result.rejected_job_ids)
            self.assertEqual(
                hashlib.sha256((root / TOOL.DEFAULT_JOBS).read_bytes()).hexdigest(),
                receipts["jobs_sha256"],
            )

    def test_prompt_order_is_base_then_amendment_then_retry(self) -> None:
        jobs, jobs_bytes, style = TOOL._load_job_set(
            REPO, REPO / TOOL.DEFAULT_JOBS
        )
        amendments, _ = TOOL._load_prompt_amendments(
            REPO, jobs, jobs_bytes, REPO / TOOL.DEFAULT_AMENDMENTS
        )
        job = jobs["jobs"][0]
        amendment = amendments[job["job_id"]]
        base = TOOL.assemble_prompt(style, job)
        supplement = "Targeted retry correction."
        self.assertEqual(
            base + ". " + amendment["text"],
            TOOL.effective_prompt(style, job, 0, None, amendment),
        )
        self.assertEqual(
            base + ". " + amendment["text"] + ". " + supplement,
            TOOL.effective_prompt(style, job, 1, supplement, amendment),
        )

    def test_pre_call_helper_rejects_every_consuming_evidence_channel(self) -> None:
        cases = ("raw", "output", "receipt", "operational")
        for evidence in cases:
            with self.subTest(evidence=evidence), tempfile.TemporaryDirectory() as temp_name:
                root = Path(temp_name)
                jobs_path, amendment_path, jobs, _ = self.copy_contract(root)
                handoff = self.build_handoff(root)
                job = jobs["jobs"][0]
                receipts_path = root / TOOL.DEFAULT_RECEIPTS
                operational_path = root / TOOL.DEFAULT_OPERATIONAL_LOG
                prepared = TOOL.prepare_amended_prompt(
                    root, jobs_path, handoff, receipts_path, operational_path,
                    job["job_id"], amendments_path=amendment_path,
                )
                self.assertIn("prompt_amendment_provenance", prepared)
                if evidence == "raw":
                    path = root / "assets/remastered/style-proof/generation/raw" / job["job_id"] / "attempt-0.png"
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"call-consumed")
                elif evidence == "output":
                    path = root / job["expected_output_path"]
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"call-consumed")
                elif evidence == "receipt":
                    receipts_path.parent.mkdir(parents=True, exist_ok=True)
                    receipts_path.write_bytes(TOOL._canonical_json({"entries": [{"job_id": job["job_id"]}]}))
                else:
                    operational_path.parent.mkdir(parents=True, exist_ok=True)
                    operational_path.write_bytes(TOOL._canonical_json({
                        "entries": [{
                            "art_output_created": True,
                            "consumes_regeneration_ordinal": True,
                            "intended_job_id": job["job_id"],
                            "operational_status": "submitted",
                            "submitted_payload": {"valid_generation_prompt": True},
                        }]
                    }))
                with self.assertRaisesRegex(TOOL.ValidationError, "cannot be used after"):
                    TOOL.prepare_amended_prompt(
                        root, jobs_path, handoff, receipts_path, operational_path,
                        job["job_id"], amendments_path=amendment_path,
                    )

    def test_amended_receipt_binds_register_and_timestamp_while_history_stays_valid(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, amendment_path, jobs, jobs_bytes = self.copy_contract(root)
            handoff = self.build_handoff(root)
            job = jobs["jobs"][0]
            raw_path = root / "assets/remastered/style-proof/generation/raw" / job["job_id"] / "attempt-0.png"
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(REFERENCES.encode_png(
                REFERENCES.RGBAImage(2, 2, bytes((12, 34, 56, 255)) * 4)
            ))
            inspected = TOOL.inspect_raw_attempt(
                root, jobs_path, handoff, job["job_id"], 0, None, amendment_path
            )
            self.assertIn("Reference interpretation correction", inspected["resolved_prompt"])
            entry = {
                "input": inspected["input"],
                "job_id": job["job_id"],
                "model_provenance": {
                    "completed_at": "2026-08-13T03:01:00Z",
                    "model": "test-model",
                    "parameters": {"quality": "test"},
                    "prediction_id": "test-prediction",
                    "provider": "unit-test",
                    "seed": None,
                    "started_at": "2026-08-13T03:00:00Z",
                    "version": "1",
                },
                "output": None,
                "policy_evidence": None,
                "post_processing": [],
                "prior_attempts": [],
                "prompt_amendment_provenance": inspected["prompt_amendment_provenance"],
                "prompt_sha256": inspected["prompt_sha256"],
                "raw_output": inspected["raw_output"],
                "regeneration_ordinal": 0,
                "rejection_reason": "Test rejection.",
                "resolved_prompt": inspected["resolved_prompt"],
                "retry_prompt_supplement": None,
                "review_status": "rejected",
                "reviewer": "Test Reviewer",
                "shared_style_sha256": inspected["shared_style_sha256"],
            }
            receipt = TOOL.empty_receipt_manifest(jobs, jobs_bytes)
            receipt["entries"].append(entry)
            receipts_path = root / TOOL.DEFAULT_RECEIPTS
            receipts_path.parent.mkdir(parents=True, exist_ok=True)
            receipts_path.write_bytes(TOOL._canonical_json(receipt))
            result = TOOL.validate(root, jobs_path, receipts_path, handoff, amendment_path)
            self.assertEqual((job["job_id"],), result.rejected_job_ids)

            supplement = "Targeted correction for the rejected ordinal-0 output."
            retry = TOOL.prepare_amended_prompt(
                root,
                jobs_path,
                handoff,
                receipts_path,
                root / TOOL.DEFAULT_OPERATIONAL_LOG,
                job["job_id"],
                regeneration_ordinal=1,
                retry_prompt_supplement=supplement,
                amendments_path=amendment_path,
            )
            self.assertEqual(1, retry["regeneration_ordinal"])
            self.assertEqual(supplement, retry["retry_prompt_supplement"])
            self.assertEqual(
                inspected["resolved_prompt"] + ". " + supplement,
                retry["resolved_prompt"],
            )

            attempt_one = (
                root
                / "assets/remastered/style-proof/generation/raw"
                / job["job_id"]
                / "attempt-1.png"
            )
            attempt_one.write_bytes(raw_path.read_bytes())
            with self.assertRaisesRegex(TOOL.ValidationError, "raw output directory"):
                TOOL.prepare_amended_prompt(
                    root,
                    jobs_path,
                    handoff,
                    receipts_path,
                    root / TOOL.DEFAULT_OPERATIONAL_LOG,
                    job["job_id"],
                    regeneration_ordinal=1,
                    retry_prompt_supplement=supplement,
                    amendments_path=amendment_path,
                )
            attempt_one.unlink()

            stale = copy.deepcopy(receipt)
            stale["entries"][0]["prompt_amendment_provenance"]["amendment_sha256"] = "0" * 64
            receipts_path.write_bytes(TOOL._canonical_json(stale))
            with self.assertRaisesRegex(TOOL.ValidationError, "provenance is stale"):
                TOOL.validate(root, jobs_path, receipts_path, handoff, amendment_path)

            predating = copy.deepcopy(receipt)
            predating["entries"][0]["model_provenance"]["started_at"] = "2026-08-13T02:54:00Z"
            receipts_path.write_bytes(TOOL._canonical_json(predating))
            with self.assertRaisesRegex(TOOL.ValidationError, "predates"):
                TOOL.validate(root, jobs_path, receipts_path, handoff, amendment_path)

    def test_cli_inspect_attempt_loads_amendment(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, amendment_path, jobs, _ = self.copy_contract(root)
            handoff = self.build_handoff(root)
            job = jobs["jobs"][0]
            raw_path = root / "assets/remastered/style-proof/generation/raw" / job["job_id"] / "attempt-0.png"
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(REFERENCES.encode_png(
                REFERENCES.RGBAImage(1, 1, bytes((1, 2, 3, 255)))
            ))
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = TOOL.main([
                    "--root", str(root), "--jobs", str(jobs_path),
                    "--inputs", str(handoff), "--amendments", str(amendment_path),
                    "inspect-attempt", "--job-id", job["job_id"],
                ])
            self.assertEqual(0, status)
            payload = json.loads(stdout.getvalue())
            self.assertEqual(
                TOOL.PROMPT_AMENDMENT_SET_SHA256,
                payload["prompt_amendment_provenance"]["amendment_set_sha256"],
            )

    def test_missing_renamed_or_tampered_register_fails_every_consumer(self) -> None:
        cases = (
            ("missing-default", None, "missing"),
            ("missing-explicit", "missing.json", "missing"),
            ("renamed-default", None, "missing"),
            ("tampered-default", None, "reviewed lock"),
        )
        for case, explicit_name, error in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp_name:
                root = Path(temp_name)
                jobs_path, amendment_path, jobs, _ = self.copy_contract(root)
                handoff = self.build_handoff(root)
                job = jobs["jobs"][0]
                raw_path = root / "assets/remastered/style-proof/generation/raw" / job["job_id"] / "attempt-0.png"
                raw_path.parent.mkdir(parents=True, exist_ok=True)
                raw_path.write_bytes(REFERENCES.encode_png(
                    REFERENCES.RGBAImage(1, 1, bytes((1, 2, 3, 255)))
                ))
                if case == "missing-default":
                    amendment_path.unlink()
                elif case == "renamed-default":
                    amendment_path.rename(amendment_path.with_name("renamed-amendments.json"))
                elif case == "tampered-default":
                    register = json.loads(amendment_path.read_bytes())
                    register["entries"][0]["reason"] += " Tampered."
                    amendment_path.write_bytes(TOOL._canonical_json(register))
                selected_path = (
                    root / explicit_name if explicit_name is not None else None
                )
                consumers = (
                    lambda: TOOL.prepare_amended_prompt(
                        root,
                        jobs_path,
                        handoff,
                        root / TOOL.DEFAULT_RECEIPTS,
                        root / TOOL.DEFAULT_OPERATIONAL_LOG,
                        job["job_id"],
                        amendments_path=selected_path,
                    ),
                    lambda: TOOL.inspect_raw_attempt(
                        root,
                        jobs_path,
                        handoff,
                        job["job_id"],
                        0,
                        None,
                        selected_path,
                    ),
                    lambda: TOOL.validate(
                        root,
                        jobs_path,
                        root / TOOL.DEFAULT_RECEIPTS,
                        handoff,
                        selected_path,
                    ),
                )
                for consume in consumers:
                    with self.assertRaisesRegex(TOOL.ValidationError, error):
                        consume()

    def test_missing_or_tampered_topology_register_fails_closed(self) -> None:
        for case, error in (("missing", "missing"), ("tampered", "reviewed lock")):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp_name:
                root = Path(temp_name)
                jobs_path, amendment_path, jobs, _ = self.copy_contract(root)
                handoff = self.build_handoff(root)
                topology_path = root / TOOL.DEFAULT_TOPOLOGY_AMENDMENTS
                job_id = "19_world_dungeon_cicn_m83"
                if case == "missing":
                    topology_path.unlink()
                else:
                    register = json.loads(topology_path.read_bytes())
                    register["entries"][-1]["reason"] += " Tampered."
                    topology_path.write_bytes(TOOL._canonical_json(register))
                consumers = (
                    lambda: TOOL.prepare_amended_prompt(
                        root,
                        jobs_path,
                        handoff,
                        root / TOOL.DEFAULT_RECEIPTS,
                        root / TOOL.DEFAULT_OPERATIONAL_LOG,
                        job_id,
                        amendments_path=amendment_path,
                        topology_amendments_path=topology_path,
                    ),
                    lambda: TOOL.validate(
                        root,
                        jobs_path,
                        root / TOOL.DEFAULT_RECEIPTS,
                        handoff,
                        amendment_path,
                        topology_path,
                    ),
                )
                for consume in consumers:
                    with self.assertRaisesRegex(TOOL.ValidationError, error):
                        consume()


if __name__ == "__main__":
    unittest.main()
