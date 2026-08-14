#!/usr/bin/env python3
"""Deterministically post-process a masked style-proof model output.

The model supplies opaque RGB or RGBA chroma pixels.  A freshly verified
generation-input handoff supplies the authoritative Classic binary alpha mask.
Chroma is removed only outside that mask; a key-colored pixel inside the mask
is rejected as model underfill unless the immutable ResourceKey and the
corresponding Classic pixel both prove that magenta is source-significant
gameplay content.

The immutable job selects the raw-attempt and final paths.  The caller supplies
the exact raw SHA-256, and every write is exclusive, so inspected inputs and
accepted outputs cannot be silently replaced.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
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
DEFAULT_SELECTION = "assets/remastered/style-proof/classic-selection.json"
RAW_ROOT = "assets/remastered/style-proof/generation/raw"
POSTPROCESS_ROOT = "assets/remastered/style-proof/generation/postprocess"
SCRIPT_NAME = "scripts/process_masked_style_proof_output.py"
SCRIPT_VERSION = "1"
STATE_MAGIC = b"REALMZ-RGBA8-STATE-V1\x00"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CHROMA_KEY = (255, 0, 255)
CHROMA_TOLERANCE = output_validator.CHROMA_TOLERANCE
MAGENTA_SOURCE_KEYS = {
    ("Data Files/Tacticals", "cicn", 9007),
    ("Data Files/The Family Jewels", "cicn", 12100),
}


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


def _decode_chroma_png(raw_png: bytes) -> tuple[RasterState, str]:
    if not raw_png.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ProcessingError("raw model output is not a PNG")
    try:
        with Image.open(io.BytesIO(raw_png)) as source:
            if source.format != "PNG":
                raise ProcessingError("raw model output did not decode as PNG")
            source.load()
            source_mode = source.mode
            if source_mode not in ("RGB", "RGBA"):
                raise ProcessingError("raw chroma PNG must use RGB or RGBA pixels")
            rgba = source.convert("RGBA")
    except (OSError, UnidentifiedImageError) as exc:
        raise ProcessingError(f"cannot decode raw model PNG: {exc}") from exc
    pixels = rgba.tobytes()
    if any(alpha != 255 for alpha in pixels[3::4]):
        raise ProcessingError(
            "raw chroma PNG must be fully opaque; the Classic handoff owns alpha"
        )
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
        raise ProcessingError("Lanczos resize did not preserve fully opaque chroma RGB")
    return RasterState(width, height, pixels)


def _nearest_source_indices(
    source: reference_tool.RGBAImage, width: int, height: int
) -> tuple[int, ...]:
    indices: list[int] = []
    for y in range(height):
        source_y = min(source.height - 1, y * source.height // height)
        for x in range(width):
            source_x = min(source.width - 1, x * source.width // width)
            indices.append(source_y * source.width + source_x)
    return tuple(indices)


def _is_near_chroma(red: int, green: int, blue: int) -> bool:
    return (
        red >= CHROMA_KEY[0] - CHROMA_TOLERANCE
        and green <= CHROMA_KEY[1] + CHROMA_TOLERANCE
        and blue >= CHROMA_KEY[2] - CHROMA_TOLERANCE
    )


def _is_source_magenta(red: int, green: int, blue: int) -> bool:
    # This is deliberately identical to the final output validator's evidence
    # predicate.  Exact key color remains ambiguous without Classic evidence.
    return (
        red >= 96
        and blue >= 96
        and red >= green + 24
        and blue >= green + 24
        and not _is_near_chroma(red, green, blue)
    )


def _resource_identity(job: dict[str, Any]) -> tuple[str, str, int] | None:
    key = job.get("resource_key")
    if not isinstance(key, dict):
        return None
    pack = key.get("pack")
    kind = key.get("type")
    resource_id = key.get("id")
    if not isinstance(pack, str) or not isinstance(kind, str) or not isinstance(resource_id, int):
        return None
    return (pack, kind, resource_id)


def _validate_classic_mask(classic: reference_tool.RGBAImage) -> None:
    if classic.width <= 0 or classic.height <= 0:
        raise ProcessingError("Classic handoff mask has invalid dimensions")
    if len(classic.pixels) != classic.width * classic.height * 4:
        raise ProcessingError("Classic handoff mask has an invalid pixel buffer")
    alpha_values = set(classic.pixels[3::4])
    if alpha_values - {0, 255} or alpha_values != {0, 255}:
        raise ProcessingError(
            "Classic handoff alpha must be a nonempty binary foreground/background mask"
        )


def _remove_chroma_outside_mask(
    resized: RasterState,
    classic: reference_tool.RGBAImage,
    nearest_indices: tuple[int, ...],
) -> tuple[RasterState, int]:
    output = bytearray(resized.pixels)
    removed = 0
    for index, source_index in enumerate(nearest_indices):
        source_alpha = classic.pixels[source_index * 4 + 3]
        offset = index * 4
        if source_alpha == 0 and _is_near_chroma(*output[offset : offset + 3]):
            output[offset + 3] = 0
            removed += 1
    return RasterState(resized.width, resized.height, bytes(output)), removed


def _apply_authoritative_mask(
    state: RasterState,
    classic: reference_tool.RGBAImage,
    nearest_indices: tuple[int, ...],
    job: dict[str, Any],
) -> tuple[RasterState, int]:
    output = bytearray(state.pixels)
    identity = _resource_identity(job)
    source_exception_enabled = identity in MAGENTA_SOURCE_KEYS
    authorized_magenta = 0
    underfill: list[tuple[int, int]] = []
    for index, source_index in enumerate(nearest_indices):
        source_offset = source_index * 4
        alpha = classic.pixels[source_offset + 3]
        output_offset = index * 4
        if alpha and _is_near_chroma(*output[output_offset : output_offset + 3]):
            source_rgb = classic.pixels[source_offset : source_offset + 3]
            if source_exception_enabled and _is_source_magenta(*source_rgb):
                authorized_magenta += 1
            else:
                underfill.append((index % state.width, index // state.width))
        output[output_offset + 3] = alpha
    if underfill:
        preview = ", ".join(f"({x},{y})" for x, y in underfill[:8])
        suffix = "" if len(underfill) <= 8 else ", ..."
        raise ProcessingError(
            f"model underfill: {len(underfill)} near-#FF00FF pixel(s) occur "
            f"inside the authoritative Classic mask at {preview}{suffix}"
        )
    return RasterState(state.width, state.height, bytes(output)), authorized_magenta


def _decontaminate_transparent_edges(
    state: RasterState,
    classic: reference_tool.RGBAImage,
    nearest_indices: tuple[int, ...],
    job: dict[str, Any],
) -> tuple[RasterState, int, int]:
    """Bleed nearest opaque RGB into transparent pixels using deterministic BFS.

    For the two source-significant magenta resources, exact-key-looking opaque
    pixels are minimally disambiguated (green becomes tolerance + 1) only where
    the nearest Classic pixel independently proves magenta subject content.
    Their alpha is never removed.
    """
    output = bytearray(state.pixels)
    identity = _resource_identity(job)
    source_exception_enabled = identity in MAGENTA_SOURCE_KEYS
    magenta_disambiguated = 0
    opaque_indices: list[int] = []
    for index, source_index in enumerate(nearest_indices):
        offset = index * 4
        if output[offset + 3] == 0:
            continue
        if _is_near_chroma(*output[offset : offset + 3]):
            source_offset = source_index * 4
            source_rgb = classic.pixels[source_offset : source_offset + 3]
            if not source_exception_enabled or not _is_source_magenta(*source_rgb):
                raise ProcessingError(
                    "near-#FF00FF opaque pixel survived the underfill gate"
                )
            output[offset + 1] = CHROMA_TOLERANCE + 1
            magenta_disambiguated += 1
        opaque_indices.append(index)
    if not opaque_indices:
        raise ProcessingError("authoritative Classic mask has no opaque pixels")

    owner = [-1] * (state.width * state.height)
    queue: deque[int] = deque()
    for index in opaque_indices:  # row-major seeds define reproducible tie breaks
        owner[index] = index
        queue.append(index)
    while queue:
        index = queue.popleft()
        x = index % state.width
        y = index // state.width
        # Fixed left, right, up, down visitation order is part of the algorithm.
        neighbors = []
        if x:
            neighbors.append(index - 1)
        if x + 1 < state.width:
            neighbors.append(index + 1)
        if y:
            neighbors.append(index - state.width)
        if y + 1 < state.height:
            neighbors.append(index + state.width)
        for neighbor in neighbors:
            if owner[neighbor] == -1:
                owner[neighbor] = owner[index]
                queue.append(neighbor)

    transparent_filled = 0
    for index, source_index in enumerate(owner):
        offset = index * 4
        if output[offset + 3] != 0:
            continue
        source_offset = source_index * 4
        output[offset : offset + 3] = output[source_offset : source_offset + 3]
        transparent_filled += 1
    for index in range(state.width * state.height):
        offset = index * 4
        if _is_near_chroma(*output[offset : offset + 3]):
            raise ProcessingError("transparent-edge decontamination retained chroma")
    return (
        RasterState(state.width, state.height, bytes(output)),
        transparent_filled,
        magenta_disambiguated,
    )


def process_masked_png(
    raw_png: bytes,
    job: dict[str, Any],
    classic: reference_tool.RGBAImage,
) -> ProcessedOutput:
    """Return canonical masked output and its validator-compatible hash chain."""
    alpha = job.get("alpha")
    if not isinstance(alpha, dict) or alpha.get("input_alpha") != "binary_mask":
        raise ProcessingError("this processor accepts only immutable binary-mask jobs")
    if (
        alpha.get("mode") != "chroma_key_then_original_mask"
        or alpha.get("chroma_key") != "#FF00FF"
    ):
        raise ProcessingError("masked job does not use the locked #FF00FF policy")
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
    _validate_classic_mask(classic)
    nearest_indices = _nearest_source_indices(classic, width, height)

    decoded, source_mode = _decode_chroma_png(raw_png)
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
                "decode an opaque RGB/RGBA PNG, convert to RGBA8, verify exact "
                "aspect by integer cross-product, and resize with "
                "PIL.Image.Resampling.LANCZOS; no crop or stretch"
            ),
            aspect_assertion="source_width*target_height == source_height*target_width",
            decoder_source_mode=source_mode,
            intermediate_state_format=(
                "REALMZ-RGBA8-STATE-V1 + big-endian width/height + row-major RGBA8"
            ),
            resampler="PIL.Image.Resampling.LANCZOS",
            source_height=decoded.height,
            source_width=decoded.width,
            target_height=height,
            target_width=width,
        )
    ]
    intermediates: list[tuple[str, bytes]] = [("01-resized.rgba8", resized_bytes)]

    chroma_removed, removed_count = _remove_chroma_outside_mask(
        resized, classic, nearest_indices
    )
    chroma_removed_bytes = chroma_removed.serialized()
    operations.append(
        _operation(
            "remove_chroma_outside_original_mask",
            SCRIPT_NAME,
            SCRIPT_VERSION,
            resized_bytes,
            chroma_removed_bytes,
            authoritative_mask_resampler="nearest_floor_integer_mapping",
            chroma_key="#FF00FF",
            chroma_tolerance=CHROMA_TOLERANCE,
            keyed_pixel_count=removed_count,
            policy="change alpha only for near-key pixels outside the Classic mask",
        )
    )
    intermediates.append(("02-chroma-outside-mask.rgba8", chroma_removed_bytes))

    masked, authorized_magenta = _apply_authoritative_mask(
        chroma_removed, classic, nearest_indices, job
    )
    masked_bytes = masked.serialized()
    operations.append(
        _operation(
            "apply_original_alpha_mask",
            SCRIPT_NAME,
            SCRIPT_VERSION,
            chroma_removed_bytes,
            masked_bytes,
            alpha_values=[0, 255],
            authoritative_mask_resampler="nearest_floor_integer_mapping",
            inside_chroma_policy="reject_as_model_underfill_unless_source-proven",
            source_authorized_magenta_pixel_count=authorized_magenta,
            source_magenta_resource_allowlist=[
                {"id": resource_id, "pack": pack, "type": kind}
                for pack, kind, resource_id in sorted(MAGENTA_SOURCE_KEYS)
            ],
        )
    )
    intermediates.append(("03-authoritative-mask.rgba8", masked_bytes))

    decontaminated, transparent_filled, magenta_disambiguated = (
        _decontaminate_transparent_edges(masked, classic, nearest_indices, job)
    )
    decontaminated_bytes = decontaminated.serialized()
    operations.append(
        _operation(
            "decontaminate_transparent_edges",
            SCRIPT_NAME,
            SCRIPT_VERSION,
            masked_bytes,
            decontaminated_bytes,
            algorithm=(
                "multi-source Manhattan BFS from row-major opaque seeds; fixed "
                "left/right/up/down neighbors; copy nearest opaque RGB while "
                "preserving exact binary alpha"
            ),
            chroma_tolerance=CHROMA_TOLERANCE,
            source_authorized_magenta_pixels_reencoded=magenta_disambiguated,
            source_magenta_reencoding=(
                "preserve red/blue and set green to chroma_tolerance+1; alpha unchanged"
            ),
            transparent_pixel_count=transparent_filled,
        )
    )
    intermediates.append(("04-decontaminated.rgba8", decontaminated_bytes))

    final_png = reference_tool.encode_png(
        reference_tool.RGBAImage(width, height, decontaminated.pixels)
    )
    operations.append(
        _operation(
            "encode_srgb_png",
            "scripts/prepare_style_proof_references.py:encode_png",
            SCRIPT_VERSION,
            decontaminated_bytes,
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
        "canonical masked style-proof output",
        expected_dimensions=(width, height),
    )
    expected_alpha = bytes(
        classic.pixels[source_index * 4 + 3] for source_index in nearest_indices
    )
    if decoded_final.pixels != decontaminated.pixels:
        raise ProcessingError("canonical PNG read-back differs from the RGBA8 state")
    if decoded_final.pixels[3::4] != expected_alpha:
        raise ProcessingError("canonical PNG read-back lost the authoritative mask")
    for index in range(width * height):
        offset = index * 4
        if _is_near_chroma(*decoded_final.pixels[offset : offset + 3]):
            raise ProcessingError("canonical PNG read-back retained near-key chroma")
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


def _load_verified_handoff_reference(
    root: Path,
    handoff: Path,
    job: dict[str, Any],
) -> reference_tool.RGBAImage:
    root = root.resolve()
    handoff = handoff.resolve()
    try:
        context = reference_tool._load_selection(
            root, root / PurePosixPath(DEFAULT_SELECTION)
        )
        reference_tool.verify_generation_inputs(context, handoff)
    except (OSError, reference_tool.ValidationError) as exc:
        raise ProcessingError(f"generation-input handoff verification failed: {exc}") from exc

    manifest_path = handoff / reference_tool.GENERATION_INPUT_MANIFEST
    try:
        manifest_bytes = manifest_path.read_bytes()
        manifest = json.loads(manifest_bytes)
    except (OSError, json.JSONDecodeError) as exc:
        raise ProcessingError(f"cannot read verified handoff manifest: {exc}") from exc
    if manifest_bytes != _canonical_json(manifest):
        raise ProcessingError("generation-input handoff manifest is not canonical")
    input_value = job.get("input")
    if not isinstance(input_value, dict):
        raise ProcessingError("immutable job has no input lock")
    expected_entry = {
        "classic_payload_sha256": input_value.get("classic_payload_sha256"),
        "decoded_png_path": input_value.get("path"),
        "decoded_png_sha256": input_value.get("decoded_png_sha256"),
        "family": job.get("family"),
        "key": job.get("resource_key"),
        "label": job.get("label"),
    }
    matches = []
    for entry in manifest.get("entries", []):
        if not isinstance(entry, dict):
            continue
        projection = {name: entry.get(name) for name in expected_entry}
        if projection == expected_entry:
            matches.append(entry)
    if len(matches) != 1:
        raise ProcessingError(
            "verified handoff does not contain exactly one entry matching the immutable job"
        )
    relative = input_value.get("path")
    if (
        not isinstance(relative, str)
        or not relative.endswith(".png")
        or "\\" in relative
        or PurePosixPath(relative).is_absolute()
        or any(part in ("", ".", "..") for part in PurePosixPath(relative).parts)
    ):
        raise ProcessingError("immutable handoff input path is unsafe")
    reference_path = (handoff / PurePosixPath(relative)).resolve()
    try:
        reference_path.relative_to(handoff)
    except ValueError as exc:
        raise ProcessingError("immutable handoff input path escapes the handoff") from exc
    try:
        data = reference_path.read_bytes()
    except OSError as exc:
        raise ProcessingError(f"cannot read verified Classic reference: {exc}") from exc
    expected_hash = input_value.get("decoded_png_sha256")
    if not isinstance(expected_hash, str) or _sha256(data) != expected_hash:
        raise ProcessingError("Classic handoff reference SHA-256 differs from its job lock")
    try:
        classic = reference_tool.decode_png(data, str(reference_path))
    except reference_tool.ValidationError as exc:
        raise ProcessingError(f"cannot decode verified Classic reference: {exc}") from exc
    _validate_classic_mask(classic)
    return classic


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
    handoff: Path,
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
    if job["alpha"]["input_alpha"] != "binary_mask":
        raise ProcessingError("this processor refuses opaque jobs")
    classic = _load_verified_handoff_reference(root, handoff, job)
    raw_relative = f"{RAW_ROOT}/{job_id}/attempt-{regeneration_ordinal}.png"
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

    processed = process_masked_png(raw_png, job, classic)
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
        "raw_output": {"path": raw_relative, "sha256": actual_raw_hash},
        "regeneration_ordinal": regeneration_ordinal,
    }

    planned: list[tuple[Path, bytes]] = []
    if write_intermediates:
        try:
            intermediate_root = output_validator._under(
                root,
                f"{POSTPROCESS_ROOT}/{job_id}/attempt-{regeneration_ordinal}",
                "post-process intermediate directory",
            )
        except output_validator.ValidationError as exc:
            raise ProcessingError(str(exc)) from exc
        artifact_records = []
        for filename, data in processed.intermediate_states:
            path = intermediate_root / filename
            planned.append((path, data))
            artifact_records.append(
                {"path": path.relative_to(root).as_posix(), "sha256": _sha256(data)}
            )
        fragment["intermediate_artifacts"] = artifact_records
        planned.append(
            (intermediate_root / "operation-fragment.json", _canonical_json(fragment))
        )
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
    parser.add_argument(
        "--handoff",
        type=Path,
        required=True,
        help="fresh generation-input directory to reverify before mask use",
    )
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
        help="lock hashed RGBA8 states and the operation fragment",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        fragment = process_job(
            args.root,
            args.jobs,
            args.handoff,
            args.job,
            args.attempt,
            args.raw_sha256,
            write_intermediates=args.write_intermediates,
        )
    except ProcessingError as exc:
        print(f"masked style-proof post-processing failed: {exc}", file=sys.stderr)
        return 1
    sys.stdout.buffer.write(_canonical_json(fragment))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
