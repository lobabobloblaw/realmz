#!/usr/bin/env bash

# Read-only structural verification for an expanded macOS application bundle.
# Development mode is intended for unsigned local builds. Release mode adds
# Developer ID/hardened-runtime checks; --require-notarization also requires a
# valid staple and Gatekeeper assessment.

set -uo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/verify-macos-artifact.sh [options] PATH.app

Options:
  --mode development|release  Skip signing checks for local development, or
                              require Developer ID signing and hardened runtime.
                              Default: development.
  --require-notarization      In release mode, require a valid stapled ticket
                              and successful Gatekeeper assessment.
  --minimum-macos VERSION     Required bundle and Mach-O deployment target.
                              Default: 13.3.
  --required-resource PATH    Require an additional path relative to
                              Contents/Resources. May be repeated.
  --tool-dir PATH             Directory containing test/tool shims named lipo,
                              otool, vtool, file, codesign, xcrun, and spctl.
  -h, --help                  Show this help.

The input must be an already expanded .app. Run this before creating a DMG and
again against the application copied from the mounted release DMG.
USAGE
}

mode="development"
minimum_macos="13.3"
require_notarization=0
tool_dir="${REALMZ_VERIFY_TOOL_DIR:-}"
app=""
# Bash 3.2 (the system Bash on supported macOS versions) treats expansion of
# an empty array as an unbound variable under `set -u`. Keep a sentinel so the
# script remains portable without weakening nounset checking.
declare -a extra_resources=("__REALMZ_NO_EXTRA_RESOURCE__")

