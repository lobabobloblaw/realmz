#!/usr/bin/env python3
"""Measure the four remastered UI material masters without model judgement.

The raw-image gate detects repeated square/grid artefacts before post-processing.
The final-image gate measures spatial colour drift and the quality of the edge
band used to make a texture tileable.  Results are canonical JSON so a review
receipt can bind the exact measurements rather than a path or a prose verdict.

Only Python's standard library and the repository's bounded PNG decoder are
used.  Optional 2x2 and half-roll images are evidence for a human reviewer;
they never replace the numeric gates.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Iterable, Sequence


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import prepare_style_proof_references as PNG  # noqa: E402


SCHEMA_VERSION = 2
UI_RESOURCE_IDS = (128, 129, 130, 131)
RAW_GRID_MAX_DIMENSION = 256
RAW_MIN_LAG = 2
RAW_MAX_LAG = 32
RAW_GRADIENT_MIN_LAG = 3
RAW_AUTOCORRELATION_LIMIT = 0.45
RAW_GRADIENT_PERIODICITY_LIMIT = 0.35
FINAL_GRID_SIZE = 4
FINAL_CELL_DE_FROM_GLOBAL_LIMIT = 4.0
FINAL_CELL_PAIRWISE_DE_LIMIT = 6.0
FINAL_RING_DE_LIMIT = 3.0
FINAL_EDGE_BAND_RATIO_MIN = 0.45
FINAL_EDGE_BAND_RATIO_MAX = 2.20
SET_ABSOLUTE_MINIMUM_LSTAR_STEP = 6.0
SET_CLASSIC_GAP_RETENTION = 0.4
GRAY_PIXEL_MEAN_CHROMA_LIMIT = 8.0
GRAY_PIXEL_P95_CHROMA_LIMIT = 12.0
FLOAT_DIGITS = 6
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


class ValidationError(RuntimeError):
    """Raised when the material contract or an input image is malformed."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _rounded(value: float) -> float:
    # Normalise negative zero as well as insignificant platform noise.
    result = round(float(value), FLOAT_DIGITS)
    return 0.0 if result == 0 else result


def _canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


_SRGB_LINEAR = tuple(
    component / (255.0 * 12.92)
    if component / 255.0 <= 0.04045
    else ((component / 255.0 + 0.055) / 1.055) ** 2.4
    for component in range(256)
)


def srgb8_to_lab(red: int, green: int, blue: int) -> tuple[float, float, float]:
    """Convert an sRGB8 triplet to CIE L*a*b* using the D65 white point."""

    linear_red = _SRGB_LINEAR[red]
    linear_green = _SRGB_LINEAR[green]
    linear_blue = _SRGB_LINEAR[blue]
    x = (
        0.4124564 * linear_red
        + 0.3575761 * linear_green
        + 0.1804375 * linear_blue
    ) / 0.95047
    y = (
        0.2126729 * linear_red
        + 0.7151522 * linear_green
        + 0.0721750 * linear_blue
    )
    z = (
        0.0193339 * linear_red
        + 0.1191920 * linear_green
        + 0.9503041 * linear_blue
    ) / 1.08883

    epsilon = 216.0 / 24389.0
    kappa = 24389.0 / 27.0

    def transform(value: float) -> float:
        if value > epsilon:
            return value ** (1.0 / 3.0)
        return (kappa * value + 16.0) / 116.0

    fx = transform(x)
    fy = transform(y)
    fz = transform(z)
    return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)


def _delta_e(first: Sequence[float], second: Sequence[float]) -> float:
    return math.sqrt(sum((left - right) ** 2 for left, right in zip(first, second)))


def _mean_lab(values: Iterable[Sequence[float]]) -> tuple[float, float, float]:
    total_l = total_a = total_b = 0.0
    count = 0
    for lightness, green_red, blue_yellow in values:
        total_l += lightness
        total_a += green_red
        total_b += blue_yellow
        count += 1
    if count == 0:
        raise ValidationError("cannot measure an empty pixel region")
    return total_l / count, total_a / count, total_b / count


