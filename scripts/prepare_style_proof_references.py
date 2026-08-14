#!/usr/bin/env python3
"""Prepare the deterministic 24-asset Classic style-proof reference set.

This script does not call an image model.  It validates the committed,
pack-aware selection against the raw-resource census, decodes each selected
Classic resource with the pinned resource_dasm tool, normalizes the result to
a deterministic RGBA PNG, and builds a labeled contact sheet.
"""

from __future__ import annotations

import argparse
import binascii
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any
import zlib


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import remaster_asset_census as census_lib  # noqa: E402


SCHEMA_VERSION = 1
SELECTION_KIND = "classic-style-proof-reference-selection"
OUTPUT_KIND = "classic-style-proof-references"
DEFAULT_SELECTION = "assets/remastered/style-proof/classic-selection.json"
OUTPUT_MANIFEST = "classic-reference-manifest.json"
CONTACT_SHEET = "classic-contact-sheet.png"
GENERATION_INPUT_MANIFEST = "generation-input-manifest.json"
RESOURCE_DASM_COMMIT = "27f64c89a5fed855e68c2a5e97b6c6c389d8eb19"
MAX_PNG_FILE_BYTES = 64 * 1024 * 1024
MAX_DECODED_PNG_BYTES = 128 * 1024 * 1024
CITY_PACK = "Scenarios/City of Bywater/Scenario"
TUTORIAL_PACK = "Scenarios/Tutorial/Scenario"
CITY_NOTICE = (
    "Repository-bundled City of Bywater, version unverified; "
    "not the Mac 7.1.2 baseline."
)
EXPECTED_FAMILY_COUNTS = {
    "item_spell": 4,
    "portrait": 4,
    "tactical_actor": 4,
    "tutorial_city": 4,
    "ui_material": 4,
    "world_dungeon": 4,
}

ValidationError = census_lib.ValidationError


@dataclass(frozen=True)
class RGBAImage:
    width: int
    height: int
    pixels: bytes


@dataclass(frozen=True)
class SelectionContext:
    root: Path
    selection_path: Path
    selection: dict[str, Any]
    selection_bytes: bytes
    census_path: Path
    census: dict[str, Any]
    census_bytes: bytes
    census_entries: dict[tuple[str, str, int], dict[str, Any]]
    sources: dict[str, dict[str, Any]]


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_exact_keys(value: dict[str, Any], keys: tuple[str, ...], where: str) -> None:
    census_lib.require_exact_keys(value, keys, where)


def _key_tuple(key: dict[str, Any]) -> tuple[str, str, int]:
    return (key["pack"], key["type"], key["id"])


