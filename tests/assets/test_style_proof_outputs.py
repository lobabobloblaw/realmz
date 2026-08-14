import copy
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


class StyleProofOutputTest(unittest.TestCase):
    def setUp(self) -> None:
        self.jobs_path = (
            REPO / "assets/remastered/style-proof/generation/jobs.json"
        )
        self.inputs = REPO / "assets/remastered/style-proof/classic-references"

    def copy_locked_contract(self, destination: Path) -> tuple[Path, dict, bytes]:
        relative_paths = (
            "assets/remastered/style-proof/generation/jobs.json",
            "assets/remastered/style-proof/generation/style-spec.json",
            "assets/remastered/style-proof/generation/prompt-amendments.json",
            "assets/remastered/style-proof/generation/masked-topology-amendments.json",
            "assets/remastered/style-proof/generation/masked-topology-amendments.schema.json",
            "assets/remastered/style-proof/classic-selection.json",
            "assets/remastered/style-proof/classic-references/classic-reference-manifest.json",
        )
        for relative in relative_paths:
            source = REPO / relative
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        jobs_path = destination / relative_paths[0]
        jobs_bytes = jobs_path.read_bytes()
        return jobs_path, json.loads(jobs_bytes), jobs_bytes

    def build_handoff(self, destination: Path) -> Path:
        handoff = destination / "generation-inputs"
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

    @staticmethod
    def write_constant_output(root: Path, job: dict, color=(74, 68, 63, 255)) -> Path:
        target = job["target_master"]
        image = REFERENCES.RGBAImage(
            target["width"],
            target["height"],
            bytes(color) * target["width"] * target["height"],
        )
        path = root / job["expected_output_path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(REFERENCES.encode_png(image))
        return path

    @staticmethod
    def receipt_entry(inspected: dict, *, status="generated") -> dict:
        return {
            "input": inspected["input"],
            "job_id": inspected["job_id"],
            "model_provenance": {
                "completed_at": "2026-08-12T20:01:00Z",
                "model": "test-model",
                "parameters": {"quality": "test"},
                "prediction_id": "test-prediction",
                "provider": "unit-test",
                "seed": 123,
                "started_at": "2026-08-12T20:00:00Z",
                "version": "1",
            },
            "output": inspected["output"],
            "policy_evidence": inspected["policy_evidence"],
            "post_processing": [],
            "prior_attempts": [],
            "prompt_sha256": inspected["prompt_sha256"],
            "raw_output": inspected["raw_output"],
            "regeneration_ordinal": 0,
            "rejection_reason": None,
            "resolved_prompt": inspected["resolved_prompt"],
            "retry_prompt_supplement": None,
            "review_status": status,
            "reviewer": None if status == "generated" else "Test Reviewer",
            "shared_style_sha256": inspected["shared_style_sha256"],
        }

    def test_missing_receipt_and_outputs_are_pending_not_approved(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, _, _ = self.copy_locked_contract(root)
            result = TOOL.validate(
                root,
                jobs_path,
                root / TOOL.DEFAULT_RECEIPTS,
                self.inputs,
            )
            self.assertEqual(21, result.total_jobs)
            self.assertEqual(21, len(result.pending_job_ids))
            self.assertEqual(3, result.refused_city_count)
            self.assertFalse(result.complete_style_proof)
            self.assertFalse(result.approval_eligible)
            with self.assertRaisesRegex(TOOL.ValidationError, "no valid"):
                TOOL.build_sheets(result, root / "sheets")

    def test_prompt_assembly_is_exact_and_bound_to_shared_style(self) -> None:
        jobs, _, style = TOOL._load_job_set(REPO, self.jobs_path)
        job = jobs["jobs"][1]
        prompt = TOOL.assemble_prompt(style, job)
        expected_components = [
            style["style_prompt"],
            job["prompt_components"]["subject"],
            job["prompt_components"]["composition"],
            job["prompt_components"]["background"],
            "; ".join(job["semantic_preservation"]),
            "; ".join(job["policies"][name] for name in ("seams", "text", "baked_lighting")),
            "; ".join(style["global_negative_prompt"]),
            job["prompt_components"]["negative"],
        ]
        self.assertEqual(". ".join(expected_components), prompt)
        self.assertNotEqual(
            TOOL._sha256_bytes(prompt.encode("utf-8")),
            TOOL._sha256_bytes((prompt + ".").encode("utf-8")),
        )

    def test_valid_single_output_receipt_and_sheets_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, jobs, jobs_bytes = self.copy_locked_contract(root)
            handoff = self.build_handoff(root)
            job = jobs["jobs"][6]  # opaque portrait: no tile post-processing gate
            output_path = self.write_constant_output(root, job)
            raw_path = (
                root
                / "assets/remastered/style-proof/generation/raw"
                / job["job_id"]
                / "attempt-0.png"
            )
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(output_path, raw_path)
            inspected = TOOL.inspect_job_output(
                root, jobs_path, handoff, job["job_id"]
            )
            receipt = TOOL.empty_receipt_manifest(jobs, jobs_bytes)
            receipt["entries"].append(self.receipt_entry(inspected))
            receipts_path = root / TOOL.DEFAULT_RECEIPTS
            receipts_path.parent.mkdir(parents=True, exist_ok=True)
            receipts_path.write_bytes(TOOL._canonical_json(receipt))

            result = TOOL.validate(root, jobs_path, receipts_path, handoff)
            self.assertEqual(1, len(result.valid_outputs))
            self.assertEqual(20, len(result.pending_job_ids))
            self.assertFalse(result.approval_eligible)
            first = TOOL.build_sheets(result, root / "review")
            first_bytes = {
                name: (root / "review" / name).read_bytes() for name in first
            }
            second = TOOL.build_sheets(result, root / "review")
            self.assertEqual(first, second)
            self.assertEqual(
                first_bytes,
                {name: (root / "review" / name).read_bytes() for name in second},
            )
            for name, digest in first.items():
                data = first_bytes[name]
                self.assertEqual(digest, TOOL._sha256_bytes(data))
                REFERENCES.decode_png(data, name)

            mutated = copy.deepcopy(receipt)
            mutated["entries"][0]["prompt_sha256"] = "0" * 64
            receipts_path.write_bytes(TOOL._canonical_json(mutated))
            with self.assertRaisesRegex(TOOL.ValidationError, "prompt SHA-256"):
                TOOL.validate(root, jobs_path, receipts_path, handoff)

    def make_masked_output(
        self,
        root: Path,
        job: dict,
        *,
        foreground=(83, 72, 64),
    ) -> tuple[Path, REFERENCES.RGBAImage]:
        input_data = (self.inputs / job["input"]["path"]).read_bytes()
        classic = REFERENCES.decode_png(input_data)
        width = job["target_master"]["width"]
        height = job["target_master"]["height"]
        alpha = TOOL._nearest_alpha(classic, width, height)
        pixels = bytearray(width * height * 4)
        for index, value in enumerate(alpha):
            if value:
                pixels[index * 4:index * 4 + 3] = bytes(foreground)
            pixels[index * 4 + 3] = value
        image = REFERENCES.RGBAImage(width, height, bytes(pixels))
        path = root / job["expected_output_path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(REFERENCES.encode_png(image))
        return path, classic

    def test_masked_output_requires_exact_source_mask_and_no_near_chroma_matte(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, jobs, _ = self.copy_locked_contract(root)
            job = jobs["jobs"][10]  # job 11: masked, no required magenta subject
            path, classic = self.make_masked_output(root, job)
            facts, evidence = TOOL._inspect_output(job, classic, path)
            self.assertEqual("RGBA8", facts.pixel_format)
            self.assertTrue(evidence["alpha"]["authoritative_source_mask_match"])
            self.assertEqual(0, evidence["alpha"]["near_chroma_matte_pixels"])

            image = REFERENCES.decode_png(path.read_bytes())
            pixels = bytearray(image.pixels)
            opaque = next(
                index for index, alpha in enumerate(pixels[3::4]) if alpha == 255
            )
            pixels[opaque * 4:opaque * 4 + 3] = bytes((252, 3, 251))
            path.write_bytes(
                REFERENCES.encode_png(
                    REFERENCES.RGBAImage(image.width, image.height, bytes(pixels))
                )
            )
            with self.assertRaisesRegex(TOOL.ValidationError, "near-#FF00FF"):
                TOOL._inspect_output(job, classic, path)

            pixels[opaque * 4:opaque * 4 + 3] = bytes((83, 72, 64))
            pixels[opaque * 4 + 3] = 0
            path.write_bytes(
                REFERENCES.encode_png(
                    REFERENCES.RGBAImage(image.width, image.height, bytes(pixels))
                )
            )
            with self.assertRaisesRegex(TOOL.ValidationError, "authoritative"):
                TOOL._inspect_output(job, classic, path)

    def test_magenta_semantic_jobs_must_retain_non_matte_magenta(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, jobs, _ = self.copy_locked_contract(root)
            job = jobs["jobs"][9]  # job 10: source-significant pink wings
            path, classic = self.make_masked_output(root, job)
            with self.assertRaisesRegex(TOOL.ValidationError, "lost.*magenta"):
                TOOL._inspect_output(job, classic, path)

            image = REFERENCES.decode_png(path.read_bytes())
            pixels = bytearray(image.pixels)
            opaque = next(
                index for index, alpha in enumerate(pixels[3::4]) if alpha == 255
            )
            pixels[opaque * 4:opaque * 4 + 3] = bytes((190, 56, 175))
            path.write_bytes(
                REFERENCES.encode_png(
                    REFERENCES.RGBAImage(image.width, image.height, bytes(pixels))
                )
            )
            _, evidence = TOOL._inspect_output(job, classic, path)
            self.assertGreater(
                evidence["alpha"]["retained_magenta_inside_mask_pixels"], 0
            )

    def test_approval_gates_require_ocr_and_baked_lighting_evidence(self) -> None:
        not_run_ocr = {
            "evidence_path": None,
            "evidence_sha256": None,
            "method": None,
            "performed": False,
            "result": "not-run",
        }
        with self.assertRaisesRegex(TOOL.ValidationError, "requires independent OCR"):
            TOOL._validate_ocr(REPO, not_run_ocr, "approved", "ocr")
        with self.assertRaisesRegex(TOOL.ValidationError, "requires baked-lighting"):
            TOOL._validate_baked_lighting(
                {"method": None, "result": "not-reviewed", "reviewed": False},
                "approved",
                "lighting",
            )

    def test_masked_postprocessing_must_record_safe_ordered_operations(self) -> None:
        image = REFERENCES.RGBAImage(1, 1, bytes((0, 0, 0, 0)))
        facts = TOOL.ImageFacts(image, "RGBA8", True, 0.0, 4, 0.0, 0.0)
        digest0 = "0" * 64
        with self.assertRaisesRegex(TOOL.ValidationError, "mask-safe operations"):
            TOOL._validate_post_processing(
                [],
                masked=True,
                ui_tile=False,
                raw_sha256=digest0,
                raw_facts=facts,
                final_sha256=digest0,
                target_dimensions=(1, 1),
                where="post",
            )
        digests = [str(index) * 64 for index in range(4)]
        TOOL._validate_post_processing(
            [
                {
                    "operation": operation,
                    "parameters": {
                        "input_sha256": digests[index],
                        "output_changed": True,
                        "output_sha256": digests[index + 1],
                    },
                    "tool": "test-tool",
                    "tool_version": "1",
                }
                for index, operation in enumerate((
                    "remove_chroma_outside_original_mask",
                    "apply_original_alpha_mask",
                    "decontaminate_transparent_edges",
                ))
            ],
            masked=True,
            ui_tile=False,
            raw_sha256=digests[0],
            raw_facts=facts,
            final_sha256=digests[-1],
            target_dimensions=(1, 1),
            where="post",
        )

    def test_retry_prompt_is_exactly_base_plus_supplement(self) -> None:
        jobs, _, style = TOOL._load_job_set(REPO, self.jobs_path)
        job = jobs["jobs"][1]
        base = TOOL.assemble_prompt(style, job)
        supplement = "Avoid checkerboard structure; produce organic continuous stone."
        self.assertEqual(
            base + ". " + supplement,
            TOOL.effective_prompt(style, job, 1, supplement),
        )
        with self.assertRaisesRegex(TOOL.ValidationError, "null retry"):
            TOOL.effective_prompt(style, job, 0, supplement)
        with self.assertRaisesRegex(TOOL.ValidationError, "non-empty"):
            TOOL.effective_prompt(style, job, 1, "")

    def test_rejected_raw_attempt_is_receipted_without_claiming_final_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            jobs_path, jobs, jobs_bytes = self.copy_locked_contract(root)
            handoff = self.build_handoff(root)
            job = jobs["jobs"][1]
            raw_path = (
                root
                / "assets/remastered/style-proof/generation/raw"
                / job["job_id"]
                / "attempt-0.png"
            )
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(
                REFERENCES.encode_png(
                    REFERENCES.RGBAImage(2, 2, bytes((34, 45, 56, 255)) * 4)
                )
            )
            inspected = TOOL.inspect_raw_attempt(
                root, jobs_path, handoff, job["job_id"], 0, None
            )
            entry = {
                "input": inspected["input"],
                "job_id": job["job_id"],
                "model_provenance": {
                    "completed_at": "2026-08-12T20:01:00Z",
                    "model": "test-model",
                    "parameters": {"quality": "test"},
                    "prediction_id": "test-prediction",
                    "provider": "unit-test",
                    "seed": None,
                    "started_at": "2026-08-12T20:00:00Z",
                    "version": "1",
                },
                "output": None,
                "policy_evidence": None,
                "post_processing": [],
                "prior_attempts": [],
                "prompt_sha256": inspected["prompt_sha256"],
                "raw_output": inspected["raw_output"],
                "regeneration_ordinal": 0,
                "rejection_reason": "Visible checkerboard structure and non-seamless edges.",
                "resolved_prompt": inspected["resolved_prompt"],
                "retry_prompt_supplement": None,
                "review_status": "rejected",
                "reviewer": "Test Reviewer",
                "shared_style_sha256": inspected["shared_style_sha256"],
            }
            receipt = TOOL.empty_receipt_manifest(jobs, jobs_bytes)
            receipt["entries"].append(entry)
            receipt_path = root / TOOL.DEFAULT_RECEIPTS
            receipt_path.parent.mkdir(parents=True, exist_ok=True)
            receipt_path.write_bytes(TOOL._canonical_json(receipt))
            result = TOOL.validate(root, jobs_path, receipt_path, handoff)
            self.assertEqual((job["job_id"],), result.rejected_job_ids)
            self.assertEqual(20, len(result.pending_job_ids))
            self.assertEqual(0, len(result.valid_outputs))
            self.assertFalse(result.approval_eligible)


if __name__ == "__main__":
    unittest.main()