def _pixel_chroma_distribution(image: PNG.RGBAImage) -> dict:
    """Measure C*ab per source pixel, before spatial or colour averaging."""

    chroma = []
    pixels = image.pixels
    for offset in range(0, len(pixels), 4):
        _, green_red, blue_yellow = srgb8_to_lab(
            pixels[offset], pixels[offset + 1], pixels[offset + 2]
        )
        chroma.append(math.hypot(green_red, blue_yellow))
    if not chroma:
        raise ValidationError("cannot measure chroma in an empty image")
    chroma.sort()
    percentile_95_index = max(0, math.ceil(0.95 * len(chroma)) - 1)
    return {
        "maximum_cstar_ab": _rounded(chroma[-1]),
        "mean_cstar_ab": _rounded(sum(chroma) / len(chroma)),
        "percentile_95_cstar_ab": _rounded(chroma[percentile_95_index]),
        "percentile_method": "nearest-rank",
        "pixel_count": len(chroma),
    }


def _opaque(image: PNG.RGBAImage) -> bool:
    return all(alpha == 255 for alpha in image.pixels[3::4])


def _block_lab_grid(
    image: PNG.RGBAImage, max_dimension: int
) -> tuple[list[list[tuple[float, float, float]]], int]:
    """Return deterministic block means, preserving all input pixels."""

    step = max(1, math.ceil(max(image.width, image.height) / max_dimension))
    output_width = math.ceil(image.width / step)
    output_height = math.ceil(image.height / step)
    sums = [0.0] * (output_width * output_height * 3)
    counts = [0] * (output_width * output_height)
    pixels = image.pixels
    for y in range(image.height):
        output_y = y // step
        for x in range(image.width):
            source = (y * image.width + x) * 4
            lightness, green_red, blue_yellow = srgb8_to_lab(
                pixels[source], pixels[source + 1], pixels[source + 2]
            )
            destination_pixel = output_y * output_width + x // step
            destination = destination_pixel * 3
            sums[destination] += lightness
            sums[destination + 1] += green_red
            sums[destination + 2] += blue_yellow
            counts[destination_pixel] += 1

    rows: list[list[tuple[float, float, float]]] = []
    for y in range(output_height):
        row = []
        for x in range(output_width):
            pixel = y * output_width + x
            offset = pixel * 3
            count = counts[pixel]
            row.append(
                (
                    sums[offset] / count,
                    sums[offset + 1] / count,
                    sums[offset + 2] / count,
                )
            )
        rows.append(row)
    return rows, step


def _box_high_pass(values: list[list[float]], radius: int = 2) -> list[list[float]]:
    height = len(values)
    width = len(values[0])
    # Integral image makes the local mean independent of texture dimensions.
    integral = [[0.0] * (width + 1) for _ in range(height + 1)]
    for y, row in enumerate(values):
        row_sum = 0.0
        for x, value in enumerate(row):
            row_sum += value
            integral[y + 1][x + 1] = integral[y][x + 1] + row_sum

    result = [[0.0] * width for _ in range(height)]
    for y in range(height):
        top = max(0, y - radius)
        bottom = min(height, y + radius + 1)
        for x in range(width):
            left = max(0, x - radius)
            right = min(width, x + radius + 1)
            region_sum = (
                integral[bottom][right]
                - integral[top][right]
                - integral[bottom][left]
                + integral[top][left]
            )
            local_mean = region_sum / ((bottom - top) * (right - left))
            result[y][x] = values[y][x] - local_mean
    return result


def _correlation(first: list[float], second: list[float]) -> float:
    if len(first) != len(second) or not first:
        raise ValidationError("correlation inputs must have equal non-zero length")
    mean_first = sum(first) / len(first)
    mean_second = sum(second) / len(second)
    covariance = variance_first = variance_second = 0.0
    for left, right in zip(first, second):
        left -= mean_first
        right -= mean_second
        covariance += left * right
        variance_first += left * left
        variance_second += right * right
    denominator = math.sqrt(variance_first * variance_second)
    return covariance / denominator if denominator > 1e-12 else 0.0


def _shift_correlation(values: list[list[float]], axis: str, lag: int) -> float:
    height = len(values)
    width = len(values[0])
    first: list[float] = []
    second: list[float] = []
    if axis == "x":
        for row in values:
            first.extend(row[:-lag])
            second.extend(row[lag:])
    elif axis == "y":
        for y in range(height - lag):
            first.extend(values[y])
            second.extend(values[y + lag])
    else:
        raise ValidationError(f"unknown correlation axis: {axis}")
    return _correlation(first, second)


def _directional_gradient(values: list[list[float]], axis: str) -> list[list[float]]:
    height = len(values)
    width = len(values[0])
    if axis == "x":
        return [
            [abs(row[x + 1] - row[x]) for x in range(width - 1)]
            for row in values
        ]
    if axis == "y":
        return [
            [abs(values[y + 1][x] - values[y][x]) for x in range(width)]
            for y in range(height - 1)
        ]
    raise ValidationError(f"unknown gradient axis: {axis}")


