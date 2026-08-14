#!/usr/bin/env python3
"""Validate style-proof generation receipts and build review-only contact sheets.

The generation job set is immutable.  Generated files are described by a separate,
canonical receipt manifest.  Missing receipts and outputs remain pending; an invalid
present receipt or an unmanifested output fails closed.  This tool never changes a
job or declares the style proof approved.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
import prepare_style_proof_references as reference_tool  # noqa: E402


DEFAULT_JOBS = "assets/remastered/style-proof/generation/jobs.json"
DEFAULT_RECEIPTS = (
    "assets/remastered/style-proof/generation/generation-receipts.json"
)
DEFAULT_AMENDMENTS = (
    "assets/remastered/style-proof/generation/prompt-amendments.json"
)
DEFAULT_TOPOLOGY_AMENDMENTS = (
    "assets/remastered/style-proof/generation/masked-topology-amendments.json"
)
DEFAULT_OPERATIONAL_LOG = (
    "assets/remastered/style-proof/generation/operational-call-log.json"
)
PROMPT_AMENDMENT_SET_SHA256 = (
    "0307871ebfaaeea6a9c41e586a644eb101a170e2e25d2fd9d093dcf66202568f"
)
MASKED_TOPOLOGY_AMENDMENT_SET_SHA256 = (
    "e01d331d658a1086131ecd56ba52f5ebd40a0a9437a93435e518dc889e9128c6"
)
AMENDED_JOB_IDS = (
    "01_ui_material_ppat_128",
    "03_ui_material_ppat_130",
    "04_ui_material_ppat_131",
    "20_world_dungeon_cicn_m167",
)
TOPOLOGY_AMENDED_JOB_IDS = (
    "09_tactical_actor_cicn_9000",
    "10_tactical_actor_cicn_9007",
    "12_tactical_actor_cicn_9104",
    "18_world_dungeon_cicn_m189",
    "19_world_dungeon_cicn_m83",
)
RECEIPT_KIND = "style-proof-generation-receipts"
CITY_BLOCKER = "authorized Mac 7.1.2 City references are not yet available"
MAX_UI_SEAM_MAE = 0.0
CHROMA_TOLERANCE = 5
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
JOB_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
RFC3339_RE = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"
    r"(?:\.[0-9]+)?(?:Z|[+-][0-9]{2}:[0-9]{2})$"
)


class ValidationError(ValueError):
    pass


@dataclass(frozen=True)
class ImageFacts:
    image: reference_tool.RGBAImage
    pixel_format: str
    has_srgb_chunk: bool
    alpha_coverage: float
    transparent_corner_count: int
    left_right_mae: float
    top_bottom_mae: float


@dataclass(frozen=True)
class ValidatedOutput:
    job: dict[str, Any]
    receipt: dict[str, Any]
    classic: reference_tool.RGBAImage
    generated: reference_tool.RGBAImage
    facts: ImageFacts


@dataclass(frozen=True)
class ValidationResult:
    total_jobs: int
    valid_outputs: tuple[ValidatedOutput, ...]
    pending_job_ids: tuple[str, ...]
    rejected_job_ids: tuple[str, ...]
    refused_city_count: int
    complete_style_proof: bool = False
    approval_eligible: bool = False


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _read_json(path: Path, where: str, canonical: bool = True) -> tuple[dict[str, Any], bytes]:
    try:
        data = path.read_bytes()
        value = json.loads(data)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read {where} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be a JSON object")
    if canonical and data != _canonical_json(value):
        raise ValidationError(f"{where} must use canonical sorted JSON")
    return value, data


def _exact_keys(value: dict[str, Any], expected: set[str], where: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ValidationError(f"{where} fields differ; missing={missing}, extra={extra}")


def _require_sha256(value: Any, where: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise ValidationError(f"{where} must be a lowercase SHA-256")
    return value


def _require_nonempty_string(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(f"{where} must be a non-empty string")
    return value


def _safe_relative(value: Any, where: str, suffix: str | None = None) -> str:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValidationError(f"{where} must be a safe relative POSIX path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValidationError(f"{where} must be a safe relative POSIX path")
    if suffix is not None and not value.endswith(suffix):
        raise ValidationError(f"{where} must end in {suffix}")
    return value


def _under(root: Path, relative: str, where: str) -> Path:
    path = (root / PurePosixPath(relative)).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as exc:
        raise ValidationError(f"{where} escapes its root") from exc
    return path


def _resource_identity(key: Any, where: str) -> tuple[str, str, int]:
    if not isinstance(key, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(key, {"pack", "type", "id"}, where)
    pack = _require_nonempty_string(key["pack"], f"{where}.pack")
    resource_type = key["type"]
    resource_id = key["id"]
    if resource_type not in ("PICT", "cicn", "ppat") or not isinstance(resource_id, int):
        raise ValidationError(f"{where} is not a valid ResourceKey")
    return (pack, resource_type, resource_id)


def _parse_rfc3339(value: Any, where: str) -> datetime:
    if not isinstance(value, str) or RFC3339_RE.fullmatch(value) is None:
        raise ValidationError(f"{where} must be an RFC 3339 timestamp")
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValidationError(f"{where} must be an RFC 3339 timestamp") from exc


def _load_locked_json(root: Path, lock: Any, where: str) -> tuple[dict[str, Any], bytes]:
    if not isinstance(lock, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(lock, {"path", "sha256"}, where)
    relative = _safe_relative(lock["path"], f"{where}.path", ".json")
    expected_hash = _require_sha256(lock["sha256"], f"{where}.sha256")
    value, data = _read_json(_under(root, relative, where), where)
    if _sha256_bytes(data) != expected_hash:
        raise ValidationError(f"{where} SHA-256 does not match its locked file")
    return value, data


def _load_job_set(root: Path, jobs_path: Path) -> tuple[dict[str, Any], bytes, dict[str, Any]]:
    jobs, jobs_bytes = _read_json(jobs_path, "generation jobs")
    required = {
        "approval_eligible", "authorization_blocker", "complete_style_proof",
        "generation_state", "input_handoff", "job_set_kind", "jobs",
        "output_receipt_contract", "refused_references", "schema_version",
        "shared_style", "source_reference_manifest", "source_selection",
    }
    _exact_keys(jobs, required, "generation jobs")
    if jobs["schema_version"] != 1 or jobs["job_set_kind"] != "style-proof-generation-jobs":
        raise ValidationError("generation jobs identity is invalid")
    if jobs["complete_style_proof"] is not False or jobs["approval_eligible"] is not False:
        raise ValidationError("generation jobs must remain incomplete and approval-ineligible")
    if jobs["authorization_blocker"] != CITY_BLOCKER:
        raise ValidationError("generation jobs City authorization blocker is missing")
    receipt_contract = jobs["output_receipt_contract"]
    if receipt_contract != {
        "jobs_are_immutable": True,
        "path": DEFAULT_RECEIPTS,
    }:
        raise ValidationError("generation jobs output receipt contract is invalid")

    style_spec, style_bytes = _load_locked_json(root, jobs["shared_style"], "shared style")
    _load_locked_json(root, jobs["source_selection"], "source selection")
    _load_locked_json(root, jobs["source_reference_manifest"], "source reference manifest")

    job_values = jobs["jobs"]
    refusals = jobs["refused_references"]
    if not isinstance(job_values, list) or len(job_values) != 21:
        raise ValidationError("generation jobs must contain exactly 21 eligible jobs")
    if not isinstance(refusals, list) or len(refusals) != 3:
        raise ValidationError("generation jobs must retain exactly three City refusals")
    job_ids: set[str] = set()
    output_paths: set[str] = set()
    input_paths: set[str] = set()
    keys: set[tuple[str, str, int]] = set()
    for index, job in enumerate(job_values):
        where = f"generation jobs.jobs[{index}]"
        if not isinstance(job, dict):
            raise ValidationError(f"{where} must be an object")
        job_id = job.get("job_id")
        if not isinstance(job_id, str) or JOB_ID_RE.fullmatch(job_id) is None:
            raise ValidationError(f"{where}.job_id is invalid")
        if job_id in job_ids:
            raise ValidationError("generation job IDs must be unique")
        job_ids.add(job_id)
        if job.get("order") != index + 1 or job.get("status") != "pending" or job.get("model_provenance") is not None:
            raise ValidationError("generation jobs must remain ordered, pending, and immutable")
        identity = _resource_identity(job.get("resource_key"), f"{where}.resource_key")
        if identity in keys:
            raise ValidationError("generation job ResourceKeys must be unique")
        keys.add(identity)
        input_value = job.get("input")
        if not isinstance(input_value, dict):
            raise ValidationError(f"{where}.input must be an object")
        input_path = _safe_relative(input_value.get("path"), f"{where}.input.path", ".png")
        _require_sha256(input_value.get("decoded_png_sha256"), f"{where}.input.decoded_png_sha256")
        _require_sha256(input_value.get("classic_payload_sha256"), f"{where}.input.classic_payload_sha256")
        if input_path in input_paths:
            raise ValidationError("generation input paths must be unique")
        input_paths.add(input_path)
        output_path = _safe_relative(job.get("expected_output_path"), f"{where}.expected_output_path", ".png")
        if output_path in output_paths:
            raise ValidationError("generation output paths must be unique")
        output_paths.add(output_path)
        if not output_path.startswith("assets/remastered/style-proof/generation/outputs/"):
            raise ValidationError(f"{where}.expected_output_path is outside the output directory")
        target = job.get("target_master")
        if not isinstance(target, dict) or target.get("format") != "PNG" or target.get("color_space") != "sRGB":
            raise ValidationError(f"{where}.target_master must request sRGB PNG")
        if not isinstance(target.get("width"), int) or not isinstance(target.get("height"), int):
            raise ValidationError(f"{where}.target_master dimensions are invalid")
        alpha = job.get("alpha")
        if not isinstance(alpha, dict) or alpha.get("input_alpha") not in ("fully_opaque", "binary_mask"):
            raise ValidationError(f"{where}.alpha is invalid")
        if alpha["input_alpha"] == "fully_opaque":
            if alpha.get("mode") != "opaque" or alpha.get("chroma_key") is not None:
                raise ValidationError(f"{where}.alpha opaque policy is inconsistent")
        elif alpha.get("mode") != "chroma_key_then_original_mask" or alpha.get("chroma_key") != "#FF00FF":
            raise ValidationError(f"{where}.alpha mask policy is inconsistent")
    for index, refusal in enumerate(refusals):
        if not isinstance(refusal, dict) or refusal.get("status") != "refused":
            raise ValidationError(f"generation jobs.refused_references[{index}] is invalid")
        identity = _resource_identity(refusal.get("resource_key"), f"refusal[{index}].resource_key")
        if identity[0] != "Scenarios/City of Bywater/Scenario":
            raise ValidationError("all refused references must be City of Bywater")
    return jobs, jobs_bytes, style_spec


def assemble_prompt(style_spec: dict[str, Any], job: dict[str, Any]) -> str:
    assembly = style_spec.get("deterministic_prompt_assembly")
    if not isinstance(assembly, dict):
        raise ValidationError("style spec prompt assembly is missing")
    order = assembly.get("component_order")
    expected_order = [
        "style_prompt", "subject", "composition", "background",
        "semantic_preservation", "policies", "global_negative_prompt", "negative",
    ]
    if order != expected_order or assembly.get("separator") != ". ":
        raise ValidationError("style spec prompt assembly contract is unsupported")
    components = job.get("prompt_components")
    policies = job.get("policies")
    semantic = job.get("semantic_preservation")
    negatives = style_spec.get("global_negative_prompt")
    if not isinstance(components, dict) or not isinstance(policies, dict):
        raise ValidationError(f"job {job.get('job_id')} prompt components are invalid")
    if not isinstance(semantic, list) or not semantic or not all(isinstance(v, str) and v for v in semantic):
        raise ValidationError(f"job {job.get('job_id')} semantic preservation is invalid")
    if not isinstance(negatives, list) or not negatives or not all(isinstance(v, str) and v for v in negatives):
        raise ValidationError("style spec global negative prompt is invalid")
    values = [
        _require_nonempty_string(style_spec.get("style_prompt"), "style_spec.style_prompt"),
        _require_nonempty_string(components.get("subject"), "prompt.subject"),
        _require_nonempty_string(components.get("composition"), "prompt.composition"),
        _require_nonempty_string(components.get("background"), "prompt.background"),
        "; ".join(semantic),
        "; ".join(
            _require_nonempty_string(policies.get(name), f"policies.{name}")
            for name in ("seams", "text", "baked_lighting")
        ),
        "; ".join(negatives),
        _require_nonempty_string(components.get("negative"), "prompt.negative"),
    ]
    return ". ".join(values)


def effective_prompt(
    style_spec: dict[str, Any],
    job: dict[str, Any],
    regeneration_ordinal: int,
    retry_prompt_supplement: Any,
    amendment: dict[str, Any] | None = None,
) -> str:
    """Resolve the exact prompt actually sent while preserving immutable intent."""
    base = assemble_prompt(style_spec, job)
    if amendment is not None:
        base += ". " + _require_nonempty_string(
            amendment.get("text"), f"{job.get('job_id')} prompt amendment text"
        )
    if regeneration_ordinal == 0:
        if retry_prompt_supplement is not None:
            raise ValidationError("ordinal-0 generation must have a null retry prompt supplement")
        return base
    if regeneration_ordinal == 1:
        supplement = _require_nonempty_string(
            retry_prompt_supplement, "ordinal-1 retry_prompt_supplement"
        )
        return base + ". " + supplement
    raise ValidationError("regeneration ordinal must be 0 or 1")


def _load_prompt_amendments(
    root: Path,
    jobs: dict[str, Any],
    jobs_bytes: bytes,
    amendments_path: Path | None = None,
) -> tuple[dict[str, dict[str, Any]], str | None]:
    """Load the immutable pre-call correction register without changing job intent."""
    path = amendments_path or (root / DEFAULT_AMENDMENTS)
    if not path.exists():
        raise ValidationError(
            f"required prompt amendment register is missing: {path}"
        )
    register, register_bytes = _read_json(path, "prompt amendment register")
    register_hash = _sha256_bytes(register_bytes)
    if register_hash != PROMPT_AMENDMENT_SET_SHA256:
        raise ValidationError("prompt amendment register SHA-256 is not the reviewed lock")
    _exact_keys(
        register,
        {
            "amendment_set_kind", "entries", "jobs_sha256", "schema_version",
            "style_spec_sha256",
        },
        "prompt amendment register",
    )
    if (
        register["schema_version"] != 1
        or register["amendment_set_kind"] != "style-proof-prompt-amendments"
        or register["jobs_sha256"] != _sha256_bytes(jobs_bytes)
        or register["style_spec_sha256"] != jobs["shared_style"]["sha256"]
    ):
        raise ValidationError("prompt amendment register locks do not match the job set")
    entries = register["entries"]
    if not isinstance(entries, list) or len(entries) != len(AMENDED_JOB_IDS):
        raise ValidationError("prompt amendment register must contain exactly four entries")
    job_by_id = {job["job_id"]: job for job in jobs["jobs"]}
    amendments: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        where = f"prompt amendment register.entries[{index}]"
        if not isinstance(entry, dict):
            raise ValidationError(f"{where} must be an object")
        _exact_keys(
            entry,
            {
                "amendment_sha256", "job_id", "reason", "recorded_at",
                "reviewer", "text",
            },
            where,
        )
        job_id = entry["job_id"]
        if job_id != AMENDED_JOB_IDS[index] or job_id not in job_by_id:
            raise ValidationError("prompt amendments may cover only uncalled jobs 01, 03, 04, and 20")
        text = _require_nonempty_string(entry["text"], f"{where}.text")
        if text != text.strip():
            raise ValidationError(f"{where}.text has leading or trailing whitespace")
        if entry["amendment_sha256"] != _sha256_bytes(text.encode("utf-8")):
            raise ValidationError(f"{where}.amendment_sha256 does not match its exact text")
        _require_nonempty_string(entry["reason"], f"{where}.reason")
        if entry["reviewer"] != "Codex preflight audit":
            raise ValidationError(f"{where}.reviewer is not the reviewed identity")
        _parse_rfc3339(entry["recorded_at"], f"{where}.recorded_at")
        amendments[job_id] = entry
    return amendments, register_hash


def _load_masked_topology_amendments(
    root: Path,
    jobs: dict[str, Any],
    jobs_bytes: bytes,
    amendments_path: Path | None = None,
) -> tuple[dict[str, dict[str, Any]], str]:
    """Load pre-call corrections derived from measured Classic alpha topology."""
    path = amendments_path or (root / DEFAULT_TOPOLOGY_AMENDMENTS)
    if not path.exists():
        raise ValidationError(
            f"required masked-topology amendment register is missing: {path}"
        )
    register, register_bytes = _read_json(
        path, "masked-topology amendment register"
    )
    register_hash = _sha256_bytes(register_bytes)
    if register_hash != MASKED_TOPOLOGY_AMENDMENT_SET_SHA256:
        raise ValidationError(
            "masked-topology amendment register SHA-256 is not the reviewed lock"
        )
    _exact_keys(
        register,
        {
            "amendment_set_kind", "entries", "jobs_sha256", "schema",
            "schema_version", "style_spec_sha256",
        },
        "masked-topology amendment register",
    )
    if (
        register["schema_version"] != 1
        or register["amendment_set_kind"]
        != "style-proof-masked-topology-amendments"
        or register["jobs_sha256"] != _sha256_bytes(jobs_bytes)
        or register["style_spec_sha256"] != jobs["shared_style"]["sha256"]
    ):
        raise ValidationError(
            "masked-topology amendment register locks do not match the job set"
        )
    schema_lock = register["schema"]
    schema_path = (
        "assets/remastered/style-proof/generation/"
        "masked-topology-amendments.schema.json"
    )
    if not isinstance(schema_lock, dict) or schema_lock.get("path") != schema_path:
        raise ValidationError("masked-topology amendment schema path is invalid")
    schema, _ = _load_locked_json(
        root, schema_lock, "masked-topology amendment schema"
    )
    if (
        schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema"
        or schema.get("$id")
        != "https://realmz-remastered.invalid/schema/"
        "masked-topology-amendments.schema.json"
    ):
        raise ValidationError("masked-topology amendment schema identity is invalid")

    entries = register["entries"]
    if not isinstance(entries, list) or len(entries) != len(TOPOLOGY_AMENDED_JOB_IDS):
        raise ValidationError(
            "masked-topology amendment register must contain exactly five entries"
        )
    job_by_id = {job["job_id"]: job for job in jobs["jobs"]}
    amendments: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        where = f"masked-topology amendment register.entries[{index}]"
        if not isinstance(entry, dict):
            raise ValidationError(f"{where} must be an object")
        _exact_keys(
            entry,
            {
                "amendment_sha256", "classic_alpha_evidence", "job_id",
                "reason", "recorded_at", "reviewer", "text",
            },
            where,
        )
        job_id = entry["job_id"]
        if job_id != TOPOLOGY_AMENDED_JOB_IDS[index] or job_id not in job_by_id:
            raise ValidationError(
                "masked-topology amendments may cover only untouched jobs "
                "09, 10, 12, 18, and 19"
            )
        job = job_by_id[job_id]
        if job.get("alpha", {}).get("input_alpha") != "binary_mask":
            raise ValidationError(f"{where} does not identify a binary-mask job")
        text = _require_nonempty_string(entry["text"], f"{where}.text")
        if text != text.strip():
            raise ValidationError(f"{where}.text has leading or trailing whitespace")
        if entry["amendment_sha256"] != _sha256_bytes(text.encode("utf-8")):
            raise ValidationError(f"{where}.amendment_sha256 does not match its exact text")
        _require_nonempty_string(entry["reason"], f"{where}.reason")
        if entry["reviewer"] != "Codex Classic-alpha audit":
            raise ValidationError(f"{where}.reviewer is not the reviewed identity")
        _parse_rfc3339(entry["recorded_at"], f"{where}.recorded_at")

        evidence = entry["classic_alpha_evidence"]
        evidence_where = f"{where}.classic_alpha_evidence"
        if not isinstance(evidence, dict):
            raise ValidationError(f"{evidence_where} must be an object")
        _exact_keys(
            evidence,
            {
                "decoded_png_sha256", "dimensions",
                "enclosed_transparent_component_sizes_4_connected",
                "foreground_bbox", "foreground_component_sizes_4_connected",
                "foreground_pixels",
            },
            evidence_where,
        )
        if (
            _require_sha256(
                evidence["decoded_png_sha256"],
                f"{evidence_where}.decoded_png_sha256",
            )
            != job["input"]["decoded_png_sha256"]
        ):
            raise ValidationError(f"{evidence_where} is bound to the wrong Classic PNG")
        dimensions = evidence["dimensions"]
        if not isinstance(dimensions, dict):
            raise ValidationError(f"{evidence_where}.dimensions must be an object")
        _exact_keys(dimensions, {"height", "width"}, f"{evidence_where}.dimensions")
        if any(type(dimensions[name]) is not int or dimensions[name] <= 0 for name in dimensions):
            raise ValidationError(f"{evidence_where}.dimensions are invalid")
        bbox = evidence["foreground_bbox"]
        if not isinstance(bbox, dict):
            raise ValidationError(f"{evidence_where}.foreground_bbox must be an object")
        _exact_keys(
            bbox, {"bottom", "left", "right", "top"},
            f"{evidence_where}.foreground_bbox",
        )
        if any(type(value) is not int or value < 0 for value in bbox.values()):
            raise ValidationError(f"{evidence_where}.foreground_bbox is invalid")
        if not (
            bbox["left"] <= bbox["right"] < dimensions["width"]
            and bbox["top"] <= bbox["bottom"] < dimensions["height"]
        ):
            raise ValidationError(f"{evidence_where}.foreground_bbox exceeds dimensions")
        foreground_pixels = evidence["foreground_pixels"]
        if type(foreground_pixels) is not int or foreground_pixels <= 0:
            raise ValidationError(f"{evidence_where}.foreground_pixels is invalid")
        for name, require_nonempty in (
            ("foreground_component_sizes_4_connected", True),
            ("enclosed_transparent_component_sizes_4_connected", False),
        ):
            sizes = evidence[name]
            if (
                not isinstance(sizes, list)
                or (require_nonempty and not sizes)
                or any(type(value) is not int or value <= 0 for value in sizes)
                or sizes != sorted(sizes, reverse=True)
            ):
                raise ValidationError(f"{evidence_where}.{name} is invalid")
        if sum(evidence["foreground_component_sizes_4_connected"]) != foreground_pixels:
            raise ValidationError(
                f"{evidence_where} foreground component sizes do not sum to its pixel count"
            )
        amendments[job_id] = entry
    return amendments, register_hash


def _load_all_prompt_amendments(
    root: Path,
    jobs: dict[str, Any],
    jobs_bytes: bytes,
    amendments_path: Path | None = None,
    topology_amendments_path: Path | None = None,
) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    legacy, legacy_hash = _load_prompt_amendments(
        root, jobs, jobs_bytes, amendments_path
    )
    topology, topology_hash = _load_masked_topology_amendments(
        root, jobs, jobs_bytes, topology_amendments_path
    )
    overlap = set(legacy).intersection(topology)
    if overlap:
        raise ValidationError(
            f"prompt amendment registers overlap on jobs {sorted(overlap)}"
        )
    amendments = {**legacy, **topology}
    set_hashes = {
        **{job_id: legacy_hash for job_id in legacy},
        **{job_id: topology_hash for job_id in topology},
    }
    return amendments, set_hashes


def _component_sets(indices: set[int], width: int, height: int) -> list[set[int]]:
    remaining = set(indices)
    components: list[set[int]] = []
    while remaining:
        first = remaining.pop()
        component = {first}
        pending = [first]
        while pending:
            index = pending.pop()
            x = index % width
            y = index // width
            for neighbor_x, neighbor_y in (
                (x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)
            ):
                if not (0 <= neighbor_x < width and 0 <= neighbor_y < height):
                    continue
                neighbor = neighbor_y * width + neighbor_x
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    component.add(neighbor)
                    pending.append(neighbor)
        components.append(component)
    return components


def _measure_classic_alpha_topology(
    classic: reference_tool.RGBAImage, decoded_png_sha256: str
) -> dict[str, Any]:
    alpha = classic.pixels[3::4]
    if set(alpha) != {0, 255}:
        raise ValidationError(
            "Classic topology evidence requires a nonempty binary foreground/background mask"
        )
    foreground = {index for index, value in enumerate(alpha) if value == 255}
    transparent = set(range(classic.width * classic.height)) - foreground
    foreground_components = _component_sets(
        foreground, classic.width, classic.height
    )
    transparent_components = _component_sets(
        transparent, classic.width, classic.height
    )

    def touches_border(component: set[int]) -> bool:
        return any(
            index % classic.width in (0, classic.width - 1)
            or index // classic.width in (0, classic.height - 1)
            for index in component
        )

    x_values = [index % classic.width for index in foreground]
    y_values = [index // classic.width for index in foreground]
    return {
        "decoded_png_sha256": decoded_png_sha256,
        "dimensions": {"height": classic.height, "width": classic.width},
        "enclosed_transparent_component_sizes_4_connected": sorted(
            (
                len(component)
                for component in transparent_components
                if not touches_border(component)
            ),
            reverse=True,
        ),
        "foreground_bbox": {
            "bottom": max(y_values),
            "left": min(x_values),
            "right": max(x_values),
            "top": min(y_values),
        },
        "foreground_component_sizes_4_connected": sorted(
            (len(component) for component in foreground_components),
            reverse=True,
        ),
        "foreground_pixels": len(foreground),
    }


def _verify_topology_amendment_evidence(
    input_dir: Path,
    job: dict[str, Any],
    amendment: dict[str, Any],
) -> None:
    job_id = job["job_id"]
    relative = _safe_relative(job["input"]["path"], f"{job_id} input", ".png")
    path = _under(input_dir.resolve(), relative, f"{job_id} topology input")
    data = path.read_bytes()
    digest = _sha256_bytes(data)
    if digest != job["input"]["decoded_png_sha256"]:
        raise ValidationError(f"{job_id} topology input PNG hash is stale")
    classic = reference_tool.decode_png(data, str(path))
    measured = _measure_classic_alpha_topology(classic, digest)
    if amendment["classic_alpha_evidence"] != measured:
        raise ValidationError(
            f"{job_id} masked-topology amendment evidence mismatches the Classic alpha"
        )


def _prompt_amendment_provenance(
    amendment: dict[str, Any],
    amendment_set_sha256: str,
    jobs: dict[str, Any],
    jobs_bytes: bytes,
) -> dict[str, Any]:
    return {
        "amendment_set_sha256": amendment_set_sha256,
        "amendment_sha256": amendment["amendment_sha256"],
        "jobs_sha256": _sha256_bytes(jobs_bytes),
        "recorded_at": amendment["recorded_at"],
        "style_spec_sha256": jobs["shared_style"]["sha256"],
    }


def _assert_amendment_is_pre_call(
    root: Path,
    job: dict[str, Any],
    receipts_path: Path,
    operational_log_path: Path,
) -> None:
    """Fail closed if any evidence says this amended prompt was already consumed."""
    job_id = job["job_id"]
    raw_dir = root / "assets/remastered/style-proof/generation/raw" / job_id
    if any(raw_dir.glob("attempt-*.png")):
        raise ValidationError(f"{job_id} prompt amendment cannot be used after a raw model output")
    if _under(root, job["expected_output_path"], f"{job_id} output").exists():
        raise ValidationError(f"{job_id} prompt amendment cannot be used after a final output")
    if receipts_path.exists():
        receipts, _ = _read_json(receipts_path, "generation receipts")
        entries = receipts.get("entries")
        if not isinstance(entries, list):
            raise ValidationError("generation receipts.entries must be an array")
        if any(isinstance(entry, dict) and entry.get("job_id") == job_id for entry in entries):
            raise ValidationError(f"{job_id} prompt amendment cannot be used after a receipt")
    if operational_log_path.exists():
        operational, _ = _read_json(operational_log_path, "operational call log")
        entries = operational.get("entries")
        if not isinstance(entries, list):
            raise ValidationError("operational call log entries must be an array")
        for entry in entries:
            if not isinstance(entry, dict) or entry.get("intended_job_id") != job_id:
                continue
            explicitly_nonconsuming = (
                entry.get("consumes_regeneration_ordinal") is False
                and entry.get("art_output_created") is False
                and entry.get("operational_status") == "excluded-pipeline-error"
                and isinstance(entry.get("submitted_payload"), dict)
                and entry["submitted_payload"].get("valid_generation_prompt") is False
            )
            if not explicitly_nonconsuming:
                raise ValidationError(
                    f"{job_id} prompt amendment cannot be used after an operational consuming call"
                )


def _assert_amended_retry_is_pre_call(
    root: Path,
    jobs_path: Path,
    input_dir: Path,
    receipts_path: Path,
    amendments_path: Path | None,
    topology_amendments_path: Path | None,
    operational_log_path: Path,
    job_id: str,
) -> None:
    """Allow one retry only after the amended ordinal-0 rejection is canonical."""
    result = validate(
        root,
        jobs_path,
        receipts_path,
        input_dir,
        amendments_path,
        topology_amendments_path,
    )
    if job_id not in result.rejected_job_ids:
        raise ValidationError(
            f"{job_id} amended retry requires one canonical rejected ordinal-0 receipt"
        )
    if operational_log_path.exists():
        operational, _ = _read_json(operational_log_path, "operational call log")
        entries = operational.get("entries")
        if not isinstance(entries, list):
            raise ValidationError("operational call log entries must be an array")
        for entry in entries:
            if not isinstance(entry, dict) or entry.get("intended_job_id") != job_id:
                continue
            explicitly_nonconsuming = (
                entry.get("consumes_regeneration_ordinal") is False
                and entry.get("art_output_created") is False
                and entry.get("operational_status") == "excluded-pipeline-error"
                and isinstance(entry.get("submitted_payload"), dict)
                and entry["submitted_payload"].get("valid_generation_prompt") is False
            )
            if not explicitly_nonconsuming:
                raise ValidationError(
                    f"{job_id} amended retry has an unreceipted operational consuming call"
                )


def _png_facts(data: bytes, where: str) -> ImageFacts:
    image = reference_tool.decode_png(data, where)
    if len(data) < 33 or data[12:16] != b"IHDR":
        raise ValidationError(f"{where}: malformed PNG header")
    bit_depth = data[24]
    color_type = data[25]
    if bit_depth != 8 or color_type not in (2, 6):
        raise ValidationError(f"{where}: only RGB8 or RGBA8 PNG is allowed")
    offset = 8
    has_srgb = False
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        if chunk_type == b"sRGB":
            if len(payload) != 1 or payload[0] > 3:
                raise ValidationError(f"{where}: malformed sRGB chunk")
            has_srgb = True
        offset += 12 + size
        if chunk_type == b"IEND":
            break
    alphas = image.pixels[3::4]
    coverage = sum(1 for alpha in alphas if alpha != 0) / len(alphas)
    corners = (
        3,
        (image.width - 1) * 4 + 3,
        ((image.height - 1) * image.width) * 4 + 3,
        (image.height * image.width - 1) * 4 + 3,
    )
    transparent_corners = sum(image.pixels[offset] == 0 for offset in corners)

    def pixel_rgb(x: int, y: int) -> tuple[int, int, int]:
        start = (y * image.width + x) * 4
        return tuple(image.pixels[start:start + 3])  # type: ignore[return-value]

    lr_total = 0
    for y in range(image.height):
        left = pixel_rgb(0, y)
        right = pixel_rgb(image.width - 1, y)
        lr_total += sum(abs(a - b) for a, b in zip(left, right))
    tb_total = 0
    for x in range(image.width):
        top = pixel_rgb(x, 0)
        bottom = pixel_rgb(x, image.height - 1)
        tb_total += sum(abs(a - b) for a, b in zip(top, bottom))
    return ImageFacts(
        image=image,
        pixel_format="RGB8" if color_type == 2 else "RGBA8",
        has_srgb_chunk=has_srgb,
        alpha_coverage=coverage,
        transparent_corner_count=transparent_corners,
        left_right_mae=lr_total / (image.height * 3 * 255),
        top_bottom_mae=tb_total / (image.width * 3 * 255),
    )


def _nearest_alpha(source: reference_tool.RGBAImage, width: int, height: int) -> bytes:
    values = bytearray(width * height)
    for y in range(height):
        source_y = min(source.height - 1, y * source.height // height)
        for x in range(width):
            source_x = min(source.width - 1, x * source.width // width)
            values[y * width + x] = source.pixels[(source_y * source.width + source_x) * 4 + 3]
    return bytes(values)


def _inspect_output(job: dict[str, Any], classic: reference_tool.RGBAImage, output_path: Path) -> tuple[ImageFacts, dict[str, Any]]:
    try:
        data = output_path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read generated output {output_path}: {exc}") from exc
    facts = _png_facts(data, str(output_path))
    target = job["target_master"]
    expected_dimensions = (target["width"], target["height"])
    if (facts.image.width, facts.image.height) != expected_dimensions:
        raise ValidationError(
            f"{job['job_id']} output dimensions are {facts.image.width}x{facts.image.height}; "
            f"expected {expected_dimensions[0]}x{expected_dimensions[1]}"
        )
    if not facts.has_srgb_chunk:
        raise ValidationError(f"{job['job_id']} output lacks an explicit sRGB PNG chunk")
    alpha_policy = job["alpha"]
    source_alpha = classic.pixels[3::4]
    retained_magenta_pixels = 0
    if alpha_policy["input_alpha"] == "fully_opaque":
        if any(alpha != 255 for alpha in source_alpha):
            raise ValidationError(f"{job['job_id']} declares an opaque input but its input has alpha")
        if any(alpha != 255 for alpha in facts.image.pixels[3::4]):
            raise ValidationError(f"{job['job_id']} opaque output contains transparency")
    else:
        if set(source_alpha) - {0, 255} or 0 not in source_alpha or 255 not in source_alpha:
            raise ValidationError(f"{job['job_id']} binary input mask declaration is false")
        if facts.pixel_format != "RGBA8":
            raise ValidationError(f"{job['job_id']} masked output must be RGBA8")
        expected_alpha = _nearest_alpha(classic, facts.image.width, facts.image.height)
        actual_alpha = facts.image.pixels[3::4]
        if actual_alpha != expected_alpha:
            raise ValidationError(f"{job['job_id']} output alpha is not the authoritative upscaled original mask")
        near_chroma_pixels = 0
        retained_magenta_pixels = 0
        transparent_chroma_pixels = 0
        for index, alpha in enumerate(actual_alpha):
            red, green, blue = facts.image.pixels[index * 4:index * 4 + 3]
            near_chroma = (
                red >= 255 - CHROMA_TOLERANCE
                and green <= CHROMA_TOLERANCE
                and blue >= 255 - CHROMA_TOLERANCE
            )
            if near_chroma:
                if alpha:
                    near_chroma_pixels += 1
                else:
                    transparent_chroma_pixels += 1
            if (
                alpha
                and red >= 96
                and blue >= 96
                and red >= green + 24
                and blue >= green + 24
                and not near_chroma
            ):
                retained_magenta_pixels += 1
        if near_chroma_pixels or transparent_chroma_pixels:
            raise ValidationError(
                f"{job['job_id']} retains {near_chroma_pixels + transparent_chroma_pixels} "
                "near-#FF00FF matte pixels after mask-safe removal"
            )
        if job["order"] in (10, 16) and retained_magenta_pixels == 0:
            raise ValidationError(
                f"{job['job_id']} lost the source-significant pink/magenta content"
            )
    seams_applicable = job.get("family") == "ui_material"
    if seams_applicable and (
        facts.left_right_mae > MAX_UI_SEAM_MAE
        or facts.top_bottom_mae > MAX_UI_SEAM_MAE
    ):
        raise ValidationError(
            f"{job['job_id']} UI tile has non-identical opposing edges"
        )
    evidence = {
        "alpha": {
            "alpha_coverage": round(facts.alpha_coverage, 8),
            "authoritative_source_mask_match": (
                True if alpha_policy["input_alpha"] == "binary_mask" else None
            ),
            "near_chroma_matte_pixels": (
                0 if alpha_policy["input_alpha"] == "binary_mask" else None
            ),
            "retained_magenta_inside_mask_pixels": (
                retained_magenta_pixels if job["order"] in (10, 16) else None
            ) if alpha_policy["input_alpha"] == "binary_mask" else None,
            "transparent_corner_count": facts.transparent_corner_count,
        },
        "seams": {
            "applicable": seams_applicable,
            "left_right_mean_absolute_error": (
                round(facts.left_right_mae, 8) if seams_applicable else None
            ),
            "top_bottom_mean_absolute_error": (
                round(facts.top_bottom_mae, 8) if seams_applicable else None
            ),
        },
    }
    return facts, evidence


def _validate_model_provenance(value: Any, where: str) -> None:
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(
        value,
        {"provider", "model", "version", "prediction_id", "seed", "parameters", "started_at", "completed_at"},
        where,
    )
    for field in ("provider", "model", "version"):
        _require_nonempty_string(value[field], f"{where}.{field}")
    if value["prediction_id"] is not None:
        _require_nonempty_string(value["prediction_id"], f"{where}.prediction_id")
    if value["seed"] is not None and not isinstance(value["seed"], (int, str)):
        raise ValidationError(f"{where}.seed must be an integer, string, or null")
    if not isinstance(value["parameters"], dict) or not value["parameters"]:
        raise ValidationError(f"{where}.parameters must be a non-empty object")
    started = _parse_rfc3339(value["started_at"], f"{where}.started_at")
    completed = _parse_rfc3339(value["completed_at"], f"{where}.completed_at")
    if completed < started:
        raise ValidationError(f"{where}.completed_at precedes started_at")


def _validate_raw_output(
    root: Path,
    value: Any,
    job: dict[str, Any],
    ordinal: int,
    where: str,
) -> tuple[str, ImageFacts, str]:
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(
        value,
        {"path", "sha256", "width", "height", "embedded_color_space", "pixel_format"},
        where,
    )
    path_text = _safe_relative(value["path"], f"{where}.path", ".png")
    expected_path = (
        "assets/remastered/style-proof/generation/raw/"
        f"{job['job_id']}/attempt-{ordinal}.png"
    )
    if path_text != expected_path:
        raise ValidationError(f"{where}.path must be {expected_path!r}")
    path = _under(root, path_text, where)
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read raw generation output {path}: {exc}") from exc
    digest = _sha256_bytes(data)
    if digest != _require_sha256(value["sha256"], f"{where}.sha256"):
        raise ValidationError(f"{where} SHA-256 does not match raw output")
    facts = _png_facts(data, str(path))
    expected = {
        "path": path_text,
        "sha256": digest,
        "width": facts.image.width,
        "height": facts.image.height,
        "embedded_color_space": "sRGB" if facts.has_srgb_chunk else None,
        "pixel_format": facts.pixel_format,
    }
    if value != expected:
        raise ValidationError(f"{where} metadata does not match decoded raw PNG")
    return digest, facts, path_text


def _validate_prior_attempts(
    root: Path,
    value: Any,
    style_spec: dict[str, Any],
    job: dict[str, Any],
    current_ordinal: int,
    where: str,
    amendment: dict[str, Any] | None = None,
    amendment_provenance: dict[str, Any] | None = None,
) -> set[str]:
    if not isinstance(value, list):
        raise ValidationError(f"{where} must be an array")
    expected_count = current_ordinal
    if len(value) != expected_count:
        raise ValidationError(
            f"{where} must contain exactly {expected_count} rejected prior attempt(s)"
        )
    paths: set[str] = set()
    for index, attempt in enumerate(value):
        attempt_where = f"{where}[{index}]"
        if not isinstance(attempt, dict):
            raise ValidationError(f"{attempt_where} must be an object")
        expected_keys = {
                "model_provenance", "prompt_sha256", "raw_output", "regeneration_ordinal",
                "rejection_reason", "resolved_prompt", "retry_prompt_supplement",
                "review_status", "reviewer",
        }
        if amendment is not None:
            expected_keys.add("prompt_amendment_provenance")
        _exact_keys(attempt, expected_keys, attempt_where)
        if attempt["regeneration_ordinal"] != index:
            raise ValidationError(f"{attempt_where}.regeneration_ordinal is out of order")
        prompt = effective_prompt(
            style_spec,
            job,
            index,
            attempt["retry_prompt_supplement"],
            amendment,
        )
        if attempt["resolved_prompt"] != prompt:
            raise ValidationError(f"{attempt_where}.resolved_prompt is stale")
        if attempt["prompt_sha256"] != _sha256_bytes(prompt.encode("utf-8")):
            raise ValidationError(f"{attempt_where}.prompt_sha256 is stale")
        _validate_model_provenance(
            attempt["model_provenance"], f"{attempt_where}.model_provenance"
        )
        if amendment is not None:
            if attempt["prompt_amendment_provenance"] != amendment_provenance:
                raise ValidationError(f"{attempt_where}.prompt_amendment_provenance is stale")
            if _parse_rfc3339(
                attempt["model_provenance"]["started_at"],
                f"{attempt_where}.model_provenance.started_at",
            ) < _parse_rfc3339(amendment["recorded_at"], f"{attempt_where}.amendment.recorded_at"):
                raise ValidationError(f"{attempt_where} predates its prompt amendment")
        _, _, raw_path = _validate_raw_output(
            root, attempt["raw_output"], job, index, f"{attempt_where}.raw_output"
        )
        paths.add(raw_path)
        if attempt["review_status"] != "rejected":
            raise ValidationError(f"{attempt_where}.review_status must be rejected")
        _require_nonempty_string(attempt["reviewer"], f"{attempt_where}.reviewer")
        _require_nonempty_string(
            attempt["rejection_reason"], f"{attempt_where}.rejection_reason"
        )
    return paths


def _validate_post_processing(
    value: Any,
    *,
    masked: bool,
    ui_tile: bool,
    raw_sha256: str,
    raw_facts: ImageFacts,
    final_sha256: str,
    target_dimensions: tuple[int, int],
    where: str,
) -> None:
    if not isinstance(value, list):
        raise ValidationError(f"{where} must be an array")
    operations: set[str] = set()
    operation_items: dict[str, dict[str, Any]] = {}
    previous_hash = raw_sha256
    for index, item in enumerate(value):
        item_where = f"{where}[{index}]"
        if not isinstance(item, dict):
            raise ValidationError(f"{item_where} must be an object")
        _exact_keys(item, {"operation", "tool", "tool_version", "parameters"}, item_where)
        operation = _require_nonempty_string(item["operation"], f"{item_where}.operation")
        if operation in operations:
            raise ValidationError(f"{where} repeats operation {operation!r}")
        operations.add(operation)
        operation_items[operation] = item
        _require_nonempty_string(item["tool"], f"{item_where}.tool")
        _require_nonempty_string(item["tool_version"], f"{item_where}.tool_version")
        parameters = item["parameters"]
        if not isinstance(parameters, dict):
            raise ValidationError(f"{item_where}.parameters must be an object")
        required_parameters = {"input_sha256", "output_sha256", "output_changed"}
        if not required_parameters.issubset(parameters):
            raise ValidationError(
                f"{item_where}.parameters lacks deterministic hash-chain fields"
            )
        input_hash = _require_sha256(
            parameters["input_sha256"], f"{item_where}.parameters.input_sha256"
        )
        output_hash = _require_sha256(
            parameters["output_sha256"], f"{item_where}.parameters.output_sha256"
        )
        if input_hash != previous_hash:
            raise ValidationError(f"{item_where} breaks the post-processing hash chain")
        if not isinstance(parameters["output_changed"], bool):
            raise ValidationError(f"{item_where}.parameters.output_changed must be boolean")
        if parameters["output_changed"] != (input_hash != output_hash):
            raise ValidationError(f"{item_where}.parameters.output_changed is false evidence")
        previous_hash = output_hash
    if previous_hash != final_sha256:
        raise ValidationError(f"{where} does not terminate at the final output SHA-256")
    if masked:
        required = {
            "remove_chroma_outside_original_mask",
            "apply_original_alpha_mask",
            "decontaminate_transparent_edges",
        }
        if not required.issubset(operations):
            raise ValidationError(f"{where} lacks required mask-safe operations {sorted(required - operations)}")
    if (raw_facts.image.width, raw_facts.image.height) != target_dimensions:
        if "resize_to_target" not in operations:
            raise ValidationError(f"{where} must record deterministic resize_to_target")
    if not raw_facts.has_srgb_chunk and "encode_srgb_png" not in operations:
        raise ValidationError(f"{where} must record explicit encode_srgb_png")
    if ui_tile:
        if "make_tileable_opposite_edges" not in operations:
            raise ValidationError(
                f"{where} must record deterministic make_tileable_opposite_edges"
            )
        seam_parameters = operation_items["make_tileable_opposite_edges"]["parameters"]
        if seam_parameters["output_changed"] is not True:
            raise ValidationError(
                f"{where} seam operation must record a non-no-op output delta"
            )


def _validate_ocr(root: Path, value: Any, review_status: str, where: str) -> None:
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(value, {"performed", "method", "result", "evidence_path", "evidence_sha256"}, where)
    performed = value["performed"]
    if not isinstance(performed, bool):
        raise ValidationError(f"{where}.performed must be boolean")
    if not performed:
        if value != {
            "performed": False,
            "method": None,
            "result": "not-run",
            "evidence_path": None,
            "evidence_sha256": None,
        }:
            raise ValidationError(f"{where} not-run evidence is inconsistent")
        if review_status == "approved":
            raise ValidationError("an approved master requires independent OCR evidence")
        return
    method = _require_nonempty_string(value["method"], f"{where}.method")
    del method
    if value["result"] not in ("no-text-detected", "text-detected"):
        raise ValidationError(f"{where}.result is invalid")
    evidence_path = _safe_relative(value["evidence_path"], f"{where}.evidence_path")
    expected_hash = _require_sha256(value["evidence_sha256"], f"{where}.evidence_sha256")
    evidence_file = _under(root, evidence_path, where)
    try:
        evidence_data = evidence_file.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read OCR evidence {evidence_file}: {exc}") from exc
    if _sha256_bytes(evidence_data) != expected_hash:
        raise ValidationError(f"{where} file hash does not match")
    if review_status == "approved" and value["result"] != "no-text-detected":
        raise ValidationError("an approved master requires OCR result no-text-detected")


def _validate_baked_lighting(value: Any, review_status: str, where: str) -> None:
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be an object")
    _exact_keys(value, {"reviewed", "method", "result"}, where)
    reviewed = value["reviewed"]
    if not isinstance(reviewed, bool):
        raise ValidationError(f"{where}.reviewed must be boolean")
    if not reviewed:
        if value != {"reviewed": False, "method": None, "result": "not-reviewed"}:
            raise ValidationError(f"{where} not-reviewed evidence is inconsistent")
        if review_status == "approved":
            raise ValidationError("an approved master requires baked-lighting review")
        return
    _require_nonempty_string(value["method"], f"{where}.method")
    if value["result"] not in (
        "no-unintended-baked-lighting",
        "unintended-baked-lighting-detected",
    ):
        raise ValidationError(f"{where}.result is invalid")
    if review_status == "approved" and value["result"] != "no-unintended-baked-lighting":
        raise ValidationError("an approved master may not have unintended baked lighting")


def empty_receipt_manifest(jobs: dict[str, Any], jobs_bytes: bytes) -> dict[str, Any]:
    return {
        "approval_eligible": False,
        "authorization_blocker": CITY_BLOCKER,
        "complete_style_proof": False,
        "entries": [],
        "jobs_sha256": _sha256_bytes(jobs_bytes),
        "receipt_set_kind": RECEIPT_KIND,
        "refused_references": jobs["refused_references"],
        "schema_version": 1,
        "style_spec_sha256": jobs["shared_style"]["sha256"],
    }


def _source_role_for_job(job: dict[str, Any]) -> str:
    pack = job["resource_key"]["pack"]
    if pack == "Scenarios/Tutorial/Scenario":
        return "tutorial-repository-bundled"
    if pack.startswith("Scenarios/"):
        raise ValidationError(f"generation-eligible job unexpectedly uses scenario pack {pack!r}")
    return "core-repository-bundled"


def _verify_input_handoff(jobs: dict[str, Any], input_dir: Path) -> None:
    """Verify the exact, City-free handoff immediately before reading its PNGs."""
    manifest_path = input_dir / "generation-input-manifest.json"
    manifest, _ = _read_json(manifest_path, "generation input handoff")
    required = {
        "approval_eligible", "authorization_blocker", "complete_style_proof",
        "entries", "export_kind", "generated_art", "model_calls",
        "refused_references", "schema_version", "selection_sha256",
    }
    _exact_keys(manifest, required, "generation input handoff")
    if (
        manifest["schema_version"] != 1
        or manifest["export_kind"] != "style-proof-generation-inputs"
        or manifest["generated_art"] is not False
        or manifest["model_calls"] != 0
        or manifest["complete_style_proof"] is not False
        or manifest["approval_eligible"] is not False
        or manifest["authorization_blocker"] != CITY_BLOCKER
        or manifest["selection_sha256"] != jobs["source_selection"]["sha256"]
    ):
        raise ValidationError("generation input handoff policy or source lock is invalid")
    entries = manifest["entries"]
    if not isinstance(entries, list) or len(entries) != len(jobs["jobs"]):
        raise ValidationError("generation input handoff must contain all 21 eligible inputs")
    expected_paths = {"generation-input-manifest.json"}
    for index, (entry, job) in enumerate(zip(entries, jobs["jobs"])):
        expected = {
            "classic_payload_sha256": job["input"]["classic_payload_sha256"],
            "decoded_png_path": job["input"]["path"],
            "decoded_png_sha256": job["input"]["decoded_png_sha256"],
            "family": job["family"],
            "key": job["resource_key"],
            "label": job["label"],
            "source_role": _source_role_for_job(job),
        }
        if entry != expected:
            raise ValidationError(
                f"generation input handoff entry {index} does not match immutable job {job['job_id']}"
            )
        relative = _safe_relative(job["input"]["path"], f"handoff entry {index} path", ".png")
        path = _under(input_dir, relative, f"handoff entry {index}")
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise ValidationError(f"cannot read generation input {path}: {exc}") from exc
        if _sha256_bytes(data) != job["input"]["decoded_png_sha256"]:
            raise ValidationError(f"generation input {relative} has a stale digest")
        reference_tool.decode_png(data, str(path))
        expected_paths.add(relative)
    refusals = manifest["refused_references"]
    if not isinstance(refusals, list) or len(refusals) != 3:
        raise ValidationError("generation input handoff must retain all three City refusals")
    if any(
        not isinstance(value, dict)
        or value.get("key", {}).get("pack") != "Scenarios/City of Bywater/Scenario"
        for value in refusals
    ):
        raise ValidationError("generation input handoff City refusal list is invalid")
    actual_paths = {
        path.relative_to(input_dir).as_posix()
        for path in input_dir.rglob("*")
        if path.is_file()
    }
    if actual_paths != expected_paths:
        raise ValidationError("generation input handoff contains missing or unmanifested files")


def validate(
    root: Path,
    jobs_path: Path,
    receipts_path: Path,
    input_dir: Path,
    amendments_path: Path | None = None,
    topology_amendments_path: Path | None = None,
) -> ValidationResult:
    root = root.resolve()
    jobs, jobs_bytes, style_spec = _load_job_set(root, jobs_path)
    amendments, amendment_set_sha256_by_job = _load_all_prompt_amendments(
        root, jobs, jobs_bytes, amendments_path, topology_amendments_path
    )
    if receipts_path.exists():
        receipts, receipt_bytes = _read_json(receipts_path, "generation receipts")
        del receipt_bytes
    else:
        receipts = empty_receipt_manifest(jobs, jobs_bytes)
    _exact_keys(
        receipts,
        {
            "approval_eligible", "authorization_blocker", "complete_style_proof",
            "entries", "jobs_sha256", "receipt_set_kind", "refused_references",
            "schema_version", "style_spec_sha256",
        },
        "generation receipts",
    )
    if receipts["schema_version"] != 1 or receipts["receipt_set_kind"] != RECEIPT_KIND:
        raise ValidationError("generation receipts identity is invalid")
    if receipts["complete_style_proof"] is not False or receipts["approval_eligible"] is not False:
        raise ValidationError("generation receipts may never claim complete proof or approval")
    if receipts["authorization_blocker"] != CITY_BLOCKER:
        raise ValidationError("generation receipts City authorization blocker is missing")
    if receipts["refused_references"] != jobs["refused_references"]:
        raise ValidationError("generation receipts do not preserve the exact three City refusals")
    if _require_sha256(receipts["jobs_sha256"], "receipts.jobs_sha256") != _sha256_bytes(jobs_bytes):
        raise ValidationError("generation receipts refer to a different immutable job set")
    if _require_sha256(receipts["style_spec_sha256"], "receipts.style_spec_sha256") != jobs["shared_style"]["sha256"]:
        raise ValidationError("generation receipts refer to a different shared style")

    job_by_id = {job["job_id"]: job for job in jobs["jobs"]}
    entries = receipts["entries"]
    if not isinstance(entries, list) or len(entries) > len(job_by_id):
        raise ValidationError("generation receipts.entries must be an array of at most 21 entries")
    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    seen_raw_paths: set[str] = set()
    valid_outputs: list[ValidatedOutput] = []
    rejected: list[str] = []
    if entries:
        _verify_input_handoff(jobs, input_dir.resolve())
    for index, receipt in enumerate(entries):
        where = f"generation receipts.entries[{index}]"
        if not isinstance(receipt, dict):
            raise ValidationError(f"{where} must be an object")
        job_id = receipt.get("job_id")
        expected_keys = {
                "input", "job_id", "model_provenance", "output", "policy_evidence",
                "post_processing", "prior_attempts", "prompt_sha256", "raw_output",
                "regeneration_ordinal", "rejection_reason", "resolved_prompt",
                "retry_prompt_supplement", "review_status", "reviewer",
                "shared_style_sha256",
        }
        if job_id in amendments:
            expected_keys.add("prompt_amendment_provenance")
        _exact_keys(receipt, expected_keys, where)
        job_id = receipt["job_id"]
        if job_id not in job_by_id or job_id in seen_ids:
            raise ValidationError(f"{where}.job_id is unknown or duplicated")
        seen_ids.add(job_id)
        job = job_by_id[job_id]
        amendment = amendments.get(job_id)
        amendment_provenance = None
        if amendment is not None:
            amendment_set_sha256 = amendment_set_sha256_by_job[job_id]
            amendment_provenance = _prompt_amendment_provenance(
                amendment, amendment_set_sha256, jobs, jobs_bytes
            )
            if receipt["prompt_amendment_provenance"] != amendment_provenance:
                raise ValidationError(f"{job_id} prompt-amendment provenance is stale")
            if job_id in TOPOLOGY_AMENDED_JOB_IDS:
                _verify_topology_amendment_evidence(input_dir, job, amendment)
        if receipt["input"] != job["input"]:
            raise ValidationError(f"{job_id} receipt input does not match the immutable job")
        if receipt["shared_style_sha256"] != jobs["shared_style"]["sha256"]:
            raise ValidationError(f"{job_id} receipt shared-style hash is stale")
        ordinal = receipt["regeneration_ordinal"]
        if ordinal not in (0, 1):
            raise ValidationError(f"{where}.regeneration_ordinal must be 0 or 1")
        resolved_prompt = effective_prompt(
            style_spec,
            job,
            ordinal,
            receipt["retry_prompt_supplement"],
            amendment,
        )
        if receipt["resolved_prompt"] != resolved_prompt:
            raise ValidationError(
                f"{job_id} resolved prompt does not match immutable base plus retry supplement"
            )
        prompt_hash = _sha256_bytes(resolved_prompt.encode("utf-8"))
        if receipt["prompt_sha256"] != prompt_hash:
            raise ValidationError(f"{job_id} prompt SHA-256 does not match")
        _validate_model_provenance(receipt["model_provenance"], f"{where}.model_provenance")
        if amendment is not None and _parse_rfc3339(
            receipt["model_provenance"]["started_at"],
            f"{where}.model_provenance.started_at",
        ) < _parse_rfc3339(amendment["recorded_at"], f"{where}.amendment.recorded_at"):
            raise ValidationError(f"{job_id} model call predates its prompt amendment")
        prior_raw_paths = _validate_prior_attempts(
            root,
            receipt["prior_attempts"],
            style_spec,
            job,
            ordinal,
            f"{where}.prior_attempts",
            amendment,
            amendment_provenance,
        )
        if seen_raw_paths.intersection(prior_raw_paths):
            raise ValidationError(f"{where}.prior_attempts repeats a raw output path")
        seen_raw_paths.update(prior_raw_paths)
        raw_hash, raw_facts, raw_path = _validate_raw_output(
            root,
            receipt["raw_output"],
            job,
            ordinal,
            f"{where}.raw_output",
        )
        if raw_path in seen_raw_paths:
            raise ValidationError(f"{where}.raw_output path is duplicated")
        seen_raw_paths.add(raw_path)
        status = receipt["review_status"]
        if status not in ("generated", "rejected", "approved"):
            raise ValidationError(f"{where}.review_status is invalid")
        reviewer = receipt["reviewer"]
        if status == "generated":
            if reviewer is not None:
                _require_nonempty_string(reviewer, f"{where}.reviewer")
        else:
            _require_nonempty_string(reviewer, f"{where}.reviewer")
        if status == "rejected":
            _require_nonempty_string(
                receipt["rejection_reason"], f"{where}.rejection_reason"
            )
            if receipt["output"] is not None or receipt["policy_evidence"] is not None:
                raise ValidationError(
                    f"{job_id} rejected raw attempt may not claim a validated final output"
                )
            if receipt["post_processing"] != []:
                raise ValidationError(
                    f"{job_id} rejected raw attempt must not claim final post-processing"
                )
            rejected.append(job_id)
            continue
        if receipt["rejection_reason"] is not None:
            raise ValidationError(f"{job_id} non-rejected receipt must have null rejection_reason")
        input_path = _under(input_dir.resolve(), job["input"]["path"], f"{job_id} input")
        try:
            input_data = input_path.read_bytes()
        except OSError as exc:
            raise ValidationError(f"cannot read {job_id} input {input_path}: {exc}") from exc
        if _sha256_bytes(input_data) != job["input"]["decoded_png_sha256"]:
            raise ValidationError(f"{job_id} input PNG hash does not match the immutable job")
        classic = reference_tool.decode_png(input_data, str(input_path))

        output = receipt["output"]
        if not isinstance(output, dict):
            raise ValidationError(f"{where}.output must be an object")
        _exact_keys(output, {"path", "sha256", "width", "height", "color_space", "pixel_format"}, f"{where}.output")
        output_path_text = _safe_relative(output["path"], f"{where}.output.path", ".png")
        if output_path_text != job["expected_output_path"] or output_path_text in seen_paths:
            raise ValidationError(f"{job_id} output path is unexpected or duplicated")
        seen_paths.add(output_path_text)
        output_path = _under(root, output_path_text, f"{job_id} output")
        try:
            output_data = output_path.read_bytes()
        except OSError as exc:
            raise ValidationError(f"cannot read {job_id} output {output_path}: {exc}") from exc
        if _sha256_bytes(output_data) != _require_sha256(output["sha256"], f"{where}.output.sha256"):
            raise ValidationError(f"{job_id} output PNG hash does not match receipt")
        facts, measured = _inspect_output(job, classic, output_path)
        expected_output_record = {
            "path": output_path_text,
            "sha256": _sha256_bytes(output_data),
            "width": facts.image.width,
            "height": facts.image.height,
            "color_space": "sRGB",
            "pixel_format": facts.pixel_format,
        }
        if output != expected_output_record:
            raise ValidationError(f"{job_id} output metadata does not match decoded PNG")
        _validate_post_processing(
            receipt["post_processing"],
            masked=job["alpha"]["input_alpha"] == "binary_mask",
            ui_tile=job["family"] == "ui_material",
            raw_sha256=raw_hash,
            raw_facts=raw_facts,
            final_sha256=output["sha256"],
            target_dimensions=(
                job["target_master"]["width"],
                job["target_master"]["height"],
            ),
            where=f"{where}.post_processing",
        )
        evidence = receipt["policy_evidence"]
        if not isinstance(evidence, dict):
            raise ValidationError(f"{where}.policy_evidence must be an object")
        _exact_keys(
            evidence,
            {"alpha", "baked_lighting", "ocr", "seams"},
            f"{where}.policy_evidence",
        )
        if evidence["alpha"] != measured["alpha"] or evidence["seams"] != measured["seams"]:
            raise ValidationError(f"{job_id} alpha or seam evidence does not match measured pixels")
        _validate_ocr(root, evidence["ocr"], status, f"{where}.policy_evidence.ocr")
        _validate_baked_lighting(
            evidence["baked_lighting"],
            status,
            f"{where}.policy_evidence.baked_lighting",
        )
        valid_outputs.append(ValidatedOutput(job, receipt, classic, facts.image, facts))

    output_root = root / "assets/remastered/style-proof/generation/outputs"
    actual_output_paths = {
        path.relative_to(root).as_posix()
        for path in output_root.glob("*.png")
        if path.is_file()
    } if output_root.exists() else set()
    if actual_output_paths != seen_paths:
        raise ValidationError("output directory contains missing, extra, or unreceipted PNG files")
    raw_root = root / "assets/remastered/style-proof/generation/raw"
    actual_raw_paths = {
        path.relative_to(root).as_posix()
        for path in raw_root.glob("*/attempt-*.png")
        if path.is_file()
    } if raw_root.exists() else set()
    if actual_raw_paths != seen_raw_paths:
        raise ValidationError("raw output directory contains missing, extra, or unreceipted PNG files")
    pending = tuple(job["job_id"] for job in jobs["jobs"] if job["job_id"] not in seen_ids)
    return ValidationResult(
        total_jobs=len(job_by_id),
        valid_outputs=tuple(valid_outputs),
        pending_job_ids=pending,
        rejected_job_ids=tuple(rejected),
        refused_city_count=len(jobs["refused_references"]),
    )


def inspect_raw_attempt(
    root: Path,
    jobs_path: Path,
    input_dir: Path,
    job_id: str,
    regeneration_ordinal: int,
    retry_prompt_supplement: str | None,
    amendments_path: Path | None = None,
    topology_amendments_path: Path | None = None,
) -> dict[str, Any]:
    jobs, jobs_bytes, style_spec = _load_job_set(root.resolve(), jobs_path)
    amendments, amendment_set_sha256_by_job = _load_all_prompt_amendments(
        root.resolve(), jobs, jobs_bytes, amendments_path,
        topology_amendments_path,
    )
    _verify_input_handoff(jobs, input_dir.resolve())
    job = next((item for item in jobs["jobs"] if item["job_id"] == job_id), None)
    if job is None:
        raise ValidationError(f"unknown job ID {job_id!r}")
    amendment = amendments.get(job_id)
    if job_id in TOPOLOGY_AMENDED_JOB_IDS:
        assert amendment is not None
        _verify_topology_amendment_evidence(input_dir, job, amendment)
    prompt = effective_prompt(
        style_spec, job, regeneration_ordinal, retry_prompt_supplement, amendment
    )
    raw_path_text = (
        "assets/remastered/style-proof/generation/raw/"
        f"{job_id}/attempt-{regeneration_ordinal}.png"
    )
    raw_path = _under(root.resolve(), raw_path_text, f"{job_id} raw output")
    raw_data = raw_path.read_bytes()
    raw_facts = _png_facts(raw_data, str(raw_path))
    result = {
        "input": job["input"],
        "job_id": job_id,
        "prompt_sha256": _sha256_bytes(prompt.encode("utf-8")),
        "raw_output": {
            "embedded_color_space": "sRGB" if raw_facts.has_srgb_chunk else None,
            "height": raw_facts.image.height,
            "path": raw_path_text,
            "pixel_format": raw_facts.pixel_format,
            "sha256": _sha256_bytes(raw_data),
            "width": raw_facts.image.width,
        },
        "regeneration_ordinal": regeneration_ordinal,
        "resolved_prompt": prompt,
        "retry_prompt_supplement": retry_prompt_supplement,
        "shared_style_sha256": jobs["shared_style"]["sha256"],
    }
    if amendment is not None:
        amendment_set_sha256 = amendment_set_sha256_by_job[job_id]
        result["prompt_amendment_provenance"] = _prompt_amendment_provenance(
            amendment, amendment_set_sha256, jobs, jobs_bytes
        )
    return result


def prepare_amended_prompt(
    root: Path,
    jobs_path: Path,
    input_dir: Path,
    receipts_path: Path,
    operational_log_path: Path,
    job_id: str,
    regeneration_ordinal: int = 0,
    retry_prompt_supplement: str | None = None,
    amendments_path: Path | None = None,
    topology_amendments_path: Path | None = None,
) -> dict[str, Any]:
    """Prepare an amended initial call or its one canonically receipted retry."""
    root = root.resolve()
    jobs, jobs_bytes, style_spec = _load_job_set(root, jobs_path)
    _verify_input_handoff(jobs, input_dir.resolve())
    amendments, amendment_set_sha256_by_job = _load_all_prompt_amendments(
        root, jobs, jobs_bytes, amendments_path, topology_amendments_path
    )
    job = next((item for item in jobs["jobs"] if item["job_id"] == job_id), None)
    if job is None:
        raise ValidationError(f"unknown job ID {job_id!r}")
    amendment = amendments.get(job_id)
    if amendment is None:
        raise ValidationError(f"{job_id} has no reviewed pre-call prompt amendment")
    amendment_set_sha256 = amendment_set_sha256_by_job[job_id]
    if job_id in TOPOLOGY_AMENDED_JOB_IDS:
        _verify_topology_amendment_evidence(input_dir, job, amendment)
    if regeneration_ordinal == 0:
        _assert_amendment_is_pre_call(
            root, job, receipts_path, operational_log_path
        )
    elif regeneration_ordinal == 1:
        _assert_amended_retry_is_pre_call(
            root,
            jobs_path,
            input_dir,
            receipts_path,
            amendments_path,
            topology_amendments_path,
            operational_log_path,
            job_id,
        )
    prompt = effective_prompt(
        style_spec,
        job,
        regeneration_ordinal,
        retry_prompt_supplement,
        amendment,
    )
    return {
        "input": job["input"],
        "job_id": job_id,
        "prompt_amendment": amendment,
        "prompt_amendment_provenance": _prompt_amendment_provenance(
            amendment, amendment_set_sha256, jobs, jobs_bytes
        ),
        "prompt_sha256": _sha256_bytes(prompt.encode("utf-8")),
        "regeneration_ordinal": regeneration_ordinal,
        "resolved_prompt": prompt,
        "retry_prompt_supplement": retry_prompt_supplement,
        "shared_style_sha256": jobs["shared_style"]["sha256"],
    }


def inspect_job_output(
    root: Path,
    jobs_path: Path,
    input_dir: Path,
    job_id: str,
    regeneration_ordinal: int = 0,
    retry_prompt_supplement: str | None = None,
    amendments_path: Path | None = None,
    topology_amendments_path: Path | None = None,
) -> dict[str, Any]:
    jobs, _, _ = _load_job_set(root.resolve(), jobs_path)
    attempt = inspect_raw_attempt(
        root,
        jobs_path,
        input_dir,
        job_id,
        regeneration_ordinal,
        retry_prompt_supplement,
        amendments_path,
        topology_amendments_path,
    )
    job = next(item for item in jobs["jobs"] if item["job_id"] == job_id)
    input_path = _under(input_dir.resolve(), job["input"]["path"], f"{job_id} input")
    input_data = input_path.read_bytes()
    if _sha256_bytes(input_data) != job["input"]["decoded_png_sha256"]:
        raise ValidationError(f"{job_id} input PNG hash does not match")
    classic = reference_tool.decode_png(input_data, str(input_path))
    output_path = _under(root.resolve(), job["expected_output_path"], f"{job_id} output")
    output_data = output_path.read_bytes()
    facts, measured = _inspect_output(job, classic, output_path)
    return {
        **attempt,
        "output": {
            "color_space": "sRGB",
            "height": facts.image.height,
            "path": job["expected_output_path"],
            "pixel_format": facts.pixel_format,
            "sha256": _sha256_bytes(output_data),
            "width": facts.image.width,
        },
        "policy_evidence": {
            **measured,
            "baked_lighting": {
                "method": None,
                "result": "not-reviewed",
                "reviewed": False,
            },
            "ocr": {
                "evidence_path": None,
                "evidence_sha256": None,
                "method": None,
                "performed": False,
                "result": "not-run",
            },
        },
    }


def _clean_label(value: str, limit: int) -> str:
    allowed = set(reference_tool.FONT_5X7)
    cleaned = "".join(character if character.upper() in allowed else " " for character in value.upper())
    return " ".join(cleaned.split())[:limit]


def _draw_preview(
    pixels: bytearray,
    canvas_width: int,
    canvas_height: int,
    image: reference_tool.RGBAImage,
    x: int,
    y: int,
    width: int,
    height: int,
) -> None:
    for checker_y in range(0, height, 8):
        for checker_x in range(0, width, 8):
            color = (100, 96, 90, 255) if (checker_x // 8 + checker_y // 8) % 2 else (137, 131, 120, 255)
            reference_tool._fill_rect(
                pixels, canvas_width, canvas_height, x + checker_x, y + checker_y,
                min(8, width - checker_x), min(8, height - checker_y), color,
            )
    scale = min(width / image.width, height / image.height)
    target_width = max(1, int(image.width * scale))
    target_height = max(1, int(image.height * scale))
    resized = reference_tool._resize_nearest(image, target_width, target_height)
    reference_tool._paste_alpha(
        pixels, canvas_width, canvas_height, resized,
        x + (width - target_width) // 2, y + (height - target_height) // 2,
    )


def _new_sheet(outputs: tuple[ValidatedOutput, ...]) -> reference_tool.RGBAImage:
    columns = 4
    cell_width = 320
    cell_height = 235
    header = 112
    rows = math.ceil(len(outputs) / columns)
    width = columns * cell_width
    height = header + rows * cell_height
    pixels = reference_tool._new_image(width, height, (24, 21, 19, 255))
    reference_tool._fill_rect(pixels, width, height, 0, 0, width, header, (42, 34, 29, 255))
    reference_tool._draw_text(pixels, width, height, 24, 18, "REALMZ REMASTERED - NEW STYLE PROOF", 3, (242, 216, 162, 255))
    reference_tool._draw_text(pixels, width, height, 24, 55, "REVIEW COPY - NOT APPROVED", 2, (255, 172, 115, 255))
    reference_tool._draw_text(pixels, width, height, 24, 82, f"VALID OUTPUTS {len(outputs):02d} OF 21 - CITY 3 BLOCKED", 1, (224, 218, 204, 255))
    for index, output in enumerate(outputs):
        x = (index % columns) * cell_width
        y = header + (index // columns) * cell_height
        reference_tool._fill_rect(pixels, width, height, x + 6, y + 6, cell_width - 12, cell_height - 12, (50, 46, 42, 255))
        reference_tool._draw_text(pixels, width, height, x + 14, y + 14, f"{output.job['order']:02d} {_clean_label(output.job['family'], 22)}", 2, (244, 225, 185, 255))
        _draw_preview(pixels, width, height, output.generated, x + 14, y + 38, 292, 156)
        reference_tool._draw_text(pixels, width, height, x + 14, y + 202, _clean_label(output.job["label"], 42), 1, (238, 232, 218, 255))
        reference_tool._draw_text(pixels, width, height, x + 14, y + 216, f"NEW SHA256 {output.receipt['output']['sha256'][:12]}", 1, (181, 170, 153, 255))
    return reference_tool.RGBAImage(width, height, bytes(pixels))


def _comparison_sheet(outputs: tuple[ValidatedOutput, ...]) -> reference_tool.RGBAImage:
    columns = 2
    cell_width = 640
    cell_height = 278
    header = 112
    rows = math.ceil(len(outputs) / columns)
    width = columns * cell_width
    height = header + rows * cell_height
    pixels = reference_tool._new_image(width, height, (24, 21, 19, 255))
    reference_tool._fill_rect(pixels, width, height, 0, 0, width, header, (42, 34, 29, 255))
    reference_tool._draw_text(pixels, width, height, 24, 18, "CLASSIC / NEW STYLE PROOF COMPARISON", 3, (242, 216, 162, 255))
    reference_tool._draw_text(pixels, width, height, 24, 55, "REVIEW COPY - NOT APPROVED", 2, (255, 172, 115, 255))
    reference_tool._draw_text(pixels, width, height, 24, 82, f"VALID PAIRS {len(outputs):02d} OF 21 - CITY 3 BLOCKED", 1, (224, 218, 204, 255))
    for index, output in enumerate(outputs):
        x = (index % columns) * cell_width
        y = header + (index // columns) * cell_height
        reference_tool._fill_rect(pixels, width, height, x + 6, y + 6, cell_width - 12, cell_height - 12, (50, 46, 42, 255))
        title = f"{output.job['order']:02d} {_clean_label(output.job['label'], 48)}"
        reference_tool._draw_text(pixels, width, height, x + 14, y + 14, title, 1, (244, 225, 185, 255))
        reference_tool._draw_text(pixels, width, height, x + 14, y + 31, "CLASSIC", 1, (205, 193, 174, 255))
        reference_tool._draw_text(pixels, width, height, x + 326, y + 31, "NEW", 1, (205, 193, 174, 255))
        _draw_preview(pixels, width, height, output.classic, x + 14, y + 48, 300, 194)
        _draw_preview(pixels, width, height, output.generated, x + 326, y + 48, 300, 194)
        reference_tool._draw_text(pixels, width, height, x + 14, y + 250, f"INPUT {output.job['input']['decoded_png_sha256'][:12]}", 1, (181, 170, 153, 255))
        reference_tool._draw_text(pixels, width, height, x + 326, y + 250, f"OUTPUT {output.receipt['output']['sha256'][:12]}", 1, (181, 170, 153, 255))
    return reference_tool.RGBAImage(width, height, bytes(pixels))


def build_sheets(result: ValidationResult, output_dir: Path) -> dict[str, str]:
    if not result.valid_outputs:
        raise ValidationError("no valid non-rejected outputs exist; review sheets were not built")
    output_dir.mkdir(parents=True, exist_ok=True)
    new_bytes = reference_tool.encode_png(_new_sheet(result.valid_outputs))
    comparison_bytes = reference_tool.encode_png(_comparison_sheet(result.valid_outputs))
    new_path = output_dir / "new-contact-sheet.png"
    comparison_path = output_dir / "classic-new-contact-sheet.png"
    new_path.write_bytes(new_bytes)
    comparison_path.write_bytes(comparison_bytes)
    return {
        new_path.name: _sha256_bytes(new_bytes),
        comparison_path.name: _sha256_bytes(comparison_bytes),
    }


def _result_json(result: ValidationResult) -> dict[str, Any]:
    return {
        "approval_eligible": False,
        "complete_style_proof": False,
        "pending": len(result.pending_job_ids),
        "pending_job_ids": list(result.pending_job_ids),
        "refused_city": result.refused_city_count,
        "rejected": len(result.rejected_job_ids),
        "rejected_job_ids": list(result.rejected_job_ids),
        "status": "incomplete-review-material-only",
        "total_jobs": result.total_jobs,
        "valid_outputs": len(result.valid_outputs),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--jobs", default=DEFAULT_JOBS, help="immutable jobs JSON")
    parser.add_argument("--receipts", default=DEFAULT_RECEIPTS, help="separate receipt JSON")
    parser.add_argument(
        "--amendments", default=DEFAULT_AMENDMENTS,
        help="reviewed pre-call prompt amendment register",
    )
    parser.add_argument(
        "--topology-amendments", default=DEFAULT_TOPOLOGY_AMENDMENTS,
        help="reviewed masked-asset topology amendment register",
    )
    parser.add_argument(
        "--inputs",
        default="assets/remastered/style-proof/classic-references",
        help="verified input handoff directory containing references/",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate", help="validate receipts; missing outputs remain pending")
    init_parser = subparsers.add_parser("init-receipts", help="write a canonical empty receipt manifest")
    init_parser.add_argument("--output", help="output path; defaults to --receipts")
    inspect_parser = subparsers.add_parser("inspect", help="print locked hashes and measured evidence for one output")
    inspect_parser.add_argument("--job-id", required=True)
    inspect_parser.add_argument("--regeneration-ordinal", type=int, choices=(0, 1), default=0)
    inspect_parser.add_argument("--retry-prompt-supplement")
    inspect_raw_parser = subparsers.add_parser(
        "inspect-attempt",
        help="print prompt/provenance locks for a raw model attempt before adoption",
    )
    inspect_raw_parser.add_argument("--job-id", required=True)
    inspect_raw_parser.add_argument("--regeneration-ordinal", type=int, choices=(0, 1), default=0)
    inspect_raw_parser.add_argument("--retry-prompt-supplement")
    sheet_parser = subparsers.add_parser("build-sheets", help="build deterministic New and Classic/New review sheets")
    sheet_parser.add_argument("--output", required=True, help="review-sheet output directory")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve()
    jobs_path = Path(args.jobs)
    if not jobs_path.is_absolute():
        jobs_path = root / jobs_path
    receipts_path = Path(args.receipts)
    if not receipts_path.is_absolute():
        receipts_path = root / receipts_path
    amendments_path = Path(args.amendments)
    if not amendments_path.is_absolute():
        amendments_path = root / amendments_path
    topology_amendments_path = Path(args.topology_amendments)
    if not topology_amendments_path.is_absolute():
        topology_amendments_path = root / topology_amendments_path
    input_dir = Path(args.inputs)
    if not input_dir.is_absolute():
        input_dir = root / input_dir
    try:
        if args.command == "init-receipts":
            jobs, jobs_bytes, _ = _load_job_set(root, jobs_path)
            destination = Path(args.output) if args.output else receipts_path
            if not destination.is_absolute():
                destination = root / destination
            if destination.exists():
                raise ValidationError(f"refusing to overwrite existing receipt manifest {destination}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(_canonical_json(empty_receipt_manifest(jobs, jobs_bytes)))
            print(destination)
        elif args.command == "inspect-attempt":
            print(
                json.dumps(
                    inspect_raw_attempt(
                        root,
                        jobs_path,
                        input_dir,
                        args.job_id,
                        args.regeneration_ordinal,
                        args.retry_prompt_supplement,
                        amendments_path,
                        topology_amendments_path,
                    ),
                    ensure_ascii=False,
                    indent=2,
                    sort_keys=True,
                )
            )
        elif args.command == "inspect":
            print(
                json.dumps(
                    inspect_job_output(
                        root,
                        jobs_path,
                        input_dir,
                        args.job_id,
                        args.regeneration_ordinal,
                        args.retry_prompt_supplement,
                        amendments_path,
                        topology_amendments_path,
                    ),
                    ensure_ascii=False,
                    indent=2,
                    sort_keys=True,
                )
            )
        else:
            result = validate(
                root, jobs_path, receipts_path, input_dir, amendments_path,
                topology_amendments_path,
            )
            response = _result_json(result)
            if args.command == "build-sheets":
                output_dir = Path(args.output)
                if not output_dir.is_absolute():
                    output_dir = root / output_dir
                response["sheets"] = build_sheets(result, output_dir)
            print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
    except (ValidationError, OSError) as exc:
        print(f"style-proof output validation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
