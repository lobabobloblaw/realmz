import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
TOOL_PATH = REPO / "scripts" / "prepare_style_proof_references.py"
sys.path.insert(0, str(TOOL_PATH.parent))
import prepare_style_proof_references as TOOL  # noqa: E402


class StyleProofReferenceTest(unittest.TestCase):
    def load_context(self, selection_path: Path | None = None):
        return TOOL._load_selection(
            REPO,
            selection_path
            or REPO / "assets/remastered/style-proof/classic-selection.json",
        )

    def test_selection_has_exact_family_coverage_and_locked_city_policy(self) -> None:
        context = self.load_context()
        entries = context.selection["entries"]
        self.assertEqual(24, len(entries))
        self.assertEqual(list(range(1, 25)), [entry["order"] for entry in entries])
        self.assertEqual(
            24,
            len(
                {
                    (entry["key"]["pack"], entry["key"]["type"], entry["key"]["id"])
                    for entry in entries
                }
            ),
        )
        self.assertFalse(context.selection["city_reference_policy"]["generation_eligible"])
        self.assertEqual(
            "classic-contact-sheet-and-audit-only",
            context.selection["city_reference_policy"]["allowed_use"],
        )

    def test_committed_reference_set_and_contact_sheet_verify(self) -> None:
        TOOL.verify_references(
            self.load_context(),
            REPO / "assets/remastered/style-proof/classic-references",
        )

    def test_city_generation_eligibility_fails_closed(self) -> None:
        selection = json.loads(
            (REPO / "assets/remastered/style-proof/classic-selection.json").read_text(
                encoding="utf-8"
            )
        )
        mutated = copy.deepcopy(selection)
        mutated["city_reference_policy"]["generation_eligible"] = True
        with tempfile.TemporaryDirectory() as temp_name:
            path = Path(temp_name) / "selection.json"
            path.write_text(
                json.dumps(mutated, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(TOOL.ValidationError, "ineligible for generation"):
                self.load_context(path)

    def test_canonical_png_round_trip_and_crc_gate(self) -> None:
        image = TOOL.RGBAImage(
            width=2,
            height=2,
            pixels=bytes(
                (
                    255,
                    0,
                    0,
                    255,
                    0,
                    255,
                    0,
                    128,
                    0,
                    0,
                    255,
                    64,
                    255,
                    255,
                    255,
                    0,
                )
            ),
        )
        encoded = TOOL.encode_png(image)
        self.assertEqual(image, TOOL.decode_png(encoded))
        corrupted = bytearray(encoded)
        corrupted[-8] ^= 1
        with self.assertRaisesRegex(TOOL.ValidationError, "CRC mismatch"):
            TOOL.decode_png(bytes(corrupted))

    def test_generation_export_refuses_repository_city(self) -> None:
        context = self.load_context()
        references = REPO / "assets/remastered/style-proof/classic-references"
        with tempfile.TemporaryDirectory() as temp_name:
            output = Path(temp_name) / "generation-inputs"
            TOOL.export_generation_inputs(context, references, output)
            manifest = json.loads(
                (output / TOOL.GENERATION_INPUT_MANIFEST).read_text(encoding="utf-8")
            )
            self.assertEqual(21, len(manifest["entries"]))
            self.assertEqual(3, len(manifest["refused_references"]))
            self.assertFalse(manifest["complete_style_proof"])
            self.assertFalse(manifest["approval_eligible"])
            self.assertTrue(
                all(
                    entry["source_role"] != "city-repository-bundled-unverified"
                    for entry in manifest["entries"]
                )
            )
            self.assertEqual(21, len(list((output / "references").glob("*.png"))))
            TOOL.verify_generation_inputs(context, output)
            (output / "references" / "unmanifested-city.png").write_bytes(b"city")
            with self.assertRaisesRegex(TOOL.ValidationError, "unmanifested files"):
                TOOL.verify_generation_inputs(context, output)

    def test_manifest_fields_and_pixels_are_bound_to_trusted_inputs(self) -> None:
        context = self.load_context()
        source = REPO / "assets/remastered/style-proof/classic-references"
        with tempfile.TemporaryDirectory() as temp_name:
            copy_root = Path(temp_name) / "copy"
            shutil.copytree(source, copy_root)
            manifest_path = copy_root / TOOL.OUTPUT_MANIFEST
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["entries"][0]["alpha_policy"] = "original_mask"
            manifest_path.write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(TOOL.ValidationError, "locked source data"):
                TOOL.verify_references(context, copy_root)

            shutil.rmtree(copy_root)
            shutil.copytree(source, copy_root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            image_path = copy_root / manifest["entries"][0]["decoded_png"]["path"]
            image = TOOL.decode_png(image_path.read_bytes())
            black = TOOL.RGBAImage(
                image.width,
                image.height,
                bytes((0, 0, 0, 255)) * image.width * image.height,
            )
            black_bytes = TOOL.encode_png(black)
            image_path.write_bytes(black_bytes)
            manifest["entries"][0]["decoded_png"]["sha256"] = TOOL._sha256_bytes(
                black_bytes
            )
            manifest_path.write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(TOOL.ValidationError, "trusted lock"):
                TOOL.verify_references(context, copy_root)


if __name__ == "__main__":
    unittest.main()