def _peak(values: list[tuple[int, float]], *, absolute: bool) -> tuple[int, float]:
    if not values:
        return 0, 0.0
    key = (lambda pair: abs(pair[1])) if absolute else (lambda pair: pair[1])
    # Earlier lag wins exact ties, keeping reports stable and interpretable.
    return max(values, key=lambda pair: (key(pair), -pair[0]))


def analyze_raw(image: PNG.RGBAImage) -> dict:
    """Measure repeated grid artefacts in an unprocessed model output."""

    grid, sample_step = _block_lab_grid(image, RAW_GRID_MAX_DIMENSION)
    lightness = [[pixel[0] for pixel in row] for row in grid]
    high_pass = _box_high_pass(lightness)
    axes = {}
    periodic_axes = []
    for axis in ("x", "y"):
        axis_length = len(lightness[0]) if axis == "x" else len(lightness)
        autocorrelation = [
            (lag, _shift_correlation(high_pass, axis, lag))
            for lag in range(RAW_MIN_LAG, min(RAW_MAX_LAG, axis_length - 2) + 1)
        ]
        gradient = _directional_gradient(lightness, axis)
        gradient_axis_length = len(gradient[0]) if axis == "x" else len(gradient)
        gradient_correlations = [
            (lag, _shift_correlation(gradient, axis, lag))
            for lag in range(
                RAW_GRADIENT_MIN_LAG,
                min(RAW_MAX_LAG, gradient_axis_length - 2) + 1,
            )
        ]
        autocorrelation_lag, autocorrelation_value = _peak(
            autocorrelation, absolute=True
        )
        gradient_lag, gradient_value = _peak(
            gradient_correlations, absolute=False
        )
        periodic = (
            abs(autocorrelation_value) >= RAW_AUTOCORRELATION_LIMIT
            and gradient_value >= RAW_GRADIENT_PERIODICITY_LIMIT
        )
        if periodic:
            periodic_axes.append(axis)
        axes[axis] = {
            "autocorrelation_peak": _rounded(autocorrelation_value),
            "autocorrelation_peak_absolute": _rounded(abs(autocorrelation_value)),
            "autocorrelation_peak_lag_pixels": autocorrelation_lag * sample_step,
            "gradient_periodicity_peak": _rounded(gradient_value),
            "gradient_periodicity_peak_lag_pixels": gradient_lag * sample_step,
            "periodic": periodic,
        }

    no_periodic_grid = not periodic_axes
    return {
        "checks": {
            "fully_opaque": _opaque(image),
            "no_periodic_grid": no_periodic_grid,
        },
        "height": image.height,
        "metrics": {
            "axes": axes,
            "sample_height": len(grid),
            "sample_step_pixels": sample_step,
            "sample_width": len(grid[0]),
        },
        "passes": _opaque(image) and no_periodic_grid,
        "width": image.width,
    }