def _load_selection(root: Path, selection_path: Path) -> SelectionContext:
    selection_value, selection_bytes = census_lib.load_json(selection_path)
    selection = census_lib.require_object(selection_value, "style-proof selection")
    if selection_bytes != census_lib.canonical_json(selection):
        raise ValidationError("style-proof selection must use canonical sorted JSON")
    _require_exact_keys(
        selection,
        (
            "city_reference_notice",
            "city_reference_policy",
            "decoder",
            "description",
            "entries",
            "family_counts",
            "generated_assets",
            "reference_text_policy",
            "schema_version",
            "selection_kind",
            "source_census",
            "source_census_sha256",
        ),
        "style-proof selection",
    )
    if selection["schema_version"] != SCHEMA_VERSION:
        raise ValidationError(f"selection.schema_version must be {SCHEMA_VERSION}")
    if selection["selection_kind"] != SELECTION_KIND:
        raise ValidationError(f"selection.selection_kind must be {SELECTION_KIND!r}")
    if selection["generated_assets"] is not False:
        raise ValidationError("selection.generated_assets must be false")
    if not isinstance(selection["description"], str) or not selection["description"]:
        raise ValidationError("selection.description must be non-empty")
    if selection["city_reference_notice"] != CITY_NOTICE:
        raise ValidationError(
            "selection.city_reference_notice must explicitly identify repository City "
            "as version-unverified and not Mac 7.1.2"
        )
    if selection["city_reference_policy"] != {
        "allowed_use": "classic-contact-sheet-and-audit-only",
        "generation_eligible": False,
        "required_replacement": (
            "authorized Mac 7.1.2 user-supplied-sha256-gated baseline"
        ),
    }:
        raise ValidationError(
            "selection.city_reference_policy must make repository City "
            "ineligible for generation"
        )

    decoder = census_lib.require_object(selection["decoder"], "selection.decoder")
    _require_exact_keys(
        decoder,
        ("expected_executable_sha256", "expected_source_commit", "name"),
        "selection.decoder",
    )
    census_lib.require_sha256(
        decoder["expected_executable_sha256"],
        "selection.decoder.expected_executable_sha256",
    )
    if (
        decoder["expected_source_commit"] != RESOURCE_DASM_COMMIT
        or decoder["name"] != "resource_dasm"
    ):
        raise ValidationError("selection.decoder does not match the reviewed resource_dasm pin")

    if selection["family_counts"] != EXPECTED_FAMILY_COUNTS:
        raise ValidationError(
            "selection.family_counts must contain exactly four references in each proof family"
        )
    if not census_lib.is_safe_relative_path(selection["source_census"], suffix=".json"):
        raise ValidationError("selection.source_census must be a safe relative JSON path")
    expected_census_hash = census_lib.require_sha256(
        selection["source_census_sha256"], "selection.source_census_sha256"
    )
    census_path = root / PurePosixPath(selection["source_census"])
    census_value, census_bytes = census_lib.load_json(census_path)
    if _sha256_bytes(census_bytes) != expected_census_hash:
        raise ValidationError("selection source census SHA-256 does not match")
    census = census_lib.validate_census_shape(census_value)

    city_baseline = census_lib.require_object(census["city_baseline"], "census.city_baseline")
    expected_city = {
        "declared_version": "repository-bundled-unverified-version",
        "kind": "repository-bundled",
        "pack": CITY_PACK,
        "source_sha256": "d1b530968d5c189b05778b7b2abbf7d89b5491a960b872b6e419f6b9b502f8ec",
    }
    if city_baseline != expected_city:
        raise ValidationError(
            "the committed style-proof selection may only use the explicitly "
            "repository-bundled, version-unverified City source"
        )

    source_values = census_lib.require_list(census["source_files"], "census.source_files")
    sources: dict[str, dict[str, Any]] = {}
    for index, source_value in enumerate(source_values):
        source = census_lib.require_object(source_value, f"census.source_files[{index}]")
        sources[source["pack"]] = source
    census_entries = {
        _key_tuple(item["key"]): item
        for item in census_lib.require_list(census["entries"], "census.entries")
    }

    entries = census_lib.require_list(selection["entries"], "selection.entries")
    if len(entries) != 24:
        raise ValidationError("selection.entries must contain exactly 24 references")
    family_counts: Counter[str] = Counter()
    identities: set[tuple[str, str, int]] = set()
    payloads: set[str] = set()
    tutorial_keys = 0
    city_keys = 0
    for index, entry_value in enumerate(entries):
        where = f"selection.entries[{index}]"
        entry = census_lib.require_object(entry_value, where)
        _require_exact_keys(
            entry,
            (
                "classic_payload_sha256",
                "expected_decoded_png_sha256",
                "expected_raw_decoder_png_sha256",
                "family",
                "key",
                "label",
                "order",
                "source_role",
            ),
            where,
        )
        if entry["order"] != index + 1:
            raise ValidationError("selection entries must be ordered contiguously from 1 to 24")
        if entry["family"] not in EXPECTED_FAMILY_COUNTS:
            raise ValidationError(f"{where}.family is invalid")
        family_counts[entry["family"]] += 1
        label = entry["label"]
        if (
            not isinstance(label, str)
            or not label
            or len(label) > 40
            or any(ord(ch) < 0x20 or ord(ch) > 0x7E for ch in label)
        ):
            raise ValidationError(f"{where}.label must be 1..40 printable ASCII characters")
        key = census_lib.validate_key(entry["key"], f"{where}.key")
        identity = _key_tuple(key)
        if identity in identities:
            raise ValidationError(f"duplicate style-proof ResourceKey: {identity}")
        identities.add(identity)
        payload_hash = census_lib.require_sha256(
            entry["classic_payload_sha256"], f"{where}.classic_payload_sha256"
        )
        census_lib.require_sha256(
            entry["expected_decoded_png_sha256"],
            f"{where}.expected_decoded_png_sha256",
        )
        census_lib.require_sha256(
            entry["expected_raw_decoder_png_sha256"],
            f"{where}.expected_raw_decoder_png_sha256",
        )
        if payload_hash in payloads:
            raise ValidationError("style-proof selection must not repeat a duplicate master")
        payloads.add(payload_hash)
        census_entry = census_entries.get(identity)
        if census_entry is None:
            raise ValidationError(f"selected ResourceKey is absent from the census: {identity}")
        if census_entry["classic_payload_sha256"] != payload_hash:
            raise ValidationError(f"selected payload hash is stale for {identity}")
        if census_entry["master_key"] != key:
            raise ValidationError(f"style-proof selection must use the canonical master for {identity}")

        pack, resource_type, _ = identity
        expected_role: str
        if entry["family"] == "ui_material":
            valid = pack == "Data Files/The Family Jewels" and resource_type == "ppat"
            expected_role = "core-repository-bundled"
        elif entry["family"] == "portrait":
            valid = pack == "Data Files/Portraits" and resource_type == "cicn"
            expected_role = "core-repository-bundled"
        elif entry["family"] == "tactical_actor":
            valid = pack == "Data Files/Tacticals" and resource_type == "cicn"
            expected_role = "core-repository-bundled"
        elif entry["family"] == "item_spell":
            valid = pack == "Data Files/The Family Jewels" and resource_type == "cicn"
            expected_role = "core-repository-bundled"
        elif entry["family"] == "world_dungeon":
            valid = (
                pack == "Data Files/The Family Jewels"
                and resource_type in ("PICT", "cicn")
            )
            expected_role = "core-repository-bundled"
        else:
            valid = pack in (TUTORIAL_PACK, CITY_PACK) and resource_type in ("PICT", "cicn")
            if pack == TUTORIAL_PACK:
                tutorial_keys += 1
                expected_role = "tutorial-repository-bundled"
            else:
                city_keys += 1
                expected_role = "city-repository-bundled-unverified"
        if not valid:
            raise ValidationError(f"{identity} is not valid for family {entry['family']!r}")
        if entry["source_role"] != expected_role:
            raise ValidationError(f"{where}.source_role must be {expected_role!r}")
        source = sources.get(pack)
        if source is None or source["source_kind"] != "repository-bundled":
            raise ValidationError(f"{identity} is not backed by a repository-bundled source")

    if dict(family_counts) != EXPECTED_FAMILY_COUNTS:
        raise ValidationError("actual style-proof family counts do not match family_counts")
    if tutorial_keys != 1 or city_keys != 3:
        raise ValidationError(
            "tutorial_city must contain one Tutorial and three repository City audit references"
        )

    scenario_shapes = Counter(
        (entry["key"]["pack"], entry["key"]["type"])
        for entry in entries
        if entry["family"] == "tutorial_city"
    )
    if scenario_shapes != Counter(
        {
            (TUTORIAL_PACK, "PICT"): 1,
            (CITY_PACK, "PICT"): 1,
            (CITY_PACK, "cicn"): 2,
        }
    ):
        raise ValidationError(
            "Tutorial/City references must contain the Tutorial PICT, City PICT, "
            "and the two City Hydra facings"
        )

    world_shapes = Counter(
        entry["key"]["type"]
        for entry in entries
        if entry["family"] == "world_dungeon"
    )
    if world_shapes != Counter({"PICT": 1, "cicn": 3}):
        raise ValidationError(
            "world_dungeon must contain one background PICT and three focused cicn subjects"
        )

    text_policy_values = census_lib.require_list(
        selection["reference_text_policy"], "selection.reference_text_policy"
    )
    expected_text_keys = {
        (TUTORIAL_PACK, "PICT", 32128),
        (CITY_PACK, "PICT", 32128),
    }
    actual_text_keys: set[tuple[str, str, int]] = set()
    for index, policy_value in enumerate(text_policy_values):
        where = f"selection.reference_text_policy[{index}]"
        policy = census_lib.require_object(policy_value, where)
        _require_exact_keys(policy, ("key", "policy"), where)
        key = census_lib.validate_key(policy["key"], f"{where}.key")
        if policy["policy"] != "remove_reference_text-and-render-code-native":
            raise ValidationError(f"{where}.policy is invalid")
        identity = _key_tuple(key)
        if identity in actual_text_keys:
            raise ValidationError(f"duplicate reference text policy for {identity}")
        actual_text_keys.add(identity)
    if actual_text_keys != expected_text_keys:
        raise ValidationError(
            "reference_text_policy must cover exactly the Tutorial and City title PICTs"
        )

    return SelectionContext(
        root=root,
        selection_path=selection_path,
        selection=selection,
        selection_bytes=selection_bytes,
        census_path=census_path,
        census=census,
        census_bytes=census_bytes,
        census_entries=census_entries,
        sources=sources,
    )


