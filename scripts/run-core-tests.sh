#!/usr/bin/env bash

# Compile and run the dependency-free presentation and asset tests without
# configuring SDL, phosg, resource_file, or the application bundle.

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo="$(CDPATH= cd -- "$script_dir/.." && pwd)"
cxx="${CXX:-c++}"
cc="${CC:-cc}"

if ! command -v "$cxx" >/dev/null 2>&1; then
  echo "error: C++ compiler not found: $cxx" >&2
  exit 1
fi
if ! command -v "$cc" >/dev/null 2>&1; then
  echo "error: C compiler not found: $cc" >&2
  exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/realmz-core-tests.XXXXXX")"
cleanup() {
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT INT TERM

common_flags=(
  # macOS 14's Apple Clang implements C++23 under its pre-standard spelling.
  -std=c++2b
  -Wall
  -Wextra
  -pedantic
  -Werror
  # Aggregate fixtures intentionally rely on default member initialization.
  -Wno-missing-field-initializers
  -Isrc
)

run_cpp_test() {
  local name="$1"
  shift
  local test_source="$1"
  if [[ ! -f "$repo/$test_source" ]]; then
    echo "notice: $name has not landed; skipping $test_source"
    return 0
  fi
  local source
  local source_path
  for source in "$@"; do
    source_path="$source"
    if [[ "$source_path" != /* ]]; then
      source_path="$repo/$source_path"
    fi
    if [[ ! -f "$source_path" ]]; then
      echo "error: $name is present but required source is missing: $source" >&2
      return 1
    fi
  done

  echo "Compiling $name"
  (
    cd "$repo"
    "$cxx" "${common_flags[@]}" "$@" -o "$tmp_dir/$name"
  )
  echo "Running $name"
  "$tmp_dir/$name" "$repo"
}

run_cpp_test PresentationCoreTest \
  src/tests/PresentationCoreTest.cpp \
  src/presentation/ResponsiveLayout.cpp \
  src/presentation/LegacyCommandBridge.cpp \
  src/presentation/PresentationHost.cpp \
  src/presentation/UIPrimitives.cpp

run_cpp_test AdaptiveShellTest \
  src/tests/AdaptiveShellTest.cpp \
  src/presentation/AdaptiveShell.cpp \
  src/presentation/RemasteredInputMapper.cpp \
  src/presentation/ResponsiveLayout.cpp

run_cpp_test GameplayChromeCoverageTest \
  src/tests/GameplayChromeCoverageTest.cpp \
  src/presentation/GameplayChromeCoverage.cpp

run_cpp_test RemasteredInputMapperTest \
  src/tests/RemasteredInputMapperTest.cpp \
  src/presentation/RemasteredInputMapper.cpp

run_cpp_test PartyRailModelTest \
  src/tests/PartyRailModelTest.cpp \
  src/presentation/PartyRailModel.cpp

run_cpp_test PartyRailLayoutTest \
  src/tests/PartyRailLayoutTest.cpp \
  src/presentation/PartyRailLayout.cpp

run_cpp_test SelectedPartyDetailsLayoutTest \
  src/tests/SelectedPartyDetailsLayoutTest.cpp \
  src/presentation/SelectedPartyDetailsLayout.cpp

run_cpp_test PartyRailControlLayoutTest \
  src/tests/PartyRailControlLayoutTest.cpp \
  src/presentation/PartyRailControlLayout.cpp \
  src/presentation/PartyRailLayout.cpp \
  src/presentation/ResponsiveLayout.cpp

run_cpp_test DrawerControlLayoutTest \
  src/tests/DrawerControlLayoutTest.cpp \
  src/presentation/DrawerControlLayout.cpp \
  src/presentation/PartyRailModel.cpp \
  src/presentation/ResponsiveLayout.cpp \
  src/presentation/ShellKeyboardInteraction.cpp

run_cpp_test RuntimeLegacyCommandBridgeTest \
  src/tests/RuntimeLegacyCommandBridgeTest.cpp \
  src/presentation/RuntimeLegacyCommandBridge.cpp \
  src/presentation/LegacyCommandBridge.cpp

run_cpp_test ShellControlLayoutTest \
  src/tests/ShellControlLayoutTest.cpp \
  src/presentation/ShellControlLayout.cpp \
  src/presentation/AdaptiveShell.cpp \
  src/presentation/ResponsiveLayout.cpp

run_cpp_test ShellKeyboardInteractionTest \
  src/tests/ShellKeyboardInteractionTest.cpp \
  src/presentation/ShellKeyboardInteraction.cpp

run_cpp_test SemanticInputBoundaryTest \
  src/tests/SemanticInputBoundaryTest.cpp \
  src/presentation/SemanticInputBoundary.cpp \
  src/presentation/RuntimeLegacyCommandBridge.cpp \
  src/presentation/LegacyCommandBridge.cpp

run_cpp_test SemanticTopLevelLoopContractTest \
  src/tests/SemanticTopLevelLoopContractTest.cpp

echo "Compiling LegacyPresentationContextTest"
(
  cd "$repo"
  "$cc" -std=c99 -Wall -Wextra -pedantic -Werror -Isrc \
    -c src/presentation/LegacyPresentationContext.c \
    -o "$tmp_dir/LegacyPresentationContext.o"
  "$cxx" "${common_flags[@]}" \
    src/tests/LegacyPresentationContextTest.cpp \
    "$tmp_dir/LegacyPresentationContext.o" \
    -o "$tmp_dir/LegacyPresentationContextTest"
)
echo "Running LegacyPresentationContextTest"
"$tmp_dir/LegacyPresentationContextTest" "$repo"

echo "Compiling LegacyTorchSource"
(
  cd "$repo"
  "$cc" -std=c99 -Wall -Wextra -pedantic -Werror -Isrc \
    -c src/presentation/LegacyTorchSource.c \
    -o "$tmp_dir/LegacyTorchSource.o"
)

echo "Compiling SemanticCombatLegacyAdapterTest"
(
  cd "$repo"
  "$cxx" "${common_flags[@]}" \
    src/tests/SemanticCombatLegacyAdapterTest.cpp \
    src/presentation/SemanticInputBoundary.cpp \
    src/presentation/LegacyGameSnapshotSource.cpp \
    src/presentation/RuntimeLegacyCommandBridge.cpp \
    src/presentation/LegacyCommandBridge.cpp \
    "$tmp_dir/LegacyPresentationContext.o" \
    "$tmp_dir/LegacyTorchSource.o" \
    -o "$tmp_dir/SemanticCombatLegacyAdapterTest"
)
echo "Running SemanticCombatLegacyAdapterTest"
"$tmp_dir/SemanticCombatLegacyAdapterTest"

echo "Compiling LegacyPartySelectionTest"
(
  cd "$repo"
  "$cc" -std=c99 -Wall -Wextra -pedantic -Werror -Isrc \
    -c src/presentation/LegacyPartySelection.c \
    -o "$tmp_dir/LegacyPartySelection.o"
  "$cxx" "${common_flags[@]}" \
    src/tests/LegacyPartySelectionTest.cpp \
    "$tmp_dir/LegacyPartySelection.o" \
    -o "$tmp_dir/LegacyPartySelectionTest"
)
echo "Running LegacyPartySelectionTest"
"$tmp_dir/LegacyPartySelectionTest"

run_cpp_test AssetResolverTest \
  src/tests/AssetResolverTest.cpp \
  src/remaster/assets/AssetManifest.cpp \
  src/remaster/assets/AssetResolver.cpp

run_cpp_test ShellMaterialCatalogTest \
  src/tests/ShellMaterialCatalogTest.cpp \
  src/remaster/assets/AssetManifest.cpp \
  src/remaster/assets/AssetResolver.cpp \
  src/remaster/assets/ShellMaterialCatalog.cpp

run_cpp_test PartyPortraitCatalogTest \
  src/tests/PartyPortraitCatalogTest.cpp \
  src/remaster/assets/AssetManifest.cpp \
  src/remaster/assets/AssetResolver.cpp \
  src/remaster/assets/PartyPortraitCatalog.cpp \
  src/remaster/assets/PayloadDigest.cpp \
  src/remaster/assets/ResourceSelectionHook.cpp

run_cpp_test ResourceSelectionHookTest \
  src/tests/ResourceSelectionHookTest.cpp \
  src/remaster/assets/AssetManifest.cpp \
  src/remaster/assets/AssetResolver.cpp \
  src/remaster/assets/PayloadDigest.cpp \
  src/remaster/assets/ResourceSelectionHook.cpp

run_cpp_test RuntimePresentationContractTest \
  src/tests/RuntimePresentationContractTest.cpp \
  src/presentation/LegacyCommandBridge.cpp \
  src/presentation/PresentationHost.cpp \
  src/remaster/assets/AssetManifest.cpp \
  src/remaster/assets/AssetResolver.cpp

run_cpp_test LegacyGameSnapshotSourceTest \
  src/tests/LegacyGameSnapshotSourceTest.cpp \
  src/presentation/LegacyGameSnapshotSource.cpp \
  "$tmp_dir/LegacyTorchSource.o"

run_cpp_test LegacyReplayStateSourceTest \
  src/tests/LegacyReplayStateSourceTest.cpp \
  src/replay/LegacyReplayStateSource.cpp

run_cpp_test UserDataMigrationTest \
  src/tests/UserDataMigrationTest.cpp \
  src/userdata/UserDataMigration.cpp

run_cpp_test UserDataPathPolicyTest \
  src/tests/UserDataPathPolicyTest.cpp \
  src/userdata/UserDataPathPolicy.cpp

run_cpp_test ReplayChildConfigTest \
  src/tests/ReplayChildConfigTest.cpp \
  src/replay/ReplayChildConfig.cpp

run_cpp_test DeterministicReplayRngTest \
  src/tests/DeterministicReplayRngTest.cpp \
  src/replay/DeterministicReplayRng.cpp

run_cpp_test ReplayRuntimeTest \
  src/tests/ReplayRuntimeTest.cpp \
  src/replay/ReplayRuntime.cpp \
  src/replay/ReplayChildConfig.cpp \
  src/replay/DeterministicReplayRng.cpp \
  src/replay/ReplayDriver.cpp \
  src/replay/ReplayStateOracle.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplayActionDecoderTest \
  src/tests/ReplayActionDecoderTest.cpp \
  src/replay/ReplayActionDecoder.cpp

run_cpp_test ReplayDriverTest \
  src/tests/ReplayDriverTest.cpp \
  src/replay/ReplayDriver.cpp \
  src/replay/ReplayStateOracle.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplayCompletionTest \
  src/tests/ReplayCompletionTest.cpp \
  src/replay/ReplayCompletion.cpp \
  src/replay/ReplayRuntime.cpp \
  src/replay/ReplayChildConfig.cpp \
  src/replay/DeterministicReplayRng.cpp \
  src/replay/ReplayDriver.cpp \
  src/replay/ReplayStateOracle.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplayResultWriterTest \
  src/tests/ReplayResultWriterTest.cpp \
  src/replay/ReplayResultWriter.cpp \
  src/replay/ReplayChildConfig.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplayStateOracleTest \
  src/tests/ReplayStateOracleTest.cpp \
  src/replay/ReplayStateOracle.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplaySlotSelectionTest \
  src/tests/ReplaySlotSelectionTest.cpp \
  src/replay/ReplaySlotSelection.cpp

run_cpp_test ReplayLoadedGameEntryContractTest \
  src/tests/ReplayLoadedGameEntryContractTest.cpp

run_cpp_test Sha256Test \
  src/tests/Sha256Test.cpp \
  src/replay/Sha256.cpp

run_cpp_test ReplayOutputOracleTest \
  src/tests/ReplayOutputOracleTest.cpp \
  src/replay/ReplayOutputOracle.cpp \
  src/replay/Sha256.cpp

run_cpp_test SemanticReplayEventIsolationContractTest \
  src/tests/SemanticReplayEventIsolationContractTest.cpp

run_cpp_test SemanticReplayStartupContractTest \
  src/tests/SemanticReplayStartupContractTest.cpp

if [[ "${REALMZ_SKIP_PYTHON_TESTS:-0}" != "1" ]] &&
    find "$repo/tests/release" -maxdepth 1 -name 'test_*.py' -print -quit 2>/dev/null | grep -q .; then
  echo "Running release verifier tests"
  (
    cd "$repo"
    python3 -m unittest discover -s tests/release -p 'test_*.py' -v
  )
fi

if [[ "${REALMZ_SKIP_PYTHON_TESTS:-0}" != "1" ]] &&
    find "$repo/tests/assets" -maxdepth 1 -name 'test_*.py' -print -quit 2>/dev/null | grep -q .; then
  echo "Running asset pipeline tests"
  (
    cd "$repo"
    python3 -m unittest discover -s tests/assets -p 'test_*.py' -v
  )
fi

if [[ "${REALMZ_SKIP_PYTHON_TESTS:-0}" != "1" ]] &&
    find "$repo/tests/semantic" -maxdepth 1 \
      -name 'test_semantic_replay_*.py' -print -quit 2>/dev/null | grep -q .; then
  echo "Running semantic replay protocol and equivalence tests"
  (
    cd "$repo"
    python3 -m unittest discover \
      -s tests/semantic \
      -p 'test_semantic_replay_*.py' \
      -v
  )
fi

if [[ -x "$repo/tests/semantic/run-exploration-action-equivalence.sh" ]]; then
  echo "Running semantic action-equivalence checks"
  "$repo/tests/semantic/run-exploration-action-equivalence.sh"
fi

if [[ -x "$repo/tests/semantic/run-combat-action-equivalence.sh" ]]; then
  echo "Running semantic combat action-equivalence checks"
  "$repo/tests/semantic/run-combat-action-equivalence.sh"
fi

if [[ -x "$repo/tests/semantic/run-legacy-combat-focus.sh" ]]; then
  echo "Running unchanged Classic combat-focus characterization checks"
  "$repo/tests/semantic/run-legacy-combat-focus.sh"
fi

if [[ -x "$repo/tests/semantic/shell_keyboard/run-shell-keyboard-interaction-contract.sh" ]]; then
  echo "Running semantic shell-keyboard contract checks"
  "$repo/tests/semantic/shell_keyboard/run-shell-keyboard-interaction-contract.sh"
fi

echo "Dependency-free core checks passed"
