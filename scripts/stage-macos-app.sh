#!/usr/bin/env bash

# Assemble the expanded unsigned macOS application bundle without invoking
# CPack's Bundle/DMG generator. The configured install rules populate
# Contents/Resources; this script adds the Bundle-specific plist, icon, and
# startup executable before artifact verification.

set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/stage-macos-app.sh --build-dir PATH --output-dir PATH [options]

Options:
  --build-dir PATH       Configured CMake build containing Realmz and Info.plist.
  --output-dir PATH      Parent directory for the expanded .app bundle.
  --configuration NAME  Install configuration. Default: Release.
  --cmake PATH           CMake executable. Default: cmake from PATH.
  -h, --help             Show this help.

The destination application must not already exist. The script stages into a
private temporary directory and publishes the complete bundle with one rename.
It does not create a DMG, sign, notarize, or upload the application.
USAGE
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/.." && pwd)"
build_dir=""
output_dir=""
configuration="Release"
cmake_command="cmake"

while (($#)); do
  case "$1" in
    --build-dir)
      (($# >= 2)) || { echo "error: --build-dir requires a value" >&2; exit 2; }
      build_dir="$2"
      shift 2
      ;;
    --output-dir)
      (($# >= 2)) || { echo "error: --output-dir requires a value" >&2; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --configuration)
      (($# >= 2)) || { echo "error: --configuration requires a value" >&2; exit 2; }
      configuration="$2"
      shift 2
      ;;
    --cmake)
      (($# >= 2)) || { echo "error: --cmake requires a value" >&2; exit 2; }
      cmake_command="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

[[ -n "$build_dir" ]] || { echo "error: --build-dir is required" >&2; exit 2; }
[[ -n "$output_dir" ]] || { echo "error: --output-dir is required" >&2; exit 2; }
[[ "$configuration" =~ ^[A-Za-z0-9_.+-]+$ &&
   "$configuration" != "." && "$configuration" != ".." ]] || {
  echo "error: invalid configuration name: $configuration" >&2
  exit 2
}
command -v python3 >/dev/null 2>&1 || {
  echo "error: required command is unavailable: python3" >&2
  exit 1
}
if [[ "$cmake_command" == */* ]]; then
  [[ -x "$cmake_command" ]] || {
    echo "error: CMake executable is unavailable: $cmake_command" >&2
    exit 1
  }
else
  cmake_command="$(command -v "$cmake_command" 2>/dev/null || true)"
  [[ -n "$cmake_command" ]] || {
    echo "error: required command is unavailable: cmake" >&2
    exit 1
  }
fi

[[ -d "$build_dir" ]] || {
  echo "error: configured build directory is missing: $build_dir" >&2
  exit 1
}
build_dir="$(CDPATH= cd -- "$build_dir" && pwd)"
plist_source="$build_dir/Info.plist"
[[ -f "$plist_source" ]] || {
  echo "error: configured Info.plist is missing: $plist_source" >&2
  exit 1
}

plist_value() {
  python3 - "$plist_source" "$1" <<'PY'
import plistlib
import sys

with open(sys.argv[1], "rb") as source:
    value = plistlib.load(source).get(sys.argv[2], "")
if not isinstance(value, str):
    raise SystemExit(1)
print(value, end="")
PY
}

bundle_name="$(plist_value CFBundleName)" || {
  echo "error: CFBundleName is missing or invalid in $plist_source" >&2
  exit 1
}
executable_name="$(plist_value CFBundleExecutable)" || {
  echo "error: CFBundleExecutable is missing or invalid in $plist_source" >&2
  exit 1
}
icon_name="$(plist_value CFBundleIconFile)" || {
  echo "error: CFBundleIconFile is missing or invalid in $plist_source" >&2
  exit 1
}

valid_filename() {
  [[ -n "$1" && "$1" != "." && "$1" != ".." && "$1" != */* &&
     "$1" != *$'\n'* && "$1" != *$'\r'* ]]
}
valid_filename "$bundle_name" || {
  echo "error: CFBundleName must be a filename: $bundle_name" >&2
  exit 1
}
valid_filename "$executable_name" || {
  echo "error: CFBundleExecutable must be a filename: $executable_name" >&2
  exit 1
}
valid_filename "$icon_name" || {
  echo "error: CFBundleIconFile must be a filename: $icon_name" >&2
  exit 1
}

executable_source="$build_dir/Realmz"
if [[ ! -f "$executable_source" ]]; then
  executable_source="$build_dir/$configuration/Realmz"
fi
[[ -f "$executable_source" && -x "$executable_source" ]] || {
  echo "error: built Realmz executable is missing or not executable" >&2
  exit 1
}

icon_bundle_name="$icon_name"
if [[ "$icon_bundle_name" != *.icns ]]; then
  icon_bundle_name="$icon_bundle_name.icns"
fi
icon_source="$repo_root/bundle/$icon_bundle_name"
[[ -f "$icon_source" ]] || {
  echo "error: configured bundle icon is missing: $icon_source" >&2
  exit 1
}

mkdir -p "$output_dir"
output_dir="$(CDPATH= cd -- "$output_dir" && pwd)"
final_app="$output_dir/$bundle_name.app"
[[ ! -e "$final_app" ]] || {
  echo "error: destination application already exists: $final_app" >&2
  exit 1
}

stage_root="$(mktemp -d "$output_dir/.realmz-app-stage.XXXXXX")"
cleanup() {
  case "$stage_root" in
    "$output_dir"/.realmz-app-stage.*) rm -rf -- "$stage_root" ;;
  esac
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

staged_app="$stage_root/$bundle_name.app"
contents="$staged_app/Contents"
resources="$contents/Resources"
macos="$contents/MacOS"
mkdir -p "$macos" "$resources"

"$cmake_command" --install "$build_dir" \
  --config "$configuration" \
  --prefix "$resources"
/usr/bin/install -m 0644 "$plist_source" "$contents/Info.plist"
/usr/bin/install -m 0644 "$icon_source" "$resources/$icon_bundle_name"
/usr/bin/install -m 0755 "$executable_source" "$macos/$executable_name"

mv -- "$staged_app" "$final_app"
printf '%s\n' "$final_app"