def _png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", binascii.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def encode_png(image: RGBAImage) -> bytes:
    if image.width <= 0 or image.height <= 0:
        raise ValidationError("cannot encode an empty image")
    if len(image.pixels) != image.width * image.height * 4:
        raise ValidationError("RGBA pixel buffer has the wrong size")
    row_bytes = image.width * 4
    scanlines = bytearray()
    for y in range(image.height):
        scanlines.append(0)
        start = y * row_bytes
        scanlines.extend(image.pixels[start : start + row_bytes])
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 6, 0, 0, 0)
    return b"".join(
        (
            b"\x89PNG\r\n\x1a\n",
            _png_chunk(b"IHDR", header),
            _png_chunk(b"sRGB", b"\x00"),
            _png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)),
            _png_chunk(b"IEND", b""),
        )
    )


def _paeth(left: int, above: int, upper_left: int) -> int:
    prediction = left + above - upper_left
    distance_left = abs(prediction - left)
    distance_above = abs(prediction - above)
    distance_upper_left = abs(prediction - upper_left)
    if distance_left <= distance_above and distance_left <= distance_upper_left:
        return left
    if distance_above <= distance_upper_left:
        return above
    return upper_left


def decode_png(
    data: bytes,
    where: str = "PNG",
    expected_dimensions: tuple[int, int] | None = None,
) -> RGBAImage:
    if len(data) > MAX_PNG_FILE_BYTES:
        raise ValidationError(f"{where}: PNG exceeds the decoder input-size cap")
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValidationError(f"{where}: invalid PNG signature")
    offset = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    saw_iend = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValidationError(f"{where}: truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + size
        if payload_end + 4 > len(data):
            raise ValidationError(f"{where}: PNG chunk exceeds file bounds")
        payload = data[payload_start:payload_end]
        expected_crc = struct.unpack_from(">I", data, payload_end)[0]
        actual_crc = binascii.crc32(chunk_type + payload) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise ValidationError(f"{where}: PNG chunk CRC mismatch")
        offset = payload_end + 4
        if chunk_type == b"IHDR":
            if width is not None or size != 13:
                raise ValidationError(f"{where}: invalid duplicate or malformed IHDR")
            width, height, bit_depth, color_type, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", payload)
            )
            if (
                not 0 < width <= 16384
                or not 0 < height <= 16384
                or bit_depth != 8
                or color_type not in (2, 6)
                or compression != 0
                or filtering != 0
                or interlace != 0
            ):
                raise ValidationError(f"{where}: unsupported PNG format")
            if expected_dimensions is not None and (width, height) != expected_dimensions:
                raise ValidationError(
                    f"{where}: PNG dimensions are {width}x{height}; "
                    f"expected {expected_dimensions[0]}x{expected_dimensions[1]}"
                )
        elif chunk_type == b"IDAT":
            idat.extend(payload)
        elif chunk_type == b"IEND":
            if payload:
                raise ValidationError(f"{where}: malformed IEND")
            saw_iend = True
            break
        elif chunk_type[:1].isupper() and chunk_type not in (b"PLTE",):
            raise ValidationError(f"{where}: unsupported critical PNG chunk {chunk_type!r}")
    if width is None or height is None or not idat or not saw_iend:
        raise ValidationError(f"{where}: PNG is missing required chunks")
    if offset != len(data):
        raise ValidationError(f"{where}: data follows IEND")

    channels = 3 if color_type == 2 else 4
    row_bytes = width * channels
    decompressor = zlib.decompressobj()
    expected_filtered_bytes = (row_bytes + 1) * height
    if expected_filtered_bytes > MAX_DECODED_PNG_BYTES:
        raise ValidationError(f"{where}: decoded PNG exceeds the memory-size cap")
    filtered = decompressor.decompress(bytes(idat), expected_filtered_bytes + 1)
    if len(filtered) > expected_filtered_bytes or decompressor.unconsumed_tail:
        raise ValidationError(f"{where}: decoded PNG byte count exceeds image dimensions")
    filtered += decompressor.flush(expected_filtered_bytes + 1 - len(filtered))
    if (
        len(filtered) > expected_filtered_bytes
        or decompressor.unused_data
        or decompressor.unconsumed_tail
        or not decompressor.eof
    ):
        raise ValidationError(f"{where}: invalid compressed PNG stream")
    if len(filtered) != expected_filtered_bytes:
        raise ValidationError(f"{where}: decoded PNG byte count is incorrect")

    rows: list[bytearray] = []
    cursor = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filter_type = filtered[cursor]
        cursor += 1
        encoded = filtered[cursor : cursor + row_bytes]
        cursor += row_bytes
        row = bytearray(row_bytes)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = _paeth(left, above, upper_left)
            else:
                raise ValidationError(f"{where}: unsupported PNG row filter {filter_type}")
            row[index] = (value + predictor) & 0xFF
        rows.append(row)
        previous = row

    if channels == 4:
        pixels = b"".join(rows)
    else:
        rgba = bytearray(width * height * 4)
        destination = 0
        for row in rows:
            for source in range(0, len(row), 3):
                rgba[destination : destination + 3] = row[source : source + 3]
                rgba[destination + 3] = 255
                destination += 4
        pixels = bytes(rgba)
    return RGBAImage(width=width, height=height, pixels=pixels)


