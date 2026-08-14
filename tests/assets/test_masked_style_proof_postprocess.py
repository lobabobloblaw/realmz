from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from PIL import Image


REPO = Path(__file__).resolve().parents[2]
SCRIPTS = REPO / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import prepare_style_proof_references as REFERENCES  # noqa: E402
import process_masked_style_proof_output as TOOL  # noqa: E402


def classic_mask(
    pixels: list[tuple[int, int, int, int]], width: int, height: int
) -> REFERENCES.RGBAImage:
    flattened = bytes(channel for pixel in pixels for channel in pixel)
    return REFERENCES.RGBAImage(width, height, flattened)


def box_mask(*, magenta_source: bool = False) -> REFERENCES.RGBAImage:
    pixels = []
    for y in range(4):
        for x in range(4):
            opaque = 1 <= x <= 2 and 1 <= y <= 2
            color = (190, 45, 180) if magenta_source and (x, y) == (1, 1) else (90, 70, 50)
            pixels.append((*color, 255 if opaque else 0))
    return classic_mask(pixels, 4, 4)


def job(
    *,
    job_id: str = "11_tactical_actor_cicn_9060",
    order: int = 11,
    pack: str = "Data Files/Tacticals",
    resource_id: int = 9060,
    family: str = "tactical_actor",
    width: int = 8,
    height: int = 8,
) -> dict:
    return {
        "alpha": {
            "chroma_key": "#FF00FF",
            "input_alpha": "binary_mask",
            "mode": "chroma_key_then_original_mask",
        },
        "expected_output_path": f"assets/remastered/style-proof/generation/outputs/{job_id}.png",
        "family": family,
        "input": {
            "classic_payload_sha256": "1" * 64,
            "decoded_png_sha256": "2" * 64,
            "path": f"references/{job_id}.png",
        },
        "job_id": job_id,
        "label": "Synthetic masked asset",
        "order": order,
        "resource_key": {"id": resource_id, "pack": pack, "type": "cicn"},
        "target_master": {
            "color_space": "sRGB",
            "format": "PNG",
            "height": height,
            "width": width,
        },
    }


