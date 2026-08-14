#!/usr/bin/env python3
"""Build the mixed Classic/approved-PNG runtime asset manifest.

The style-proof job ledger remains the review authority. This script projects
only human-approved, hash-verified outputs into the exhaustive phase-one
manifest; every other ResourceKey remains a Classic passthrough.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


DEFAULT_OUTPUT = "assets/remastered/scopes/phase1.runtime-manifest.json"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def canonical_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def key_tuple(value: dict[str, Any]) -> tuple[str, str, int]:
    return (value["pack"], value["type"], value["id"])


def relative_asset_path(root: Path, repository_path: str) -> str:
    asset_root = (root / "assets/remastered").resolve()
    absolute = (root / repository_path).resolve()
    try:
        return absolute.relative_to(asset_root).as_posix()
    except ValueError as error:
        raise ValueError(f"approved output escapes assets/remastered: {repository_path}") from error


def build(root: Path) -> dict[str, Any]:
    placeholder_path = root / "assets/remastered/scopes/phase1.placeholder-manifest.json"
    jobs_path = root / "assets/remastered/style-proof/generation/jobs.json"
    receipts_path = root / "assets/remastered/style-proof/generation/generation-receipts.json"
    manifest = load_json(placeholder_path)
    jobs = load_json(jobs_path)
    receipts = load_json(receipts_path)

    jobs_by_id = {job["job_id"]: job for job in jobs["jobs"]}
    receipts_by_id = {receipt["job_id"]: receipt for receipt in receipts["entries"]}
    if len(jobs_by_id) != len(jobs["jobs"]) or len(receipts_by_id) != len(receipts["entries"]):
        raise ValueError("duplicate job_id in jobs or receipts")

    approved_by_key: dict[tuple[str, str, int], tuple[dict[str, Any], dict[str, Any]]] = {}
    for job_id, receipt in receipts_by_id.items():
        if receipt["review_status"] != "approved":
            continue
        job = jobs_by_id.get(job_id)
        if job is None:
            raise ValueError(f"approved receipt has no immutable job: {job_id}")
        output = receipt.get("output")
        provenance = receipt.get("model_provenance")
        reviewer = receipt.get("reviewer")
        if not isinstance(output, dict) or not isinstance(provenance, dict) or not reviewer:
            raise ValueError(f"approved receipt is incomplete: {job_id}")
        output_path = root / output["path"]
        if not output_path.is_file() or sha256(output_path) != output["sha256"]:
            raise ValueError(f"approved output hash mismatch: {job_id}")
        if receipt["input"]["classic_payload_sha256"] != job["input"]["classic_payload_sha256"]:
            raise ValueError(f"approved receipt input changed: {job_id}")
        key = key_tuple(job["resource_key"])
        if key in approved_by_key:
            raise ValueError(f"multiple approved jobs claim ResourceKey {key}")
        approved_by_key[key] = (job, receipt)

    projected = 0
    for entry in manifest["entries"]:
        approved = approved_by_key.get(key_tuple(entry["key"]))
        if approved is None:
            continue
        job, receipt = approved
        output = receipt["output"]
        provenance = receipt["model_provenance"]
        if entry["classic_payload_sha256"] != job["input"]["classic_payload_sha256"]:
            raise ValueError(f"Classic payload mismatch for {job['job_id']}")
        entry["asset_path"] = relative_asset_path(root, output["path"])
        entry["generation_provenance"] = {
            "kind": "imagegen",
            "model": provenance["model"],
            "provider": provenance["provider"],
        }
        entry["post_processing"] = [step["operation"] for step in receipt["post_processing"]]
        entry["prompt_sha256"] = receipt["prompt_sha256"]
        entry["reviewer"] = receipt["reviewer"]
        entry["shared_master_sha256"] = output["sha256"]
        entry["status"] = "approved"
        projected += 1

    if projected != len(approved_by_key):
        raise ValueError(
            f"only {projected} of {len(approved_by_key)} approved ResourceKeys exist in phase one"
        )
    manifest["manifest_kind"] = "production"
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    output_path = (root / args.output).resolve()
    generated = canonical_bytes(build(root))

    if args.check:
        if not output_path.is_file() or output_path.read_bytes() != generated:
            raise SystemExit(f"runtime asset manifest is stale: {output_path}")
    else:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(generated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