FONT_5X7 = {
    " ": ("00000",) * 7,
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
    "/": ("00001", "00010", "00100", "01000", "10000", "00000", "00000"),
    ":": ("00000", "01100", "01100", "00000", "01100", "01100", "00000"),
    "_": ("00000", "00000", "00000", "00000", "00000", "00000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
}


def _new_image(width: int, height: int, color: tuple[int, int, int, int]) -> bytearray:
    return bytearray(bytes(color) * width * height)


def _fill_rect(
    pixels: bytearray,
    canvas_width: int,
    canvas_height: int,
    x: int,
    y: int,
    width: int,
    height: int,
    color: tuple[int, int, int, int],
) -> None:
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(canvas_width, x + width)
    y1 = min(canvas_height, y + height)
    if x0 >= x1 or y0 >= y1:
        return
    row = bytes(color) * (x1 - x0)
    for destination_y in range(y0, y1):
        start = (destination_y * canvas_width + x0) * 4
        pixels[start : start + len(row)] = row


def _resize_nearest(image: RGBAImage, width: int, height: int) -> RGBAImage:
    output = bytearray(width * height * 4)
    for y in range(height):
        source_y = min(image.height - 1, y * image.height // height)
        for x in range(width):
            source_x = min(image.width - 1, x * image.width // width)
            source = (source_y * image.width + source_x) * 4
            destination = (y * width + x) * 4
            output[destination : destination + 4] = image.pixels[source : source + 4]
    return RGBAImage(width=width, height=height, pixels=bytes(output))


def _paste_alpha(
    destination: bytearray,
    destination_width: int,
    destination_height: int,
    source: RGBAImage,
    x: int,
    y: int,
) -> None:
    for source_y in range(source.height):
        destination_y = y + source_y
        if not 0 <= destination_y < destination_height:
            continue
        for source_x in range(source.width):
            destination_x = x + source_x
            if not 0 <= destination_x < destination_width:
                continue
            source_offset = (source_y * source.width + source_x) * 4
            destination_offset = (destination_y * destination_width + destination_x) * 4
            alpha = source.pixels[source_offset + 3]
            if alpha == 255:
                destination[destination_offset : destination_offset + 4] = source.pixels[
                    source_offset : source_offset + 4
                ]
            elif alpha:
                inverse = 255 - alpha
                for channel in range(3):
                    destination[destination_offset + channel] = (
                        source.pixels[source_offset + channel] * alpha
                        + destination[destination_offset + channel] * inverse
                        + 127
                    ) // 255
                destination[destination_offset + 3] = 255


def _draw_text(
    pixels: bytearray,
    canvas_width: int,
    canvas_height: int,
    x: int,
    y: int,
    text: str,
    scale: int,
    color: tuple[int, int, int, int],
) -> None:
    cursor_x = x
    for character in text.upper():
        glyph = FONT_5X7.get(character)
        if glyph is None:
            raise ValidationError(f"contact-sheet label contains unsupported character {character!r}")
        for row_index, row in enumerate(glyph):
            for column_index, enabled in enumerate(row):
                if enabled == "1":
                    _fill_rect(
                        pixels,
                        canvas_width,
                        canvas_height,
                        cursor_x + column_index * scale,
                        y + row_index * scale,
                        scale,
                        scale,
                        color,
                    )
        cursor_x += 6 * scale


FAMILY_DISPLAY = {
    "ui_material": "UI MATERIAL",
    "portrait": "PORTRAIT",
    "tactical_actor": "TACTICAL ACTOR",
    "item_spell": "ITEM / SPELL",
    "world_dungeon": "WORLD / DUNGEON",
    "tutorial_city": "TUTORIAL / CITY",
}


def _source_display(source_role: str) -> str:
    if source_role == "core-repository-bundled":
        return "CORE"
    if source_role == "tutorial-repository-bundled":
        return "TUTORIAL"
    if source_role == "city-repository-bundled-unverified":
        return "CITY-UNVERIFIED"
    raise AssertionError(source_role)


def build_contact_sheet(
    selection_entries: list[dict[str, Any]],
    census_entries: dict[tuple[str, str, int], dict[str, Any]],
    images: list[RGBAImage],
) -> RGBAImage:
    if len(selection_entries) != 24 or len(images) != 24:
        raise ValidationError("contact sheet requires exactly 24 references")
    sheet_width = 1280
    header_height = 132
    cell_width = 320
    cell_height = 240
    sheet_height = header_height + 6 * cell_height
    pixels = _new_image(sheet_width, sheet_height, (24, 21, 19, 255))
    _fill_rect(pixels, sheet_width, sheet_height, 0, 0, sheet_width, header_height, (42, 34, 29, 255))
    _draw_text(
        pixels,
        sheet_width,
        sheet_height,
        28,
        18,
        "REALMZ REMASTERED - CLASSIC STYLE PROOF REFERENCES",
        3,
        (242, 216, 162, 255),
    )
    _draw_text(
        pixels,
        sheet_width,
        sheet_height,
        28,
        54,
        "24 CLASSIC REFERENCES / NO GENERATED ART",
        2,
        (224, 218, 204, 255),
    )
    _draw_text(
        pixels,
        sheet_width,
        sheet_height,
        28,
        82,
        "CITY IS REPOSITORY-BUNDLED - VERSION UNVERIFIED - NOT MAC 7.1.2",
        2,
        (255, 172, 115, 255),
    )

    for index, (selection_entry, image) in enumerate(zip(selection_entries, images)):
        column = index % 4
        row = index // 4
        cell_x = column * cell_width
        cell_y = header_height + row * cell_height
        _fill_rect(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 6,
            cell_y + 6,
            cell_width - 12,
            cell_height - 12,
            (50, 46, 42, 255),
        )
        _fill_rect(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 8,
            cell_y + 8,
            cell_width - 16,
            2,
            (151, 119, 75, 255),
        )
        title = f"{index + 1:02d} {FAMILY_DISPLAY[selection_entry['family']]}"
        _draw_text(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 16,
            cell_y + 16,
            title,
            2,
            (244, 225, 185, 255),
        )

        preview_x = cell_x + 16
        preview_y = cell_y + 38
        preview_width = 288
        preview_height = 142
        for checker_y in range(0, preview_height, 8):
            for checker_x in range(0, preview_width, 8):
                checker_color = (
                    (112, 106, 96, 255)
                    if (checker_x // 8 + checker_y // 8) % 2
                    else (145, 137, 123, 255)
                )
                _fill_rect(
                    pixels,
                    sheet_width,
                    sheet_height,
                    preview_x + checker_x,
                    preview_y + checker_y,
                    min(8, preview_width - checker_x),
                    min(8, preview_height - checker_y),
                    checker_color,
                )
        scale = min(preview_width / image.width, preview_height / image.height)
        if image.width <= 64 and image.height <= 64:
            scale = max(1, min(5, int(scale)))
        target_width = max(1, min(preview_width, int(image.width * scale)))
        target_height = max(1, min(preview_height, int(image.height * scale)))
        resized = _resize_nearest(image, target_width, target_height)
        _paste_alpha(
            pixels,
            sheet_width,
            sheet_height,
            resized,
            preview_x + (preview_width - target_width) // 2,
            preview_y + (preview_height - target_height) // 2,
        )

        label = selection_entry["label"].upper()
        _draw_text(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 16,
            cell_y + 188,
            label,
            1,
            (238, 232, 218, 255),
        )
        key = selection_entry["key"]
        census_entry = census_entries[_key_tuple(key)]
        dimensions = census_entry["logical_dimensions"]
        source_line = (
            f"{_source_display(selection_entry['source_role'])} "
            f"{key['type']}:{key['id']} "
            f"{dimensions['width']}X{dimensions['height']}"
        )
        _draw_text(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 16,
            cell_y + 202,
            source_line,
            1,
            (205, 193, 174, 255),
        )
        _draw_text(
            pixels,
            sheet_width,
            sheet_height,
            cell_x + 16,
            cell_y + 216,
            f"PAYLOAD SHA256 {selection_entry['classic_payload_sha256'][:12]}",
            1,
            (181, 170, 153, 255),
        )
    return RGBAImage(width=sheet_width, height=sheet_height, pixels=bytes(pixels))


def _stable_reference_path(entry: dict[str, Any]) -> str:
    resource_id = entry["key"]["id"]
    id_text = f"m{abs(resource_id)}" if resource_id < 0 else str(resource_id)
    return (
        f"references/{entry['order']:02d}_{entry['family']}_"
        f"{entry['key']['type']}_{id_text}.png"
    )


def _load_resource_records(context: SelectionContext) -> dict[str, dict[tuple[str, int], dict[str, Any]]]:
    records_by_pack: dict[str, dict[tuple[str, int], dict[str, Any]]] = {}
    for pack in sorted({entry["key"]["pack"] for entry in context.selection["entries"]}):
        source = context.sources[pack]
        if not census_lib.is_safe_relative_path(source["source"], suffix=".rsrc"):
            raise ValidationError(f"unsafe resource-fork path for {pack}")
        source_path = context.root / PurePosixPath(source["source"])
        try:
            source_data = source_path.read_bytes()
        except OSError as exc:
            raise ValidationError(f"cannot read {source_path}: {exc}") from exc
        if _sha256_bytes(source_data) != source["source_sha256"]:
            raise ValidationError(f"live source fork hash does not match census for {pack}")
        records = census_lib.parse_resource_fork(source_data, str(source_path))
        records_by_pack[pack] = {
            (record["type"], record["id"]): record for record in records
        }
    return records_by_pack


def _decode_one(
    decoder: Path,
    source_path: Path,
    entry: dict[str, Any],
    expected_dimensions: tuple[int, int],
    temporary_root: Path,
) -> tuple[RGBAImage, str]:
    key = entry["key"]
    work = temporary_root / f"{entry['order']:02d}"
    work.mkdir()
    command = [
        str(decoder),
        "--data-fork",
        "--skip-external-decoders",
        f"--target={key['type']}:{key['id']}",
        "--image-format=png",
        "--save-raw=no",
        "--filename-format=decoded/%t/%i",
        str(source_path),
        ".",
    ]
    completed = subprocess.run(
        command,
        cwd=work,
        check=False,
        capture_output=True,
        text=True,
    )
    diagnostics = completed.stdout + completed.stderr
    if completed.returncode != 0:
        raise ValidationError(
            f"resource_dasm failed for {_key_tuple(key)} with status "
            f"{completed.returncode}: {diagnostics.strip()}"
        )
    if "warning:" in diagnostics.lower():
        raise ValidationError(
            f"resource_dasm warned while decoding {_key_tuple(key)}: {diagnostics.strip()}"
        )
    decoded_path = work / "decoded" / key["type"] / f"{key['id']}.png"
    try:
        decoded_bytes = decoded_path.read_bytes()
    except OSError as exc:
        raise ValidationError(
            f"resource_dasm did not produce the primary PNG for {_key_tuple(key)}"
        ) from exc
    image = decode_png(
        decoded_bytes,
        f"resource_dasm output for {_key_tuple(key)}",
        expected_dimensions,
    )
    return image, _sha256_bytes(decoded_bytes)


def build_references(context: SelectionContext, decoder: Path, output_dir: Path) -> None:
    if output_dir.exists():
        raise ValidationError(f"output directory already exists: {output_dir}")
    decoder = decoder.resolve()
    if not decoder.is_file() or not os.access(decoder, os.X_OK):
        raise ValidationError(f"resource_dasm executable is not runnable: {decoder}")
    decoder_hash = _sha256_file(decoder)
    expected_decoder_hash = context.selection["decoder"]["expected_executable_sha256"]
    if decoder_hash != expected_decoder_hash:
        raise ValidationError(
            "resource_dasm executable SHA-256 does not match the independently locked "
            "reviewed binary"
        )
    records_by_pack = _load_resource_records(context)

    output_parent = output_dir.parent
    output_parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_parent))
    try:
        reference_root = staging / "references"
        reference_root.mkdir()
        built_entries: list[dict[str, Any]] = []
        images: list[RGBAImage] = []
        with tempfile.TemporaryDirectory(prefix="realmz-style-proof-decode-") as temp_name:
            temporary_root = Path(temp_name)
            for selection_entry in context.selection["entries"]:
                key = selection_entry["key"]
                identity = _key_tuple(key)
                census_entry = context.census_entries[identity]
                source = context.sources[key["pack"]]
                source_path = context.root / PurePosixPath(source["source"])
                live_record = records_by_pack[key["pack"]].get((key["type"], key["id"]))
                if live_record is None:
                    raise ValidationError(f"live source fork does not contain {identity}")
                live_payload_hash = _sha256_bytes(live_record["payload"])
                if live_payload_hash != selection_entry["classic_payload_sha256"]:
                    raise ValidationError(f"live payload hash does not match selection for {identity}")

                dimensions = census_entry["logical_dimensions"]
                image, raw_decoder_hash = _decode_one(
                    decoder,
                    source_path,
                    selection_entry,
                    (dimensions["width"], dimensions["height"]),
                    temporary_root,
                )
                if (image.width, image.height) != (
                    dimensions["width"],
                    dimensions["height"],
                ):
                    raise ValidationError(
                        f"decoded dimensions for {identity} are {image.width}x{image.height}; "
                        f"expected {dimensions['width']}x{dimensions['height']}"
                    )
                canonical_png = encode_png(image)
                canonical_hash = _sha256_bytes(canonical_png)
                if canonical_hash != selection_entry["expected_decoded_png_sha256"]:
                    raise ValidationError(
                        f"canonical decoded PNG hash is stale for {identity}; "
                        "review and update the selection lock deliberately"
                    )
                if raw_decoder_hash != selection_entry["expected_raw_decoder_png_sha256"]:
                    raise ValidationError(
                        f"raw resource_dasm PNG hash is stale for {identity}; "
                        "review the pinned decoder output before updating the selection lock"
                    )
                relative_path = _stable_reference_path(selection_entry)
                destination = staging / PurePosixPath(relative_path)
                destination.write_bytes(canonical_png)
                images.append(image)
                built_entries.append(
                    {
                        "alpha_policy": census_entry["alpha_policy"],
                        "anchor": census_entry["anchor"],
                        "classic_payload_bytes": census_entry["classic_payload_bytes"],
                        "classic_payload_sha256": selection_entry["classic_payload_sha256"],
                        "decoded_png": {
                            "height": image.height,
                            "path": relative_path,
                            "sha256": canonical_hash,
                            "width": image.width,
                        },
                        "family": selection_entry["family"],
                        "generated_asset": False,
                        "hotspot": census_entry["hotspot"],
                        "key": key,
                        "label": selection_entry["label"],
                        "logical_dimensions": dimensions,
                        "order": selection_entry["order"],
                        "raw_decoder_png_sha256": raw_decoder_hash,
                        "source_file_sha256": source["source_sha256"],
                        "source_path": source["source"],
                        "source_role": selection_entry["source_role"],
                        "source_version": source["source_version"],
                    }
                )

        contact_sheet = build_contact_sheet(
            context.selection["entries"], context.census_entries, images
        )
        contact_bytes = encode_png(contact_sheet)
        (staging / CONTACT_SHEET).write_bytes(contact_bytes)
        manifest = {
            "artifact_policy": {
                "canonical_png": "RGBA8, sRGB intent, filter-none, zlib-level-9",
                "generated_art": False,
                "model_calls": 0,
            },
            "census_sha256": _sha256_bytes(context.census_bytes),
            "city_reference": {
                "declared_version": context.census["city_baseline"]["declared_version"],
                "kind": context.census["city_baseline"]["kind"],
                "notice": context.selection["city_reference_notice"],
                "source_sha256": context.census["city_baseline"]["source_sha256"],
                "use_policy": context.selection["city_reference_policy"],
            },
            "contact_sheet": {
                "height": contact_sheet.height,
                "path": CONTACT_SHEET,
                "sha256": _sha256_bytes(contact_bytes),
                "width": contact_sheet.width,
            },
            "decoder": {
                "executable_sha256": decoder_hash,
                "expected_source_commit": RESOURCE_DASM_COMMIT,
                "name": "resource_dasm",
            },
            "entries": built_entries,
            "generated_by": "scripts/prepare_style_proof_references.py",
            "manifest_kind": OUTPUT_KIND,
            "reference_text_policy": context.selection["reference_text_policy"],
            "schema_version": SCHEMA_VERSION,
            "selection_sha256": _sha256_bytes(context.selection_bytes),
        }
        (staging / OUTPUT_MANIFEST).write_bytes(census_lib.canonical_json(manifest))
        staging.replace(output_dir)
    except BaseException:
        if staging.exists():
            shutil.rmtree(staging)
        raise


def verify_references(context: SelectionContext, output_dir: Path) -> None:
    records_by_pack = _load_resource_records(context)
    manifest_path = output_dir / OUTPUT_MANIFEST
    manifest_value, _ = census_lib.load_json(manifest_path)
    manifest = census_lib.require_object(manifest_value, "classic reference manifest")
    _require_exact_keys(
        manifest,
        (
            "artifact_policy",
            "census_sha256",
            "city_reference",
            "contact_sheet",
            "decoder",
            "entries",
            "generated_by",
            "manifest_kind",
            "reference_text_policy",
            "schema_version",
            "selection_sha256",
        ),
        "classic reference manifest",
    )
    if manifest["schema_version"] != SCHEMA_VERSION or manifest["manifest_kind"] != OUTPUT_KIND:
        raise ValidationError("classic reference manifest identity is invalid")
    if manifest["generated_by"] != "scripts/prepare_style_proof_references.py":
        raise ValidationError("classic reference manifest generator is invalid")
    if manifest["selection_sha256"] != _sha256_bytes(context.selection_bytes):
        raise ValidationError("classic reference manifest selection hash is stale")
    if manifest["census_sha256"] != _sha256_bytes(context.census_bytes):
        raise ValidationError("classic reference manifest census hash is stale")
    if manifest["artifact_policy"] != {
        "canonical_png": "RGBA8, sRGB intent, filter-none, zlib-level-9",
        "generated_art": False,
        "model_calls": 0,
    }:
        raise ValidationError("classic reference artifact policy is invalid")
    if manifest["reference_text_policy"] != context.selection["reference_text_policy"]:
        raise ValidationError("classic reference text policy is stale")
    if manifest["city_reference"] != {
        "declared_version": "repository-bundled-unverified-version",
        "kind": "repository-bundled",
        "notice": CITY_NOTICE,
        "source_sha256": "d1b530968d5c189b05778b7b2abbf7d89b5491a960b872b6e419f6b9b502f8ec",
        "use_policy": {
            "allowed_use": "classic-contact-sheet-and-audit-only",
            "generation_eligible": False,
            "required_replacement": (
                "authorized Mac 7.1.2 user-supplied-sha256-gated baseline"
            ),
        },
    }:
        raise ValidationError("classic reference City provenance is invalid")
    decoder = census_lib.require_object(manifest["decoder"], "manifest.decoder")
    _require_exact_keys(
        decoder,
        ("executable_sha256", "expected_source_commit", "name"),
        "manifest.decoder",
    )
    decoder_hash = census_lib.require_sha256(
        decoder["executable_sha256"], "manifest.decoder.executable_sha256"
    )
    if (
        decoder_hash != context.selection["decoder"]["expected_executable_sha256"]
        or decoder["name"] != "resource_dasm"
        or decoder["expected_source_commit"] != RESOURCE_DASM_COMMIT
    ):
        raise ValidationError("classic reference decoder provenance is invalid")

    manifest_entries = census_lib.require_list(manifest["entries"], "manifest.entries")
    if len(manifest_entries) != 24:
        raise ValidationError("classic reference manifest must contain exactly 24 entries")
    images: list[RGBAImage] = []
    expected_paths: set[str] = set()
    for index, (selection_entry, built_value) in enumerate(
        zip(context.selection["entries"], manifest_entries)
    ):
        where = f"manifest.entries[{index}]"
        built = census_lib.require_object(built_value, where)
        _require_exact_keys(
            built,
            (
                "alpha_policy",
                "anchor",
                "classic_payload_bytes",
                "classic_payload_sha256",
                "decoded_png",
                "family",
                "generated_asset",
                "hotspot",
                "key",
                "label",
                "logical_dimensions",
                "order",
                "raw_decoder_png_sha256",
                "source_file_sha256",
                "source_path",
                "source_role",
                "source_version",
            ),
            where,
        )
        if built.get("order") != index + 1 or built.get("key") != selection_entry["key"]:
            raise ValidationError(f"{where} does not match the committed selection order")
        if built.get("family") != selection_entry["family"] or built.get("label") != selection_entry["label"]:
            raise ValidationError(f"{where} selection metadata is stale")
        if built.get("source_role") != selection_entry["source_role"]:
            raise ValidationError(f"{where}.source_role is stale")
        if built.get("generated_asset") is not False:
            raise ValidationError(f"{where}.generated_asset must be false")
        if built.get("classic_payload_sha256") != selection_entry["classic_payload_sha256"]:
            raise ValidationError(f"{where}.classic_payload_sha256 is stale")
        key = selection_entry["key"]
        identity = _key_tuple(key)
        census_entry = context.census_entries[identity]
        source = context.sources[key["pack"]]
        expected_projection = {
            "alpha_policy": census_entry["alpha_policy"],
            "anchor": census_entry["anchor"],
            "classic_payload_bytes": census_entry["classic_payload_bytes"],
            "classic_payload_sha256": selection_entry["classic_payload_sha256"],
            "family": selection_entry["family"],
            "generated_asset": False,
            "hotspot": census_entry["hotspot"],
            "key": key,
            "label": selection_entry["label"],
            "logical_dimensions": census_entry["logical_dimensions"],
            "order": selection_entry["order"],
            "source_file_sha256": source["source_sha256"],
            "source_path": source["source"],
            "source_role": selection_entry["source_role"],
            "source_version": source["source_version"],
        }
        for field, expected_value in expected_projection.items():
            if built.get(field) != expected_value:
                raise ValidationError(f"{where}.{field} does not match the locked source data")
        live_record = records_by_pack[key["pack"]].get((key["type"], key["id"]))
        if live_record is None or _sha256_bytes(live_record["payload"]) != selection_entry[
            "classic_payload_sha256"
        ]:
            raise ValidationError(f"{where} no longer matches its live Classic payload")
        raw_decoder_hash = census_lib.require_sha256(
            built.get("raw_decoder_png_sha256"), f"{where}.raw_decoder_png_sha256"
        )
        if raw_decoder_hash != selection_entry["expected_raw_decoder_png_sha256"]:
            raise ValidationError(f"{where}.raw_decoder_png_sha256 does not match its trusted lock")
        decoded = census_lib.require_object(built.get("decoded_png"), f"{where}.decoded_png")
        _require_exact_keys(decoded, ("height", "path", "sha256", "width"), f"{where}.decoded_png")
        expected_path = _stable_reference_path(selection_entry)
        if decoded["path"] != expected_path or not census_lib.is_safe_relative_path(
            decoded["path"], suffix=".png"
        ):
            raise ValidationError(f"{where}.decoded_png.path is invalid")
        expected_paths.add(expected_path)
        image_path = output_dir / PurePosixPath(expected_path)
        image_bytes = image_path.read_bytes()
        decoded_hash = census_lib.require_sha256(
            decoded["sha256"], f"{where}.decoded_png.sha256"
        )
        if decoded_hash != selection_entry["expected_decoded_png_sha256"]:
            raise ValidationError(f"{where}.decoded_png.sha256 does not match its trusted lock")
        if _sha256_bytes(image_bytes) != decoded_hash:
            raise ValidationError(f"{where} decoded PNG hash does not match")
        census_dimensions = census_entry["logical_dimensions"]
        image = decode_png(
            image_bytes,
            str(image_path),
            (census_dimensions["width"], census_dimensions["height"]),
        )
        if (image.width, image.height) != (decoded["width"], decoded["height"]):
            raise ValidationError(f"{where} decoded PNG dimensions do not match")
        if (image.width, image.height) != (
            census_dimensions["width"],
            census_dimensions["height"],
        ):
            raise ValidationError(f"{where} decoded PNG dimensions differ from the census")
        images.append(image)

    actual_paths = {
        path.relative_to(output_dir).as_posix()
        for path in (output_dir / "references").glob("*.png")
    }
    if actual_paths != expected_paths:
        raise ValidationError("reference directory does not contain exactly the 24 manifest PNGs")

    expected_files = expected_paths | {OUTPUT_MANIFEST, CONTACT_SHEET}
    actual_files = {
        path.relative_to(output_dir).as_posix()
        for path in output_dir.rglob("*")
        if path.is_file()
    }
    if actual_files != expected_files:
        raise ValidationError("classic reference output contains unmanifested files")

    expected_contact = encode_png(
        build_contact_sheet(context.selection["entries"], context.census_entries, images)
    )
    contact = census_lib.require_object(manifest["contact_sheet"], "manifest.contact_sheet")
    _require_exact_keys(contact, ("height", "path", "sha256", "width"), "manifest.contact_sheet")
    if contact["path"] != CONTACT_SHEET:
        raise ValidationError("manifest.contact_sheet.path is invalid")
    contact_path = output_dir / CONTACT_SHEET
    contact_bytes = contact_path.read_bytes()
    if contact_bytes != expected_contact:
        raise ValidationError("contact sheet is not the deterministic rendering of its 24 inputs")
    if _sha256_bytes(contact_bytes) != census_lib.require_sha256(
        contact["sha256"], "manifest.contact_sheet.sha256"
    ):
        raise ValidationError("contact sheet hash does not match")
    contact_image = decode_png(
        contact_bytes,
        str(contact_path),
        (contact["width"], contact["height"]),
    )
    if (contact_image.width, contact_image.height) != (contact["width"], contact["height"]):
        raise ValidationError("contact sheet dimensions do not match")


def export_generation_inputs(
    context: SelectionContext, reference_dir: Path, output_dir: Path
) -> None:
    """Export only generation-eligible references; repository City is refused."""
    verify_references(context, reference_dir)
    if output_dir.exists():
        raise ValidationError(f"output directory already exists: {output_dir}")
    selected = [
        entry
        for entry in context.selection["entries"]
        if entry["source_role"] != "city-repository-bundled-unverified"
    ]
    refused = [
        entry
        for entry in context.selection["entries"]
        if entry["source_role"] == "city-repository-bundled-unverified"
    ]
    if len(selected) != 21 or len(refused) != 3:
        raise ValidationError(
            "generation exporter expected 21 eligible references and 3 refused City references"
        )
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent)
    )
    try:
        files = staging / "references"
        files.mkdir()
        exported_entries = []
        for entry in selected:
            source_relative = _stable_reference_path(entry)
            source_path = reference_dir / PurePosixPath(source_relative)
            destination_relative = f"references/{Path(source_relative).name}"
            destination_path = staging / PurePosixPath(destination_relative)
            data = source_path.read_bytes()
            destination_path.write_bytes(data)
            exported_entries.append(
                {
                    "classic_payload_sha256": entry["classic_payload_sha256"],
                    "decoded_png_path": destination_relative,
                    "decoded_png_sha256": _sha256_bytes(data),
                    "family": entry["family"],
                    "key": entry["key"],
                    "label": entry["label"],
                    "source_role": entry["source_role"],
                }
            )
        export_manifest = {
            "approval_eligible": False,
            "authorization_blocker": (
                "authorized Mac 7.1.2 City references are not yet available"
            ),
            "complete_style_proof": False,
            "entries": exported_entries,
            "export_kind": "style-proof-generation-inputs",
            "generated_art": False,
            "model_calls": 0,
            "refused_references": [
                {
                    "key": entry["key"],
                    "reason": (
                        "repository City is version-unverified and generation-ineligible; "
                        "replace with the authorized Mac 7.1.2 baseline"
                    ),
                }
                for entry in refused
            ],
            "schema_version": SCHEMA_VERSION,
            "selection_sha256": _sha256_bytes(context.selection_bytes),
        }
        (staging / GENERATION_INPUT_MANIFEST).write_bytes(
            census_lib.canonical_json(export_manifest)
        )
        verify_generation_inputs(context, staging)
        staging.replace(output_dir)
    except BaseException:
        if staging.exists():
            shutil.rmtree(staging)
        raise


