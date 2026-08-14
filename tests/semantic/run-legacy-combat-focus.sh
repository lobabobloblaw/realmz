#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
cc="${CC:-cc}"
cxx="${CXX:-c++}"

if ! command -v "$cc" >/dev/null 2>&1; then
  echo "error: C compiler not found: $cc" >&2
  exit 1
fi
if ! command -v "$cxx" >/dev/null 2>&1; then
  echo "error: C++ compiler not found: $cxx" >&2
  exit 1
fi

test_source="$repo/src/tests/LegacyCombatFocusTest.cpp"
if [[ ! -f "$test_source" ]]; then
  echo "error: Legacy combat-focus fixture not found: $test_source" >&2
  exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/realmz-legacy-combat-focus.XXXXXX")"
cleanup() {
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT INT TERM

include_flags=(
  -Isrc
  -Isrc/realmz_orig
  -Ivendored/SDL/include
)
c_flags=(
  -std=c99
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wno-zero-length-array
  -Wno-strict-prototypes
  -Wno-char-subscripts
  "${include_flags[@]}"
)
cxx_flags=(
  -std=c++23
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  "${include_flags[@]}"
)

if [[ "${REALMZ_ENABLE_SANITIZERS:-0}" == "1" ]]; then
  c_flags+=(
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
  cxx_flags+=(
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
fi

(
  cd "$repo"
  "$cc" "${c_flags[@]}" \
    -c src/realmz_orig/centerstage.c \
    -o "$tmp_dir/centerstage.o"
  "$cxx" "${cxx_flags[@]}" \
    src/tests/LegacyCombatFocusTest.cpp \
    "$tmp_dir/centerstage.o" \
    -o "$tmp_dir/LegacyCombatFocusTest"
)

"$tmp_dir/LegacyCombatFocusTest"
