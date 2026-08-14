#!/usr/bin/env bash

# Build the two non-submodule Realmz dependencies from reviewed commits.
# The default output is a universal macOS 13.3 prefix suitable for configuring
# Realmz with -DCMAKE_PREFIX_PATH=<prefix>.

set -euo pipefail

readonly PHOSG_URL="https://github.com/fuzziqersoftware/phosg.git"
readonly PHOSG_COMMIT="b2e0c12edb7e274a5e20c460f44eee44f49f57ef"
readonly RESOURCE_DASM_URL="https://github.com/fuzziqersoftware/resource_dasm.git"
readonly RESOURCE_DASM_COMMIT="27f64c89a5fed855e68c2a5e97b6c6c389d8eb19"

usage() {
  cat <<'USAGE'
Usage: scripts/bootstrap-macos-dependencies.sh [options]

Options:
  --work-dir PATH          Source and build workspace. Default: build/dependencies
  --prefix PATH            Installation prefix. Default: <work-dir>/install
  --architectures LIST     CMake architecture list. Default: x86_64;arm64
  --deployment-target VER  Minimum macOS version. Default: 13.3
  --build-type TYPE        CMake build type. Default: Release
  --jobs N                 Parallel build jobs. Default: logical CPU count
  -h, --help               Show this help.

Existing source directories are accepted only when they are the expected Git
repositories at the exact reviewed commits. The script never resets or deletes
an existing checkout.
USAGE
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/.." && pwd)"
work_dir="$repo_root/build/dependencies"
prefix=""
architectures="x86_64;arm64"
deployment_target="13.3"
build_type="Release"
jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')"

while (($#)); do
  case "$1" in
    --work-dir)
      (($# >= 2)) || { echo "error: --work-dir requires a value" >&2; exit 2; }
      work_dir="$2"
      shift 2
      ;;
    --prefix)
      (($# >= 2)) || { echo "error: --prefix requires a value" >&2; exit 2; }
      prefix="$2"
      shift 2
      ;;
    --architectures)
      (($# >= 2)) || { echo "error: --architectures requires a value" >&2; exit 2; }
      architectures="$2"
      shift 2
      ;;
    --deployment-target)
      (($# >= 2)) || { echo "error: --deployment-target requires a value" >&2; exit 2; }
      deployment_target="$2"
      shift 2
      ;;
    --build-type)
      (($# >= 2)) || { echo "error: --build-type requires a value" >&2; exit 2; }
      build_type="$2"
      shift 2
      ;;
    --jobs)
      (($# >= 2)) || { echo "error: --jobs requires a value" >&2; exit 2; }
      jobs="$2"
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

[[ "$(uname -s)" == "Darwin" ]] || {
  echo "error: this bootstrap builds macOS dependencies and must run on macOS" >&2
  exit 1
}
[[ "$deployment_target" =~ ^[0-9]+([.][0-9]+){1,2}$ ]] || {
  echo "error: invalid deployment target: $deployment_target" >&2
  exit 2
}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || {
  echo "error: --jobs must be a positive integer" >&2
  exit 2
}
for command_name in cmake git lipo; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "error: required command is unavailable: $command_name" >&2
    exit 1
  }
done

mkdir -p "$work_dir"
work_dir="$(CDPATH= cd -- "$work_dir" && pwd)"
lock_dir="$work_dir/.bootstrap.lock"
if ! mkdir "$lock_dir" 2>/dev/null; then
  echo "error: another dependency bootstrap is using $work_dir" >&2
  exit 1
fi
cleanup_lock() {
  rmdir "$lock_dir" 2>/dev/null || true
}
trap cleanup_lock EXIT INT TERM
if [[ -z "$prefix" ]]; then
  prefix="$work_dir/install"
fi
mkdir -p "$prefix"
prefix="$(CDPATH= cd -- "$prefix" && pwd)"

prepare_source() {
  local name="$1"
  local url="$2"
  local commit="$3"
  local source="$work_dir/$name"

  if [[ ! -e "$source" ]]; then
    git clone --no-checkout "$url" "$source"
    git -C "$source" checkout --detach "$commit"
  elif [[ ! -d "$source/.git" ]]; then
    echo "error: existing dependency path is not a Git checkout: $source" >&2
    exit 1
  fi

  local actual
  actual="$(git -C "$source" rev-parse HEAD)"
  if [[ "$actual" != "$commit" ]]; then
    echo "error: $name is at $actual; expected reviewed commit $commit" >&2
    exit 1
  fi
  if ! git -C "$source" diff --quiet --no-ext-diff ||
      ! git -C "$source" diff --cached --quiet --no-ext-diff; then
    echo "error: $name has tracked modifications: $source" >&2
    exit 1
  fi
  printf '%s\n' "$source"
}

configure_and_install() {
  local source="$1"
  local build="$2"
  shift 2
  cmake --fresh -S "$source" -B "$build" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_OSX_ARCHITECTURES="$architectures" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    "$@"
  cmake --build "$build" --target install --parallel "$jobs"
}

phosg_source="$(prepare_source phosg "$PHOSG_URL" "$PHOSG_COMMIT")"
configure_and_install "$phosg_source" "$work_dir/phosg-build"

resource_source="$(prepare_source resource_dasm "$RESOURCE_DASM_URL" "$RESOURCE_DASM_COMMIT")"
configure_and_install "$resource_source" "$work_dir/resource-dasm-build" \
  -DCMAKE_PREFIX_PATH="$prefix" \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=TRUE

for library in libphosg.a libresource_file.a; do
  path="$prefix/lib/$library"
  [[ -f "$path" ]] || {
    echo "error: expected installed library is missing: $path" >&2
    exit 1
  }
  archs="$(lipo -archs "$path")"
  for expected_arch in ${architectures//;/ }; do
    case " $archs " in
      *" $expected_arch "*) ;;
      *)
        echo "error: $library lacks $expected_arch (reported: $archs)" >&2
        exit 1
        ;;
    esac
  done
done

for config in \
  "$prefix/lib/cmake/phosg/phosgConfig.cmake" \
  "$prefix/lib/cmake/resource_file/resource_fileConfig.cmake"; do
  [[ -f "$config" ]] || {
    echo "error: installed CMake package is missing: $config" >&2
    exit 1
  }
done

printf 'Realmz dependency bootstrap passed\n'
printf '  prefix: %s\n' "$prefix"
printf '  architectures: %s\n' "$architectures"
printf '  deployment target: macOS %s\n' "$deployment_target"