def verify_generation_inputs(context: SelectionContext, output_dir: Path) -> None:
    manifest_path = output_dir / GENERATION_INPUT_MANIFEST
    manifest_value, manifest_bytes = census_lib.load_json(manifest_path)
    manifest = census_lib.require_object(manifest_value, "generation input manifest")
    if manifest_bytes != census_lib.canonical_json(manifest):
        raise ValidationError("generation input manifest must use canonical sorted JSON")
    _require_exact_keys(
        manifest,
        (
            "approval_eligible",
            "authorization_blocker",
            "complete_style_proof",
            "entries",
            "export_kind",
            "generated_art",
            "model_calls",
            "refused_references",
            "schema_version",
            "selection_sha256",
        ),
        "generation input manifest",
    )
    if (
        manifest["schema_version"] != SCHEMA_VERSION
        or manifest["export_kind"] != "style-proof-generation-inputs"
        or manifest["generated_art"] is not False
        or manifest["model_calls"] != 0
        or manifest["complete_style_proof"] is not False
        or manifest["approval_eligible"] is not False
        or manifest["authorization_blocker"]
        != "authorized Mac 7.1.2 City references are not yet available"
        or manifest["selection_sha256"] != _sha256_bytes(context.selection_bytes)
    ):
        raise ValidationError("generation input manifest policy or identity is invalid")

    eligible = [
        entry
        for entry in context.selection["entries"]
        if entry["source_role"] != "city-repository-bundled-unverified"
    ]
    refused = [
        entry
        for entry in context.selection["entries"]
        if entry["source_role"] == "city-repository-bundled-unverified"
    ]
    entries = census_lib.require_list(manifest["entries"], "generation input entries")
    if len(entries) != 21:
        raise ValidationError("generation input manifest must contain exactly 21 eligible entries")
    expected_files = {GENERATION_INPUT_MANIFEST}
    for index, (actual_value, source_entry) in enumerate(zip(entries, eligible)):
        where = f"generation input entries[{index}]"
        actual = census_lib.require_object(actual_value, where)
        _require_exact_keys(
            actual,
            (
                "classic_payload_sha256",
                "decoded_png_path",
                "decoded_png_sha256",
                "family",
                "key",
                "label",
                "source_role",
            ),
            where,
        )
        if actual["source_role"] == "city-repository-bundled-unverified":
            raise ValidationError("generation input artifact contains a refused City reference")
        expected_path = f"references/{Path(_stable_reference_path(source_entry)).name}"
        expected = {
            "classic_payload_sha256": source_entry["classic_payload_sha256"],
            "decoded_png_path": expected_path,
            "decoded_png_sha256": source_entry["expected_decoded_png_sha256"],
            "family": source_entry["family"],
            "key": source_entry["key"],
            "label": source_entry["label"],
            "source_role": source_entry["source_role"],
        }
        if actual != expected:
            raise ValidationError(f"{where} does not match the eligible selection projection")
        path = output_dir / PurePosixPath(expected_path)
        data = path.read_bytes()
        if _sha256_bytes(data) != source_entry["expected_decoded_png_sha256"]:
            raise ValidationError(f"{where} PNG does not match its trusted decoded lock")
        dimensions = context.census_entries[_key_tuple(source_entry["key"])][
            "logical_dimensions"
        ]
        decode_png(
            data,
            str(path),
            (dimensions["width"], dimensions["height"]),
        )
        expected_files.add(expected_path)

    refused_entries = census_lib.require_list(
        manifest["refused_references"], "generation input refused_references"
    )
    expected_refused = [
        {
            "key": entry["key"],
            "reason": (
                "repository City is version-unverified and generation-ineligible; "
                "replace with the authorized Mac 7.1.2 baseline"
            ),
        }
        for entry in refused
    ]
    if refused_entries != expected_refused or len(refused_entries) != 3:
        raise ValidationError("generation input refusal list is incomplete or stale")

    actual_files = {
        path.relative_to(output_dir).as_posix()
        for path in output_dir.rglob("*")
        if path.is_file()
    }
    if actual_files != expected_files:
        raise ValidationError(
            "generation input artifact contains missing, extra, or unmanifested files"
        )