def target_mask(source: REFERENCES.RGBAImage, width: int, height: int) -> bytes:
    values = bytearray()
    for y in range(height):
        source_y = min(source.height - 1, y * source.height // height)
        for x in range(width):
            source_x = min(source.width - 1, x * source.width // width)
            values.append(source.pixels[(source_y * source.width + source_x) * 4 + 3])
    return bytes(values)


def chroma_png(
    source: REFERENCES.RGBAImage,
    width: int,
    height: int,
    *,
    mode: str = "RGB",
    subject=(74, 89, 111),
) -> bytes:
    alpha = target_mask(source, width, height)
    pixels = bytearray()
    for value in alpha:
        pixels.extend(subject if value else (255, 0, 255))
        if mode == "RGBA":
            pixels.append(255)
    image = Image.frombytes(mode, (width, height), bytes(pixels))
    output = io.BytesIO()
    image.save(output, format="PNG")
    return output.getvalue()


class MaskedStyleProofPostprocessTests(unittest.TestCase):
    def test_exact_operation_chain_mask_and_canonical_output(self) -> None:
        classic = box_mask()
        definition = job(width=8, height=8)
        raw = chroma_png(classic, 8, 8)
        first = TOOL.process_masked_png(raw, definition, classic)
        second = TOOL.process_masked_png(raw, definition, classic)
        self.assertEqual(first, second)
        self.assertEqual(
            [
                "resize_to_target",
                "remove_chroma_outside_original_mask",
                "apply_original_alpha_mask",
                "decontaminate_transparent_edges",
                "encode_srgb_png",
            ],
            [operation["operation"] for operation in first.operations],
        )
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

        output = REFERENCES.decode_png(first.final_png, expected_dimensions=(8, 8))
        self.assertEqual(target_mask(classic, 8, 8), output.pixels[3::4])
        self.assertIn(b"sRGB\x00", first.final_png)
        for index in range(8 * 8):
            rgb = output.pixels[index * 4:index * 4 + 3]
            self.assertFalse(TOOL._is_near_chroma(*rgb))
        TOOL.output_validator._validate_post_processing(
            list(first.operations),
            masked=True,
            ui_tile=False,
            raw_sha256=hashlib.sha256(raw).hexdigest(),
            raw_facts=TOOL.output_validator._png_facts(raw, "synthetic raw"),
            final_sha256=hashlib.sha256(first.final_png).hexdigest(),
            target_dimensions=(8, 8),
            where="synthetic post-processing",
        )

    def test_rgb_and_fully_opaque_rgba_chroma_inputs_are_accepted(self) -> None:
        classic = box_mask()
        definition = job()
        for mode in ("RGB", "RGBA"):
            with self.subTest(mode=mode):
                result = TOOL.process_masked_png(
                    chroma_png(classic, 8, 8, mode=mode), definition, classic
                )
                self.assertEqual(
                    target_mask(classic, 8, 8),
                    REFERENCES.decode_png(result.final_png).pixels[3::4],
                )

    def test_arbitrary_square_resize_and_aspect_failure(self) -> None:
        classic = box_mask()
        definition = job(width=8, height=8)
        result = TOOL.process_masked_png(
            chroma_png(classic, 16, 16), definition, classic
        )
        self.assertEqual((8, 8), (
            REFERENCES.decode_png(result.final_png).width,
            REFERENCES.decode_png(result.final_png).height,
        ))
        rectangular = Image.new("RGB", (12, 8), (74, 89, 111))
        stream = io.BytesIO()
        rectangular.save(stream, format="PNG")
        with self.assertRaisesRegex(TOOL.ProcessingError, "aspect ratios differ"):
            TOOL.process_masked_png(stream.getvalue(), definition, classic)

    def test_inside_key_color_is_rejected_as_model_underfill(self) -> None:
        classic = box_mask()
        raw = Image.new("RGB", (8, 8), (255, 0, 255))
        stream = io.BytesIO()
        raw.save(stream, format="PNG")
        with self.assertRaisesRegex(TOOL.ProcessingError, "model underfill"):
            TOOL.process_masked_png(stream.getvalue(), job(), classic)

    def test_source_magenta_exception_is_identity_and_pixel_locked(self) -> None:
        classic = box_mask(magenta_source=True)
        definition = job(
            job_id="10_tactical_actor_cicn_9007",
            order=10,
            pack="Data Files/Tacticals",
            resource_id=9007,
        )
        raw = bytearray()
        nearest = TOOL._nearest_source_indices(classic, 8, 8)
        for source_index in nearest:
            source_offset = source_index * 4
            alpha = classic.pixels[source_offset + 3]
            source_rgb = classic.pixels[source_offset:source_offset + 3]
            if not alpha:
                raw.extend((255, 0, 255))
            elif TOOL._is_source_magenta(*source_rgb):
                raw.extend((255, 0, 255))
            else:
                raw.extend((74, 89, 111))
        image = Image.frombytes("RGB", (8, 8), bytes(raw))
        stream = io.BytesIO()
        image.save(stream, format="PNG")
        result = TOOL.process_masked_png(stream.getvalue(), definition, classic)
        output = REFERENCES.decode_png(result.final_png)
        retained_magenta = 0
        for index, alpha in enumerate(output.pixels[3::4]):
            rgb = output.pixels[index * 4:index * 4 + 3]
            self.assertFalse(TOOL._is_near_chroma(*rgb))
            if alpha and TOOL._is_source_magenta(*rgb):
                retained_magenta += 1
        self.assertGreater(retained_magenta, 0)
        self.assertGreater(
            result.operations[3]["parameters"][
                "source_authorized_magenta_pixels_reencoded"
            ],
            0,
        )

        wrong_identity = job()  # Same Classic colors cannot authorize job 11.
        with self.assertRaisesRegex(TOOL.ProcessingError, "model underfill"):
            TOOL.process_masked_png(stream.getvalue(), wrong_identity, classic)

        nonmagenta_classic = box_mask(magenta_source=False)
        with self.assertRaisesRegex(TOOL.ProcessingError, "model underfill"):
            TOOL.process_masked_png(stream.getvalue(), definition, nonmagenta_classic)

    def test_real_anchor_jobs_11_and_13_accept_verified_masks(self) -> None:
        jobs = json.loads(
            (REPO / "assets/remastered/style-proof/generation/jobs.json").read_bytes()
        )["jobs"]
        selected = {entry["order"]: entry for entry in jobs}
        context = REFERENCES._load_selection(
            REPO, REPO / "assets/remastered/style-proof/classic-selection.json"
        )
        with tempfile.TemporaryDirectory() as temp_name:
            handoff = Path(temp_name) / "handoff"
            REFERENCES.export_generation_inputs(
                context,
                REPO / "assets/remastered/style-proof/classic-references",
                handoff,
            )
            for order in (11, 13):
                with self.subTest(order=order):
                    definition = selected[order]
                    classic = TOOL._load_verified_handoff_reference(
                        REPO, handoff, definition
                    )
                    target = definition["target_master"]
                    raw = chroma_png(
                        classic, target["width"], target["height"], subject=(81, 92, 104)
                    )
                    result = TOOL.process_masked_png(raw, definition, classic)
                    output = REFERENCES.decode_png(result.final_png)
                    self.assertEqual(
                        target_mask(classic, target["width"], target["height"]),
                        output.pixels[3::4],
                    )

            tampered = handoff / selected[11]["input"]["path"]
            tampered.write_bytes(tampered.read_bytes() + b"tamper")
            with self.assertRaisesRegex(TOOL.ProcessingError, "handoff verification failed"):
                TOOL._load_verified_handoff_reference(REPO, handoff, selected[11])

    def test_verified_raw_hash_and_no_overwrite_are_fail_closed(self) -> None:
        classic = box_mask()
        definition = job()
        raw = chroma_png(classic, 8, 8)
        digest = hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as temp_name:
            root = Path(temp_name)
            raw_path = root / f"{TOOL.RAW_ROOT}/{definition['job_id']}/attempt-0.png"
            raw_path.parent.mkdir(parents=True)
            raw_path.write_bytes(raw)
            with (
                mock.patch.object(TOOL, "_load_immutable_job", return_value=definition),
                mock.patch.object(
                    TOOL, "_load_verified_handoff_reference", return_value=classic
                ),
            ):
                with self.assertRaisesRegex(TOOL.ProcessingError, "differs"):
                    TOOL.process_job(
                        root, Path("jobs.json"), Path("handoff"),
                        definition["job_id"], 0, "0" * 64,
                    )
                fragment = TOOL.process_job(
                    root, Path("jobs.json"), Path("handoff"),
                    definition["job_id"], 0, digest, write_intermediates=True,
                )
                self.assertEqual(
                    [
                        "resize_to_target",
                        "remove_chroma_outside_original_mask",
                        "apply_original_alpha_mask",
                        "decontaminate_transparent_edges",
                        "encode_srgb_png",
                    ],
                    [item["operation"] for item in fragment["post_processing"]],
                )
                with self.assertRaisesRegex(TOOL.ProcessingError, "overwrite"):
                    TOOL.process_job(
                        root, Path("jobs.json"), Path("handoff"),
                        definition["job_id"], 0, digest,
                    )


if __name__ == "__main__":
    unittest.main()
