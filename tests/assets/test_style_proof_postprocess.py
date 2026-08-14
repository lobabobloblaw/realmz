from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPO = Path(__file__).resolve().parents[2]
SCRIPTS = REPO / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import prepare_style_proof_references as REFERENCES  # noqa: E402
import process_style_proof_output as TOOL  # noqa: E402


def png(width: int, height: int) -> bytes:
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            pixels.extend(((x * 31 + y * 7) % 256, (x * 5 + y * 29) % 256, 80, 255))
    return REFERENCES.encode_png(REFERENCES.RGBAImage(width, height, bytes(pixels)))


def job(*, width: int, height: int, family: str = "portrait") -> dict:
    return {
        "alpha": {"input_alpha": "fully_opaque", "mode": "opaque"},
        "expected_output_path": "assets/remastered/style-proof/generation/outputs/test_job.png",
        "family": family,
        "job_id": "test_job",
        "target_master": {
            "color_space": "sRGB",
            "format": "PNG",
            "height": height,
            "width": width,
        },
    }


class StyleProofPostprocessTests(unittest.TestCase):
    def test_exact_dimensions_srgb_rgba8_and_opaque(self) -> None:
        result = TOOL.process_opaque_png(png(8, 8), job(width=16, height=16))
        image = REFERENCES.decode_png(
            result.final_png, expected_dimensions=(16, 16)
        )
        self.assertEqual(16 * 16 * 4, len(image.pixels))
        self.assertTrue(all(value == 255 for value in image.pixels[3::4]))
        self.assertIn(b"sRGB\x00", result.final_png)
        self.assertEqual(
            ["resize_to_target", "encode_srgb_png"],
            [operation["operation"] for operation in result.operations],
        )
        self.assertEqual(
            hashlib.sha256(result.final_png).hexdigest(),
            result.operations[-1]["parameters"]["output_sha256"],
        )

    def test_ui_blend_changes_edge_band_and_makes_exact_seams(self) -> None:
        raw = png(16, 16)
        original = REFERENCES.decode_png(raw)
        result = TOOL.process_opaque_png(
            raw, job(width=16, height=16, family="ui_material")
        )
        output = REFERENCES.decode_png(result.final_png)
        for y in range(output.height):
            left = (y * output.width) * 4
            right = (y * output.width + output.width - 1) * 4
            self.assertEqual(
                output.pixels[left:left + 4], output.pixels[right:right + 4]
            )
        row_bytes = output.width * 4
        self.assertEqual(output.pixels[:row_bytes], output.pixels[-row_bytes:])
        middle_y = 8
        interior_band_pixel = (middle_y * 16 + 1) * 4
        self.assertNotEqual(
            original.pixels[interior_band_pixel:interior_band_pixel + 4],
            output.pixels[interior_band_pixel:interior_band_pixel + 4],
        )
        seam = result.operations[1]
        self.assertEqual("make_tileable_opposite_edges", seam["operation"])
        self.assertTrue(seam["parameters"]["output_changed"])
        self.assertGreaterEqual(seam["parameters"]["edge_band_pixels"], 2)

    def test_reproducible_bytes_and_operation_chain(self) -> None:
        raw = png(11, 11)
        definition = job(width=22, height=22, family="ui_material")
        first = TOOL.process_opaque_png(raw, definition)
        second = TOOL.process_opaque_png(raw, definition)
        self.assertEqual(first.final_png, second.final_png)
        self.assertEqual(first.operations, second.operations)
        self.assertEqual(first.intermediate_states, second.intermediate_states)
        previous = hashlib.sha256(raw).hexdigest()
        for operation in first.operations:
            parameters = operation["parameters"]
            self.assertEqual(previous, parameters["input_sha256"])
            self.assertEqual(
                parameters["input_sha256"] != parameters["output_sha256"],
                parameters["output_changed"],
            )
            previous = parameters["output_sha256"]
        self.assertEqual(hashlib.sha256(first.final_png).hexdigest(), previous)

    def test_aspect_mismatch_is_rejected_without_crop_or_stretch(self) -> None:
        with self.assertRaisesRegex(TOOL.ProcessingError, "aspect ratios differ"):
            TOOL.process_opaque_png(png(6, 4), job(width=8, height=8))

    def test_hash_input_and_overwrite_are_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            definition = job(width=8, height=8)
            raw = png(4, 4)
            raw_path = (
                root
                / "assets/remastered/style-proof/generation/raw/test_job/attempt-0.png"
            )
            raw_path.parent.mkdir(parents=True)
            raw_path.write_bytes(raw)
            digest = hashlib.sha256(raw).hexdigest()
            with mock.patch.object(
                TOOL, "_load_immutable_job", return_value=definition
            ):
                with self.assertRaisesRegex(TOOL.ProcessingError, "differs"):
                    TOOL.process_job(
                        root, Path("jobs.json"), "test_job", 0, "0" * 64
                    )
                fragment = TOOL.process_job(
                    root,
                    Path("jobs.json"),
                    "test_job",
                    0,
                    digest,
                    write_intermediates=True,
                )
                with self.assertRaisesRegex(TOOL.ProcessingError, "overwrite"):
                    TOOL.process_job(
                        root, Path("jobs.json"), "test_job", 0, digest
                    )
            output_path = root / definition["expected_output_path"]
            self.assertTrue(output_path.is_file())
            artifacts = fragment["intermediate_artifacts"]
            self.assertEqual(1, len(artifacts))
            artifact_path = root / artifacts[0]["path"]
            self.assertEqual(
                artifacts[0]["sha256"],
                hashlib.sha256(artifact_path.read_bytes()).hexdigest(),
            )
            operation_fragment = artifact_path.parent / "operation-fragment.json"
            parsed = json.loads(operation_fragment.read_bytes())
            self.assertEqual(fragment, parsed)
            self.assertEqual(
                TOOL._canonical_json(parsed), operation_fragment.read_bytes()
            )


if __name__ == "__main__":
    unittest.main()
