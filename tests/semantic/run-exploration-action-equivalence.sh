#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
cxx="${CXX:-c++}"

if ! command -v "$cxx" >/dev/null 2>&1; then
  echo "error: C++ compiler not found: $cxx" >&2
  exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/realmz-semantic-test.XXXXXX")"
cleanup() {
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT INT TERM

flags=(
  -std=c++23
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Isrc
)

if [[ "${REALMZ_ENABLE_SANITIZERS:-0}" == "1" ]]; then
  flags+=(
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
fi

(
  cd "$repo"
  "$cxx" "${flags[@]}" \
    tests/semantic/ExplorationActionEquivalenceTest.cpp \
    src/presentation/LegacyCommandBridge.cpp \
    -o "$tmp_dir/ExplorationActionEquivalenceTest"
)

"$tmp_dir/ExplorationActionEquivalenceTest"
