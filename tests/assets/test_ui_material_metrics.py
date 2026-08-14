import json
import math
from pathlib import Path
import random
import shutil
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT_DIR = REPO / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))
import prepare_style_proof_references as PNG  # noqa: E402
import validate_ui_material_set as TOOL  # noqa: E402


def image_from_function(width, height, function):
    pixels = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            red, green, blue = function(x, y)
            offset = (y * width + x) * 4
            pixels[offset : offset + 4] = bytes((red, green, blue, 255))
    return PNG.RGBAImage(width, height, bytes(pixels))


def tileable_noise(size=96, seed=9182):
    random_source = random.Random(seed)
    pixels = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            value = max(0, min(255, 116 + random_source.randrange(-18, 19)))
            offset = (y * size + x) * 4
            pixels[offset : offset + 4] = bytes((value, value + 3, value + 7, 255))
    # Authoritative exact seams, with otherwise unchanged edge-band statistics.
    for y in range(size):
        first = (y * size) * 4
        last = (y * size + size - 1) * 4
        pixels[last : last + 4] = pixels[first : first + 4]
    for x in range(size):
        first = x * 4
        last = ((size - 1) * size + x) * 4
        pixels[last : last + 4] = pixels[first : first + 4]
    return PNG.RGBAImage(size, size, bytes(pixels))


def copy_validation_fixture(destination):
    jobs_relative = "assets/remastered/style-proof/generation/jobs.json"
    receipt_relative = (
        "assets/remastered/style-proof/generation/generation-receipts.json"
    )
    jobs = json.loads((REPO / jobs_relative).read_bytes())
    paths = [jobs_relative]
    for job in jobs["jobs"][:4]:
        paths.append(
            "assets/remastered/style-proof/classic-references/"
            + job["input"]["path"]
        )
    paths.extend(
        (
            "assets/remastered/style-proof/generation/raw/"
            "02_ui_material_ppat_129/attempt-0.png",
            "assets/remastered/style-proof/generation/raw/"
            "02_ui_material_ppat_129/attempt-1.png",
            "assets/remastered/style-proof/generation/outputs/"
            "02_ui_material_ppat_129.png",
        )
    )
    for relative in paths:
        source = REPO / relative
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    receipt = json.loads((REPO / receipt_relative).read_bytes())
    receipt["entries"] = [
        entry
        for entry in receipt["entries"]
        if entry["job_id"] == "02_ui_material_ppat_129"
    ]
    receipt_path = destination / receipt_relative
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_bytes(TOOL._canonical_json(receipt))
    return receipt_path


