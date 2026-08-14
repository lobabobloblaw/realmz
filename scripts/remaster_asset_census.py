#!/usr/bin/env python3
"""Build and validate the phase-one Realmz remaster asset inventory.

This tool intentionally uses only the Python standard library.  It reads the
raw Classic Mac resource forks, so its content hashes do not depend on a
particular resource_dasm build or on decoded image encoder metadata.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
from typing import Any, Iterable


SCHEMA_VERSION = 1
SHA256_HEX_LENGTH = 64
DEFAULT_SCOPE = "assets/remastered/scopes/phase1.json"
DEFAULT_CENSUS = "assets/remastered/scopes/phase1.census.json"
DEFAULT_MANIFEST = "assets/remastered/scopes/phase1.placeholder-manifest.json"


class ValidationError(RuntimeError):
    pass


def _object_without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def load_json(path: Path) -> tuple[Any, bytes]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read {path}: {exc}") from exc
    try:
        parsed = json.loads(
            data.decode("utf-8"), object_pairs_hook=_object_without_duplicate_keys
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"invalid UTF-8 JSON in {path}: {exc}") from exc
    return parsed, data


def canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ValidationError(f"cannot hash {path}: {exc}") from exc
    return digest.hexdigest()


def require_object(value: Any, where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValidationError(f"{where} must be an object")
    return value


def require_list(value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValidationError(f"{where} must be an array")
    return value


def require_exact_keys(value: dict[str, Any], keys: Iterable[str], where: str) -> None:
    expected = set(keys)
    actual = set(value)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing {missing}")
        if extra:
            details.append(f"unknown {extra}")
        raise ValidationError(f"{where} has invalid fields: {', '.join(details)}")


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == SHA256_HEX_LENGTH
        and value == value.lower()
        and all(ch in "0123456789abcdef" for ch in value)
    )


def require_sha256(value: Any, where: str) -> str:
    if not is_sha256(value):
        raise ValidationError(f"{where} must be a lowercase SHA-256 hex digest")
    return value


def is_safe_relative_path(value: Any, *, suffix: str | None = None) -> bool:
    if not isinstance(value, str) or not value or "\\" in value or "\x00" in value:
        return False
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return False
    if any(ord(ch) < 0x20 for ch in value):
        return False
    return suffix is None or value.lower().endswith(suffix.lower())


def validate_pack(pack: Any, where: str) -> str:
    if not is_safe_relative_path(pack):
        raise ValidationError(f"{where} must be a safe logical relative path")
    return pack


def validate_resource_type(resource_type: Any, where: str) -> str:
    if not isinstance(resource_type, str):
        raise ValidationError(f"{where} must be a string")
    try:
        encoded = resource_type.encode("mac_roman")
    except UnicodeEncodeError as exc:
        raise ValidationError(f"{where} is not Mac Roman") from exc
    if len(encoded) != 4 or any(byte < 0x20 for byte in encoded):
        raise ValidationError(f"{where} must encode to four printable Mac Roman bytes")
    return resource_type


def key_tuple(key: dict[str, Any]) -> tuple[str, str, int]:
    return (key["pack"], key["type"], key["id"])


def validate_key(value: Any, where: str) -> dict[str, Any]:
    key = require_object(value, where)
    require_exact_keys(key, ("pack", "type", "id"), where)
    validate_pack(key["pack"], f"{where}.pack")
    validate_resource_type(key["type"], f"{where}.type")
    if (
        not isinstance(key["id"], int)
        or isinstance(key["id"], bool)
        or not -32768 <= key["id"] <= 32767
    ):
        raise ValidationError(f"{where}.id must be a signed 16-bit integer")
    return key


def _checked_slice(data: bytes, offset: int, size: int, where: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValidationError(f"{where} is outside the resource fork")
    return data[offset : offset + size]


def _u16(data: bytes, offset: int, where: str) -> int:
    return struct.unpack(">H", _checked_slice(data, offset, 2, where))[0]


def _s16(data: bytes, offset: int, where: str) -> int:
    return struct.unpack(">h", _checked_slice(data, offset, 2, where))[0]


def _u32(data: bytes, offset: int, where: str) -> int:
    return struct.unpack(">I", _checked_slice(data, offset, 4, where))[0]


def parse_resource_fork(data: bytes, source: str) -> list[dict[str, Any]]:
    """Return raw resource records without decoding or rewriting payloads."""
    if len(data) < 16:
        raise ValidationError(f"{source}: resource fork header is truncated")
    data_offset, map_offset, data_length, map_length = struct.unpack_from(">4I", data)
    _checked_slice(data, data_offset, data_length, f"{source}: data area")
    resource_map = _checked_slice(data, map_offset, map_length, f"{source}: map")
    if len(resource_map) < 28:
        raise ValidationError(f"{source}: resource map header is truncated")
    if resource_map[:16] != data[:16]:
        raise ValidationError(f"{source}: resource map header copy does not match")

    type_list_offset = _u16(resource_map, 24, f"{source}: type-list offset")
    name_list_offset = _u16(resource_map, 26, f"{source}: name-list offset")
    type_list = type_list_offset
    name_list = name_list_offset
    type_count = _u16(resource_map, type_list, f"{source}: type count") + 1
    if type_count > 65536:
        raise ValidationError(f"{source}: impossible resource type count")

    records: list[dict[str, Any]] = []
    seen: set[tuple[str, int]] = set()
    for type_index in range(type_count):
        type_entry = type_list + 2 + type_index * 8
        raw_type = _checked_slice(
            resource_map, type_entry, 4, f"{source}: type {type_index}"
        )
        try:
            resource_type = raw_type.decode("mac_roman")
        except UnicodeDecodeError as exc:  # pragma: no cover; Mac Roman is total
            raise ValidationError(f"{source}: invalid resource type") from exc
        count = _u16(resource_map, type_entry + 4, f"{source}: resource count") + 1
        reference_offset = _u16(
            resource_map, type_entry + 6, f"{source}: reference-list offset"
        )
        for resource_index in range(count):
            reference = type_list + reference_offset + resource_index * 12
            resource_id = _s16(resource_map, reference, f"{source}: resource id")
            name_offset = _s16(resource_map, reference + 2, f"{source}: name offset")
            attrs_and_offset = _u32(
                resource_map, reference + 4, f"{source}: attributes/data offset"
            )
            attributes = attrs_and_offset >> 24
            payload_offset = attrs_and_offset & 0x00FFFFFF
            payload_size = _u32(
                data, data_offset + payload_offset, f"{source}: payload size"
            )
            payload = _checked_slice(
                data,
                data_offset + payload_offset + 4,
                payload_size,
                f"{source}: payload {resource_type}:{resource_id}",
            )
            if name_offset == -1:
                name = ""
            else:
                name_position = name_list + name_offset
                name_size = _checked_slice(
                    resource_map, name_position, 1, f"{source}: resource name"
                )[0]
                name = _checked_slice(
                    resource_map,
                    name_position + 1,
                    name_size,
                    f"{source}: resource name",
                ).decode("mac_roman")
            identity = (resource_type, resource_id)
            if identity in seen:
                raise ValidationError(
                    f"{source}: duplicate resource {resource_type}:{resource_id}"
                )
            seen.add(identity)
            records.append(
                {
                    "type": resource_type,
                    "id": resource_id,
                    "name": name,
                    "flags": attributes,
                    "payload": payload,
                }
            )
    return records


def _rect_dimensions(payload: bytes, offset: int, where: str) -> dict[str, int]:
    top, left, bottom, right = struct.unpack(
        ">hhhh", _checked_slice(payload, offset, 8, f"{where}: bounds")
    )
    width = right - left
    height = bottom - top
    if not 0 < width <= 16384 or not 0 < height <= 16384:
        raise ValidationError(f"{where}: invalid logical bounds {width}x{height}")
    return {"height": height, "width": width}


def resource_metadata(resource_type: str, payload: bytes, where: str) -> dict[str, Any]:
    if resource_type == "PICT":
        dimensions = _rect_dimensions(payload, 2, where)
        hotspot = None
        alpha_policy = "opaque_or_embedded_mask"
    elif resource_type == "cicn":
        # A cicn starts with a four-byte unused baseAddr followed by PixMap.
        dimensions = _rect_dimensions(payload, 6, where)
        hotspot = None
        alpha_policy = "original_mask"
    elif resource_type == "crsr":
        pixel_map_offset = _u32(payload, 2, f"{where}: pixel-map offset")
        # Stored offsets point to a handle-sized placeholder immediately before PixMap.
        dimensions = _rect_dimensions(payload, pixel_map_offset + 6, where)
        hotspot_y = _u16(payload, 84, f"{where}: hotspot y")
        hotspot_x = _u16(payload, 86, f"{where}: hotspot x")
        hotspot = {"x": hotspot_x, "y": hotspot_y}
        if hotspot_x >= dimensions["width"] or hotspot_y >= dimensions["height"]:
            raise ValidationError(f"{where}: cursor hotspot is outside its bounds")
        alpha_policy = "original_mask"
    elif resource_type == "ppat":
        pattern_type = _u16(payload, 0, f"{where}: pattern type")
        if pattern_type in (0, 2):
            dimensions = {"height": 8, "width": 8}
        else:
            pixel_map_offset = _u32(payload, 2, f"{where}: pixel-map offset")
            dimensions = _rect_dimensions(payload, pixel_map_offset + 6, where)
        hotspot = None
        alpha_policy = "opaque_tile"
    else:
        raise ValidationError(f"{where}: unsupported selected type {resource_type!r}")
    return {
        "alpha_policy": alpha_policy,
        "anchor": {"x": 0, "y": 0},
        "hotspot": hotspot,
        "logical_dimensions": dimensions,
    }


def validate_scope(scope: Any) -> dict[str, Any]:
    scope = require_object(scope, "scope")
    require_exact_keys(
        scope,
        (
            "description",
            "packs",
            "schema_version",
            "scope",
            "selected_resource_types",
        ),
        "scope",
    )
    if scope["schema_version"] != SCHEMA_VERSION:
        raise ValidationError(f"scope.schema_version must be {SCHEMA_VERSION}")
    if not isinstance(scope["scope"], str) or not scope["scope"]:
        raise ValidationError("scope.scope must be a non-empty string")
    if not isinstance(scope["description"], str) or not scope["description"]:
        raise ValidationError("scope.description must be a non-empty string")
    selected_types = require_list(
        scope["selected_resource_types"], "scope.selected_resource_types"
    )
    if selected_types != ["PICT", "cicn", "crsr", "ppat"]:
        raise ValidationError(
            "phase-one selected_resource_types must be exactly PICT, cicn, crsr, ppat"
        )
    for index, resource_type in enumerate(selected_types):
        validate_resource_type(resource_type, f"scope.selected_resource_types[{index}]")

    packs = require_list(scope["packs"], "scope.packs")
    if len(packs) != 5:
        raise ValidationError("phase-one scope must contain exactly five resource forks")
    seen_packs: set[str] = set()
    city_count = 0
    for index, value in enumerate(packs):
        where = f"scope.packs[{index}]"
        pack = require_object(value, where)
        require_exact_keys(
            pack,
            ("expected_sha256", "pack", "role", "source", "source_version"),
            where,
        )
        pack_name = validate_pack(pack["pack"], f"{where}.pack")
        if pack_name in seen_packs:
            raise ValidationError(f"duplicate scope pack: {pack_name}")
        seen_packs.add(pack_name)
        if not is_safe_relative_path(pack["source"], suffix=".rsrc"):
            raise ValidationError(f"{where}.source must be a safe .rsrc path")
        require_sha256(pack["expected_sha256"], f"{where}.expected_sha256")
        if pack["role"] not in ("core", "tutorial", "city-baseline"):
            raise ValidationError(f"{where}.role is invalid")
        if not isinstance(pack["source_version"], str) or not pack["source_version"]:
            raise ValidationError(f"{where}.source_version must be non-empty")
        if pack["role"] == "city-baseline":
            city_count += 1
            if "7.1.2" in pack["source_version"]:
                raise ValidationError(
                    "the repository City source must not be labeled as Mac 7.1.2"
                )
    if city_count != 1:
        raise ValidationError("phase-one scope must contain one city-baseline pack")
    return scope


def semantic_family(pack: str, resource_type: str) -> str:
    if pack == "Data Files/Portraits":
        return "portrait"
    if pack == "Data Files/Tacticals":
        return "tactical_actor"
    if pack.startswith("Scenarios/"):
        return "scenario_illustration" if resource_type == "PICT" else "scenario_actor"
    if resource_type == "PICT":
        return "world_dungeon_or_ui"
    if resource_type == "cicn":
        return "item_monster_spell_effect_or_ui"
    if resource_type == "crsr":
        return "cursor"
    if resource_type == "ppat":
        return "ui_surface"
    raise AssertionError(resource_type)


def build_census(
    root: Path,
    scope: dict[str, Any],
    city_baseline: Path | None,
    city_sha256: str | None,
) -> dict[str, Any]:
    if (city_baseline is None) != (city_sha256 is None):
        raise ValidationError(
            "--city-baseline and --city-sha256 must be supplied together"
        )
    if city_sha256 is not None:
        city_sha256 = require_sha256(city_sha256.lower(), "--city-sha256")

    if city_baseline is not None:
        repository_city_spec = next(
            pack
            for pack in scope["packs"]
            if pack["role"] == "city-baseline"
        )
        repository_city = root / PurePosixPath(repository_city_spec["source"])
        try:
            same_as_repository = city_baseline.resolve() == repository_city.resolve()
        except OSError as exc:
            raise ValidationError(f"cannot resolve --city-baseline: {exc}") from exc
        same_as_repository_hash = city_sha256 == repository_city_spec["expected_sha256"]
        if same_as_repository or same_as_repository_hash:
            raise ValidationError(
                "--city-baseline must differ from the repository City fork by path and SHA-256; "
                "the bundled fork is intentionally version-unverified"
            )

    selected_types = set(scope["selected_resource_types"])
    entries: list[dict[str, Any]] = []
    source_files: list[dict[str, Any]] = []
    city_record: dict[str, Any] | None = None
    for pack_spec in scope["packs"]:
        use_external_city = (
            pack_spec["role"] == "city-baseline" and city_baseline is not None
        )
        if use_external_city:
            source_path = city_baseline
            recorded_source = "external:user-supplied-city-baseline"
            expected_sha256 = city_sha256
            source_kind = "user-supplied-sha256-gated"
            source_version = "Mac 7.1.2 (user-attested)"
        else:
            source_path = root / PurePosixPath(pack_spec["source"])
            recorded_source = pack_spec["source"]
            expected_sha256 = pack_spec["expected_sha256"]
            source_kind = "repository-bundled"
            source_version = pack_spec["source_version"]

        if source_path is None or not source_path.is_file():
            raise ValidationError(f"resource fork does not exist: {source_path}")
        source_data = source_path.read_bytes()
        actual_sha256 = sha256_bytes(source_data)
        if actual_sha256 != expected_sha256:
            raise ValidationError(
                f"SHA-256 mismatch for {source_path}: expected {expected_sha256}, "
                f"got {actual_sha256}"
            )
        records = parse_resource_fork(source_data, str(source_path))
        if use_external_city:
            repository_path = root / PurePosixPath(pack_spec["source"])
            repository_data = repository_path.read_bytes()
            repository_sha256 = sha256_bytes(repository_data)
            if repository_sha256 != pack_spec["expected_sha256"]:
                raise ValidationError(
                    f"SHA-256 mismatch for repository City fork {repository_path}: "
                    f"expected {pack_spec['expected_sha256']}, got {repository_sha256}"
                )
            repository_keys = {
                (record["type"], record["id"])
                for record in parse_resource_fork(repository_data, str(repository_path))
                if record["type"] in selected_types
            }
            external_keys = {
                (record["type"], record["id"])
                for record in records
                if record["type"] in selected_types
            }
            if external_keys != repository_keys:
                missing = sorted(repository_keys - external_keys)
                extra = sorted(external_keys - repository_keys)
                raise ValidationError(
                    "--city-baseline selected resource keys do not match the repository "
                    f"compatibility surface: {len(missing)} missing, {len(extra)} extra"
                )
        selected_count = 0
        for record in records:
            if record["type"] not in selected_types:
                continue
            selected_count += 1
            key = {
                "id": record["id"],
                "pack": pack_spec["pack"],
                "type": record["type"],
            }
            payload_hash = sha256_bytes(record["payload"])
            entry = {
                "classic_payload_bytes": len(record["payload"]),
                "classic_payload_sha256": payload_hash,
                "content_address": f"sha256:{payload_hash}",
                "flags": record["flags"],
                "key": key,
                "source_file_sha256": actual_sha256,
                "source_resource_name": record["name"],
            }
            entry.update(
                resource_metadata(
                    record["type"],
                    record["payload"],
                    f"{pack_spec['pack']}:{record['type']}:{record['id']}",
                )
            )
            entries.append(entry)
        source_files.append(
            {
                "pack": pack_spec["pack"],
                "role": pack_spec["role"],
                "selected_resource_count": selected_count,
                "source": recorded_source,
                "source_kind": source_kind,
                "source_sha256": actual_sha256,
                "source_version": source_version,
            }
        )
        if pack_spec["role"] == "city-baseline":
            city_record = {
                "declared_version": source_version,
                "kind": source_kind,
                "pack": pack_spec["pack"],
                "source_sha256": actual_sha256,
            }

    entries.sort(key=lambda entry: key_tuple(entry["key"]))
    seen_keys: set[tuple[str, str, int]] = set()
    canonical_for_hash: dict[str, dict[str, Any]] = {}
    for entry in entries:
        identity = key_tuple(entry["key"])
        if identity in seen_keys:
            raise ValidationError(f"duplicate ResourceKey in census: {identity}")
        seen_keys.add(identity)
        canonical_for_hash.setdefault(entry["classic_payload_sha256"], entry["key"])
    for entry in entries:
        entry["master_key"] = canonical_for_hash[entry["classic_payload_sha256"]]

    by_pack: dict[str, int] = {}
    by_type: dict[str, int] = {}
    for entry in entries:
        by_pack[entry["key"]["pack"]] = by_pack.get(entry["key"]["pack"], 0) + 1
        by_type[entry["key"]["type"]] = by_type.get(entry["key"]["type"], 0) + 1
    if city_record is None:  # pragma: no cover; scope validation guarantees it
        raise AssertionError("missing City baseline record")
    return {
        "city_baseline": city_record,
        "entries": entries,
        "generated_by": "scripts/remaster_asset_census.py",
        "schema_version": SCHEMA_VERSION,
        "scope": scope["scope"],
        "selected_resource_types": scope["selected_resource_types"],
        "source_files": source_files,
        "statistics": {
            "by_pack": by_pack,
            "by_type": by_type,
            "duplicate_resource_keys": len(entries) - len(canonical_for_hash),
            "resource_keys": len(entries),
            "unique_classic_payloads": len(canonical_for_hash),
        },
    }


def build_placeholder_manifest(census: dict[str, Any], census_bytes: bytes) -> dict[str, Any]:
    entries = []
    for item in census["entries"]:
        entries.append(
            {
                "alpha_policy": item["alpha_policy"],
                "anchor": item["anchor"],
                "asset_path": None,
                "atlas_order": None,
                "classic_payload_sha256": item["classic_payload_sha256"],
                "generation_provenance": {
                    "kind": "none",
                    "model": None,
                    "provider": None,
                },
                "hotspot": item["hotspot"],
                "input_sha256": item["classic_payload_sha256"],
                "key": item["key"],
                "logical_dimensions": item["logical_dimensions"],
                "master_key": item["master_key"],
                "post_processing": [],
                "prompt_sha256": None,
                "reviewer": None,
                "semantic_family": semantic_family(
                    item["key"]["pack"], item["key"]["type"]
                ),
                "shared_master_sha256": item["classic_payload_sha256"],
                "status": "classic_passthrough",
            }
        )
    return {
        "census_sha256": sha256_bytes(census_bytes),
        "city_baseline": census["city_baseline"],
        "entries": entries,
        "manifest_kind": "zero-cost-placeholder",
        "schema_version": SCHEMA_VERSION,
        "scope": census["scope"],
    }


def _validate_dimensions(value: Any, where: str) -> dict[str, int]:
    dimensions = require_object(value, where)
    require_exact_keys(dimensions, ("height", "width"), where)
    for name in ("height", "width"):
        coordinate = dimensions[name]
        if (
            not isinstance(coordinate, int)
            or isinstance(coordinate, bool)
            or not 0 < coordinate <= 16384
        ):
            raise ValidationError(f"{where}.{name} must be an integer in 1..16384")
    return dimensions


def _validate_point(value: Any, where: str, *, allow_null: bool = False) -> dict[str, int] | None:
    if value is None and allow_null:
        return None
    point = require_object(value, where)
    require_exact_keys(point, ("x", "y"), where)
    for name in ("x", "y"):
        if not isinstance(point[name], int) or isinstance(point[name], bool):
            raise ValidationError(f"{where}.{name} must be an integer")
    return point


def validate_census_shape(census: Any) -> dict[str, Any]:
    census = require_object(census, "census")
    require_exact_keys(
        census,
        (
            "city_baseline",
            "entries",
            "generated_by",
            "schema_version",
            "scope",
            "selected_resource_types",
            "source_files",
            "statistics",
        ),
        "census",
    )
    if census["schema_version"] != SCHEMA_VERSION:
        raise ValidationError(f"census.schema_version must be {SCHEMA_VERSION}")
    if not isinstance(census["scope"], str) or not census["scope"]:
        raise ValidationError("census.scope must be non-empty")
    entries = require_list(census["entries"], "census.entries")
    previous: tuple[str, str, int] | None = None
    identities: set[tuple[str, str, int]] = set()
    hashes: dict[str, tuple[str, str, int]] = {}
    for index, item_value in enumerate(entries):
        where = f"census.entries[{index}]"
        item = require_object(item_value, where)
        require_exact_keys(
            item,
            (
                "alpha_policy",
                "anchor",
                "classic_payload_bytes",
                "classic_payload_sha256",
                "content_address",
                "flags",
                "hotspot",
                "key",
                "logical_dimensions",
                "master_key",
                "source_file_sha256",
                "source_resource_name",
            ),
            where,
        )
        key = validate_key(item["key"], f"{where}.key")
        identity = key_tuple(key)
        if previous is not None and identity <= previous:
            raise ValidationError("census entries must be strictly ResourceKey-sorted")
        previous = identity
        if identity in identities:
            raise ValidationError(f"duplicate census ResourceKey: {identity}")
        identities.add(identity)
        digest = require_sha256(
            item["classic_payload_sha256"], f"{where}.classic_payload_sha256"
        )
        require_sha256(item["source_file_sha256"], f"{where}.source_file_sha256")
        if item["content_address"] != f"sha256:{digest}":
            raise ValidationError(f"{where}.content_address does not match its payload")
        if (
            not isinstance(item["classic_payload_bytes"], int)
            or item["classic_payload_bytes"] < 0
        ):
            raise ValidationError(f"{where}.classic_payload_bytes is invalid")
        if not isinstance(item["flags"], int) or not 0 <= item["flags"] <= 255:
            raise ValidationError(f"{where}.flags is invalid")
        if not isinstance(item["source_resource_name"], str):
            raise ValidationError(f"{where}.source_resource_name must be a string")
        dimensions = _validate_dimensions(
            item["logical_dimensions"], f"{where}.logical_dimensions"
        )
        _validate_point(item["anchor"], f"{where}.anchor")
        hotspot = _validate_point(item["hotspot"], f"{where}.hotspot", allow_null=True)
        if hotspot is not None and (
            hotspot["x"] < 0
            or hotspot["y"] < 0
            or hotspot["x"] >= dimensions["width"]
            or hotspot["y"] >= dimensions["height"]
        ):
            raise ValidationError(f"{where}.hotspot is outside logical dimensions")
        if item["alpha_policy"] not in (
            "opaque_or_embedded_mask",
            "original_mask",
            "opaque_tile",
        ):
            raise ValidationError(f"{where}.alpha_policy is invalid")
        master_key = validate_key(item["master_key"], f"{where}.master_key")
        hashes.setdefault(digest, identity)
        if key_tuple(master_key) != hashes[digest]:
            raise ValidationError(f"{where}.master_key is not the canonical duplicate master")
    return census


def _asset_path_on_disk(root: Path, value: str) -> Path:
    asset_root = (root / "assets/remastered").resolve()
    candidate = (asset_root / PurePosixPath(value)).resolve()
    try:
        candidate.relative_to(asset_root)
    except ValueError as exc:
        raise ValidationError(f"asset path escapes assets/remastered: {value!r}") from exc
    return candidate


def validate_manifest(
    root: Path, manifest: Any, manifest_bytes: bytes, census: dict[str, Any], census_bytes: bytes
) -> dict[str, Any]:
    del manifest_bytes  # Retained in the API for symmetric file validation.
    manifest = require_object(manifest, "manifest")
    require_exact_keys(
        manifest,
        (
            "census_sha256",
            "city_baseline",
            "entries",
            "manifest_kind",
            "schema_version",
            "scope",
        ),
        "manifest",
    )
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise ValidationError(f"manifest.schema_version must be {SCHEMA_VERSION}")
    if manifest["scope"] != census["scope"]:
        raise ValidationError("manifest.scope does not match census.scope")
    if manifest["city_baseline"] != census["city_baseline"]:
        raise ValidationError("manifest.city_baseline does not match the census")
    if manifest["manifest_kind"] not in ("zero-cost-placeholder", "production"):
        raise ValidationError("manifest.manifest_kind is invalid")
    expected_census_sha = sha256_bytes(census_bytes)
    if manifest["census_sha256"] != expected_census_sha:
        raise ValidationError(
            "manifest.census_sha256 does not match the exact census file bytes"
        )

    census_by_key = {key_tuple(item["key"]): item for item in census["entries"]}
    entries = require_list(manifest["entries"], "manifest.entries")
    seen: set[tuple[str, str, int]] = set()
    approved_path_for_master: dict[str, str] = {}
    previous_identity: tuple[str, str, int] | None = None
    for index, entry_value in enumerate(entries):
        where = f"manifest.entries[{index}]"
        entry = require_object(entry_value, where)
        require_exact_keys(
            entry,
            (
                "alpha_policy",
                "anchor",
                "asset_path",
                "atlas_order",
                "classic_payload_sha256",
                "generation_provenance",
                "hotspot",
                "input_sha256",
                "key",
                "logical_dimensions",
                "master_key",
                "post_processing",
                "prompt_sha256",
                "reviewer",
                "semantic_family",
                "shared_master_sha256",
                "status",
            ),
            where,
        )
        key = validate_key(entry["key"], f"{where}.key")
        identity = key_tuple(key)
        if previous_identity is not None and identity <= previous_identity:
            raise ValidationError(
                "manifest entries must be strictly ResourceKey-sorted"
            )
        previous_identity = identity
        if identity in seen:
            raise ValidationError(f"duplicate manifest ResourceKey: {identity}")
        seen.add(identity)
        census_entry = census_by_key.get(identity)
        if census_entry is None:
            raise ValidationError(f"manifest contains out-of-scope ResourceKey: {identity}")
        for field in (
            "alpha_policy",
            "anchor",
            "classic_payload_sha256",
            "hotspot",
            "logical_dimensions",
            "master_key",
        ):
            if entry[field] != census_entry[field]:
                raise ValidationError(f"{where}.{field} does not match the census")
        require_sha256(entry["shared_master_sha256"], f"{where}.shared_master_sha256")
        require_sha256(entry["input_sha256"], f"{where}.input_sha256")
        if entry["input_sha256"] != entry["classic_payload_sha256"]:
            raise ValidationError(f"{where}.input_sha256 must hash the classic payload")
        if not isinstance(entry["semantic_family"], str) or not entry["semantic_family"]:
            raise ValidationError(f"{where}.semantic_family must be non-empty")
        if entry["atlas_order"] is not None and (
            not isinstance(entry["atlas_order"], int)
            or isinstance(entry["atlas_order"], bool)
            or entry["atlas_order"] < 0
        ):
            raise ValidationError(f"{where}.atlas_order must be null or non-negative")
        if not isinstance(entry["post_processing"], list) or not all(
            isinstance(item, str) and item for item in entry["post_processing"]
        ):
            raise ValidationError(f"{where}.post_processing must be strings")
        provenance = require_object(
            entry["generation_provenance"], f"{where}.generation_provenance"
        )
        require_exact_keys(
            provenance,
            ("kind", "model", "provider"),
            f"{where}.generation_provenance",
        )
        if entry["status"] == "classic_passthrough":
            if entry["shared_master_sha256"] != entry["classic_payload_sha256"]:
                raise ValidationError(
                    f"{where}.shared_master_sha256 must hash the Classic passthrough"
                )
            if entry["asset_path"] is not None:
                raise ValidationError(f"{where}.asset_path must be null for passthrough")
            if entry["prompt_sha256"] is not None or entry["reviewer"] is not None:
                raise ValidationError(f"{where} passthrough cannot claim review/generation")
            if provenance != {"kind": "none", "model": None, "provider": None}:
                raise ValidationError(f"{where} passthrough provenance must be none")
        elif entry["status"] == "approved":
            if not is_safe_relative_path(entry["asset_path"], suffix=".png"):
                raise ValidationError(f"{where}.asset_path is unsafe or not PNG")
            disk_path = _asset_path_on_disk(root, entry["asset_path"])
            if not disk_path.is_file():
                raise ValidationError(f"{where}.asset_path does not exist: {disk_path}")
            if sha256_file(disk_path) != entry["shared_master_sha256"]:
                raise ValidationError(
                    f"{where}.asset_path does not match shared_master_sha256"
                )
            require_sha256(entry["prompt_sha256"], f"{where}.prompt_sha256")
            if not isinstance(entry["reviewer"], str) or not entry["reviewer"].strip():
                raise ValidationError(f"{where}.reviewer must be non-empty for approved art")
            if provenance["kind"] != "imagegen" or not all(
                isinstance(provenance[name], str) and provenance[name]
                for name in ("model", "provider")
            ):
                raise ValidationError(f"{where} approved art needs ImageGen provenance")
            previous_path = approved_path_for_master.setdefault(
                entry["shared_master_sha256"], entry["asset_path"]
            )
            if previous_path != entry["asset_path"]:
                raise ValidationError(
                    f"{where} duplicates must reuse one approved master asset_path"
                )
        else:
            raise ValidationError(f"{where}.status is invalid")

    census_keys = set(census_by_key)
    if seen != census_keys:
        missing = sorted(census_keys - seen)
        extra = sorted(seen - census_keys)
        raise ValidationError(
            f"manifest/census parity failure: {len(missing)} missing, {len(extra)} extra"
        )
    if manifest["manifest_kind"] == "zero-cost-placeholder" and any(
        entry["status"] != "classic_passthrough" for entry in entries
    ):
        raise ValidationError("zero-cost-placeholder manifests may not contain overrides")
    return manifest


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        os.chmod(path, 0o644)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def load_and_validate_scope(path: Path) -> dict[str, Any]:
    scope, _ = load_json(path)
    return validate_scope(scope)


def command_generate(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    scope = load_and_validate_scope(root / args.scope)
    census = build_census(root, scope, args.city_baseline, args.city_sha256)
    validate_census_shape(census)
    census_bytes = canonical_json(census)
    manifest = build_placeholder_manifest(census, census_bytes)
    manifest_bytes = canonical_json(manifest)
    validate_manifest(root, manifest, manifest_bytes, census, census_bytes)
    write_atomic(root / args.census, census_bytes)
    write_atomic(root / args.manifest, manifest_bytes)
    stats = census["statistics"]
    print(
        f"generated {args.census}: {stats['resource_keys']} ResourceKeys, "
        f"{stats['unique_classic_payloads']} unique payloads, "
        f"{stats['duplicate_resource_keys']} duplicate reuses"
    )
    print(f"generated {args.manifest}: zero-cost classic passthrough coverage")
    print(
        f"City baseline: {census['city_baseline']['kind']} "
        f"{census['city_baseline']['source_sha256']}"
    )
    return 0


def command_validate(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    scope = load_and_validate_scope(root / args.scope)
    census_path = root / args.census
    manifest_path = root / args.manifest
    census, census_bytes = load_json(census_path)
    validate_census_shape(census)
    expected = build_census(root, scope, args.city_baseline, args.city_sha256)
    if census != expected:
        raise ValidationError(
            "census does not exactly match the selected resource forks; regenerate it"
        )
    if census_bytes != canonical_json(census):
        raise ValidationError("census is not in deterministic canonical JSON form")
    manifest, manifest_bytes = load_json(manifest_path)
    validate_manifest(root, manifest, manifest_bytes, census, census_bytes)
    if manifest_bytes != canonical_json(manifest):
        raise ValidationError("manifest is not in deterministic canonical JSON form")
    stats = census["statistics"]
    print(
        f"validated {args.manifest}: {stats['resource_keys']} covered ResourceKeys, "
        f"{stats['unique_classic_payloads']} masters"
    )
    return 0


def command_self_test(_: argparse.Namespace) -> int:
    # Validate path and identity invariants without repository or third-party modules.
    safe_paths = ("generated/ab/cd.png", "art/City of Bywater/32128.png")
    unsafe_paths = ("", "/absolute.png", "../escape.png", "a/../b.png", "a\\b.png")
    assert all(is_safe_relative_path(path, suffix=".png") for path in safe_paths)
    assert not any(is_safe_relative_path(path, suffix=".png") for path in unsafe_paths)
    assert key_tuple(
        validate_key(
            {"pack": "Scenarios/Tutorial/Scenario", "type": "PICT", "id": 32128},
            "test key",
        )
    ) == ("Scenarios/Tutorial/Scenario", "PICT", 32128)
    try:
        _object_without_duplicate_keys([("a", 1), ("a", 2)])
    except ValidationError:
        pass
    else:  # pragma: no cover
        raise AssertionError("duplicate-key rejection did not run")
    try:
        build_census(Path("."), {"selected_resource_types": [], "packs": []}, Path("x"), None)
    except ValidationError:
        pass
    else:  # pragma: no cover
        raise AssertionError("City SHA pairing gate did not run")
    print("remaster_asset_census.py self-test passed")
    return 0


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--scope", default=DEFAULT_SCOPE)
    parser.add_argument("--census", default=DEFAULT_CENSUS)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--city-baseline",
        type=Path,
        help="user-owned Mac City of Bywater 7.1.2 Scenario resource fork",
    )
    parser.add_argument(
        "--city-sha256",
        help="required expected SHA-256 for --city-baseline; mismatch is fatal",
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate", help="write census and placeholder manifest")
    add_common_arguments(generate)
    generate.set_defaults(function=command_generate)
    validate = subparsers.add_parser("validate", help="strictly validate checked-in metadata")
    add_common_arguments(validate)
    validate.set_defaults(function=command_validate)
    self_test = subparsers.add_parser("self-test", help="run dependency-free invariant tests")
    self_test.set_defaults(function=command_self_test)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return args.function(args)
    except ValidationError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