while (($#)); do
  case "$1" in
    --mode)
      (($# >= 2)) || { echo "error: --mode requires a value" >&2; exit 2; }
      mode="$2"
      shift 2
      ;;
    --minimum-macos)
      (($# >= 2)) || { echo "error: --minimum-macos requires a value" >&2; exit 2; }
      minimum_macos="$2"
      shift 2
      ;;
    --required-resource)
      (($# >= 2)) || { echo "error: --required-resource requires a value" >&2; exit 2; }
      extra_resources+=("$2")
      shift 2
      ;;
    --tool-dir)
      (($# >= 2)) || { echo "error: --tool-dir requires a value" >&2; exit 2; }
      tool_dir="$2"
      shift 2
      ;;
    --require-notarization)
      require_notarization=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "$app" ]]; then
        echo "error: only one .app path may be supplied" >&2
        exit 2
      fi
      app="$1"
      shift
      ;;
  esac
done

case "$mode" in
  development|release) ;;
  *) echo "error: mode must be development or release" >&2; exit 2 ;;
esac
if ((require_notarization)) && [[ "$mode" != "release" ]]; then
  echo "error: --require-notarization is only valid with --mode release" >&2
  exit 2
fi
if [[ ! "$minimum_macos" =~ ^[0-9]+([.][0-9]+){1,2}$ ]]; then
  echo "error: invalid macOS version: $minimum_macos" >&2
  exit 2
fi
if [[ -z "$app" ]]; then
  echo "error: an expanded .app path is required" >&2
  usage >&2
  exit 2
fi
if [[ ! -d "$app" || "$app" != *.app ]]; then
  echo "error: not an application bundle: $app" >&2
  exit 2
fi
app="$(CDPATH= cd -- "$app" && pwd)"

errors=0
warnings=0

ok() {
  printf 'ok: %s\n' "$*"
}

warn() {
  printf 'warning: %s\n' "$*" >&2
  warnings=$((warnings + 1))
}

fail() {
  printf 'error: %s\n' "$*" >&2
  errors=$((errors + 1))
}

tool_path() {
  local name="$1"
  local env_name="$2"
  local override="${!env_name:-}"
  if [[ -n "$override" ]]; then
    printf '%s\n' "$override"
  elif [[ -n "$tool_dir" && -x "$tool_dir/$name" ]]; then
    printf '%s/%s\n' "$tool_dir" "$name"
  else
    command -v "$name" 2>/dev/null || true
  fi
}

python_tool="$(tool_path python3 PYTHON3)"
file_tool="$(tool_path file FILE_TOOL)"
lipo_tool="$(tool_path lipo LIPO)"
otool_tool="$(tool_path otool OTOOL)"
vtool_tool="$(tool_path vtool VTOOL)"
codesign_tool="$(tool_path codesign CODESIGN)"
xcrun_tool="$(tool_path xcrun XCRUN)"
spctl_tool="$(tool_path spctl SPCTL)"

tools_ready=1
for pair in \
  "python3|$python_tool" \
  "file|$file_tool" \
  "lipo|$lipo_tool" \
  "otool|$otool_tool"; do
  name="${pair%%|*}"
  path="${pair#*|}"
  if [[ -z "$path" || ! -x "$path" ]]; then
    fail "required tool is unavailable: $name"
    tools_ready=0
  fi
done
if [[ -z "$vtool_tool" || ! -x "$vtool_tool" ]]; then
  warn "vtool is unavailable; Mach-O minimum versions will be read with otool"
  vtool_tool=""
fi
if [[ "$mode" == "release" ]] && [[ -z "$codesign_tool" || ! -x "$codesign_tool" ]]; then
  fail "codesign is required in release mode"
  tools_ready=0
fi
if ((require_notarization)); then
  if [[ -z "$xcrun_tool" || ! -x "$xcrun_tool" ]]; then
    fail "xcrun is required for notarization validation"
    tools_ready=0
  fi
  if [[ -z "$spctl_tool" || ! -x "$spctl_tool" ]]; then
    fail "spctl is required for Gatekeeper validation"
    tools_ready=0
  fi
fi

if ((tools_ready == 0)); then
  printf 'macOS artifact verification stopped: required inspection tooling is unavailable\n' >&2
  exit 1
fi

plist="$app/Contents/Info.plist"
resources="$app/Contents/Resources"
if [[ ! -f "$plist" ]]; then
  fail "Contents/Info.plist is missing"
fi
if [[ ! -d "$resources" ]]; then
  fail "Contents/Resources is missing"
fi

plist_get() {
  local key="$1"
  "$python_tool" - "$plist" "$key" <<'PY'
import plistlib
import sys

with open(sys.argv[1], "rb") as f:
    data = plistlib.load(f)
value = data.get(sys.argv[2], "")
if isinstance(value, bool):
    print("true" if value else "false")
elif isinstance(value, (str, int, float)):
    print(value)
else:
    print("")
PY
}

bundle_id=""
display_name=""
bundle_name=""
executable_name=""
bundle_minimum=""
high_resolution=""
package_type=""
icon_name=""
if [[ -f "$plist" && -n "$python_tool" && -x "$python_tool" ]]; then
  if ! bundle_id="$(plist_get CFBundleIdentifier 2>/dev/null)"; then fail "Info.plist is not a readable property list"; fi
  display_name="$(plist_get CFBundleDisplayName 2>/dev/null || true)"
  bundle_name="$(plist_get CFBundleName 2>/dev/null || true)"
  executable_name="$(plist_get CFBundleExecutable 2>/dev/null || true)"
  bundle_minimum="$(plist_get LSMinimumSystemVersion 2>/dev/null || true)"
  high_resolution="$(plist_get NSHighResolutionCapable 2>/dev/null || true)"
  package_type="$(plist_get CFBundlePackageType 2>/dev/null || true)"
  icon_name="$(plist_get CFBundleIconFile 2>/dev/null || true)"

  identity_name="${display_name:-$bundle_name}"
  identity_lower="$(printf '%s' "$identity_name" | tr '[:upper:]' '[:lower:]')"
  bundle_id_lower="$(printf '%s' "$bundle_id" | tr '[:upper:]' '[:lower:]')"
  if [[ "$bundle_id" =~ ^[A-Za-z0-9-]+([.][A-Za-z0-9-]+)+$ ]] &&
      [[ "$bundle_id_lower" == *realmz* ]] &&
      [[ "$bundle_id_lower" == *remaster* ]] &&
      [[ "$bundle_id_lower" != com.fantasoft.* ]]; then
    ok "bundle identifier is distinct: $bundle_id"
  else
    fail "CFBundleIdentifier must be a distinct reverse-DNS remaster identifier outside com.fantasoft (found '$bundle_id')"
  fi
  if [[ "$identity_lower" == *"realmz remastered"* && "$identity_lower" == *unofficial* ]]; then
    ok "display name clearly identifies the unofficial remaster"
  else
    fail "CFBundleDisplayName/CFBundleName must include 'Realmz Remastered' and 'Unofficial' (found '$identity_name')"
  fi
  if [[ "$bundle_minimum" == "$minimum_macos" ]]; then
    ok "bundle minimum macOS version is $minimum_macos"
  else
    fail "LSMinimumSystemVersion is '${bundle_minimum:-missing}'; expected $minimum_macos"
  fi
  if [[ "$high_resolution" == "true" ]]; then
    ok "bundle declares high-resolution support"
  else
    fail "NSHighResolutionCapable must be true"
  fi
  if [[ "$package_type" == "APPL" ]]; then
    ok "bundle package type is APPL"
  else
    fail "CFBundlePackageType is '${package_type:-missing}'; expected APPL"
  fi
fi

if [[ -z "$icon_name" ]]; then
  fail "CFBundleIconFile is missing"
elif [[ "$icon_name" == /* || "$icon_name" == */* || "$icon_name" == *".."* ]]; then
  fail "CFBundleIconFile must be a filename inside Contents/Resources: $icon_name"
else
  [[ "$icon_name" == *.icns ]] || icon_name="$icon_name.icns"
  icon_path="$resources/$icon_name"
  if [[ ! -f "$icon_path" ]]; then
    fail "bundle icon is missing: Contents/Resources/$icon_name"
  elif "$python_tool" - "$icon_path" <<'PY'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
if len(data) < 16 or data[:4] != b"icns":
    raise SystemExit(1)
declared_size = struct.unpack(">I", data[4:8])[0]
if declared_size != len(data):
    raise SystemExit(1)
PY
  then
    ok "bundle icon is a structurally valid ICNS container"
  else
    fail "bundle icon is not a structurally valid ICNS file: Contents/Resources/$icon_name"
  fi
fi

executable=""
if [[ -z "$executable_name" ]]; then
  fail "CFBundleExecutable is missing"
elif [[ "$executable_name" == */* || "$executable_name" == "." || "$executable_name" == ".." ]]; then
  fail "CFBundleExecutable must be a filename, not a path: $executable_name"
else
  executable="$app/Contents/MacOS/$executable_name"
  if [[ -f "$executable" && -x "$executable" ]]; then
    ok "bundle executable exists and is executable"
  else
    fail "bundle executable is missing or not executable: Contents/MacOS/$executable_name"
  fi
fi

require_nonempty_path() {
  local rel="$1"
  local path="$resources/$rel"
  if [[ -f "$path" && -s "$path" ]]; then
    ok "phase-one resource exists: $rel"
  elif [[ -d "$path" ]] && find "$path" -type f -print -quit 2>/dev/null | grep -q .; then
    ok "phase-one resource directory is populated: $rel"
  else
    fail "required phase-one resource is missing or empty: $rel"
  fi
}

for rel in \
  "realmz.rsrc" \
  "Data Files" \
  "Scenarios/Tutorial" \
  "Scenarios/City of Bywater"; do
  require_nonempty_path "$rel"
done
for rel in "${extra_resources[@]}"; do
  [[ "$rel" == "__REALMZ_NO_EXTRA_RESOURCE__" ]] && continue
  if [[ "$rel" == /* || "$rel" == *".."* ]]; then
    fail "additional resource path must be relative and cannot contain '..': $rel"
  else
    require_nonempty_path "$rel"
  fi
done

phase_manifest="$resources/Remastered/phase1.manifest.json"
placeholder_manifest="$resources/Remastered/phase1.placeholder-manifest.json"
if [[ "$mode" == "release" ]]; then
  if [[ -s "$phase_manifest" ]]; then
    if "$python_tool" - "$phase_manifest" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    manifest = json.load(f)
if not isinstance(manifest, dict):
    raise SystemExit("manifest root must be an object")
if manifest.get("scope") not in ("phase1", "core-tutorial-city"):
    raise SystemExit("manifest scope is not phase1")
if manifest.get("status") != "approved":
    raise SystemExit("manifest status is not approved")
failures = manifest.get("coverage_failures", [])
if not isinstance(failures, list) or failures:
    raise SystemExit("manifest has coverage failures")
PY
    then
      ok "approved phase-one remaster manifest has zero coverage failures"
    else
      fail "phase-one remaster manifest is invalid or incomplete"
    fi
  else
    fail "release bundle requires Remastered/phase1.manifest.json"
  fi
else
  if [[ -s "$phase_manifest" ]]; then
    ok "phase-one remaster manifest is present"
  elif [[ -s "$placeholder_manifest" ]]; then
    ok "development bundle contains the phase-one placeholder manifest"
  else
    fail "development bundle requires a final or placeholder phase-one manifest under Remastered"
  fi
fi

runtime_manifest="$resources/Remastered/phase1.runtime-manifest.json"
asset_census="$resources/Remastered/phase1.census.json"
if native_material_diagnostic="$("$python_tool" - \
    "$resources/Remastered" "$runtime_manifest" "$asset_census" <<'PY'
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

root = Path(sys.argv[1])
runtime_path = Path(sys.argv[2])
census_path = Path(sys.argv[3])

if root.is_symlink() or not root.is_dir():
    raise SystemExit("bundled Remastered root is missing or is a symlink")
for item in root.rglob("*"):
    if item.is_symlink():
        raise SystemExit(
            "symlink leaked into bundled Remastered tree: "
            + item.relative_to(root).as_posix()
        )

for label, path in (("runtime manifest", runtime_path), ("asset census", census_path)):
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f"missing or empty {label}: {path.name}")
try:
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    census = json.loads(census_path.read_text(encoding="utf-8"))
except (OSError, UnicodeError, json.JSONDecodeError) as exc:
    raise SystemExit(f"cannot parse runtime material metadata: {exc}")
if not isinstance(runtime, dict) or not isinstance(runtime.get("entries"), list):
    raise SystemExit("runtime manifest entries are invalid")
if not isinstance(census, dict) or not isinstance(census.get("entries"), list):
    raise SystemExit("asset census entries are invalid")
census_digest = hashlib.sha256(census_path.read_bytes()).hexdigest()
if runtime.get("census_sha256") != census_digest:
    raise SystemExit("runtime manifest is not bound to the bundled census")

expected_outputs = {
    "style-proof/generation/outputs/01_ui_material_ppat_128.png":
        "3b3a30342aef0e49b0b43a04bec3592abda5d629963bc8a68c8cb296eef1961e",
    "style-proof/generation/outputs/02_ui_material_ppat_129.png":
        "dfe2dadfa74ef37ee4d6de5f8e607eb082285f6e1a994a5e0e26ba2463d7f52f",
    "style-proof/generation/outputs/03_ui_material_ppat_130.png":
        "df44c09cef3bd20e1c8de43e39c6c88573942a9f17ab5f4f3968f4c04ab72504",
    "style-proof/generation/outputs/04_ui_material_ppat_131.png":
        "7081f60b8a7ea247fb5d181e6d8b2dfcaaf7a12046a58c31fef318c21f5fc3f9",
    "style-proof/generation/outputs/05_portrait_cicn_257.png":
        "8eae998f6e9d745911ed518f7ad2629abd95a72d5c66c5ba0e9655663e0f2cc1",
    "style-proof/generation/outputs/06_portrait_cicn_267.png":
        "5786f9cbf6901739f34acaa72f603cdc62a53423d94b31956fbe90f1af9be281",
    "style-proof/generation/outputs/07_portrait_cicn_297.png":
        "c5bc0a5c315cfc115d6d1a806f9396670e59734a84102dc492d547cced78aca0",
    "style-proof/generation/outputs/08_portrait_cicn_337.png":
        "3bc7ac4e0d6bb17cab9562f0f2f4db178c9eddc584f658e524ab947b59a3ec5e",
    "style-proof/generation/outputs/17_world_dungeon_PICT_50.png":
        "86e0607e8092710896fc07a1fab3c034cbd2c10340de7bec09eee0aecfd2d899",
    "style-proof/generation/outputs/20_world_dungeon_cicn_m167.png":
        "44ba9ea12d1afee69d5ed23ff2032cd0d553b3cfd04aad8300076bb140d07578",
    "style-proof/generation/outputs/21_tutorial_city_PICT_32128.png":
        "b3ad37374ae9b92a0bf745627c4db0d5022b53e16fa9d106bc4b5b4ecc032bba",
}
approved_outputs = {}
for index, entry in enumerate(runtime["entries"]):
    if not isinstance(entry, dict) or entry.get("status") != "approved":
        continue
    relative = entry.get("asset_path")
    digest = entry.get("shared_master_sha256")
    if not isinstance(relative, str) or not isinstance(digest, str):
        raise SystemExit(f"runtime approved entry {index} has invalid output metadata")
    normalized = PurePosixPath(relative)
    if (
        normalized.is_absolute()
        or relative != normalized.as_posix()
        or normalized.parts[:3] != ("style-proof", "generation", "outputs")
        or len(normalized.parts) != 4
        or normalized.suffix != ".png"
        or relative in approved_outputs
    ):
        raise SystemExit(f"runtime approved output path is unsafe or duplicated: {relative}")
    approved_outputs[relative] = digest
if approved_outputs != expected_outputs:
    raise SystemExit("runtime approved output path/hash set is not the reviewed 11-file set")

allowed_directories = {
    "style-proof",
    "style-proof/generation",
    "style-proof/generation/outputs",
}
allowed_files = set(expected_outputs) | {
    "phase1.manifest.json",
    "phase1.placeholder-manifest.json",
    "phase1.runtime-manifest.json",
    "phase1.census.json",
    "phase1.scope.json",
}
for item in root.rglob("*"):
    relative = item.relative_to(root).as_posix()
    if item.is_dir():
        if relative not in allowed_directories:
            raise SystemExit(f"private Remastered directory leaked into bundle: {relative}")
    elif item.is_file():
        if relative not in allowed_files:
            raise SystemExit(f"unexpected Remastered file leaked into bundle: {relative}")
    else:
        raise SystemExit(f"special Remastered entry leaked into bundle: {relative}")

for relative, digest in expected_outputs.items():
    output = root / relative
    if output.is_symlink() or not output.is_file() or output.stat().st_size == 0:
        raise SystemExit(f"approved runtime output is missing: {relative}")
    if hashlib.sha256(output.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"approved runtime output hash mismatch: {relative}")

expected = (
    (128, "style-proof/generation/outputs/01_ui_material_ppat_128.png",
     "3b3a30342aef0e49b0b43a04bec3592abda5d629963bc8a68c8cb296eef1961e"),
    (129, "style-proof/generation/outputs/02_ui_material_ppat_129.png",
     "dfe2dadfa74ef37ee4d6de5f8e607eb082285f6e1a994a5e0e26ba2463d7f52f"),
    (130, "style-proof/generation/outputs/03_ui_material_ppat_130.png",
     "df44c09cef3bd20e1c8de43e39c6c88573942a9f17ab5f4f3968f4c04ab72504"),
    (131, "style-proof/generation/outputs/04_ui_material_ppat_131.png",
     "7081f60b8a7ea247fb5d181e6d8b2dfcaaf7a12046a58c31fef318c21f5fc3f9"),
)
pack = "Data Files/The Family Jewels"
for resource_id, relative, digest in expected:
    key = {"pack": pack, "type": "ppat", "id": resource_id}
    matches = [entry for entry in runtime["entries"] if entry.get("key") == key]
    if len(matches) != 1:
        raise SystemExit(f"runtime manifest must contain exactly one ppat {resource_id}")
    entry = matches[0]
    if (
        entry.get("master_key") != key
        or entry.get("status") != "approved"
        or entry.get("semantic_family") != "ui_surface"
        or entry.get("alpha_policy") != "opaque_tile"
        or entry.get("logical_dimensions") != {"width": 64, "height": 64}
        or entry.get("asset_path") != relative
        or entry.get("shared_master_sha256") != digest
    ):
        raise SystemExit(f"runtime ppat {resource_id} binding is invalid")
    census_matches = [
        entry for entry in census["entries"] if entry.get("key") == key
    ]
    if len(census_matches) != 1:
        raise SystemExit(f"asset census must contain exactly one ppat {resource_id}")
PY
)"; then
  ok "runtime manifest, census, and 11 approved public outputs are hash-valid"
else
  fail "native shell material bundle is invalid: $native_material_diagnostic"
fi

notices="$resources/Notices"
declare -a notice_files=(LICENSE ATTRIBUTION.md MODIFICATIONS.md CONTENT_PROVENANCE.md)
for notice in "${notice_files[@]}"; do
  if [[ -s "$notices/$notice" ]]; then
    ok "notice is present: Notices/$notice"
  else
    fail "required notice is missing or empty: Notices/$notice"
  fi
done
if [[ -s "$notices/LICENSE" ]] && grep -Fq "Creative Commons Attribution-NonCommercial-ShareAlike 4.0" "$notices/LICENSE"; then
  ok "bundled license is CC BY-NC-SA 4.0"
elif [[ -s "$notices/LICENSE" ]]; then
  fail "bundled LICENSE does not identify CC BY-NC-SA 4.0"
fi
if [[ -s "$notices/ATTRIBUTION.md" ]]; then
  attribution_lower="$(tr '[:upper:]' '[:lower:]' < "$notices/ATTRIBUTION.md")"
  [[ "$attribution_lower" == *"tim phillips"* ]] || fail "ATTRIBUTION.md must credit Tim Phillips"
  [[ "$attribution_lower" == *unofficial* ]] || fail "ATTRIBUTION.md must state that the fork is unofficial"
fi
if [[ -s "$notices/MODIFICATIONS.md" ]] && ! grep -Eiq 'modif|remaster|changed' "$notices/MODIFICATIONS.md"; then
  fail "MODIFICATIONS.md does not describe modifications"
fi

declare -a machos=()
if [[ -n "$executable" && -f "$executable" ]]; then
  machos+=("$executable")
fi
while IFS= read -r -d '' candidate; do
  [[ "$candidate" == "$executable" ]] && continue
  description="$($file_tool -b "$candidate" 2>/dev/null || true)"
  if [[ "$description" == *Mach-O* ]]; then
    machos+=("$candidate")
  fi
done < <(find "$app/Contents" -type f -print0 2>/dev/null)

if ((${#machos[@]} == 0)); then
  fail "no Mach-O files were found in the bundle"
fi

resolve_inside_contents() {
  local base="$1"
  local suffix="$2"
  "$python_tool" - "$app/Contents" "$base" "$suffix" <<'PY'
import os
import sys

contents = os.path.realpath(sys.argv[1])
candidate = os.path.realpath(os.path.join(sys.argv[2], sys.argv[3].lstrip("/")))
try:
    inside = os.path.commonpath((contents, candidate)) == contents
except ValueError:
    inside = False
if not inside:
    raise SystemExit(1)
print(candidate)
PY
}

resolve_bundle_token() {
  local token="$1"
  local loader="$2"
  local base=""
  local suffix=""
  case "$token" in
    @executable_path|@executable_path/*)
      base="$(dirname -- "$executable")"
      suffix="${token#@executable_path}"
      ;;
    @loader_path|@loader_path/*)
      base="$(dirname -- "$loader")"
      suffix="${token#@loader_path}"
      ;;
    *)
      return 1
      ;;
  esac
  resolve_inside_contents "$base" "$suffix"
}

# dyld keeps the main executable's LC_RPATH entries on the run-path stack while
# loading transitive dylib dependencies. Record those resolved bundle paths so
# a library such as SDL_image may legitimately find SDL through the executable
# without carrying a duplicate LC_RPATH of its own.
executable_rpaths_text=""
if [[ -n "$executable" && -f "$executable" ]]; then
  executable_load_commands="$($otool_tool -l "$executable" 2>/dev/null || true)"
  while IFS= read -r executable_rpath; do
    [[ -z "$executable_rpath" ]] && continue
    case "$executable_rpath" in
      @loader_path|@loader_path/*|@executable_path|@executable_path/*)
        if resolved_executable_rpath="$(resolve_bundle_token "$executable_rpath" "$executable" 2>/dev/null)"; then
          if [[ -z "$executable_rpaths_text" ]]; then
            executable_rpaths_text="$resolved_executable_rpath"
          else
            executable_rpaths_text="$executable_rpaths_text"$'\n'"$resolved_executable_rpath"
          fi
        fi
        ;;
    esac
  done < <(printf '%s\n' "$executable_load_commands" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { in_rpath=1; next }
    in_rpath && $1 == "path" { print $2; in_rpath=0 }
  ')
fi

for macho in "${machos[@]}"; do
  rel="${macho#"$app/"}"
  archs="$($lipo_tool -archs "$macho" 2>/dev/null || true)"
  has_x86=0
  has_arm=0
  for arch in $archs; do
    [[ "$arch" == "x86_64" ]] && has_x86=1
    [[ "$arch" == "arm64" ]] && has_arm=1
  done
  if ((has_x86 && has_arm)); then
    ok "$rel contains x86_64 and arm64 slices"
  else
    fail "$rel is not universal x86_64/arm64 (reported: '${archs:-none}')"
  fi

  load_commands="$($otool_tool -l "$macho" 2>/dev/null || true)"
  rpaths_text=""
  dylib_id=""
  if [[ -z "$load_commands" ]]; then
    fail "could not inspect Mach-O load commands for $rel"
  else
    # `otool -L` prints a dylib's own LC_ID_DYLIB as its first entry. It is an
    # install name, not a runtime dependency, and does not need to resolve from
    # that dylib's own LC_RPATH.
    dylib_id="$(printf '%s\n' "$load_commands" | awk '
      $1 == "cmd" && $2 == "LC_ID_DYLIB" { in_id=1; next }
      in_id && $1 == "name" { print $2; exit }
    ')"
    while IFS= read -r rpath; do
      [[ -z "$rpath" ]] && continue
      case "$rpath" in
        @loader_path|@loader_path/*|@executable_path|@executable_path/*)
          if resolved_rpath="$(resolve_bundle_token "$rpath" "$macho" 2>/dev/null)"; then
            if [[ -z "$rpaths_text" ]]; then
              rpaths_text="$resolved_rpath"
            else
              rpaths_text="$rpaths_text"$'\n'"$resolved_rpath"
            fi
          else
            fail "$rel has an LC_RPATH that escapes Contents: $rpath"
          fi
          ;;
        *) fail "$rel has a non-bundle-relative LC_RPATH: $rpath" ;;
      esac
    done < <(printf '%s\n' "$load_commands" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" { in_rpath=1; next }
      in_rpath && $1 == "path" { print $2; in_rpath=0 }
    ')
  fi

  load_output="$($otool_tool -L "$macho" 2>/dev/null || true)"
  if [[ -z "$load_output" ]]; then
    fail "could not inspect dynamic-library loads for $rel"
  else
    while IFS= read -r dependency; do
      dependency="${dependency#"${dependency%%[![:space:]]*}"}"
      dependency="${dependency%%[[:space:]]*}"
      [[ -z "$dependency" ]] && continue
      [[ -n "$dylib_id" && "$dependency" == "$dylib_id" ]] && continue
      case "$dependency" in
        /usr/lib/*|/System/Library/*|/Library/Apple/System/Library/*)
          ;;
        @loader_path|@loader_path/*|@executable_path|@executable_path/*)
          if resolved_dependency="$(resolve_bundle_token "$dependency" "$macho" 2>/dev/null)"; then
            if [[ ! -e "$resolved_dependency" ]]; then
              fail "$rel has an unresolved bundle-relative dependency: $dependency"
            fi
          else
            fail "$rel has a dependency that escapes Contents: $dependency"
          fi
          ;;
        @rpath/*)
          rpath_suffix="${dependency#@rpath}"
          resolved_from_rpath=0
          dependency_rpaths_text="$rpaths_text"
          if [[ "$macho" != "$executable" && -n "$executable_rpaths_text" ]]; then
            if [[ -n "$dependency_rpaths_text" ]]; then
              dependency_rpaths_text="$dependency_rpaths_text"$'\n'"$executable_rpaths_text"
            else
              dependency_rpaths_text="$executable_rpaths_text"
            fi
          fi
          while IFS= read -r rpath_base; do
            [[ -z "$rpath_base" ]] && continue
            if candidate="$(resolve_inside_contents "$rpath_base" "$rpath_suffix" 2>/dev/null)" && [[ -e "$candidate" ]]; then
              resolved_from_rpath=1
              break
            fi
          done <<<"$dependency_rpaths_text"
          if ((resolved_from_rpath == 0)); then
            fail "$rel has an @rpath dependency that does not resolve inside the bundle: $dependency"
          fi
          ;;
        *) fail "$rel has a non-system, non-bundle-relative dependency: $dependency" ;;
      esac
    done < <(printf '%s\n' "$load_output" | awk '
      /^[[:space:]]/ {
        dependency=$1
        if (dependency != "") print dependency
      }
    ')
  fi

  if [[ -n "$vtool_tool" ]]; then
    version_output="$($vtool_tool -show-build "$macho" 2>/dev/null || true)"
  else
    version_output="$load_commands"
  fi
  versions="$(printf '%s\n' "$version_output" | awk '$1 == "minos" { print $2 }')"
  if [[ -z "$versions" ]]; then
    fail "could not find LC_BUILD_VERSION minos for $rel"
  else
    mismatch=0
    while IFS= read -r version; do
      [[ "$version" == "$minimum_macos" ]] || mismatch=1
    done <<<"$versions"
    if ((mismatch)); then
      fail "$rel has a deployment target other than $minimum_macos: $(printf '%s' "$versions" | tr '\n' ' ')"
    else
      ok "$rel targets macOS $minimum_macos in every slice"
    fi
  fi
done

if [[ "$mode" == "release" && -n "$codesign_tool" && -x "$codesign_tool" ]]; then
  if "$codesign_tool" --verify --deep --strict --verbose=2 "$app" >/dev/null 2>&1; then
    ok "bundle signature passes strict deep verification"
  else
    fail "bundle signature does not pass strict deep verification"
  fi
  signature_info="$($codesign_tool -d --verbose=4 "$app" 2>&1 || true)"
  if printf '%s\n' "$signature_info" | grep -Eq '^Authority=Developer ID Application:'; then
    ok "bundle is signed with a Developer ID Application certificate"
  else
    fail "bundle is not signed with a Developer ID Application certificate"
  fi
  if printf '%s\n' "$signature_info" | grep -Eq '^TeamIdentifier=[A-Z0-9]+$'; then
    ok "signature contains a TeamIdentifier"
  else
    fail "signature TeamIdentifier is missing"
  fi
  if printf '%s\n' "$signature_info" | grep -Eiq '^CodeDirectory .*flags=.*runtime'; then
    ok "hardened runtime is enabled"
  else
    fail "signature does not enable hardened runtime"
  fi
fi

if ((require_notarization)) &&
    [[ -n "$xcrun_tool" && -x "$xcrun_tool" ]] &&
    [[ -n "$spctl_tool" && -x "$spctl_tool" ]]; then
  if "$xcrun_tool" stapler validate "$app" >/dev/null 2>&1; then
    ok "stapled notarization ticket validates"
  else
    fail "stapled notarization ticket is absent or invalid"
  fi
  if "$spctl_tool" --assess --type execute --verbose=4 "$app" >/dev/null 2>&1; then
    ok "Gatekeeper accepts the application"
  else
    fail "Gatekeeper rejects the application"
  fi
fi

if ((errors)); then
  printf 'macOS artifact verification failed: %d error(s), %d warning(s)\n' "$errors" "$warnings" >&2
  exit 1
fi

printf 'macOS artifact verification passed (%s mode): %d Mach-O file(s), %d warning(s)\n' \
  "$mode" "${#machos[@]}" "$warnings"