def _resolve_context(args: argparse.Namespace) -> SelectionContext:
    root = Path(args.root).resolve()
    selection_path = Path(args.selection)
    if not selection_path.is_absolute():
        selection_path = root / selection_path
    return _load_selection(root, selection_path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument(
        "--selection",
        default=DEFAULT_SELECTION,
        help="committed 24-reference selection manifest",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate", help="validate selection and provenance without decoding")
    build_parser = subparsers.add_parser("build", help="decode references and build contact sheet")
    build_parser.add_argument("--resource-dasm", required=True, help="pinned resource_dasm executable")
    build_parser.add_argument("--output", required=True, help="new output directory")
    verify_parser = subparsers.add_parser("verify", help="verify an existing output directory")
    verify_parser.add_argument("--output", required=True, help="output directory to verify")
    export_parser = subparsers.add_parser(
        "export-generation-inputs",
        help="export only generation-eligible references and refuse repository City",
    )
    export_parser.add_argument(
        "--references", required=True, help="verified Classic reference directory"
    )
    export_parser.add_argument("--output", required=True, help="new generation-input directory")
    verify_export_parser = subparsers.add_parser(
        "verify-generation-inputs",
        help="verify the fail-closed generation-input artifact before consumption",
    )
    verify_export_parser.add_argument(
        "--output", required=True, help="generation-input directory to verify"
    )
    args = parser.parse_args(argv)

    try:
        context = _resolve_context(args)
        if args.command == "validate":
            print(
                "validated 24 Classic style-proof references "
                "(4 per family; City repository-bundled and version-unverified)"
            )
        elif args.command == "build":
            build_references(context, Path(args.resource_dasm), Path(args.output).resolve())
            verify_references(context, Path(args.output).resolve())
            print(f"built and verified 24 Classic references at {Path(args.output).resolve()}")
            print("model calls: 0")
            print(f"City notice: {CITY_NOTICE}")
        elif args.command == "verify":
            verify_references(context, Path(args.output).resolve())
            print(f"verified 24 Classic references at {Path(args.output).resolve()}")
            print(f"City notice: {CITY_NOTICE}")
        elif args.command == "export-generation-inputs":
            export_generation_inputs(
                context,
                Path(args.references).resolve(),
                Path(args.output).resolve(),
            )
            print(f"exported 21 generation-eligible references at {Path(args.output).resolve()}")
            print("refused 3 repository City references; authorized Mac 7.1.2 replacement required")
        else:
            verify_generation_inputs(context, Path(args.output).resolve())
            print(f"verified 21 generation-eligible references at {Path(args.output).resolve()}")
            print("proof remains incomplete and approval-ineligible until authorized City replacement")
        return 0
    except (OSError, ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
