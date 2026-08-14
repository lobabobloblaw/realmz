#!/usr/bin/env python3
"""Deterministically post-process an opaque style-proof model output.

The immutable generation job selects both the raw-attempt path and the final
output path.  The caller must also supply the expected raw SHA-256, so a model
output cannot be silently replaced between inspection and post-processing.
Pillow is used only to decode the provider PNG and perform the documented
Lanczos resize.  Final PNG bytes come from the repository's canonical encoder.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any

from PIL import Image, UnidentifiedImageError


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
import prepare_style_proof_references as reference_tool  # noqa: E402
import validate_style_proof_outputs as output_validator  # noqa: E402


DEFAULT_JOBS = "assets/remastered/style-proof/generation/jobs.json"
RAW_ROOT = "assets/remastered/style-proof/generation/raw"
POSTPROCESS_ROOT = "assets/remastered/style-proof/generation/postprocess"
SCRIPT_NAME = "scripts/process_style_proof_output.py"
SCRIPT_VERSION = "1"
STATE_MAGIC = b"REALMZ-RGBA8-STATE-V1\x00"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ProcessingError(ValueError):
    """Raised when an immutable input or deterministic output gate fails."""


@dataclass(frozen=True)
class RasterState:
    width: int
    height: int
    pixels: bytes

    def serialized(self) -> bytes:
        if self.width <= 0 or self.height <= 0:
            raise ProcessingError("RGBA8 state dimensions must be positive")
        if len(self.pixels) != self.width * self.height * 4:
            raise ProcessingError("RGBA8 state has an invalid pixel-buffer size")
        return STATE_MAGIC + struct.pack(">II", self.width, self.height) + self.pixels


@dataclass(frozen=True)
class ProcessedOutput:
    final_png: bytes
    operations: tuple[dict[str, Any], ...]
    intermediate_states: tuple[tuple[str, bytes], ...]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _operation(
    name: str,
    tool: str,
    tool_version: str,
    input_bytes: bytes,
    output_bytes: bytes,
    **parameters: Any,
) -> dict[str, Any]:
    input_hash = _sha256(input_bytes)
    output_hash = _sha256(output_bytes)
    return {
        "operation": name,
        "parameters": {
            **parameters,
            "input_sha256": input_hash,
            "output_changed": input_hash != output_hash,
            "output_sha256": output_hash,
        },
        "tool": tool,
        "tool_version": tool_version,
    }


def _decode_opaque_png(raw_png: bytes) -> tuple[RasterState, str]:
    if not raw_png.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ProcessingError("raw model output is not a PNG")
    try:
        with Image.open(io.BytesIO(raw_png)) as source:
            if source.format != "PNG":
                raise ProcessingError("raw model output did not decode as PNG")
            source.load()
            source_mode = source.mode
            rgba = source.convert("RGBA")
    except (OSError, UnidentifiedImageError) as exc:
        raise ProcessingError(f"cannot decode raw model PNG: {exc}") from exc
    pixels = rgba.tobytes()
    if any(alpha != 255 for alpha in pixels[3::4]):
        raise ProcessingError("opaque post-processing refuses raw PNG transparency")
    return RasterState(rgba.width, rgba.height, pixels), source_mode


def _resize_lanczos(source: RasterState, width: int, height: int) -> RasterState:
    if source.width * height != source.height * width:
        raise ProcessingError(
            "raw and target aspect ratios differ; stretching and cropping are forbidden"
        )
    image = Image.frombytes("RGBA", (source.width, source.height), source.pixels)
    if image.size != (width, height):
        image = image.resize((width, height), resample=Image.Resampling.LANCZOS)
    pixels = image.tobytes()
    if any(alpha != 255 for alpha in pixels[3::4]):
        raise ProcessingError("Lanczos resize did not preserve fully opaque alpha")
    return RasterState(width, height, pixels)


def _smoothstep_fraction(distance: int, band: int) -> tuple[int, int]:
    """Return fixed-point cubic smoothstep weight for 0 <= distance < band."""
    scale = 65535
    position = (distance * scale) // band
    numerator = position * position * (3 * scale - 2 * position)
    denominator = scale * scale * scale
    return numerator, denominator


def _toward_shared(
    original: int,
    shared: int,
    weight_numerator: int,
    weight_denominator: int,
) -> int:
    return (
        original * weight_numerator
        + shared * (weight_denominator - weight_numerator)
        + weight_denominator // 2
    ) // weight_denominator


def _blend_opposing_axis(state: RasterState, horizontal: bool, band: int) -> RasterState:
    source = state.pixels
    output = bytearray(source)
    outer = state.height if horizontal else state.width
    extent = state.width if horizontal else state.height
    for outer_index in range(outer):
        for distance in range(band):
            low = distance
            high = extent - 1 - distance
            if horizontal:
                low_pixel = outer_index * state.width + low
                high_pixel = outer_index * state.width + high
            else:
                low_pixel = low * state.width + outer_index
                high_pixel = high * state.width + outer_index
            weight_numerator, weight_denominator = _smoothstep_fraction(distance, band)
            for channel in range(4):
                low_offset = low_pixel * 4 + channel
                high_offset = high_pixel * 4 + channel
                low_value = source[low_offset]
                high_value = source[high_offset]
                shared = (low_value + high_value + 1) // 2
                output[low_offset] = _toward_shared(
                    low_value, shared, weight_numerator, weight_denominator
                )
                output[high_offset] = _toward_shared(
                    high_value, shared, weight_numerator, weight_denominator
                )
    return RasterState(state.width, state.height, bytes(output))


def _make_tileable(state: RasterState) -> tuple[RasterState, int]:
    minimum_extent = min(state.width, state.height)
    if minimum_extent < 4:
        raise ProcessingError("UI tile target is too small for smooth seam blending")
    band = min(max(2, minimum_extent // 8), (minimum_extent - 1) // 2)
    horizontal = _blend_opposing_axis(state, True, band)
    result = _blend_opposing_axis(horizontal, False, band)
    pixels = result.pixels
    for y in range(result.height):
        left = (y * result.width) * 4
        right = (y * result.width + result.width - 1) * 4
        if pixels[left : left + 4] != pixels[right : right + 4]:
            raise ProcessingError("horizontal seam assertion failed")
    row_bytes = result.width * 4
    if pixels[:row_bytes] != pixels[-row_bytes:]:
        raise ProcessingError("vertical seam assertion failed")
    if any(alpha != 255 for alpha in pixels[3::4]):
        raise ProcessingError("seam blending did not preserve fully opaque alpha")
    if result.serialized() == state.serialized():
        raise ProcessingError("UI seam operation was unexpectedly a no-op")
    return result, band


def process_opaque_png(raw_png: bytes, job: dict[str, Any]) -> ProcessedOutput:
    """Return canonical output and its validator-compatible operation chain."""
    alpha = job.get("alpha")
    if not isinstance(alpha, dict) or alpha.get("mode") != "opaque":
        raise ProcessingError("this processor accepts only immutable opaque jobs")
    target = job.get("target_master")
    if not isinstance(target, dict):
        raise ProcessingError("job has no target_master")
    width = target.get("width")
    height = target.get("height")
    if (
        not isinstance(width, int)
        or isinstance(width, bool)
        or not isinstance(height, int)
        or isinstance(height, bool)
        or width <= 0
        or height <= 0
        or target.get("format") != "PNG"
        or target.get("color_space") != "sRGB"
    ):
        raise ProcessingError("job target must be a positive-size sRGB PNG")

    decoded, source_mode = _decode_opaque_png(raw_png)
    resized = _resize_lanczos(decoded, width, height)
    resized_bytes = resized.serialized()
    operations: list[dict[str, Any]] = [
        _operation(
            "resize_to_target",
            "Pillow",
            Image.__version__,
            raw_png,
            resized_bytes,
            algorithm=(
                "decode PNG, convert fully opaque pixels to RGBA8, verify exact "
                "aspect by integer cross-product, and resize with "
                "PIL.Image.Resampling.LANCZOS; no crop or stretch"
            ),
            aspect_assertion="source_width*target_height == source_height*target_width",
            decoder_source_mode=source_mode,
            intermediate_state_format="REALMZ-RGBA8-STATE-V1 + big-endian width/height + row-major RGBA8",
            resampler="PIL.Image.Resampling.LANCZOS",
            source_height=decoded.height,
            source_width=decoded.width,
            target_height=height,
            target_width=width,
        )
    ]
    intermediates: list[tuple[str, bytes]] = [("01-resized.rgba8", resized_bytes)]
    final_state = resized

    if job.get("family") == "ui_material":
        tileable, band = _make_tileable(resized)
        tileable_bytes = tileable.serialized()
        operations.append(
            _operation(
                "make_tileable_opposite_edges",
                SCRIPT_NAME,
                SCRIPT_VERSION,
                resized_bytes,
                tileable_bytes,
                algorithm=(
                    "blend symmetric opposing samples across an edge band using "
                    "Q16 integer cubic smoothstep t^2(3-2t); each terminal pair "
                    "receives the same rounded half-sum; horizontal axis then "
                    "vertical axis; assert exact opposing RGBA8 boundaries"
                ),
                axis_order="horizontal_then_vertical",
                boundary_color="rounded_half_sum_of_opposing_samples",
                edge_band_pixels=band,
                smoothstep="Q16 integer cubic t^2(3-2t)",
            )
        )
        intermediates.append(("02-tileable.rgba8", tileable_bytes))
        final_state = tileable

    final_state_bytes = final_state.serialized()
    final_png = reference_tool.encode_png(
        reference_tool.RGBAImage(
            final_state.width, final_state.height, final_state.pixels
        )
    )
    operations.append(
        _operation(
            "encode_srgb_png",
            "scripts/prepare_style_proof_references.py:encode_png",
            SCRIPT_VERSION,
            final_state_bytes,
            final_png,
            algorithm=(
                "canonical RGBA8 PNG with filter type 0 rows, IHDR color type 6, "
                "sRGB rendering intent 0, zlib level 9, and no variable metadata"
            ),
            input_state_format="REALMZ-RGBA8-STATE-V1",
            output_color_space="sRGB",
            output_pixel_format="RGBA8",
        )
    )

    decoded_final = reference_tool.decode_png(
        final_png,
        "canonical style-proof output",
        expected_dimensions=(width, height),
    )
    if decoded_final.pixels != final_state.pixels:
        raise ProcessingError("canonical PNG read-back differs from the encoded RGBA8 state")
    if any(alpha_value != 255 for alpha_value in decoded_final.pixels[3::4]):
        raise ProcessingError("canonical PNG read-back is not fully opaque")
    return ProcessedOutput(final_png, tuple(operations), tuple(intermediates))


def _load_immutable_job(root: Path, jobs_path: Path, job_id: str) -> dict[str, Any]:
    try:
        jobs, _, _ = output_validator._load_job_set(root, jobs_path)
    except output_validator.ValidationError as exc:
        raise ProcessingError(str(exc)) from exc
    matches = [job for job in jobs["jobs"] if job["job_id"] == job_id]
    if len(matches) != 1:
        raise ProcessingError(f"unknown immutable style-proof job {job_id!r}")
    return matches[0]


def _exclusive_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as output:
            output.write(data)
    except FileExistsError as exc:
        raise ProcessingError(f"refusing to overwrite {path}") from exc


def process_job(
    root: Path,
    jobs_path: Path,
    job_id: str,
    regeneration_ordinal: int,
    expected_raw_sha256: str,
    *,
    write_intermediates: bool = False,
) -> dict[str, Any]:
    root = root.resolve()
    if not jobs_path.is_absolute():
        jobs_path = root / jobs_path
    jobs_path = jobs_path.resolve()
    try:
        jobs_path.relative_to(root)
    except ValueError as exc:
        raise ProcessingError("jobs path escapes the repository root") from exc
    if regeneration_ordinal not in (0, 1):
        raise ProcessingError("raw attempt ordinal must be 0 or 1")
    if SHA256_RE.fullmatch(expected_raw_sha256) is None:
        raise ProcessingError("expected raw hash must be a lowercase SHA-256")

    job = _load_immutable_job(root, jobs_path, job_id)
    if job["alpha"]["input_alpha"] != "fully_opaque":
        raise ProcessingError("this processor refuses masked or chroma-key jobs")
    raw_relative = (
        f"{RAW_ROOT}/{job_id}/attempt-{regeneration_ordinal}.png"
    )
    try:
        raw_path = output_validator._under(root, raw_relative, "raw model output")
        final_path = output_validator._under(
            root, job["expected_output_path"], "final style-proof output"
        )
    except output_validator.ValidationError as exc:
        raise ProcessingError(str(exc)) from exc
    try:
        raw_png = raw_path.read_bytes()
    except OSError as exc:
        raise ProcessingError(f"cannot read raw model output {raw_path}: {exc}") from exc
    actual_raw_hash = _sha256(raw_png)
    if actual_raw_hash != expected_raw_sha256:
        raise ProcessingError(
            "raw model output SHA-256 differs from the caller's immutable input hash"
        )
    if final_path.exists() or final_path.is_symlink():
        raise ProcessingError(f"refusing to overwrite {final_path}")

    processed = process_opaque_png(raw_png, job)
    output_record = {
        "color_space": "sRGB",
        "height": job["target_master"]["height"],
        "path": job["expected_output_path"],
        "pixel_format": "RGBA8",
        "sha256": _sha256(processed.final_png),
        "width": job["target_master"]["width"],
    }
    fragment: dict[str, Any] = {
        "job_id": job_id,
        "output": output_record,
        "post_processing": list(processed.operations),
        "raw_output": {
            "path": raw_relative,
            "sha256": actual_raw_hash,
        },
        "regeneration_ordinal": regeneration_ordinal,
    }

    planned: list[tuple[Path, bytes]] = []
    if write_intermediates:
        intermediate_root = output_validator._under(
            root,
            f"{POSTPROCESS_ROOT}/{job_id}/attempt-{regeneration_ordinal}",
            "post-process intermediate directory",
        )
        artifact_records = []
        for filename, data in processed.intermediate_states:
            path = intermediate_root / filename
            planned.append((path, data))
            artifact_records.append(
                {
                    "path": path.relative_to(root).as_posix(),
                    "sha256": _sha256(data),
                }
            )
        fragment["intermediate_artifacts"] = artifact_records
        receipt_path = intermediate_root / "operation-fragment.json"
        planned.append((receipt_path, _canonical_json(fragment)))
    planned.append((final_path, processed.final_png))

    for path, _ in planned:
        if path.exists() or path.is_symlink():
            raise ProcessingError(f"refusing to overwrite {path}")
    written: list[Path] = []
    try:
        for path, data in planned:
            _exclusive_write(path, data)
            written.append(path)
    except Exception:
        for path in reversed(written):
            try:
                path.unlink()
            except OSError:
                pass
        raise
    return fragment


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--jobs", type=Path, default=Path(DEFAULT_JOBS))
    parser.add_argument("--job", required=True, help="immutable style-proof job ID")
    parser.add_argument(
        "--attempt",
        "--regeneration-ordinal",
        dest="attempt",
        required=True,
        type=int,
        choices=(0, 1),
        help="raw generation attempt ordinal",
    )
    parser.add_argument(
        "--raw-sha256",
        required=True,
        help="expected SHA-256 of the exact raw PNG to consume",
    )
    parser.add_argument(
        "--write-intermediates",
        action="store_true",
        help="lock hashed RGBA8 states and the operation fragment under generation/postprocess",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        fragment = process_job(
            args.root,
            args.jobs,
            args.job,
            args.attempt,
            args.raw_sha256,
            write_intermediates=args.write_intermediates,
        )
    except ProcessingError as exc:
        print(f"style-proof post-processing failed: {exc}", file=sys.stderr)
        return 1
    sys.stdout.buffer.write(_canonical_json(fragment))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
