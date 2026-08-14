#!/usr/bin/env python3
"""Print one hash-bound amended style-proof prompt before its first model call."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import validate_style_proof_outputs as contract


def _resolve(root: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".")
    parser.add_argument("--jobs", default=contract.DEFAULT_JOBS)
    parser.add_argument("--inputs", required=True)
    parser.add_argument("--receipts", default=contract.DEFAULT_RECEIPTS)
    parser.add_argument("--amendments", default=contract.DEFAULT_AMENDMENTS)
    parser.add_argument(
        "--topology-amendments", default=contract.DEFAULT_TOPOLOGY_AMENDMENTS
    )
    parser.add_argument("--operational-log", default=contract.DEFAULT_OPERATIONAL_LOG)
    parser.add_argument("--job-id", required=True)
    parser.add_argument("--regeneration-ordinal", type=int, choices=(0, 1), default=0)
    parser.add_argument("--retry-prompt-supplement")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve()
    try:
        result = contract.prepare_amended_prompt(
            root=root,
            jobs_path=_resolve(root, args.jobs),
            input_dir=_resolve(root, args.inputs),
            receipts_path=_resolve(root, args.receipts),
            operational_log_path=_resolve(root, args.operational_log),
            job_id=args.job_id,
            regeneration_ordinal=args.regeneration_ordinal,
            retry_prompt_supplement=args.retry_prompt_supplement,
            amendments_path=_resolve(root, args.amendments),
            topology_amendments_path=_resolve(root, args.topology_amendments),
        )
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    except (contract.ValidationError, OSError) as exc:
        print(f"style-proof prompt preparation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
