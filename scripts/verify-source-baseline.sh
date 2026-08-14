#!/usr/bin/env bash

# Verify that a Realmz Remastered source tree is derived from the reviewed
# upstream snapshot and that all external content/dependency declarations are
# internally consistent. Development mode deliberately permits working-tree
# changes; release mode requires a reproducible, clean checkout.

set -uo pipefail

readonly DEFAULT_BASELINE_COMMIT="4089d550ab606172bac850ac055677c36c6ff547"
readonly DEFAULT_SDL_COMMIT="8e37db5e797b6167f3a00d697d816a684bd259c7"
readonly DEFAULT_SDL_IMAGE_COMMIT="bec9134a26c7d0f31b36d6083c25296e04cabff5"
readonly DEFAULT_SDL_TTF_COMMIT="a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b"
readonly DEFAULT_PHOSG_COMMIT="b2e0c12edb7e274a5e20c460f44eee44f49f57ef"
readonly DEFAULT_RESOURCE_DASM_COMMIT="27f64c89a5fed855e68c2a5e97b6c6c389d8eb19"

usage() {
  cat <<'USAGE'
Usage: scripts/verify-source-baseline.sh [options]

Options:
  --mode development|release  Development permits local changes; release
                              requires a clean tracked and untracked tree.
                              Default: development.
  --repo PATH                 Repository root. Default: script parent.
  -h, --help                  Show this help.

The verifier never modifies the repository. In development mode it reports
working changes without rejecting them, so it is safe to run during normal
implementation. Release mode also requires approved City of Bywater and music
provenance manifests; see docs/CONTENT_PROVENANCE.md.
USAGE
}

mode="development"
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo="$(CDPATH= cd -- "$script_dir/.." && pwd)"

