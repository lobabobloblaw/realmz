#!/usr/bin/env python3
"""Prepare fail-closed ImageGen reference inputs for an item job.

Every item job must first appear in the jobs-hash-bound classification
manifest.  Standalone, base, and animation-frame jobs receive their own locked
Classic PNG.  A job declaring ``variant_of`` receives that Classic PNG followed
by the approved remastered master of its declared base job.  Known candidates
remain blocked until human classification.  The emitted classification hash
and reference records are suitable for copying into the generation receipt;
``referenced_image_paths`` is the exact, ordered list supplied to ImageGen.

This command is a pre-call gate only.  It never invokes a model and never
modifies the immutable job or receipt manifests.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


DEFAULT_JOBS = "assets/remastered/style-proof/generation/jobs.json"
DEFAULT_RECEIPTS = (
    "assets/remastered/style-proof/generation/generation-receipts.json"
)
DEFAULT_INPUTS = "assets/remastered/style-proof/classic-references"
DEFAULT_POLICY = (
    "assets/remastered/style-proof/generation/variant-reference-policy.json"
)
DEFAULT_CLASSIFICATIONS = (
    "assets/remastered/style-proof/generation/item-classifications.json"
)

REFERENCE_SET_KIND = "item-generation-reference-set"
REFERENCE_SET_SCHEMA_VERSION = 1
OWN_CLASSIC_ROLE = "own_locked_classic"
BASE_MASTER_ROLE = "approved_base_master"
DEFAULT_ITEM_FAMILIES = frozenset({"item_spell"})
CITY_PACK_PREFIX = "Scenarios/City of Bywater/"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
JOB_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
RFC3339_RE = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"
    r"(?:\.[0-9]+)?(?:Z|[+-][0-9]{2}:[0-9]{2})$"
)


class GateError(ValueError):
    """A model call must not start because a reference invariant failed."""


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_json_bytes(value: Any) -> bytes:
    """Project canonical JSON used for manifests and emitted call records."""
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _canonical_reference_bytes(value: list[dict[str, Any]]) -> bytes:
    """Stable byte representation hashed as ``reference_set_sha256``."""
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def _read_json(path: Path, where: str) -> tuple[dict[str, Any], bytes]:
    try:
        data = path.read_bytes()
        value = json.loads(data)
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"cannot read {where} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise GateError(f"{where} must be a JSON object")
    if data != _canonical_json_bytes(value):
        raise GateError(f"{where} must use canonical sorted JSON")
    return value, data


def _nonempty_string(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise GateError(f"{where} must be a non-empty string")
    return value


def _sha256(value: Any, where: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise GateError(f"{where} must be a lowercase SHA-256")
    return value


def _relative_path(value: Any, where: str, suffix: str | None = None) -> str:
    if not isinstance(value, str) or not value or "\\" in value:
        raise GateError(f"{where} must be a safe relative POSIX path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise GateError(f"{where} must be a safe relative POSIX path")
    if suffix is not None and not value.endswith(suffix):
        raise GateError(f"{where} must end in {suffix}")
    return value


def _under(directory: Path, relative: str, where: str) -> Path:
    base = directory.resolve()
    path = (base / PurePosixPath(relative)).resolve()
    try:
        path.relative_to(base)
    except ValueError as exc:
        raise GateError(f"{where} escapes {base}") from exc
    return path


def _read_hashed_file(path: Path, expected: str, where: str) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise GateError(f"cannot read {where} {path}: {exc}") from exc
    if _sha256_bytes(data) != expected:
        raise GateError(f"{where} content SHA-256 does not match its lock")
    return data


def _resource_key(value: Any, where: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != {"pack", "type", "id"}:
        raise GateError(f"{where} must contain exactly pack, type, and id")
    pack = _nonempty_string(value["pack"], f"{where}.pack")
    resource_type = value["type"]
    resource_id = value["id"]
    if resource_type not in ("PICT", "cicn", "ppat"):
        raise GateError(f"{where}.type is invalid")
    if not isinstance(resource_id, int) or isinstance(resource_id, bool):
        raise GateError(f"{where}.id must be an integer")
    return {"id": resource_id, "pack": pack, "type": resource_type}


def _key_tuple(value: dict[str, Any]) -> tuple[str, str, int]:
    return (value["pack"], value["type"], value["id"])


def _parse_time(value: Any, where: str) -> datetime:
    if not isinstance(value, str) or RFC3339_RE.fullmatch(value) is None:
        raise GateError(f"{where} must be an RFC 3339 timestamp with an offset")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise GateError(f"{where} must be an RFC 3339 timestamp") from exc
    if parsed.utcoffset() is None:
        raise GateError(f"{where} must include a UTC offset")
    return parsed


def _is_city(key: dict[str, Any]) -> bool:
    return key["pack"].startswith(CITY_PACK_PREFIX)


def _load_optional_policy(root: Path, policy_path: Path) -> dict[str, Any] | None:
    """Load the project policy when present; its strict shape is versioned there.

    The initial gate also carries the invariants in code so a standalone
    synthetic contract can be tested before the policy artifact is copied into
    a temporary root.  Once a policy is present, disagreement fails closed.
    """
    if not policy_path.exists():
        raise GateError(f"variant reference policy is missing: {policy_path}")
    policy, _ = _read_json(policy_path, "variant reference policy")
    if policy.get("schema_version") != 1:
        raise GateError("variant reference policy schema_version must be 1")
    kind = policy.get("policy_kind")
    if kind not in (
        "realmz-item-variant-reference-policy",
        "item-variant-reference-policy",
        "item-variant-generation-reference-policy",
    ):
        raise GateError("variant reference policy kind is unsupported")

    rules = policy.get("rules")
    if rules is not None and not isinstance(rules, dict):
        raise GateError("variant reference policy rules must be an object")
    rule_source = rules if isinstance(rules, dict) else policy
    roles = rule_source.get(
        "ordered_reference_roles", rule_source.get("variant_reference_roles")
    )
    if roles is not None and roles != [OWN_CLASSIC_ROLE, BASE_MASTER_ROLE]:
        raise GateError("variant reference policy role order disagrees with the gate")
    standalone = rule_source.get("standalone_reference_roles")
    if standalone is not None and standalone != [OWN_CLASSIC_ROLE]:
        raise GateError("standalone reference policy disagrees with the gate")
    required_true = (
        "base_approval_required_before_variant_call",
        "model_call_must_use_every_emitted_reference_path",
        "receipt_reference_set_sha256_required",
        "city_relationships_require_authorized_baseline",
    )
    for name in required_true:
        if name in rule_source and rule_source[name] is not True:
            raise GateError(f"variant reference policy must enable {name}")
    return policy


def _allowed_families(policy: dict[str, Any] | None) -> frozenset[str]:
    if policy is None:
        return DEFAULT_ITEM_FAMILIES
    value = policy.get("item_families", policy.get("applies_to_families"))
    if value is None and isinstance(policy.get("scope"), dict):
        value = policy["scope"].get("families")
    if value is None:
        return DEFAULT_ITEM_FAMILIES
    if not isinstance(value, list) or not value or not all(
        isinstance(item, str) and item for item in value
    ):
        raise GateError("variant reference policy item families are invalid")
    return frozenset(value)


def _known_relationships(policy: dict[str, Any] | None) -> list[dict[str, Any]]:
    if policy is None:
        return []
    values = policy.get("known_relationships", [])
    if not isinstance(values, list):
        raise GateError("variant reference policy known_relationships must be an array")
    result: list[dict[str, Any]] = []
    for index, value in enumerate(values):
        where = f"variant reference policy.known_relationships[{index}]"
        if not isinstance(value, dict):
            raise GateError(f"{where} must be an object")
        result.append(
            {
                "base_resource_key": _resource_key(
                    value.get("base_resource_key"), f"{where}.base_resource_key"
                ),
                "variant_resource_key": _resource_key(
                    value.get("variant_resource_key"), f"{where}.variant_resource_key"
                ),
            }
        )
    return result


def _historical_attempts(value: Any, where: str) -> None:
    if value is None:
        return
    if not isinstance(value, dict) or set(value) != {"attempt_ordinals", "status"}:
        raise GateError(f"{where} must contain exactly attempt_ordinals and status")
    if value["attempt_ordinals"] != [0, 1]:
        raise GateError(f"{where}.attempt_ordinals must preserve rejected attempts 0 and 1")
    if value["status"] != "rejected_provenance_incomplete_do_not_backfill":
        raise GateError(f"{where}.status may not claim reconstructed provider proof")


def _validate_animation_sequence(
    root: Path,
    value: Any,
    resource_key: dict[str, Any],
    where: str,
) -> None:
    if not isinstance(value, dict) or set(value) != {
        "base_resource_id", "frame_count", "frame_index", "resource_ids",
        "source", "spelllook1"
    }:
        raise GateError(f"{where} has invalid animation sequence fields")
    base = value["base_resource_id"]
    count = value["frame_count"]
    index = value["frame_index"]
    ids = value["resource_ids"]
    spelllook1 = value["spelllook1"]
    if (
        not isinstance(base, int) or isinstance(base, bool)
        or not isinstance(count, int) or isinstance(count, bool) or count <= 0
        or not isinstance(index, int) or isinstance(index, bool)
        or index < 0 or index >= count
        or ids != list(range(base, base + count))
        or not isinstance(spelllook1, int) or isinstance(spelllook1, bool)
        or spelllook1 < 0 or base != 11992 + spelllook1 * 8
        or resource_key["id"] != base + index
    ):
        raise GateError(f"{where} frame order or ResourceKey membership is invalid")
    source = value["source"]
    if not isinstance(source, dict) or set(source) != {
        "excerpt_sha256", "file_sha256", "first_line", "formula", "last_line", "path"
    }:
        raise GateError(f"{where}.source fields are invalid")
    source_path_text = _relative_path(source["path"], f"{where}.source.path")
    source_path = _under(root, source_path_text, f"{where}.source")
    file_hash = _sha256(source["file_sha256"], f"{where}.source.file_sha256")
    source_data = _read_hashed_file(source_path, file_hash, f"{where}.source")
    first = source["first_line"]
    last = source["last_line"]
    if (
        not isinstance(first, int) or isinstance(first, bool) or first < 1
        or not isinstance(last, int) or isinstance(last, bool) or last < first
    ):
        raise GateError(f"{where}.source line range is invalid")
    lines = source_data.splitlines(keepends=True)
    if last > len(lines):
        raise GateError(f"{where}.source line range exceeds the locked source")
    excerpt = b"".join(lines[first - 1:last])
    if _sha256_bytes(excerpt) != _sha256(
        source["excerpt_sha256"], f"{where}.source.excerpt_sha256"
    ):
        raise GateError(f"{where}.source excerpt SHA-256 is stale")
    formula = _nonempty_string(source["formula"], f"{where}.source.formula")
    if (
        formula != "11992 + spellinfo.spelllook1 * 8 + spellindex; spellindex cycles 0..7"
        or b"11992 + spellinfo.spelllook1 * 8 + spellindex++" not in excerpt
        or b"if (spellindex > 7)" not in excerpt
        or b"spellindex = 0;" not in excerpt
    ):
        raise GateError(f"{where}.source does not prove the declared eight-frame formula")


def _load_item_classifications(
    root: Path,
    classification_path: Path,
    jobs: dict[str, Any],
    jobs_bytes: bytes,
    by_id: dict[str, dict[str, Any]],
    allowed_families: frozenset[str],
    policy: dict[str, Any],
) -> tuple[dict[str, dict[str, Any]], str]:
    if not classification_path.exists():
        raise GateError(f"item classification manifest is missing: {classification_path}")
    manifest, manifest_bytes = _read_json(
        classification_path, "item classification manifest"
    )
    if set(manifest) != {
        "classification_set_kind", "entries", "jobs_sha256", "rules", "schema_version"
    }:
        raise GateError("item classification manifest fields are invalid")
    if (
        manifest["schema_version"] != 1
        or manifest["classification_set_kind"]
        != "style-proof-item-generation-classifications"
        or manifest["jobs_sha256"] != _sha256_bytes(jobs_bytes)
    ):
        raise GateError("item classification manifest does not match the jobs file")
    expected_rules = {
        "classification_manifest_sha256_required_in_call_record": True,
        "classification_required_before_item_call": True,
        "generation_reference_set_required_in_call_record": True,
        "historical_rejected_attempts_may_not_be_backfilled": True,
        "known_variant_candidate_generation_allowed": False,
    }
    if manifest["rules"] != expected_rules:
        raise GateError("item classification manifest rules are not fail-closed")
    entries = manifest["entries"]
    if not isinstance(entries, list):
        raise GateError("item classification manifest entries must be an array")
    item_ids = {
        job_id for job_id, job in by_id.items()
        if job.get("family") in allowed_families
    }
    result: dict[str, dict[str, Any]] = {}
    known_relationships = _known_relationships(policy)
    for index, entry in enumerate(entries):
        where = f"item classification manifest.entries[{index}]"
        if not isinstance(entry, dict):
            raise GateError(f"{where} must be an object")
        job_id = entry.get("job_id")
        if job_id not in item_ids or job_id in result:
            raise GateError(f"{where}.job_id is unknown, duplicated, or not item-family")
        job = by_id[job_id]
        key = _resource_key(entry.get("resource_key"), f"{where}.resource_key")
        if key != _resource_key(job.get("resource_key"), f"job {job_id}.resource_key"):
            raise GateError(f"{where}.resource_key does not match its job")
        classification = entry.get("classification")
        declaration = _variant_declaration(job)
        _historical_attempts(entry.get("historical_attempts"), f"{where}.historical_attempts")
        common = {"classification", "generation_allowed", "job_id", "resource_key"}
        historical = {"historical_attempts"} if "historical_attempts" in entry else set()
        if classification in ("standalone", "base", "animation_frame"):
            expected = common | historical | {"reference_roles"}
            if classification == "animation_frame":
                expected.add("sequence")
            if set(entry) != expected:
                raise GateError(f"{where} fields do not match classification {classification}")
            if entry["generation_allowed"] is not True:
                raise GateError(f"{where}.generation_allowed must be true")
            if entry["reference_roles"] != [OWN_CLASSIC_ROLE]:
                raise GateError(f"{where}.reference_roles must contain only own Classic")
            if declaration is not None:
                raise GateError(f"{where} conflicts with job {job_id}.variant_of")
            if classification == "animation_frame":
                _validate_animation_sequence(root, entry["sequence"], key, f"{where}.sequence")
        elif classification == "variant":
            expected = common | historical | {
                "base_job_id", "base_resource_key", "reference_roles"
            }
            if set(entry) != expected:
                raise GateError(f"{where} fields do not match classification variant")
            if declaration is None:
                raise GateError(f"{where} requires an explicit job variant_of declaration")
            base_id, base_key = declaration
            if (
                entry["generation_allowed"] is not True
                or entry["base_job_id"] != base_id
                or _resource_key(entry["base_resource_key"], f"{where}.base_resource_key") != base_key
                or entry["reference_roles"] != [OWN_CLASSIC_ROLE, BASE_MASTER_ROLE]
            ):
                raise GateError(f"{where} does not match its exact declared base and reference roles")
        elif classification == "known_variant_candidate":
            expected = common | historical | {
                "blocked_reason", "candidate_base_resource_key"
            }
            if set(entry) != expected or entry["generation_allowed"] is not False:
                raise GateError(f"{where} known candidate must be explicitly blocked")
            _nonempty_string(entry["blocked_reason"], f"{where}.blocked_reason")
            candidate_base = _resource_key(
                entry["candidate_base_resource_key"],
                f"{where}.candidate_base_resource_key",
            )
            if declaration is not None:
                raise GateError(f"{where} remains a candidate and may not claim variant_of")
            if not any(
                relation["variant_resource_key"] == key
                and relation["base_resource_key"] == candidate_base
                for relation in known_relationships
            ):
                raise GateError(f"{where} does not match the reviewed candidate relationship")
        else:
            raise GateError(f"{where}.classification is unsupported")
        result[job_id] = entry
    if set(result) != item_ids:
        missing = sorted(item_ids - set(result))
        extra = sorted(set(result) - item_ids)
        raise GateError(
            f"item classification manifest must classify every item job; missing={missing}, extra={extra}"
        )
    for job_id, entry in result.items():
        if entry["classification"] == "variant":
            base_entry = result.get(entry["base_job_id"])
            if base_entry is None or base_entry["classification"] != "base":
                raise GateError(
                    f"item classification {job_id} requires its declared base job "
                    "to be classified as base"
                )
    for relation in known_relationships:
        target = next(
            (
                entry for entry in result.values()
                if entry["resource_key"] == relation["variant_resource_key"]
            ),
            None,
        )
        if target is None:
            raise GateError("reviewed known variant candidate has no classified item job")
        if target["classification"] == "known_variant_candidate":
            if target["candidate_base_resource_key"] != relation["base_resource_key"]:
                raise GateError("known variant candidate classification has a different base")
        elif target["classification"] == "variant":
            if target["base_resource_key"] != relation["base_resource_key"]:
                raise GateError("classified variant conflicts with its reviewed known base")
        else:
            raise GateError("known variant candidate may not be classified as standalone or base")
    del jobs
    return result, _sha256_bytes(manifest_bytes)


def _load_reference_manifest(root: Path, jobs: dict[str, Any]) -> dict[str, Any]:
    lock = jobs.get("source_reference_manifest")
    if not isinstance(lock, dict) or set(lock) != {"path", "sha256"}:
        raise GateError("jobs.source_reference_manifest lock is invalid")
    relative = _relative_path(lock["path"], "jobs.source_reference_manifest.path", ".json")
    digest = _sha256(lock["sha256"], "jobs.source_reference_manifest.sha256")
    path = _under(root, relative, "source reference manifest")
    manifest, data = _read_json(path, "source reference manifest")
    if _sha256_bytes(data) != digest:
        raise GateError("source reference manifest SHA-256 does not match the jobs lock")
    return manifest


def _index_jobs(jobs: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], set[tuple[str, str, int]]]:
    values = jobs.get("jobs")
    if not isinstance(values, list) or not values:
        raise GateError("jobs.jobs must be a non-empty array")
    by_id: dict[str, dict[str, Any]] = {}
    seen_keys: set[tuple[str, str, int]] = set()
    for index, job in enumerate(values):
        where = f"jobs.jobs[{index}]"
        if not isinstance(job, dict):
            raise GateError(f"{where} must be an object")
        job_id = job.get("job_id")
        if not isinstance(job_id, str) or JOB_ID_RE.fullmatch(job_id) is None:
            raise GateError(f"{where}.job_id is invalid")
        if job_id in by_id:
            raise GateError(f"duplicate job_id {job_id!r}")
        key = _resource_key(job.get("resource_key"), f"{where}.resource_key")
        identity = _key_tuple(key)
        if identity in seen_keys:
            raise GateError(f"duplicate ResourceKey on job {job_id!r}")
        seen_keys.add(identity)
        by_id[job_id] = job
    return by_id, seen_keys


def _index_manifest(manifest: dict[str, Any]) -> dict[tuple[str, str, int], dict[str, Any]]:
    values = manifest.get("entries")
    if not isinstance(values, list):
        raise GateError("source reference manifest entries must be an array")
    result: dict[tuple[str, str, int], dict[str, Any]] = {}
    for index, entry in enumerate(values):
        where = f"source reference manifest.entries[{index}]"
        if not isinstance(entry, dict):
            raise GateError(f"{where} must be an object")
        key = _resource_key(entry.get("key"), f"{where}.key")
        identity = _key_tuple(key)
        if identity in result:
            raise GateError(f"source reference manifest duplicates ResourceKey {identity!r}")
        result[identity] = entry
    return result


def _validate_job_input(
    job: dict[str, Any],
    *,
    manifest_by_key: dict[tuple[str, str, int], dict[str, Any]],
    input_dir: Path,
) -> tuple[dict[str, Any], str, Path, str]:
    job_id = job["job_id"]
    key = _resource_key(job.get("resource_key"), f"job {job_id}.resource_key")
    if _is_city(key):
        raise GateError(f"job {job_id} uses blocked City of Bywater input")
    family = _nonempty_string(job.get("family"), f"job {job_id}.family")
    input_value = job.get("input")
    if not isinstance(input_value, dict):
        raise GateError(f"job {job_id}.input must be an object")
    input_path = _relative_path(
        input_value.get("path"), f"job {job_id}.input.path", ".png"
    )
    decoded_hash = _sha256(
        input_value.get("decoded_png_sha256"),
        f"job {job_id}.input.decoded_png_sha256",
    )
    classic_hash = _sha256(
        input_value.get("classic_payload_sha256"),
        f"job {job_id}.input.classic_payload_sha256",
    )

    manifest_entry = manifest_by_key.get(_key_tuple(key))
    if manifest_entry is None:
        raise GateError(f"job {job_id} ResourceKey is missing from the locked reference manifest")
    decoded = manifest_entry.get("decoded_png")
    if not isinstance(decoded, dict):
        raise GateError(f"job {job_id} locked reference has no decoded_png record")
    if (
        decoded.get("path") != input_path
        or decoded.get("sha256") != decoded_hash
        or manifest_entry.get("classic_payload_sha256") != classic_hash
        or manifest_entry.get("family") != family
    ):
        raise GateError(f"job {job_id} input path, hashes, family, or ResourceKey mismatches the locked reference manifest")

    actual_path = _under(input_dir, input_path, f"job {job_id} Classic input")
    _read_hashed_file(actual_path, decoded_hash, f"job {job_id} Classic input")
    return key, input_path, actual_path, decoded_hash


def _variant_declaration(job: dict[str, Any]) -> tuple[str, dict[str, Any]] | None:
    value = job.get("variant_of")
    if value is None:
        return None
    if not isinstance(value, dict) or set(value) != {"job_id", "resource_key"}:
        raise GateError(
            f"job {job['job_id']}.variant_of must contain exactly job_id and resource_key"
        )
    base_id = value["job_id"]
    if not isinstance(base_id, str) or JOB_ID_RE.fullmatch(base_id) is None:
        raise GateError(f"job {job['job_id']}.variant_of.job_id is invalid")
    return base_id, _resource_key(
        value["resource_key"], f"job {job['job_id']}.variant_of.resource_key"
    )


def _validate_variant_graph(by_id: dict[str, dict[str, Any]]) -> None:
    edges: dict[str, str] = {}
    for job_id, job in by_id.items():
        declaration = _variant_declaration(job)
        if declaration is None:
            continue
        base_id, declared_key = declaration
        base = by_id.get(base_id)
        if base is None:
            raise GateError(f"job {job_id} declares missing base job {base_id!r}")
        actual_key = _resource_key(
            base.get("resource_key"), f"base job {base_id}.resource_key"
        )
        if declared_key != actual_key:
            raise GateError(f"job {job_id} variant_of ResourceKey mismatches base job {base_id}")
        edges[job_id] = base_id

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(job_id: str, trail: list[str]) -> None:
        if job_id in visiting:
            start = trail.index(job_id)
            cycle = trail[start:] + [job_id]
            raise GateError(f"variant_of cycle detected: {' -> '.join(cycle)}")
        if job_id in visited:
            return
        visiting.add(job_id)
        trail.append(job_id)
        base_id = edges.get(job_id)
        if base_id is not None:
            visit(base_id, trail)
        trail.pop()
        visiting.remove(job_id)
        visited.add(job_id)

    for job_id in by_id:
        visit(job_id, [])


def _index_receipts(
    receipts: dict[str, Any], jobs_bytes: bytes
) -> dict[str, dict[str, Any]]:
    jobs_hash = _sha256(receipts.get("jobs_sha256"), "receipts.jobs_sha256")
    if jobs_hash != _sha256_bytes(jobs_bytes):
        raise GateError("receipts refer to a different jobs file")
    entries = receipts.get("entries")
    if not isinstance(entries, list):
        raise GateError("receipts.entries must be an array")
    result: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        where = f"receipts.entries[{index}]"
        if not isinstance(entry, dict):
            raise GateError(f"{where} must be an object")
        job_id = entry.get("job_id")
        if not isinstance(job_id, str) or JOB_ID_RE.fullmatch(job_id) is None:
            raise GateError(f"{where}.job_id is invalid")
        if job_id in result:
            raise GateError(f"receipts duplicate job_id {job_id!r}")
        result[job_id] = entry
    return result


def _approved_base_reference(
    *,
    root: Path,
    base_job: dict[str, Any],
    receipt: dict[str, Any] | None,
    call_started_at: datetime,
) -> tuple[dict[str, Any], Path]:
    base_id = base_job["job_id"]
    if receipt is None:
        raise GateError(f"base job {base_id} has no generation receipt")
    if receipt.get("input") != base_job.get("input"):
        raise GateError(f"base job {base_id} receipt input mismatches its locked job")
    receipt_key = receipt.get("resource_key")
    base_key = _resource_key(
        base_job.get("resource_key"), f"base job {base_id}.resource_key"
    )
    if receipt_key is not None and _resource_key(
        receipt_key, f"base receipt {base_id}.resource_key"
    ) != base_key:
        raise GateError(f"base job {base_id} receipt ResourceKey mismatches its job")
    if _is_city(base_key):
        raise GateError(f"base job {base_id} uses blocked City of Bywater content")
    if receipt.get("review_status") != "approved":
        raise GateError(f"base job {base_id} is not approved")
    _nonempty_string(receipt.get("reviewer"), f"base receipt {base_id}.reviewer")
    reviewed_at = _parse_time(
        receipt.get("reviewed_at"), f"base receipt {base_id}.reviewed_at"
    )
    if reviewed_at >= call_started_at:
        raise GateError(
            f"base job {base_id} reviewed_at was not before call_started_at"
        )
    provenance = receipt.get("model_provenance")
    if not isinstance(provenance, dict):
        raise GateError(f"base receipt {base_id}.model_provenance is missing")
    completed_at = _parse_time(
        provenance.get("completed_at"),
        f"base receipt {base_id}.model_provenance.completed_at",
    )
    if reviewed_at < completed_at:
        raise GateError(f"base job {base_id} approval predates generation completion")

    evidence = receipt.get("policy_evidence")
    if not isinstance(evidence, dict):
        raise GateError(f"base receipt {base_id}.policy_evidence is missing")
    ocr = evidence.get("ocr")
    if not isinstance(ocr, dict):
        raise GateError(f"base receipt {base_id} OCR evidence is missing")
    if (
        ocr.get("performed") is not True
        or ocr.get("result") != "no-text-detected"
    ):
        raise GateError(f"base job {base_id} lacks approved OCR evidence")
    _nonempty_string(ocr.get("method"), f"base receipt {base_id}.policy_evidence.ocr.method")
    evidence_path_text = _relative_path(
        ocr.get("evidence_path"),
        f"base receipt {base_id}.policy_evidence.ocr.evidence_path",
    )
    evidence_hash = _sha256(
        ocr.get("evidence_sha256"),
        f"base receipt {base_id}.policy_evidence.ocr.evidence_sha256",
    )
    _read_hashed_file(
        _under(root, evidence_path_text, f"base job {base_id} OCR evidence"),
        evidence_hash,
        f"base job {base_id} OCR evidence",
    )
    lighting = evidence.get("baked_lighting")
    if not isinstance(lighting, dict):
        raise GateError(f"base receipt {base_id} baked-lighting evidence is missing")
    if (
        lighting.get("reviewed") is not True
        or lighting.get("result") != "no-unintended-baked-lighting"
    ):
        raise GateError(f"base job {base_id} lacks approved baked-lighting evidence")
    _nonempty_string(
        lighting.get("method"),
        f"base receipt {base_id}.policy_evidence.baked_lighting.method",
    )

    output = receipt.get("output")
    if not isinstance(output, dict):
        raise GateError(f"base receipt {base_id}.output is missing")
    path_text = _relative_path(
        output.get("path"), f"base receipt {base_id}.output.path", ".png"
    )
    if path_text != base_job.get("expected_output_path"):
        raise GateError(f"base job {base_id} output path mismatches its locked job")
    output_hash = _sha256(
        output.get("sha256"), f"base receipt {base_id}.output.sha256"
    )
    output_path = _under(root, path_text, f"base job {base_id} approved output")
    _read_hashed_file(output_path, output_hash, f"base job {base_id} approved output")
    return {
        "path": path_text,
        "resource_key": base_key,
        "role": BASE_MASTER_ROLE,
        "sha256": output_hash,
    }, output_path


def prepare_call(
    root: Path,
    jobs_path: Path,
    receipts_path: Path,
    input_dir: Path,
    policy_path: Path,
    job_id: str,
    call_started_at_text: str,
    classification_path: Path | None = None,
) -> dict[str, Any]:
    """Validate and return the exact ordered reference contract for one call."""
    root = root.resolve()
    if JOB_ID_RE.fullmatch(job_id) is None:
        raise GateError("job_id is invalid")
    call_started_at = _parse_time(call_started_at_text, "call_started_at")
    policy = _load_optional_policy(root, policy_path)
    allowed_families = _allowed_families(policy)

    jobs, jobs_bytes = _read_json(jobs_path, "generation jobs")
    by_id, _ = _index_jobs(jobs)
    _validate_variant_graph(by_id)
    if classification_path is None:
        classification_path = root / DEFAULT_CLASSIFICATIONS
    classifications, classification_manifest_sha256 = _load_item_classifications(
        root,
        classification_path,
        jobs,
        jobs_bytes,
        by_id,
        allowed_families,
        policy,
    )
    receipts, _ = _read_json(receipts_path, "generation receipts")
    receipt_by_id = _index_receipts(receipts, jobs_bytes)
    job = by_id.get(job_id)
    if job is None:
        raise GateError(f"unknown generation job {job_id!r}")
    if job.get("family") not in allowed_families:
        raise GateError(f"job {job_id} is not an item-family generation job")
    classification = classifications[job_id]
    if classification["generation_allowed"] is not True:
        raise GateError(
            f"job {job_id} classification {classification['classification']} blocks generation: "
            f"{classification.get('blocked_reason', 'generation is not authorized')}"
        )

    manifest = _load_reference_manifest(root, jobs)
    manifest_by_key = _index_manifest(manifest)
    own_key, own_path_text, own_path, own_hash = _validate_job_input(
        job,
        manifest_by_key=manifest_by_key,
        input_dir=input_dir,
    )
    references: list[dict[str, Any]] = [
        {
            "path": own_path_text,
            "resource_key": own_key,
            "role": OWN_CLASSIC_ROLE,
            "sha256": own_hash,
        }
    ]
    exact_paths = [str(own_path)]

    declaration = _variant_declaration(job)
    known_for_target = [
        item
        for item in _known_relationships(policy)
        if item["variant_resource_key"] == own_key
    ]
    rules = policy.get("rules", {}) if policy is not None else {}
    if (
        declaration is None
        and known_for_target
        and rules.get("unclassified_item_generation_allowed") is False
    ):
        raise GateError(
            f"job {job_id} is a known variant candidate but has no explicit variant_of classification"
        )
    if declaration is not None:
        base_id, _ = declaration
        base_job = by_id[base_id]
        if base_job.get("family") not in allowed_families:
            raise GateError(f"base job {base_id} is not an item-family generation job")
        base_key = _resource_key(
            base_job.get("resource_key"), f"base job {base_id}.resource_key"
        )
        if known_for_target and not any(
            item["base_resource_key"] == base_key for item in known_for_target
        ):
            raise GateError(
                f"job {job_id} variant_of conflicts with its known base ResourceKey"
            )
        # Validate the base's Classic identity too: its approved master cannot be
        # detached from the locked source resource it remasters.
        _validate_job_input(
            base_job,
            manifest_by_key=manifest_by_key,
            input_dir=input_dir,
        )
        base_reference, base_path = _approved_base_reference(
            root=root,
            base_job=base_job,
            receipt=receipt_by_id.get(base_id),
            call_started_at=call_started_at,
        )
        references.append(base_reference)
        exact_paths.append(str(base_path))

    expected_roles = (
        [OWN_CLASSIC_ROLE, BASE_MASTER_ROLE]
        if declaration is not None
        else [OWN_CLASSIC_ROLE]
    )
    if [item["role"] for item in references] != expected_roles:
        raise GateError("internal reference role order invariant failed")
    if classification.get("reference_roles") != expected_roles:
        raise GateError("item classification reference roles disagree with the exact emitted set")
    return {
        "call_gate_kind": REFERENCE_SET_KIND,
        "call_started_at": call_started_at_text,
        "classification_manifest_sha256": classification_manifest_sha256,
        "generation_references": references,
        "item_classification": classification,
        "job_id": job_id,
        "reference_set_schema_version": REFERENCE_SET_SCHEMA_VERSION,
        "reference_set_sha256": _sha256_bytes(
            _canonical_reference_bytes(references)
        ),
        "referenced_image_paths": exact_paths,
    }


def _resolve_from_root(root: Path, value: str, where: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path.resolve()
    return _under(root, _relative_path(value, where), where)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--jobs", default=DEFAULT_JOBS)
    parser.add_argument("--receipts", default=DEFAULT_RECEIPTS)
    parser.add_argument("--input-dir", default=DEFAULT_INPUTS)
    parser.add_argument("--policy", default=DEFAULT_POLICY)
    parser.add_argument("--classifications", default=DEFAULT_CLASSIFICATIONS)
    parser.add_argument("--job-id", required=True)
    parser.add_argument("--call-started-at", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = args.root.resolve()
    try:
        value = prepare_call(
            root=root,
            jobs_path=_resolve_from_root(root, args.jobs, "--jobs"),
            receipts_path=_resolve_from_root(root, args.receipts, "--receipts"),
            input_dir=_resolve_from_root(root, args.input_dir, "--input-dir"),
            policy_path=_resolve_from_root(root, args.policy, "--policy"),
            classification_path=_resolve_from_root(
                root, args.classifications, "--classifications"
            ),
            job_id=args.job_id,
            call_started_at_text=args.call_started_at,
        )
    except GateError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    sys.stdout.buffer.write(_canonical_json_bytes(value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
