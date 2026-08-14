#!/usr/bin/env bash

# Wrap the upstream 32x32 BMP placeholder in a valid modern ICNS container.
# This is packaging scaffolding, not the approved remastered app artwork.

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/.." && pwd)"
input="${1:-$repo_root/bundle/AppIcon.placeholder.bmp}"
output="${2:-$repo_root/bundle/AppIcon.icns}"

[[ "$(uname -s)" == "Darwin" ]] || {
  echo "error: sips is required to build the placeholder ICNS" >&2
  exit 1
}
[[ -f "$input" ]] || {
  echo "error: placeholder bitmap is missing: $input" >&2
  exit 1
}
for command_name in python3 sips; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "error: required command is unavailable: $command_name" >&2
    exit 1
  }
done

temporary_root="$(mktemp -d /tmp/realmz-placeholder-icon.XXXXXX)"
cleanup() {
  rm -rf -- "$temporary_root"
}
trap cleanup EXIT

base_png="$temporary_root/base.png"
sips -s format png "$input" --out "$base_png" >/dev/null

declare -a chunks=(
  "icp4:16"
  "icp5:32"
  "icp6:64"
  "ic07:128"
  "ic08:256"
  "ic09:512"
  "ic10:1024"
)
for chunk in "${chunks[@]}"; do
  type="${chunk%%:*}"
  size="${chunk#*:}"
  sips -z "$size" "$size" "$base_png" \
    --out "$temporary_root/$type.png" >/dev/null
done

python3 - "$temporary_root" "$output" <<'PY'
import pathlib
import struct
import sys

source = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
chunk_types = ("icp4", "icp5", "icp6", "ic07", "ic08", "ic09", "ic10")
chunks = []
for chunk_type in chunk_types:
    payload = (source / f"{chunk_type}.png").read_bytes()
    if not payload.startswith(b"\x89PNG\r\n\x1a\n"):
        raise SystemExit(f"{chunk_type} payload is not PNG")
    chunks.append(chunk_type.encode("ascii") + struct.pack(">I", len(payload) + 8) + payload)
container = b"icns" + struct.pack(">I", 8 + sum(map(len, chunks))) + b"".join(chunks)
temporary = output.with_name(output.name + ".tmp")
temporary.write_bytes(container)
temporary.replace(output)
PY

printf 'built placeholder ICNS: %s\n' "$output"