class UIMaterialMetricTest(unittest.TestCase):
    def test_srgb_to_lab_reference_points(self):
        self.assertEqual((0.0, 0.0, 0.0), TOOL.srgb8_to_lab(0, 0, 0))
        white = TOOL.srgb8_to_lab(255, 255, 255)
        self.assertAlmostEqual(100.0, white[0], places=5)
        self.assertAlmostEqual(0.0, white[1], places=3)
        self.assertAlmostEqual(0.0, white[2], places=3)
        red = TOOL.srgb8_to_lab(255, 0, 0)
        self.assertAlmostEqual(53.2408, red[0], places=3)
        self.assertAlmostEqual(80.0925, red[1], places=3)
        self.assertAlmostEqual(67.2032, red[2], places=3)

    def test_raw_checker_fails_and_deterministic_noise_passes(self):
        checker = image_from_function(
            128,
            128,
            lambda x, y: ((74, 74, 74) if ((x // 8 + y // 8) % 2) else (178, 178, 178)),
        )
        checker_result = TOOL.analyze_raw(checker)
        self.assertFalse(checker_result["passes"])
        self.assertTrue(checker_result["metrics"]["axes"]["x"]["periodic"])
        self.assertTrue(checker_result["metrics"]["axes"]["y"]["periodic"])

        noise_result = TOOL.analyze_raw(tileable_noise())
        self.assertTrue(noise_result["passes"])
        self.assertTrue(noise_result["checks"]["no_periodic_grid"])

    def test_final_gradient_is_caught_by_spatial_drift(self):
        # Cosine endpoints match exactly, isolating drift from exact-seam failure.
        gradient = image_from_function(
            128,
            128,
            lambda x, _y: (
                round(124 + 48 * math.cos(2 * math.pi * x / 127)),
            )
            * 3,
        )
        result = TOOL.analyze_final(gradient)
        self.assertTrue(result["checks"]["opposite_edges_exact"])
        self.assertFalse(result["checks"]["no_directional_or_ring_drift"])
        self.assertGreater(
            result["metrics"]["drift"]["cell_max_pairwise_delta_e76"],
            TOOL.FINAL_CELL_PAIRWISE_DE_LIMIT,
        )
        self.assertFalse(result["passes"])

    def test_final_tileable_noise_passes_and_flat_edge_band_fails(self):
        good = tileable_noise()
        good_result = TOOL.analyze_final(good)
        self.assertTrue(good_result["passes"])
        self.assertTrue(good_result["checks"]["edge_band_matches_interior"])

        width = good.width
        pixels = bytearray(good.pixels)
        flat_width = 8
        for y in range(width):
            for x in range(width):
                if (
                    x < flat_width
                    or x >= width - flat_width
                    or y < flat_width
                    or y >= width - flat_width
                ):
                    offset = (y * width + x) * 4
                    pixels[offset : offset + 4] = bytes((120, 123, 127, 255))
        flat_edge = PNG.RGBAImage(width, width, bytes(pixels))
        flat_result = TOOL.analyze_final(flat_edge)
        self.assertTrue(flat_result["checks"]["opposite_edges_exact"])
        self.assertFalse(flat_result["checks"]["edge_band_matches_interior"])
        self.assertLess(
            flat_result["metrics"]["edge_gradient"]["x"][
                "band_to_interior_ratio"
            ],
            TOOL.FINAL_EDGE_BAND_RATIO_MIN,
        )

    def test_complete_set_enforces_lightness_roles_and_gray_neutrality(self):
        colors = {
            128: (157, 160, 184),
            129: (112, 116, 142),
            130: (67, 71, 96),
            131: (112, 112, 112),
        }
        finals = {
            resource_id: TOOL.analyze_final(
                image_from_function(64, 64, lambda _x, _y, color=color: color)
            )
            for resource_id, color in colors.items()
        }
        classic = TOOL._classic_material_baselines(REPO, TOOL._ui_jobs(REPO))
        result = TOOL.evaluate_set(finals, classic)
        self.assertTrue(result["complete"])
        self.assertTrue(result["passes"])
        self.assertGreaterEqual(
            result["luminance_order_128_gt_129_gt_130"]["steps_lstar"][
                "128_minus_129"
            ],
            result["luminance_order_128_gt_129_gt_130"][
                "required_steps_lstar"
            ]["128_minus_129"],
        )
        self.assertLessEqual(
            result["gray_131_neutrality"]["per_pixel_chroma"]["mean_cstar_ab"],
            TOOL.GRAY_PIXEL_MEAN_CHROMA_LIMIT,
        )

        bad_order = dict(finals)
        bad_order[128] = finals[130]
        self.assertFalse(TOOL.evaluate_set(bad_order, classic)["passes"])

        tinted_gray = dict(finals)
        tinted_gray[131] = TOOL.analyze_final(
            image_from_function(64, 64, lambda _x, _y: (75, 105, 170))
        )
        self.assertFalse(TOOL.evaluate_set(tinted_gray, classic)["passes"])

    def test_gray_neutrality_rejects_saturated_complements_that_average_neutral(self):
        colors = {
            128: (157, 160, 184),
            129: (112, 116, 142),
            130: (67, 71, 96),
            131: (112, 112, 112),
        }
        finals = {
            resource_id: TOOL.analyze_final(
                image_from_function(65, 65, lambda _x, _y, color=color: color)
            )
            for resource_id, color in colors.items()
        }
        # These two Lab vectors nearly cancel, so measuring only mean a*/b*
        # would incorrectly approve an intensely green/lavender checker.
        finals[131] = TOOL.analyze_final(
            image_from_function(
                65,
                65,
                lambda x, y: (
                    (120, 177, 115) if (x + y) % 2 else (201, 153, 212)
                ),
            )
        )
        classic = TOOL._classic_material_baselines(REPO, TOOL._ui_jobs(REPO))
        result = TOOL.evaluate_set(finals, classic)
        neutrality = result["gray_131_neutrality"]
        self.assertLess(neutrality["mean_lab_chroma_cstar_ab"], 3.0)
        self.assertGreater(
            neutrality["per_pixel_chroma"]["mean_cstar_ab"],
            TOOL.GRAY_PIXEL_MEAN_CHROMA_LIMIT,
        )
        self.assertGreater(
            neutrality["per_pixel_chroma"]["percentile_95_cstar_ab"],
            TOOL.GRAY_PIXEL_P95_CHROMA_LIMIT,
        )
        self.assertFalse(neutrality["passes"])
        self.assertFalse(result["passes"])

    def test_lightness_order_rejects_compressed_but_correctly_sorted_states(self):
        colors = {
            128: (132, 132, 132),
            129: (126, 126, 126),
            130: (120, 120, 120),
            131: (112, 112, 112),
        }
        finals = {
            resource_id: TOOL.analyze_final(
                image_from_function(64, 64, lambda _x, _y, color=color: color)
            )
            for resource_id, color in colors.items()
        }
        classic = TOOL._classic_material_baselines(REPO, TOOL._ui_jobs(REPO))
        result = TOOL.evaluate_set(finals, classic)
        ordering = result["luminance_order_128_gt_129_gt_130"]
        self.assertGreater(ordering["steps_lstar"]["128_minus_129"], 0)
        self.assertGreater(ordering["steps_lstar"]["129_minus_130"], 0)
        self.assertEqual(
            6.0, ordering["required_steps_lstar"]["128_minus_129"]
        )
        self.assertGreater(
            ordering["required_steps_lstar"]["129_minus_130"], 7.0
        )
        self.assertFalse(ordering["passes"])
        self.assertFalse(result["passes"])

    def test_committed_job02_rejected_and_selected_raw_regression(self):
        base = REPO / "assets/remastered/style-proof/generation"
        raw_zero = PNG.decode_png(
            (base / "raw/02_ui_material_ppat_129/attempt-0.png").read_bytes()
        )
        raw_one = PNG.decode_png(
            (base / "raw/02_ui_material_ppat_129/attempt-1.png").read_bytes()
        )
        rejected = TOOL.analyze_raw(raw_zero)
        selected = TOOL.analyze_raw(raw_one)
        self.assertFalse(rejected["passes"])
        self.assertTrue(selected["passes"])
        self.assertAlmostEqual(
            0.538925,
            rejected["metrics"]["axes"]["x"]["autocorrelation_peak_absolute"],
            places=6,
        )
        self.assertAlmostEqual(
            0.580703,
            rejected["metrics"]["axes"]["x"]["gradient_periodicity_peak"],
            places=6,
        )
        self.assertAlmostEqual(
            0.277669,
            selected["metrics"]["axes"]["x"]["autocorrelation_peak_absolute"],
            places=6,
        )
        self.assertAlmostEqual(
            0.099483,
            selected["metrics"]["axes"]["x"]["gradient_periodicity_peak"],
            places=6,
        )

    def test_committed_ui_material_set_and_job02_regression_pass(self):
        report = TOOL.validate(REPO)
        self.assertTrue(report["passes"])
        self.assertTrue(report["complete"])
        self.assertTrue(report["set_checks"]["passes"])
        self.assertTrue(
            report["set_checks"]["luminance_order_128_gt_129_gt_130"]["passes"]
        )
        self.assertTrue(report["set_checks"]["gray_131_neutrality"]["passes"])
        material = next(
            item for item in report["materials"] if item["resource_id"] == 129
        )
        self.assertEqual("passes", material["state"])
        self.assertTrue(material["current_passes"])
        self.assertEqual([False, True], [item["passes"] for item in material["raw_attempts"]])
        final = material["final"]
        self.assertTrue(final["passes"])
        self.assertEqual(
            0,
            final["metrics"]["seams"]["left_right"]["mismatched_pixel_pairs"],
        )
        self.assertAlmostEqual(
            0.762745,
            final["metrics"]["edge_gradient"]["x"]["band_to_interior_ratio"],
            places=6,
        )
        self.assertEqual(
            TOOL._canonical_json(report),
            TOOL._canonical_json(TOOL.validate(REPO)),
        )

    def test_stale_receipt_hashes_fail_closed(self):
        for field_path, message in (
            (("raw_output", "sha256"), "selected raw SHA-256 is stale"),
            (("output", "sha256"), "receipt-bound final SHA-256 is stale"),
        ):
            with self.subTest(field_path=field_path):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    receipt_path = copy_validation_fixture(root)
                    receipt = json.loads(receipt_path.read_bytes())
                    entry = next(
                        item
                        for item in receipt["entries"]
                        if item["job_id"] == "02_ui_material_ppat_129"
                    )
                    entry[field_path[0]][field_path[1]] = "0" * 64
                    receipt_path.write_bytes(TOOL._canonical_json(receipt))
                    with self.assertRaisesRegex(TOOL.ValidationError, message):
                        TOOL.validate(root)

    def test_malformed_receipt_path_status_ordinal_and_hash_fail_closed(self):
        mutations = (
            (
                "raw path",
                lambda entry: entry["raw_output"].__setitem__(
                    "path",
                    "assets/remastered/style-proof/generation/raw/"
                    "02_ui_material_ppat_129/attempt-0.png",
                ),
                "raw_output.path is not the selected attempt",
            ),
            (
                "final path",
                lambda entry: entry["output"].__setitem__(
                    "path",
                    "assets/remastered/style-proof/generation/outputs/wrong.png",
                ),
                "output.path does not match the job",
            ),
            (
                "status",
                lambda entry: entry.__setitem__("review_status", "reviewed"),
                "review_status is invalid",
            ),
            (
                "ordinal",
                lambda entry: entry.__setitem__("regeneration_ordinal", True),
                "regeneration_ordinal must be 0 or 1",
            ),
            (
                "raw hash syntax",
                lambda entry: entry["raw_output"].__setitem__("sha256", "bad"),
                "raw_output.sha256 is invalid",
            ),
        )
        for label, mutate, message in mutations:
            with self.subTest(label=label):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    receipt_path = copy_validation_fixture(root)
                    receipt = json.loads(receipt_path.read_bytes())
                    entry = next(
                        item
                        for item in receipt["entries"]
                        if item["job_id"] == "02_ui_material_ppat_129"
                    )
                    mutate(entry)
                    receipt_path.write_bytes(TOOL._canonical_json(receipt))
                    with self.assertRaisesRegex(TOOL.ValidationError, message):
                        TOOL.validate(root)

    def test_evidence_images_are_deterministic_and_decodable(self):
        image = tileable_noise(size=32)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            first = TOOL._write_evidence(image, directory, "test_material", directory)
            first_bytes = {
                name: (directory / value["path"]).read_bytes()
                for name, value in first.items()
            }
            second = TOOL._write_evidence(image, directory, "test_material", directory)
            self.assertEqual(first, second)
            for name, value in second.items():
                data = (directory / value["path"]).read_bytes()
                self.assertEqual(first_bytes[name], data)
                self.assertEqual(value["sha256"], TOOL._sha256(data))
                PNG.decode_png(data, name)


if __name__ == "__main__":
    unittest.main()
