import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT_DIR = REPO / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))
import prepare_item_variant_call as GATE  # noqa: E402


class ItemVariantCallGateTest(unittest.TestCase):
    CALL_TIME = "2026-08-13T04:00:00Z"
    CANDIDATE_ID = "13_item_spell_cicn_6106"
    STANDALONE_ID = "14_item_spell_cicn_566"
    VARIANT_ID = "15_item_spell_cicn_12044"
    BASE_ID = "14_item_spell_cicn_566"

    @classmethod
    def setUpClass(cls) -> None:
        cls.generation = REPO / "assets/remastered/style-proof/generation"
        cls.jobs_path = cls.generation / "jobs.json"
        cls.policy_path = cls.generation / "variant-reference-policy.json"
        cls.classification_path = cls.generation / "item-classifications.json"
        cls.input_dir = REPO / "assets/remastered/style-proof/classic-references"

    @staticmethod
    def sha(data: bytes) -> str:
        return hashlib.sha256(data).hexdigest()

    def write_synthetic_contract(
        self,
        root: Path,
        *,
        mutate_jobs=None,
        mutate_receipt=None,
        include_receipt=True,
    ) -> tuple[Path, Path, dict, dict]:
        jobs = json.loads(self.jobs_path.read_bytes())
        by_id = {job["job_id"]: job for job in jobs["jobs"]}
        variant = by_id[self.VARIANT_ID]
        base = by_id[self.BASE_ID]
        variant["variant_of"] = {
            "job_id": self.BASE_ID,
            "resource_key": copy.deepcopy(base["resource_key"]),
        }
        if mutate_jobs is not None:
            mutate_jobs(jobs, by_id, variant, base)

        manifest_relative = jobs["source_reference_manifest"]["path"]
        manifest_source = REPO / manifest_relative
        manifest_target = root / manifest_relative
        manifest_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(manifest_source, manifest_target)

        jobs_path = root / GATE.DEFAULT_JOBS
        jobs_path.parent.mkdir(parents=True, exist_ok=True)
        jobs_bytes = GATE._canonical_json_bytes(jobs)
        jobs_path.write_bytes(jobs_bytes)

        classifications = json.loads(self.classification_path.read_bytes())
        classifications["jobs_sha256"] = self.sha(jobs_bytes)
        classification_by_id = {
            entry["job_id"]: entry for entry in classifications["entries"]
        }
        base_classification = classification_by_id[self.BASE_ID]
        base_classification.clear()
        base_classification.update({
            "classification": "base",
            "generation_allowed": True,
            "job_id": self.BASE_ID,
            "reference_roles": [GATE.OWN_CLASSIC_ROLE],
            "resource_key": copy.deepcopy(base["resource_key"]),
        })
        variant_classification = classification_by_id[self.VARIANT_ID]
        variant_classification.clear()
        variant_classification.update({
            "base_job_id": self.BASE_ID,
            "base_resource_key": copy.deepcopy(base["resource_key"]),
            "classification": "variant",
            "generation_allowed": True,
            "job_id": self.VARIANT_ID,
            "reference_roles": [GATE.OWN_CLASSIC_ROLE, GATE.BASE_MASTER_ROLE],
            "resource_key": copy.deepcopy(variant["resource_key"]),
        })
        for entry in classifications["entries"]:
            entry["resource_key"] = copy.deepcopy(
                by_id[entry["job_id"]]["resource_key"]
            )
        classification_path = root / GATE.DEFAULT_CLASSIFICATIONS
        classification_path.write_bytes(GATE._canonical_json_bytes(classifications))
        source_relative = "src/realmz_orig/combatinfo-combatchoice.c"
        source_target = root / source_relative
        source_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / source_relative, source_target)

        base_source = self.input_dir / base["input"]["path"]
        base_output = root / base["expected_output_path"]
        base_output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(base_source, base_output)
        output_data = base_output.read_bytes()

        evidence_relative = (
            "assets/remastered/style-proof/generation/evidence/"
            f"{self.BASE_ID}-ocr.txt"
        )
        evidence = root / evidence_relative
        evidence.parent.mkdir(parents=True, exist_ok=True)
        evidence.write_bytes(b"deterministic OCR fixture: no text detected\n")

        receipt = {
            "input": copy.deepcopy(base["input"]),
            "job_id": self.BASE_ID,
            "model_provenance": {
                "completed_at": "2026-08-13T02:59:00Z",
            },
            "output": {
                "path": base["expected_output_path"],
                "sha256": self.sha(output_data),
            },
            "policy_evidence": {
                "baked_lighting": {
                    "method": "human review of isolated master",
                    "result": "no-unintended-baked-lighting",
                    "reviewed": True,
                },
                "ocr": {
                    "evidence_path": evidence_relative,
                    "evidence_sha256": self.sha(evidence.read_bytes()),
                    "method": "fixture OCR",
                    "performed": True,
                    "result": "no-text-detected",
                },
            },
            "resource_key": copy.deepcopy(base["resource_key"]),
            "review_status": "approved",
            "reviewed_at": "2026-08-13T03:00:00Z",
            "reviewer": "Test Human Reviewer",
        }
        if mutate_receipt is not None:
            mutate_receipt(receipt, root, base)
        receipts = {
            "entries": [receipt] if include_receipt else [],
            "jobs_sha256": self.sha(jobs_bytes),
        }
        receipts_path = root / GATE.DEFAULT_RECEIPTS
        receipts_path.write_bytes(GATE._canonical_json_bytes(receipts))
        return jobs_path, receipts_path, jobs, receipt

    def prepare(
        self,
        root: Path,
        jobs_path: Path,
        receipts_path: Path,
        job_id: str | None = None,
    ) -> dict:
        return GATE.prepare_call(
            root=root,
            jobs_path=jobs_path,
            receipts_path=receipts_path,
            input_dir=self.input_dir,
            policy_path=self.policy_path,
            job_id=job_id or self.VARIANT_ID,
            call_started_at_text=self.CALL_TIME,
            classification_path=root / GATE.DEFAULT_CLASSIFICATIONS,
        )

    def test_standalone_emits_only_its_own_locked_classic_path(self) -> None:
        value = GATE.prepare_call(
            root=REPO,
            jobs_path=self.jobs_path,
            receipts_path=self.generation / "generation-receipts.json",
            input_dir=self.input_dir,
            policy_path=self.policy_path,
            job_id=self.STANDALONE_ID,
            call_started_at_text=self.CALL_TIME,
            classification_path=self.classification_path,
        )
        job = next(
            job
            for job in json.loads(self.jobs_path.read_bytes())["jobs"]
            if job["job_id"] == self.STANDALONE_ID
        )
        self.assertEqual([GATE.OWN_CLASSIC_ROLE], [
            item["role"] for item in value["generation_references"]
        ])
        self.assertEqual(
            [str((self.input_dir / job["input"]["path"]).resolve())],
            value["referenced_image_paths"],
        )
        expected_hash = GATE._sha256_bytes(
            GATE._canonical_reference_bytes(value["generation_references"])
        )
        self.assertEqual(expected_hash, value["reference_set_sha256"])
        self.assertEqual(
            self.sha(self.classification_path.read_bytes()),
            value["classification_manifest_sha256"],
        )
        self.assertEqual("standalone", value["item_classification"]["classification"])
        self.assertEqual(
            value["item_classification"]["reference_roles"],
            [item["role"] for item in value["generation_references"]],
        )

    def test_known_variant_candidate_cannot_sneak_through_as_standalone(self) -> None:
        with self.assertRaisesRegex(GATE.GateError, "known_variant_candidate"):
            GATE.prepare_call(
                root=REPO,
                jobs_path=self.jobs_path,
                receipts_path=self.generation / "generation-receipts.json",
                input_dir=self.input_dir,
                policy_path=self.policy_path,
                job_id=self.CANDIDATE_ID,
                call_started_at_text=self.CALL_TIME,
                classification_path=self.classification_path,
            )

    def test_missing_policy_blocks_before_a_call_can_be_prepared(self) -> None:
        with self.assertRaisesRegex(GATE.GateError, "policy is missing"):
            GATE.prepare_call(
                root=REPO,
                jobs_path=self.jobs_path,
                receipts_path=self.generation / "generation-receipts.json",
                input_dir=self.input_dir,
                policy_path=REPO / "does-not-exist-variant-policy.json",
                job_id=self.VARIANT_ID,
                call_started_at_text=self.CALL_TIME,
            )

    def test_missing_incomplete_or_stale_classification_blocks_pre_call(self) -> None:
        with self.assertRaisesRegex(GATE.GateError, "classification manifest is missing"):
            GATE.prepare_call(
                root=REPO,
                jobs_path=self.jobs_path,
                receipts_path=self.generation / "generation-receipts.json",
                input_dir=self.input_dir,
                policy_path=self.policy_path,
                job_id=self.STANDALONE_ID,
                call_started_at_text=self.CALL_TIME,
                classification_path=REPO / "does-not-exist-item-classifications.json",
            )

        for mutation, expected in (
            (lambda value: value["entries"].pop(), "classify every item job"),
            (lambda value: value.update(jobs_sha256="0" * 64), "does not match the jobs"),
            (
                lambda value: value["entries"][1].update(classification="animation_frame"),
                "fields do not match classification animation_frame",
            ),
        ):
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as name:
                root = Path(name)
                path = root / "item-classifications.json"
                value = json.loads(self.classification_path.read_bytes())
                mutation(value)
                path.write_bytes(GATE._canonical_json_bytes(value))
                with self.assertRaisesRegex(GATE.GateError, expected):
                    GATE.prepare_call(
                        root=REPO,
                        jobs_path=self.jobs_path,
                        receipts_path=self.generation / "generation-receipts.json",
                        input_dir=self.input_dir,
                        policy_path=self.policy_path,
                        job_id=self.STANDALONE_ID,
                        call_started_at_text=self.CALL_TIME,
                        classification_path=path,
                    )

    def test_animation_sequence_membership_and_source_evidence_are_locked(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            jobs_path, receipts_path, _, _ = self.write_synthetic_contract(root)
            classification_path = root / GATE.DEFAULT_CLASSIFICATIONS
            value = json.loads(classification_path.read_bytes())
            animation = next(
                entry for entry in value["entries"]
                if entry["job_id"] == "16_item_spell_cicn_12100"
            )
            animation["sequence"]["frame_index"] = 3
            classification_path.write_bytes(GATE._canonical_json_bytes(value))
            with self.assertRaisesRegex(GATE.GateError, "frame order or ResourceKey membership"):
                self.prepare(root, jobs_path, receipts_path)

        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            jobs_path, receipts_path, _, _ = self.write_synthetic_contract(root)
            source = root / "src/realmz_orig/combatinfo-combatchoice.c"
            source.write_bytes(source.read_bytes() + b"\n/* tampered evidence */\n")
            with self.assertRaisesRegex(GATE.GateError, "source content SHA-256"):
                self.prepare(root, jobs_path, receipts_path)

    def test_variant_emits_exact_ordered_classic_and_approved_base(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            jobs_path, receipts_path, jobs, receipt = self.write_synthetic_contract(root)
            value = self.prepare(root, jobs_path, receipts_path)
            by_id = {job["job_id"]: job for job in jobs["jobs"]}
            variant = by_id[self.VARIANT_ID]
            self.assertEqual(
                [GATE.OWN_CLASSIC_ROLE, GATE.BASE_MASTER_ROLE],
                [item["role"] for item in value["generation_references"]],
            )
            self.assertEqual(
                [
                    str((self.input_dir / variant["input"]["path"]).resolve()),
                    str((root / receipt["output"]["path"]).resolve()),
                ],
                value["referenced_image_paths"],
            )
            self.assertEqual(
                variant["resource_key"],
                value["generation_references"][0]["resource_key"],
            )
            self.assertEqual(
                by_id[self.BASE_ID]["resource_key"],
                value["generation_references"][1]["resource_key"],
            )
            self.assertEqual(
                GATE._sha256_bytes(
                    GATE._canonical_reference_bytes(value["generation_references"])
                ),
                value["reference_set_sha256"],
            )
            self.assertEqual(
                self.sha((root / GATE.DEFAULT_CLASSIFICATIONS).read_bytes()),
                value["classification_manifest_sha256"],
            )
            self.assertEqual("variant", value["item_classification"]["classification"])
            self.assertEqual(
                [GATE.OWN_CLASSIC_ROLE, GATE.BASE_MASTER_ROLE],
                value["item_classification"]["reference_roles"],
            )

    def test_variant_rejects_unapproved_stale_or_unverified_base(self) -> None:
        cases = {
            "not approved": lambda receipt, _root, _base: receipt.update(
                review_status="generated"
            ),
            "reviewer": lambda receipt, _root, _base: receipt.update(reviewer=None),
            "reviewed_at": lambda receipt, _root, _base: receipt.update(
                reviewed_at=self.CALL_TIME
            ),
            "OCR evidence": lambda receipt, _root, _base: receipt[
                "policy_evidence"
            ]["ocr"].update(performed=False, result="not-run"),
            "baked-lighting evidence": lambda receipt, _root, _base: receipt[
                "policy_evidence"
            ]["baked_lighting"].update(
                reviewed=False, result="not-reviewed"
            ),
            "output content": lambda receipt, root, _base: (
                root / receipt["output"]["path"]
            ).write_bytes(b"tampered output"),
            "receipt ResourceKey": lambda receipt, _root, _base: receipt[
                "resource_key"
            ].update(id=999999),
        }
        for expected, mutation in cases.items():
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as name:
                root = Path(name)
                jobs_path, receipts_path, _, _ = self.write_synthetic_contract(
                    root, mutate_receipt=mutation
                )
                with self.assertRaisesRegex(GATE.GateError, expected):
                    self.prepare(root, jobs_path, receipts_path)

    def test_variant_rejects_missing_base_key_mismatch_cycle_and_city(self) -> None:
        def missing(_jobs, _by_id, variant, _base):
            variant["variant_of"]["job_id"] = "missing_item_base"

        def mismatch(_jobs, _by_id, variant, _base):
            variant["variant_of"]["resource_key"]["id"] += 1

        def cycle(_jobs, _by_id, variant, base):
            base["variant_of"] = {
                "job_id": variant["job_id"],
                "resource_key": copy.deepcopy(variant["resource_key"]),
            }

        def city(_jobs, _by_id, variant, _base):
            variant["resource_key"]["pack"] = "Scenarios/City of Bywater/Scenario"

        cases = {
            "missing base": missing,
            "ResourceKey mismatches": mismatch,
            "cycle detected": cycle,
            "blocked City": city,
        }
        for expected, mutation in cases.items():
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as name:
                root = Path(name)
                jobs_path, receipts_path, _, _ = self.write_synthetic_contract(
                    root, mutate_jobs=mutation
                )
                with self.assertRaisesRegex(GATE.GateError, expected):
                    self.prepare(root, jobs_path, receipts_path)

    def test_variant_rejects_missing_base_receipt_and_own_classic_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            jobs_path, receipts_path, _, _ = self.write_synthetic_contract(
                root, include_receipt=False
            )
            with self.assertRaisesRegex(GATE.GateError, "no generation receipt"):
                self.prepare(root, jobs_path, receipts_path)

        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            jobs_path, receipts_path, jobs, _ = self.write_synthetic_contract(root)
            copied_inputs = root / "inputs"
            shutil.copytree(self.input_dir / "references", copied_inputs / "references")
            variant = next(
                job for job in jobs["jobs"] if job["job_id"] == self.VARIANT_ID
            )
            (copied_inputs / variant["input"]["path"]).write_bytes(b"tampered Classic")
            with self.assertRaisesRegex(GATE.GateError, "Classic input content SHA-256"):
                GATE.prepare_call(
                    root=root,
                    jobs_path=jobs_path,
                    receipts_path=receipts_path,
                    input_dir=copied_inputs,
                    policy_path=self.policy_path,
                    job_id=self.VARIANT_ID,
                    call_started_at_text=self.CALL_TIME,
                )


if __name__ == "__main__":
    unittest.main()