def _cell_and_ring_metrics(
    grid: list[list[tuple[float, float, float]]]
) -> dict:
    height = len(grid)
    width = len(grid[0])
    global_mean = _mean_lab(pixel for row in grid for pixel in row)
    cell_means = []
    for cell_y in range(FINAL_GRID_SIZE):
        top = cell_y * height // FINAL_GRID_SIZE
        bottom = (cell_y + 1) * height // FINAL_GRID_SIZE
        for cell_x in range(FINAL_GRID_SIZE):
            left = cell_x * width // FINAL_GRID_SIZE
            right = (cell_x + 1) * width // FINAL_GRID_SIZE
            cell_means.append(
                _mean_lab(
                    grid[y][x]
                    for y in range(top, bottom)
                    for x in range(left, right)
                )
            )

    max_from_global = max(_delta_e(cell, global_mean) for cell in cell_means)
    max_pairwise = max(
        _delta_e(left, right)
        for index, left in enumerate(cell_means)
        for right in cell_means[index + 1 :]
    )
    ring_width = max(1, min(width, height) // 8)
    ring = []
    interior = []
    for y, row in enumerate(grid):
        for x, pixel in enumerate(row):
            target = (
                ring
                if x < ring_width
                or x >= width - ring_width
                or y < ring_width
                or y >= height - ring_width
                else interior
            )
            target.append(pixel)
    ring_mean = _mean_lab(ring)
    interior_mean = _mean_lab(interior)
    return {
        "cell_grid": f"{FINAL_GRID_SIZE}x{FINAL_GRID_SIZE}",
        "cell_max_delta_e76_from_global": _rounded(max_from_global),
        "cell_max_pairwise_delta_e76": _rounded(max_pairwise),
        "global_mean_lab": [_rounded(value) for value in global_mean],
        "interior_mean_lab": [_rounded(value) for value in interior_mean],
        "ring_delta_e76_from_interior": _rounded(_delta_e(ring_mean, interior_mean)),
        "ring_mean_lab": [_rounded(value) for value in ring_mean],
        "ring_width_pixels": ring_width,
    }


def _edge_gradient_metrics(
    grid: list[list[tuple[float, float, float]]], axis: str
) -> dict:
    height = len(grid)
    width = len(grid[0])
    axis_length = width if axis == "x" else height
    band_width = max(2, min(width, height) // 32)
    band: list[float] = []
    interior: list[float] = []
    if axis == "x":
        for y in range(height):
            for x in range(width - 1):
                delta = _delta_e(grid[y][x], grid[y][x + 1])
                if x < band_width or x >= width - 1 - band_width:
                    band.append(delta)
                elif axis_length // 4 <= x < 3 * axis_length // 4:
                    interior.append(delta)
    else:
        for y in range(height - 1):
            for x in range(width):
                delta = _delta_e(grid[y][x], grid[y + 1][x])
                if y < band_width or y >= height - 1 - band_width:
                    band.append(delta)
                elif axis_length // 4 <= y < 3 * axis_length // 4:
                    interior.append(delta)
    band_mean = sum(band) / len(band)
    interior_mean = sum(interior) / len(interior)
    if interior_mean <= 1e-12:
        ratio = 1.0 if band_mean <= 1e-12 else math.inf
    else:
        ratio = band_mean / interior_mean
    ratio_in_range = (
        math.isfinite(ratio)
        and FINAL_EDGE_BAND_RATIO_MIN <= ratio <= FINAL_EDGE_BAND_RATIO_MAX
    )
    return {
        "band_mean_delta_e76": _rounded(band_mean),
        "band_to_interior_ratio": _rounded(ratio) if math.isfinite(ratio) else None,
        "band_width_pixels": band_width,
        "interior_mean_delta_e76": _rounded(interior_mean),
        "ratio_in_range": ratio_in_range,
    }


def _exact_seam(image: PNG.RGBAImage, axis: str) -> dict:
    mismatched = 0
    maximum_channel_difference = 0
    pixels = image.pixels
    if axis == "x":
        pairs = (
            ((y * image.width) * 4, (y * image.width + image.width - 1) * 4)
            for y in range(image.height)
        )
    else:
        pairs = (
            (x * 4, ((image.height - 1) * image.width + x) * 4)
            for x in range(image.width)
        )
    for first, second in pairs:
        left = pixels[first : first + 4]
        right = pixels[second : second + 4]
        if left != right:
            mismatched += 1
        maximum_channel_difference = max(
            maximum_channel_difference,
            *(abs(a - b) for a, b in zip(left, right)),
        )
    return {
        "exact": mismatched == 0,
        "maximum_channel_difference": maximum_channel_difference,
        "mismatched_pixel_pairs": mismatched,
    }


def analyze_final(image: PNG.RGBAImage) -> dict:
    """Measure drift and seam quality in a post-processed material master."""

    # UI masters are currently 512 square, so this preserves every final pixel.
    grid, sample_step = _block_lab_grid(image, 512)
    drift = _cell_and_ring_metrics(grid)
    seam_x = _exact_seam(image, "x")
    seam_y = _exact_seam(image, "y")
    gradient_x = _edge_gradient_metrics(grid, "x")
    gradient_y = _edge_gradient_metrics(grid, "y")
    checks = {
        "edge_band_matches_interior": (
            gradient_x["ratio_in_range"] and gradient_y["ratio_in_range"]
        ),
        "fully_opaque": _opaque(image),
        "no_directional_or_ring_drift": (
            drift["cell_max_delta_e76_from_global"]
            <= FINAL_CELL_DE_FROM_GLOBAL_LIMIT
            and drift["cell_max_pairwise_delta_e76"]
            <= FINAL_CELL_PAIRWISE_DE_LIMIT
            and drift["ring_delta_e76_from_interior"] <= FINAL_RING_DE_LIMIT
        ),
        "opposite_edges_exact": seam_x["exact"] and seam_y["exact"],
    }
    return {
        "checks": checks,
        "height": image.height,
        "metrics": {
            "chroma_distribution": _pixel_chroma_distribution(image),
            "drift": drift,
            "edge_gradient": {"x": gradient_x, "y": gradient_y},
            "sample_step_pixels": sample_step,
            "seams": {"left_right": seam_x, "top_bottom": seam_y},
        },
        "passes": all(checks.values()),
        "width": image.width,
    }


def evaluate_set(
    finals_by_resource_id: dict[int, dict],
    classic_by_resource_id: dict[int, dict],
) -> dict:
    """Evaluate palette-role constraints once the required members exist."""

    available = sorted(finals_by_resource_id)
    complete = all(resource_id in finals_by_resource_id for resource_id in UI_RESOURCE_IDS)
    classic_complete = all(
        resource_id in classic_by_resource_id for resource_id in UI_RESOURCE_IDS
    )
    result = {
        "available_resource_ids": available,
        "classic_baseline_complete": classic_complete,
        "complete": complete,
        "gray_131_neutrality": {"status": "not_evaluated"},
        "luminance_order_128_gt_129_gt_130": {"status": "not_evaluated"},
        "passes": None,
    }
    if not complete or not classic_complete:
        return result

    lightness = {
        resource_id: finals_by_resource_id[resource_id]["metrics"]["drift"][
            "global_mean_lab"
        ][0]
        for resource_id in (128, 129, 130)
    }
    steps = {
        "128_minus_129": _rounded(lightness[128] - lightness[129]),
        "129_minus_130": _rounded(lightness[129] - lightness[130]),
    }
    classic_lightness = {
        resource_id: classic_by_resource_id[resource_id]["global_mean_lab"][0]
        for resource_id in (128, 129, 130)
    }
    classic_steps = {
        "128_minus_129": _rounded(classic_lightness[128] - classic_lightness[129]),
        "129_minus_130": _rounded(classic_lightness[129] - classic_lightness[130]),
    }
    if any(step <= 0 for step in classic_steps.values()):
        raise ValidationError("locked Classic UI materials do not preserve 128 > 129 > 130")
    required_steps = {
        name: _rounded(
            max(
                SET_ABSOLUTE_MINIMUM_LSTAR_STEP,
                SET_CLASSIC_GAP_RETENTION * classic_step,
            )
        )
        for name, classic_step in classic_steps.items()
    }
    order_passes = all(steps[name] >= required_steps[name] for name in steps)
    gray_lab = finals_by_resource_id[131]["metrics"]["drift"]["global_mean_lab"]
    gray_mean_lab_chroma = math.hypot(gray_lab[1], gray_lab[2])
    gray_distribution = finals_by_resource_id[131]["metrics"][
        "chroma_distribution"
    ]
    gray_passes = (
        gray_distribution["mean_cstar_ab"] <= GRAY_PIXEL_MEAN_CHROMA_LIMIT
        and gray_distribution["percentile_95_cstar_ab"]
        <= GRAY_PIXEL_P95_CHROMA_LIMIT
    )
    result["luminance_order_128_gt_129_gt_130"] = {
        "classic_lightness_lstar": {
            str(key): _rounded(value) for key, value in classic_lightness.items()
        },
        "classic_steps_lstar": classic_steps,
        "classic_step_retention": SET_CLASSIC_GAP_RETENTION,
        "lightness_lstar": {str(key): _rounded(value) for key, value in lightness.items()},
        "absolute_minimum_step_lstar": SET_ABSOLUTE_MINIMUM_LSTAR_STEP,
        "passes": order_passes,
        "required_steps_lstar": required_steps,
        "status": "evaluated",
        "steps_lstar": steps,
    }
    result["gray_131_neutrality"] = {
        "mean_lab_chroma_cstar_ab": _rounded(gray_mean_lab_chroma),
        "mean_lab": gray_lab,
        "per_pixel_chroma": gray_distribution,
        "passes": gray_passes,
        "status": "evaluated",
    }
    result["passes"] = order_passes and gray_passes
    return result


def _load_json(path: Path, where: str) -> dict:
    try:
        value = json.loads(path.read_bytes())
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read {where}: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be a JSON object")
    return value


def _ui_jobs(root: Path) -> dict[int, dict]:
    jobs_path = root / "assets/remastered/style-proof/generation/jobs.json"
    document = _load_json(jobs_path, "style-proof jobs")
    result = {}
    for job in document.get("jobs", []):
        if not isinstance(job, dict) or job.get("family") != "ui_material":
            continue
        key = job.get("resource_key")
        resource_id = key.get("id") if isinstance(key, dict) else None
        if resource_id in UI_RESOURCE_IDS:
            if resource_id in result:
                raise ValidationError(f"duplicate UI material resource id {resource_id}")
            result[resource_id] = job
    if sorted(result) != list(UI_RESOURCE_IDS):
        raise ValidationError("jobs.json must define UI materials 128, 129, 130, and 131")
    return result


def _receipt_bindings(root: Path, jobs: dict[int, dict]) -> dict[str, dict]:
    """Load the authoritative current-attempt binding for each UI material."""

    path = root / "assets/remastered/style-proof/generation/generation-receipts.json"
    if not path.is_file():
        return {}
    document = _load_json(path, "generation receipts")
    entries = document.get("entries")
    if not isinstance(entries, list):
        raise ValidationError("generation receipts.entries must be an array")
    jobs_by_id = {job["job_id"]: job for job in jobs.values()}
    result: dict[str, dict] = {}
    for index, entry in enumerate(entries):
        where = f"generation receipts.entries[{index}]"
        if not isinstance(entry, dict):
            raise ValidationError(f"{where} must be an object")
        job_id = entry.get("job_id")
        if not isinstance(job_id, str):
            raise ValidationError(f"{where}.job_id must be a string")
        if job_id not in jobs_by_id:
            continue
        if job_id in result:
            raise ValidationError(f"{where}.job_id duplicates UI material {job_id}")
        job = jobs_by_id[job_id]
        ordinal = entry.get("regeneration_ordinal")
        if type(ordinal) is not int or ordinal not in (0, 1):
            raise ValidationError(f"{where}.regeneration_ordinal must be 0 or 1")
        status = entry.get("review_status")
        if status not in ("generated", "rejected", "approved"):
            raise ValidationError(f"{where}.review_status is invalid")

        raw = entry.get("raw_output")
        if not isinstance(raw, dict):
            raise ValidationError(f"{where}.raw_output must be an object")
        expected_raw_path = (
            "assets/remastered/style-proof/generation/raw/"
            f"{job_id}/attempt-{ordinal}.png"
        )
        if raw.get("path") != expected_raw_path:
            raise ValidationError(f"{where}.raw_output.path is not the selected attempt")
        raw_sha256 = raw.get("sha256")
        if not isinstance(raw_sha256, str) or not SHA256_RE.fullmatch(raw_sha256):
            raise ValidationError(f"{where}.raw_output.sha256 is invalid")

        output = entry.get("output")
        if status == "rejected":
            if output is not None:
                raise ValidationError(f"{where} rejected attempt may not bind a final output")
            output_binding = None
        else:
            if not isinstance(output, dict):
                raise ValidationError(f"{where}.output must bind the generated final PNG")
            if output.get("path") != job["expected_output_path"]:
                raise ValidationError(f"{where}.output.path does not match the job")
            output_sha256 = output.get("sha256")
            if not isinstance(output_sha256, str) or not SHA256_RE.fullmatch(
                output_sha256
            ):
                raise ValidationError(f"{where}.output.sha256 is invalid")
            output_binding = {
                "path": output["path"],
                "sha256": output_sha256,
            }
        result[job_id] = {
            "output": output_binding,
            "raw_output": {"path": raw["path"], "sha256": raw_sha256},
            "regeneration_ordinal": ordinal,
            "review_status": status,
        }
    return result


_ATTEMPT_RE = re.compile(r"attempt-(0|[1-9][0-9]*)\.png\Z")


def _load_png(path: Path) -> tuple[PNG.RGBAImage, str]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read PNG {path}: {exc}") from exc
    try:
        image = PNG.decode_png(data, str(path))
    except PNG.ValidationError as exc:
        raise ValidationError(str(exc)) from exc
    return image, _sha256(data)


def _classic_material_baselines(root: Path, jobs: dict[int, dict]) -> dict[int, dict]:
    reference_root = root / "assets/remastered/style-proof/classic-references"
    result = {}
    for resource_id, job in jobs.items():
        input_record = job.get("input")
        if not isinstance(input_record, dict):
            raise ValidationError(f"{job['job_id']} input lock is malformed")
        relative = input_record.get("path")
        expected_sha256 = input_record.get("decoded_png_sha256")
        if (
            not isinstance(relative, str)
            or Path(relative).is_absolute()
            or ".." in Path(relative).parts
        ):
            raise ValidationError(f"{job['job_id']} Classic reference path is unsafe")
        if not isinstance(expected_sha256, str) or not SHA256_RE.fullmatch(
            expected_sha256
        ):
            raise ValidationError(f"{job['job_id']} Classic reference hash is malformed")
        image, actual_sha256 = _load_png(reference_root / relative)
        if actual_sha256 != expected_sha256:
            raise ValidationError(f"{job['job_id']} Classic reference hash is stale")
        grid, _ = _block_lab_grid(image, max(image.width, image.height))
        mean_lab = _mean_lab(pixel for row in grid for pixel in row)
        result[resource_id] = {
            "decoded_png_sha256": actual_sha256,
            "global_mean_lab": [_rounded(value) for value in mean_lab],
            "path": f"assets/remastered/style-proof/classic-references/{relative}",
        }
    return result


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def _tile_2x2(image: PNG.RGBAImage) -> PNG.RGBAImage:
    width = image.width * 2
    height = image.height * 2
    output = bytearray(width * height * 4)
    for y in range(height):
        source_y = y % image.height
        for x in range(width):
            source_x = x % image.width
            source = (source_y * image.width + source_x) * 4
            destination = (y * width + x) * 4
            output[destination : destination + 4] = image.pixels[source : source + 4]
    return PNG.RGBAImage(width, height, bytes(output))


def _half_roll(image: PNG.RGBAImage) -> PNG.RGBAImage:
    output = bytearray(len(image.pixels))
    x_offset = image.width // 2
    y_offset = image.height // 2
    for y in range(image.height):
        for x in range(image.width):
            source_x = (x + x_offset) % image.width
            source_y = (y + y_offset) % image.height
            source = (source_y * image.width + source_x) * 4
            destination = (y * image.width + x) * 4
            output[destination : destination + 4] = image.pixels[source : source + 4]
    return PNG.RGBAImage(image.width, image.height, bytes(output))


def _write_evidence(
    image: PNG.RGBAImage,
    evidence_dir: Path,
    job_id: str,
    root: Path,
) -> dict:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, evidence in (("2x2", _tile_2x2(image)), ("half-roll", _half_roll(image))):
        path = evidence_dir / f"{job_id}-{name}.png"
        data = PNG.encode_png(evidence)
        path.write_bytes(data)
        # Decode what was written: evidence is not trusted merely because encoding returned.
        decoded = PNG.decode_png(path.read_bytes(), str(path))
        if decoded != evidence:
            raise ValidationError(f"evidence PNG read-back differs: {path}")
        result[name] = {"path": _relative(path, root), "sha256": _sha256(data)}
    return result


def validate(root: Path, evidence_dir: Path | None = None) -> dict:
    root = root.resolve()
    jobs = _ui_jobs(root)
    receipt_bindings = _receipt_bindings(root, jobs)
    classic_baselines = _classic_material_baselines(root, jobs)
    materials = []
    present_finals = {}
    current_failures = []
    for resource_id in UI_RESOURCE_IDS:
        job = jobs[resource_id]
        job_id = job["job_id"]
        binding = receipt_bindings.get(job_id)
        raw_dir = root / "assets/remastered/style-proof/generation/raw" / job_id
        raw_attempts = []
        if raw_dir.is_dir():
            attempts = []
            for path in raw_dir.iterdir():
                match = _ATTEMPT_RE.fullmatch(path.name)
                if match and path.is_file():
                    attempts.append((int(match.group(1)), path))
            for ordinal, path in sorted(attempts):
                image, digest = _load_png(path)
                analysis = analyze_raw(image)
                relative_path = _relative(path, root)
                bound_raw = (
                    binding is not None
                    and binding["regeneration_ordinal"] == ordinal
                    and binding["raw_output"]["path"] == relative_path
                )
                if bound_raw and binding["raw_output"]["sha256"] != digest:
                    raise ValidationError(f"{job_id} selected raw SHA-256 is stale")
                raw_attempts.append(
                    {
                        **analysis,
                        "path": relative_path,
                        "regeneration_ordinal": ordinal,
                        "selected_by_receipt": (
                            bound_raw
                            and binding["review_status"] in ("generated", "approved")
                        ),
                        "sha256": digest,
                    }
                )

        bound_raw_attempts = []
        if binding is not None:
            bound_raw_attempts = [
                attempt
                for attempt in raw_attempts
                if attempt["regeneration_ordinal"] == binding["regeneration_ordinal"]
                and attempt["path"] == binding["raw_output"]["path"]
            ]
            if len(bound_raw_attempts) != 1:
                raise ValidationError(f"{job_id} receipt-bound raw PNG is missing")

        output_path = root / job["expected_output_path"]
        final = None
        if output_path.is_file():
            image, digest = _load_png(output_path)
            final = {
                **analyze_final(image),
                "path": _relative(output_path, root),
                "sha256": digest,
            }
            if evidence_dir is not None:
                final["evidence"] = _write_evidence(
                    image, evidence_dir.resolve(), job_id, root
                )
            present_finals[resource_id] = final

        if binding is not None and binding["review_status"] in ("generated", "approved"):
            if final is None:
                raise ValidationError(f"{job_id} receipt-bound final PNG is missing")
            if binding["output"]["path"] != final["path"]:
                raise ValidationError(f"{job_id} receipt-bound final path is stale")
            if binding["output"]["sha256"] != final["sha256"]:
                raise ValidationError(f"{job_id} receipt-bound final SHA-256 is stale")
        elif binding is not None and binding["review_status"] == "rejected" and final is not None:
            raise ValidationError(f"{job_id} rejected receipt conflicts with a final PNG")

        selected = [attempt for attempt in raw_attempts if attempt["selected_by_receipt"]]
        if binding is not None and binding["review_status"] == "rejected":
            state = "rejected"
            current_passes = None
        elif final is None:
            state = "pending"
            current_passes = None
        elif len(selected) != 1:
            state = "invalid"
            current_passes = False
            current_failures.append(f"{job_id}: final exists without one selected raw receipt")
        else:
            current_passes = final["passes"] and selected[0]["passes"]
            state = "passes" if current_passes else "fails"
            if not current_passes:
                current_failures.append(f"{job_id}: selected raw or final material failed")

        materials.append(
            {
                "current_passes": current_passes,
                "final": final,
                "job_id": job_id,
                "raw_attempts": raw_attempts,
                "resource_id": resource_id,
                "state": state,
            }
        )

    set_checks = evaluate_set(present_finals, classic_baselines)
    if set_checks["passes"] is False:
        current_failures.append("complete UI material set failed palette-role checks")
    complete = len(present_finals) == len(UI_RESOURCE_IDS)
    return {
        "complete": complete,
        "current_failures": current_failures,
        "materials": materials,
        "passes": not current_failures,
        "schema_version": SCHEMA_VERSION,
        "set_checks": set_checks,
        "thresholds": {
            "final_cell_delta_e76_from_global_max": FINAL_CELL_DE_FROM_GLOBAL_LIMIT,
            "final_cell_pairwise_delta_e76_max": FINAL_CELL_PAIRWISE_DE_LIMIT,
            "final_edge_band_to_interior_ratio": [
                FINAL_EDGE_BAND_RATIO_MIN,
                FINAL_EDGE_BAND_RATIO_MAX,
            ],
            "final_ring_delta_e76_max": FINAL_RING_DE_LIMIT,
            "gray_131_pixel_mean_chroma_cstar_ab_max": (
                GRAY_PIXEL_MEAN_CHROMA_LIMIT
            ),
            "gray_131_pixel_p95_chroma_cstar_ab_max": (
                GRAY_PIXEL_P95_CHROMA_LIMIT
            ),
            "raw_autocorrelation_peak_absolute_max": RAW_AUTOCORRELATION_LIMIT,
            "raw_gradient_periodicity_peak_max": RAW_GRADIENT_PERIODICITY_LIMIT,
            "set_absolute_minimum_lightness_step_lstar": (
                SET_ABSOLUTE_MINIMUM_LSTAR_STEP
            ),
            "set_classic_gap_retention": SET_CLASSIC_GAP_RETENTION,
        },
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=SCRIPT_DIR.parent)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--output", type=Path, help="also write canonical JSON here")
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="fail while any of the four final masters is still pending",
    )
    arguments = parser.parse_args(argv)
    try:
        report = validate(arguments.root, arguments.evidence_dir)
        payload = _canonical_json(report)
        if arguments.output is not None:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_bytes(payload)
        sys.stdout.buffer.write(payload)
        if not report["passes"]:
            return 1
        if arguments.require_complete and not report["complete"]:
            return 1
        return 0
    except ValidationError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