while (($#)); do
  case "$1" in
    --mode)
      (($# >= 2)) || { echo "error: --mode requires a value" >&2; exit 2; }
      mode="$2"
      shift 2
      ;;
    --repo)
      (($# >= 2)) || { echo "error: --repo requires a value" >&2; exit 2; }
      repo="$2"
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

case "$mode" in
  development|release) ;;
  *) echo "error: mode must be development or release" >&2; exit 2 ;;
esac

if [[ ! -d "$repo" ]]; then
  echo "error: repository path does not exist: $repo" >&2
  exit 2
fi
repo="$(CDPATH= cd -- "$repo" && pwd)"

# Tests can substitute pins only when the explicit guard is set. Normal runs
# always use the reviewed production values above.
baseline_commit="$DEFAULT_BASELINE_COMMIT"
sdl_commit="$DEFAULT_SDL_COMMIT"
sdl_image_commit="$DEFAULT_SDL_IMAGE_COMMIT"
sdl_ttf_commit="$DEFAULT_SDL_TTF_COMMIT"
phosg_commit="$DEFAULT_PHOSG_COMMIT"
resource_dasm_commit="$DEFAULT_RESOURCE_DASM_COMMIT"
if [[ "$mode" == "development" && "${REALMZ_VERIFY_ALLOW_TEST_PINS:-0}" == "1" ]]; then
  baseline_commit="${REALMZ_TEST_BASELINE_COMMIT:-$baseline_commit}"
  sdl_commit="${REALMZ_TEST_SDL_COMMIT:-$sdl_commit}"
  sdl_image_commit="${REALMZ_TEST_SDL_IMAGE_COMMIT:-$sdl_image_commit}"
  sdl_ttf_commit="${REALMZ_TEST_SDL_TTF_COMMIT:-$sdl_ttf_commit}"
  phosg_commit="${REALMZ_TEST_PHOSG_COMMIT:-$phosg_commit}"
  resource_dasm_commit="${REALMZ_TEST_RESOURCE_DASM_COMMIT:-$resource_dasm_commit}"
fi

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

require_file() {
  local rel="$1"
  if [[ -f "$repo/$rel" ]]; then
    ok "$rel exists"
  else
    fail "required source file is missing: $rel"
  fi
}

contains_literal() {
  local rel="$1"
  local literal="$2"
  local description="$3"
  if [[ -f "$repo/$rel" ]] && grep -Fq -- "$literal" "$repo/$rel"; then
    ok "$description"
  else
    fail "$description is not declared in $rel"
  fi
}

if ! command -v git >/dev/null 2>&1; then
  fail "git is required"
else
  if ! git -C "$repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    fail "$repo is not a Git working tree"
  else
    if git -C "$repo" cat-file -e "${baseline_commit}^{commit}" 2>/dev/null; then
      ok "reviewed upstream commit $baseline_commit is present"
      if git -C "$repo" merge-base --is-ancestor "$baseline_commit" HEAD 2>/dev/null; then
        ok "HEAD descends from the reviewed upstream commit"
      else
        fail "HEAD does not descend from reviewed upstream commit $baseline_commit"
      fi
    else
      fail "reviewed upstream commit is absent: $baseline_commit (use a full-history checkout)"
    fi

    tracked_changes=0
    staged_changes=0
    untracked_changes=0
    git -C "$repo" diff --quiet --no-ext-diff 2>/dev/null || tracked_changes=1
    git -C "$repo" diff --cached --quiet --no-ext-diff 2>/dev/null || staged_changes=1
    # `--directory` collapses untracked build trees instead of recursively
    # enumerating them. We need only know whether one exists for the clean gate.
    if [[ -n "$(git -C "$repo" ls-files --others --exclude-standard --directory 2>/dev/null | sed -n '1p')" ]]; then
      untracked_changes=1
    fi
    if ((tracked_changes || staged_changes || untracked_changes)); then
      if [[ "$mode" == "release" ]]; then
        fail "release verification requires a clean tracked, staged, and untracked tree"
      else
        warn "development tree has intentional/uncommitted changes; provenance checks continue"
      fi
    else
      ok "working tree is clean"
    fi

    declare -a submodule_checks=(
      "vendored/SDL|$sdl_commit|https://github.com/libsdl-org/SDL"
      "vendored/SDL_image|$sdl_image_commit|https://github.com/libsdl-org/SDL_image"
      "vendored/SDL_ttf|$sdl_ttf_commit|https://github.com/libsdl-org/SDL_ttf"
    )
    for check in "${submodule_checks[@]}"; do
      IFS='|' read -r path expected url <<<"$check"
      actual="$(git -C "$repo" ls-tree HEAD -- "$path" 2>/dev/null | awk '$1 == "160000" { print $3 }')"
      if [[ "$actual" == "$expected" ]]; then
        ok "$path gitlink is pinned to $expected"
      elif [[ -z "$actual" ]]; then
        fail "$path is not a gitlink in HEAD"
      else
        fail "$path gitlink is $actual; expected $expected"
      fi

      module_name="${path#vendored/}"
      declared_url="$(git -C "$repo" config -f .gitmodules --get "submodule.vendored/${module_name}.url" 2>/dev/null || true)"
      if [[ "$declared_url" == "$url" || "$declared_url" == "$url.git" ]]; then
        ok "$path uses the reviewed upstream URL"
      else
        fail "$path URL is '${declared_url:-missing}'; expected $url"
      fi

      # An uninitialized submodule is acceptable for dependency-free QA. When
      # initialized, its checkout must match the recorded pin and be clean in
      # release mode.
      if git -C "$repo/$path" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        checkout="$(git -C "$repo/$path" rev-parse HEAD 2>/dev/null || true)"
        if [[ "$checkout" == "$expected" ]]; then
          ok "$path checkout matches its gitlink"
        else
          fail "$path checkout is ${checkout:-unreadable}; expected $expected"
        fi
        if [[ "$mode" == "release" ]] && [[ -n "$(git -C "$repo/$path" status --porcelain 2>/dev/null || true)" ]]; then
          fail "$path has local changes in release mode"
        fi
      fi
    done
  fi
fi

for required in README.md CMakeLists.txt .gitmodules LICENSE \
  docs/CONTENT_PROVENANCE.md scripts/bootstrap-macos-dependencies.sh \
  scripts/build-placeholder-app-icon.sh bundle/AppIcon.placeholder.bmp \
  bundle/AppIcon.icns; do
  require_file "$required"
done

contains_literal README.md "$phosg_commit" "phosg commit pin"
contains_literal README.md "$resource_dasm_commit" "resource_dasm/resource_file commit pin"
contains_literal scripts/bootstrap-macos-dependencies.sh "$phosg_commit" "phosg bootstrap commit pin"
contains_literal scripts/bootstrap-macos-dependencies.sh "$resource_dasm_commit" "resource_dasm bootstrap commit pin"
contains_literal scripts/bootstrap-macos-dependencies.sh 'x86_64;arm64' "universal dependency bootstrap architecture default"
contains_literal scripts/bootstrap-macos-dependencies.sh 'deployment_target="13.3"' "dependency bootstrap deployment target"
contains_literal CMakeLists.txt "find_package(phosg REQUIRED)" "required phosg package declaration"
contains_literal CMakeLists.txt "find_package(resource_file REQUIRED)" "required resource_file package declaration"
contains_literal docs/CONTENT_PROVENANCE.md "$baseline_commit" "upstream source baseline declaration"
contains_literal docs/CONTENT_PROVENANCE.md "$sdl_commit" "SDL provenance pin"
contains_literal docs/CONTENT_PROVENANCE.md "$sdl_image_commit" "SDL_image provenance pin"
contains_literal docs/CONTENT_PROVENANCE.md "$sdl_ttf_commit" "SDL_ttf provenance pin"
contains_literal docs/CONTENT_PROVENANCE.md "$phosg_commit" "phosg provenance pin"
contains_literal docs/CONTENT_PROVENANCE.md "$resource_dasm_commit" "resource_file provenance pin"
contains_literal LICENSE "Creative Commons Attribution-NonCommercial-ShareAlike 4.0" "CC BY-NC-SA 4.0 license text"

# Asset census files are generated by the asset pipeline. A partially checked
# in set is always an error. A complete set is validated without regenerating
# it, so this verifier remains read-only.
asset_tool="scripts/remaster_asset_census.py"
asset_scope="assets/remastered/scopes/phase1.json"
asset_census="assets/remastered/scopes/phase1.census.json"
asset_manifest="assets/remastered/scopes/phase1.placeholder-manifest.json"
asset_runtime_builder="scripts/build_runtime_asset_manifest.py"
asset_runtime_manifest="assets/remastered/scopes/phase1.runtime-manifest.json"
asset_present=0
for rel in "$asset_tool" "$asset_scope" "$asset_census" "$asset_manifest" \
    "$asset_runtime_builder" "$asset_runtime_manifest"; do
  [[ -f "$repo/$rel" ]] && asset_present=$((asset_present + 1))
done

if ((asset_present == 0)); then
  if [[ "$mode" == "release" ]]; then
    fail "phase-one asset census and manifest are required in release mode"
  else
    warn "phase-one asset census has not landed yet"
  fi
elif ((asset_present != 6)); then
  fail "phase-one asset census is incomplete ($asset_present of 6 required files present)"
else
  if command -v python3 >/dev/null 2>&1; then
    if (
      cd "$repo" &&
      python3 "$asset_tool" validate \
        --root . \
        --scope "$asset_scope" \
        --census "$asset_census" \
        --manifest "$asset_manifest" &&
      python3 "$asset_runtime_builder" --root . \
        --output "$asset_runtime_manifest" --check &&
      python3 "$asset_tool" validate \
        --root . \
        --scope "$asset_scope" \
        --census "$asset_census" \
        --manifest "$asset_runtime_manifest"
    ); then
      ok "phase-one asset census, placeholder, and runtime manifest validate"
    else
      fail "phase-one asset census validation failed"
    fi
  else
    fail "python3 is required to validate the phase-one asset census"
  fi
fi

# Validate content provenance records in one place. Missing approval records
# are warnings during development and hard blockers for a release.
if command -v python3 >/dev/null 2>&1; then
  if python3 - "$repo" "$mode" <<'PY'
import hashlib
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
provenance_dir = root / "provenance" / "content"
city_path = provenance_dir / "city-of-bywater-mac-7.1.2.json"
music_path = provenance_dir / "phase1-music.json"
errors = []
warnings = []
sha_re = re.compile(r"^[0-9a-fA-F]{64}$")
date_re = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def load_json(path):
    try:
        with path.open("r", encoding="utf-8") as f:
            value = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read {path.relative_to(root)}: {exc}")
        return None
    if not isinstance(value, dict):
        errors.append(f"{path.relative_to(root)} must contain a JSON object")
        return None
    if value.get("schema_version") != 1:
        errors.append(f"{path.relative_to(root)} schema_version must be 1")
    return value


if provenance_dir.exists():
    for candidate in sorted(provenance_dir.glob("*.json")):
        if candidate not in (city_path, music_path):
            load_json(candidate)

if city_path.exists():
    city = load_json(city_path)
    if city:
        if city.get("content_id") != "city-of-bywater-mac-7.1.2":
            errors.append("City provenance content_id is incorrect")
        if city.get("version") != "Mac 7.1.2":
            errors.append("City provenance version must be 'Mac 7.1.2'")
        if city.get("status") != "approved-reference":
            errors.append("City provenance status must be approved-reference")
        artifact = city.get("artifact")
        authorization = city.get("authorization")
        if not isinstance(artifact, dict):
            errors.append("City provenance requires an artifact object")
        else:
            if not sha_re.fullmatch(str(artifact.get("sha256", ""))):
                errors.append("City artifact sha256 must be a 64-digit hex digest")
            if not isinstance(artifact.get("size_bytes"), int) or artifact["size_bytes"] <= 0:
                errors.append("City artifact size_bytes must be positive")
            if artifact.get("stored_in_repository") is not False:
                errors.append("authorized City baseline must not be stored in the repository")
        if not isinstance(authorization, dict):
            errors.append("City provenance requires an authorization object")
        else:
            for field in ("basis", "reviewer"):
                if not str(authorization.get(field, "")).strip():
                    errors.append(f"City authorization {field} is required")
            if not date_re.fullmatch(str(authorization.get("reviewed_at", ""))):
                errors.append("City authorization reviewed_at must be YYYY-MM-DD")
            if authorization.get("approved_as_generation_reference") is not True:
                errors.append("City baseline is not approved as a generation reference")
        differences = city.get("differences_from_repository")
        if not isinstance(differences, list) or not differences:
            errors.append("City provenance must document differences_from_repository")
else:
    (errors if mode == "release" else warnings).append(
        "authorized Mac 7.1.2 City of Bywater provenance record is missing"
    )

music_files = []
music_root = root / "base" / "Realmz" / "Realmz Music"
if music_root.is_dir():
    music_files = [
        p for p in sorted(music_root.iterdir())
        if p.is_file() and not p.name.lower().startswith("where to find")
    ]

if music_path.exists():
    music = load_json(music_path)
    if music:
        if music.get("content_id") != "phase1-music" or music.get("status") != "reviewed":
            errors.append("music provenance must identify reviewed phase1-music")
        tracks = music.get("tracks")
        if not isinstance(tracks, list):
            errors.append("music provenance tracks must be an array")
            tracks = []
        by_path = {}
        for index, track in enumerate(tracks):
            if not isinstance(track, dict):
                errors.append(f"music track {index} must be an object")
                continue
            rel = str(track.get("path", ""))
            if not rel or rel in by_path:
                errors.append(f"music track {index} has a missing or duplicate path")
                continue
            by_path[rel] = track
            distribution = track.get("distribution")
            if distribution not in ("bundled-cleared", "user-import-only"):
                errors.append(f"music track {rel} has invalid distribution status")
                continue
            if not str(track.get("reviewer", "")).strip() or not date_re.fullmatch(str(track.get("reviewed_at", ""))):
                errors.append(f"music track {rel} needs reviewer and YYYY-MM-DD reviewed_at")
            host = root / rel
            if distribution == "bundled-cleared":
                for field in ("composition_provenance", "sample_provenance"):
                    if not str(track.get(field, "")).strip():
                        errors.append(f"bundled music track {rel} lacks {field}")
                digest = str(track.get("sha256", ""))
                if not sha_re.fullmatch(digest):
                    errors.append(f"bundled music track {rel} has invalid sha256")
                elif not host.is_file():
                    errors.append(f"bundled music track is missing: {rel}")
                else:
                    actual = hashlib.sha256(host.read_bytes()).hexdigest()
                    if actual.lower() != digest.lower():
                        errors.append(f"bundled music track hash mismatch: {rel}")
            elif host.exists():
                errors.append(f"user-import-only music must not be tracked: {rel}")

        for host in music_files:
            rel = host.relative_to(root).as_posix()
            record = by_path.get(rel)
            if not record or record.get("distribution") != "bundled-cleared":
                errors.append(f"bundled music lacks a cleared provenance record: {rel}")
else:
    (errors if mode == "release" else warnings).append(
        "reviewed composition/sample provenance for phase-one music is missing"
    )

for warning in warnings:
    print(f"warning: {warning}", file=sys.stderr)
for error in errors:
    print(f"error: {error}", file=sys.stderr)
sys.exit(1 if errors else 0)
PY
  then
    ok "content provenance manifests validate for $mode mode"
  else
    fail "content provenance manifest validation failed"
  fi
else
  fail "python3 is required to validate content provenance manifests"
fi

if ((errors)); then
  printf 'source baseline verification failed: %d verifier error(s); see diagnostics above\n' "$errors" >&2
  exit 1
fi

if [[ "$mode" == "development" ]]; then
  printf 'source baseline verification passed (development mode); any warnings above are non-blocking\n'
else
  printf 'source baseline verification passed (release mode)\n'
fi
