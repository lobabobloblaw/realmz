import os
import plistlib
import shutil
import stat
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
ARTIFACT_VERIFIER = REPO / "scripts" / "verify-macos-artifact.sh"
APP_STAGER = REPO / "scripts" / "stage-macos-app.sh"
SOURCE_VERIFIER = REPO / "scripts" / "verify-source-baseline.sh"


def write_executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class MacAppStagerTest(unittest.TestCase):
    PRODUCT = "Realmz Remastered — Unofficial"

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.build = self.root / "configured-build"
        self.output = self.root / "staged-output"
        self.tools = self.root / "tools"
        self.build.mkdir()
        self.tools.mkdir()

        plist = {
            "CFBundleName": self.PRODUCT,
            "CFBundleExecutable": self.PRODUCT,
            "CFBundleIconFile": "AppIcon.icns",
        }
        with (self.build / "Info.plist").open("wb") as output:
            plistlib.dump(plist, output)
        write_executable(self.build / "Realmz", "synthetic universal executable\n")

        self.fake_cmake = self.tools / "cmake"
        write_executable(
            self.fake_cmake,
            """#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == "--install" ]]
prefix=""
while (($#)); do
  if [[ "$1" == "--prefix" ]]; then
    prefix="$2"
    shift 2
  else
    shift
  fi
done
[[ -n "$prefix" ]]
mkdir -p "$prefix/Data Files" "$prefix/lib"
printf 'installed resource\n' > "$prefix/Data Files/Data I1"
if [[ "${MOCK_CMAKE_INSTALL_FAILURE:-0}" == "1" ]]; then
  exit 17
fi
printf 'synthetic dylib\n' > "$prefix/lib/libSDL3.dylib"
""",
        )

    def tearDown(self) -> None:
        self.temp.cleanup()

    def run_stager(self, env=None) -> subprocess.CompletedProcess:
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(
            [
                "bash",
                str(APP_STAGER),
                "--build-dir",
                str(self.build),
                "--output-dir",
                str(self.output),
                "--configuration",
                "Release",
                "--cmake",
                str(self.fake_cmake),
            ],
            text=True,
            capture_output=True,
            env=merged_env,
        )

    def test_stages_complete_expanded_bundle_without_dmg(self) -> None:
        result = self.run_stager()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        app = self.output / f"{self.PRODUCT}.app"
        executable = app / "Contents" / "MacOS" / self.PRODUCT
        resources = app / "Contents" / "Resources"
        self.assertEqual(result.stdout.strip(), str(app))
        self.assertEqual(
            (app / "Contents" / "Info.plist").read_bytes(),
            (self.build / "Info.plist").read_bytes(),
        )
        self.assertEqual(
            (resources / "AppIcon.icns").read_bytes(),
            (REPO / "bundle" / "AppIcon.icns").read_bytes(),
        )
        self.assertEqual(
            executable.read_text(encoding="utf-8"),
            "synthetic universal executable\n",
        )
        self.assertTrue(executable.stat().st_mode & stat.S_IXUSR)
        self.assertEqual(
            (resources / "Data Files" / "Data I1").read_text(encoding="utf-8"),
            "installed resource\n",
        )
        self.assertEqual(list(self.output.glob(".realmz-app-stage.*")), [])

    def test_existing_destination_is_preserved_and_rejected(self) -> None:
        app = self.output / f"{self.PRODUCT}.app"
        app.mkdir(parents=True)
        marker = app / "owner-data"
        marker.write_text("preserve\n", encoding="utf-8")

        result = self.run_stager()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("destination application already exists", result.stderr)
        self.assertEqual(marker.read_text(encoding="utf-8"), "preserve\n")

    def test_failed_install_does_not_publish_partial_bundle(self) -> None:
        result = self.run_stager(env={"MOCK_CMAKE_INSTALL_FAILURE": "1"})

        self.assertEqual(result.returncode, 17)
        self.assertFalse((self.output / f"{self.PRODUCT}.app").exists())
        self.assertEqual(list(self.output.glob(".realmz-app-stage.*")), [])

    def test_unsafe_plist_executable_name_is_rejected(self) -> None:
        with (self.build / "Info.plist").open("rb") as source:
            plist = plistlib.load(source)
        plist["CFBundleExecutable"] = "../Realmz"
        with (self.build / "Info.plist").open("wb") as output:
            plistlib.dump(plist, output)

        result = self.run_stager()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CFBundleExecutable must be a filename", result.stderr)
        self.assertFalse(self.output.exists())


class MacArtifactVerifierTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / "Realmz Remastered — Unofficial.app"
        self.resources = self.app / "Contents" / "Resources"
        self.macos = self.app / "Contents" / "MacOS"
        self.tools = self.root / "tools"
        self.macos.mkdir(parents=True)
        self.resources.mkdir(parents=True)
        self.tools.mkdir()
        self._make_bundle()
        self._make_tools()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _make_bundle(self) -> None:
        plist = {
            "CFBundleIdentifier": "org.realmz-remastered.unofficial",
            "CFBundleDisplayName": "Realmz Remastered — Unofficial",
            "CFBundleName": "Realmz Remastered — Unofficial",
            "CFBundleExecutable": "RealmzRemastered",
            "CFBundlePackageType": "APPL",
            "CFBundleIconFile": "AppIcon.icns",
            "CFBundleVersion": "0.1.0",
            "LSMinimumSystemVersion": "13.3",
            "NSHighResolutionCapable": True,
        }
        with (self.app / "Contents" / "Info.plist").open("wb") as f:
            plistlib.dump(plist, f)

        # Minimal structurally valid ICNS container with one empty element. The
        # verifier checks container identity/length, not image rendition.
        (self.resources / "AppIcon.icns").write_bytes(
            b"icns" + struct.pack(">I", 16) + b"ic07" + struct.pack(">I", 8)
        )

        executable = self.macos / "RealmzRemastered"
        write_executable(executable, "mock Mach-O\n")
        lib = self.resources / "lib" / "libSDL3.dylib"
        lib.parent.mkdir()
        write_executable(lib, "mock Mach-O dylib\n")
        write_executable(lib.parent / "libSDL3_image.dylib", "mock Mach-O dylib\n")

        (self.resources / "realmz.rsrc").write_bytes(b"resource fork")
        for rel in (
            "Data Files/Data ID",
            "Scenarios/Tutorial/Scenario Data",
            "Scenarios/City of Bywater/Scenario Data",
        ):
            path = self.resources / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")

        remastered = self.resources / "Remastered"
        remastered.mkdir()
        (remastered / "phase1.placeholder-manifest.json").write_text(
            '{"schema_version":1,"status":"placeholder"}\n', encoding="utf-8"
        )
        shutil.copyfile(
            REPO / "assets/remastered/scopes/phase1.runtime-manifest.json",
            remastered / "phase1.runtime-manifest.json",
        )
        shutil.copyfile(
            REPO / "assets/remastered/scopes/phase1.census.json",
            remastered / "phase1.census.json",
        )
        material_source = (
            REPO / "assets/remastered/style-proof/generation/outputs"
        )
        material_destination = (
            remastered / "style-proof/generation/outputs"
        )
        material_destination.mkdir(parents=True)
        for source in material_source.glob("*.png"):
            shutil.copyfile(source, material_destination / source.name)

        notices = self.resources / "Notices"
        notices.mkdir()
        shutil.copyfile(REPO / "LICENSE", notices / "LICENSE")
        (notices / "ATTRIBUTION.md").write_text(
            "Realmz by Tim Phillips. This remaster is unofficial.\n", encoding="utf-8"
        )
        (notices / "MODIFICATIONS.md").write_text(
            "Modified into an adaptive remaster.\n", encoding="utf-8"
        )
        (notices / "CONTENT_PROVENANCE.md").write_text(
            "Reviewed content provenance.\n", encoding="utf-8"
        )

    def _make_tools(self) -> None:
        write_executable(
            self.tools / "file",
            """#!/usr/bin/env bash
target="${!#}"
case "$target" in
  */MacOS/*|*.dylib) echo "Mach-O 64-bit universal binary" ;;
  *) echo "ASCII text" ;;
esac
""",
        )
        write_executable(
            self.tools / "lipo",
            """#!/usr/bin/env bash
echo "${MOCK_ARCHS:-x86_64 arm64}"
""",
        )
        write_executable(
            self.tools / "vtool",
            """#!/usr/bin/env bash
echo "Load command 1"
echo "      minos ${MOCK_MINOS:-13.3}"
echo "Load command 2"
echo "      minos ${MOCK_MINOS:-13.3}"
""",
        )
        write_executable(
            self.tools / "otool",
            """#!/usr/bin/env bash
mode="$1"
target="$2"
if [[ "$mode" == "-L" ]]; then
  echo "$target:"
  if [[ "${MOCK_BAD_DYLIB:-0}" == "1" ]]; then
    echo "    /tmp/build/libUnsafe.dylib (compatibility version 1.0.0, current version 1.0.0)"
  elif [[ "$target" == *.dylib ]]; then
    echo "    @rpath/$(basename "$target") (compatibility version 1.0.0, current version 1.0.0)"
    if [[ "${MOCK_TRANSITIVE_DYLIB:-0}" == "1" && "$target" == *libSDL3_image.dylib ]]; then
      echo "    @rpath/libSDL3.dylib (compatibility version 1.0.0, current version 1.0.0)"
    fi
  else
    echo "    @rpath/libSDL3.dylib (compatibility version 1.0.0, current version 1.0.0)"
  fi
  echo "    /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)"
else
  if [[ "$target" == *.dylib ]]; then
    echo "Load command 0"
    echo "          cmd LC_ID_DYLIB"
    echo "      cmdsize 64"
    echo "         name @rpath/$(basename "$target") (offset 24)"
  fi
  if [[ "$target" != *.dylib || "${MOCK_DYLIB_NO_RPATH:-0}" != "1" ]]; then
  echo "Load command 1"
  echo "          cmd LC_RPATH"
  echo "      cmdsize 64"
  echo "         path ${MOCK_RPATH:-@executable_path/../Resources/lib} (offset 12)"
  fi
  echo "      minos ${MOCK_MINOS:-13.3}"
fi
""",
        )
        write_executable(
            self.tools / "codesign",
            """#!/usr/bin/env bash
if [[ "$1" == "--verify" ]]; then
  [[ "${MOCK_BAD_SIGNATURE:-0}" != "1" ]]
  exit
fi
cat >&2 <<'INFO'
Executable=/mock/RealmzRemastered
Authority=Developer ID Application: Realmz Remastered Test (ABCDE12345)
TeamIdentifier=ABCDE12345
CodeDirectory v=20500 size=123 flags=0x10000(runtime) hashes=1+7 location=embedded
INFO
""",
        )
        write_executable(
            self.tools / "xcrun",
            """#!/usr/bin/env bash
[[ "${MOCK_BAD_NOTARIZATION:-0}" != "1" ]]
""",
        )
        write_executable(
            self.tools / "spctl",
            """#!/usr/bin/env bash
[[ "${MOCK_BAD_GATEKEEPER:-0}" != "1" ]]
""",
        )

    def run_verifier(self, *args: str, env=None) -> subprocess.CompletedProcess:
        command = [
            "bash",
            str(ARTIFACT_VERIFIER),
            "--tool-dir",
            str(self.tools),
            *args,
            str(self.app),
        ]
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(command, text=True, capture_output=True, env=merged_env)

    def test_development_bundle_passes_without_signature(self) -> None:
        result = self.run_verifier("--mode", "development")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("macOS artifact verification passed", result.stdout)

    def test_native_shell_material_bundle_is_hash_bound_and_public_only(self) -> None:
        material = (
            self.resources
            / "Remastered/style-proof/generation/outputs"
            / "01_ui_material_ppat_128.png"
        )
        original = material.read_bytes()
        material.write_bytes(original + b"tampered")
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("approved runtime output hash mismatch", result.stderr)

        material.write_bytes(original)
        private_input = (
            self.resources
            / "Remastered/style-proof/generation/raw/attempt.png"
        )
        private_input.parent.mkdir(parents=True)
        private_input.write_bytes(b"private")
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("private Remastered directory leaked", result.stderr)

        shutil.rmtree(private_input.parent)
        unexpected = (
            self.resources
            / "Remastered/style-proof/generation/postprocess/receipt.json"
        )
        unexpected.parent.mkdir(parents=True)
        unexpected.write_text("private\n", encoding="utf-8")
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("private Remastered directory leaked", result.stderr)

    def test_remastered_metadata_symlink_is_rejected(self) -> None:
        runtime = self.resources / "Remastered/phase1.runtime-manifest.json"
        external = self.root / "external-runtime-manifest.json"
        external.write_bytes(runtime.read_bytes())
        runtime.unlink()
        runtime.symlink_to(external)
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink leaked into bundled Remastered tree", result.stderr)

    def test_every_approved_runtime_output_is_required(self) -> None:
        output = (
            self.resources
            / "Remastered/style-proof/generation/outputs"
            / "05_portrait_cicn_257.png"
        )
        output.unlink()
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("approved runtime output is missing", result.stderr)

    def test_native_shell_material_metadata_is_required(self) -> None:
        census = self.resources / "Remastered/phase1.census.json"
        census.unlink()
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing or empty asset census", result.stderr)

    def test_original_bundle_identity_is_rejected(self) -> None:
        plist_path = self.app / "Contents" / "Info.plist"
        with plist_path.open("rb") as f:
            plist = plistlib.load(f)
        plist["CFBundleIdentifier"] = "com.fantasoft.Realmz"
        plist["CFBundleDisplayName"] = "Realmz"
        with plist_path.open("wb") as f:
            plistlib.dump(plist, f)
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("distinct reverse-DNS remaster identifier", result.stderr)
        self.assertIn("must include 'Realmz Remastered' and 'Unofficial'", result.stderr)

    def test_bitmap_mislabeled_as_icns_is_rejected(self) -> None:
        (self.resources / "AppIcon.icns").write_bytes(b"BM" + bytes(64))
        result = self.run_verifier("--mode", "development")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a structurally valid ICNS file", result.stderr)

    def test_dylib_install_name_does_not_require_own_rpath(self) -> None:
        result = self.run_verifier(
            "--mode",
            "development",
            env={"MOCK_DYLIB_NO_RPATH": "1"},
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dylib_may_inherit_executable_runpath(self) -> None:
        result = self.run_verifier(
            "--mode",
            "development",
            env={"MOCK_DYLIB_NO_RPATH": "1", "MOCK_TRANSITIVE_DYLIB": "1"},
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_thin_binary_and_absolute_dependency_are_rejected(self) -> None:
        result = self.run_verifier(
            "--mode",
            "development",
            env={"MOCK_ARCHS": "arm64", "MOCK_BAD_DYLIB": "1"},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not universal x86_64/arm64", result.stderr)
        self.assertIn("non-system, non-bundle-relative dependency", result.stderr)

    def test_escaping_rpath_and_wrong_deployment_target_are_rejected(self) -> None:
        result = self.run_verifier(
            "--mode",
            "development",
            env={
                "MOCK_RPATH": "@executable_path/../../../../outside",
                "MOCK_MINOS": "14.0",
            },
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LC_RPATH that escapes Contents", result.stderr)
        self.assertIn("deployment target other than 13.3", result.stderr)

    def test_release_signature_and_notarization_pass(self) -> None:
        manifest = self.resources / "Remastered" / "phase1.manifest.json"
        manifest.write_text(
            '{"scope":"phase1","status":"approved","coverage_failures":[]}\n',
            encoding="utf-8",
        )
        result = self.run_verifier("--mode", "release", "--require-notarization")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Developer ID Application", result.stdout)
        self.assertIn("stapled notarization ticket validates", result.stdout)


class SourceBaselineVerifierTest(unittest.TestCase):
    SDL = "1111111111111111111111111111111111111111"
    SDL_IMAGE = "2222222222222222222222222222222222222222"
    SDL_TTF = "3333333333333333333333333333333333333333"
    PHOSG = "4444444444444444444444444444444444444444"
    RESOURCE = "5555555555555555555555555555555555555555"

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name)
        (self.repo / "docs").mkdir()
        (self.repo / "scripts").mkdir()
        (self.repo / "README.md").write_text(
            f"phosg {self.PHOSG}\nresource_dasm {self.RESOURCE}\n", encoding="utf-8"
        )
        (self.repo / "CMakeLists.txt").write_text(
            "find_package(phosg REQUIRED)\nfind_package(resource_file REQUIRED)\n",
            encoding="utf-8",
        )
        (self.repo / "LICENSE").write_text(
            "Creative Commons Attribution-NonCommercial-ShareAlike 4.0\n",
            encoding="utf-8",
        )
        (self.repo / "scripts" / "bootstrap-macos-dependencies.sh").write_text(
            "\n".join(
                [
                    f'phosg_commit="{self.PHOSG}"',
                    f'resource_dasm_commit="{self.RESOURCE}"',
                    'architectures="x86_64;arm64"',
                    'deployment_target="13.3"',
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        (self.repo / "scripts" / "build-placeholder-app-icon.sh").write_text(
            "#!/usr/bin/env bash\n", encoding="utf-8"
        )
        (self.repo / "bundle").mkdir()
        (self.repo / "bundle" / "AppIcon.placeholder.bmp").write_bytes(b"BM")
        (self.repo / "bundle" / "AppIcon.icns").write_bytes(
            b"icns" + struct.pack(">I", 16) + b"ic07" + struct.pack(">I", 8)
        )
        (self.repo / ".gitmodules").write_text(
            """[submodule "vendored/SDL"]
\tpath = vendored/SDL
\turl = https://github.com/libsdl-org/SDL
[submodule "vendored/SDL_image"]
\tpath = vendored/SDL_image
\turl = https://github.com/libsdl-org/SDL_image
[submodule "vendored/SDL_ttf"]
\tpath = vendored/SDL_ttf
\turl = https://github.com/libsdl-org/SDL_ttf
""",
            encoding="utf-8",
        )
        subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.email", "qa@example.invalid"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.name", "QA Test"], cwd=self.repo, check=True)
        subprocess.run(["git", "add", "."], cwd=self.repo, check=True)
        subprocess.run(["git", "commit", "-qm", "baseline"], cwd=self.repo, check=True)
        self.baseline = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=self.repo, text=True
        ).strip()

        provenance_text = "\n".join(
            [
                self.baseline,
                self.SDL,
                self.SDL_IMAGE,
                self.SDL_TTF,
                self.PHOSG,
                self.RESOURCE,
            ]
        )
        (self.repo / "docs" / "CONTENT_PROVENANCE.md").write_text(
            provenance_text + "\n", encoding="utf-8"
        )
        subprocess.run(["git", "add", "docs/CONTENT_PROVENANCE.md"], cwd=self.repo, check=True)
        for path, digest in (
            ("vendored/SDL", self.SDL),
            ("vendored/SDL_image", self.SDL_IMAGE),
            ("vendored/SDL_ttf", self.SDL_TTF),
        ):
            subprocess.run(
                ["git", "update-index", "--add", "--cacheinfo", f"160000,{digest},{path}"],
                cwd=self.repo,
                check=True,
            )
        subprocess.run(["git", "commit", "-qm", "pin dependencies"], cwd=self.repo, check=True)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def verifier_env(self):
        env = os.environ.copy()
        env.update(
            {
                "REALMZ_VERIFY_ALLOW_TEST_PINS": "1",
                "REALMZ_TEST_BASELINE_COMMIT": self.baseline,
                "REALMZ_TEST_SDL_COMMIT": self.SDL,
                "REALMZ_TEST_SDL_IMAGE_COMMIT": self.SDL_IMAGE,
                "REALMZ_TEST_SDL_TTF_COMMIT": self.SDL_TTF,
                "REALMZ_TEST_PHOSG_COMMIT": self.PHOSG,
                "REALMZ_TEST_RESOURCE_DASM_COMMIT": self.RESOURCE,
            }
        )
        return env

    def run_verifier(self) -> subprocess.CompletedProcess:
        return subprocess.run(
            ["bash", str(SOURCE_VERIFIER), "--mode", "development", "--repo", str(self.repo)],
            text=True,
            capture_output=True,
            env=self.verifier_env(),
        )

    def test_development_mode_allows_intentional_working_changes(self) -> None:
        (self.repo / "intentional-untracked.txt").write_text("work in progress\n", encoding="utf-8")
        result = self.run_verifier()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("intentional/uncommitted", result.stderr)

    def test_empty_gitlink_directories_are_uninitialized_submodules(self) -> None:
        for path in ("vendored/SDL", "vendored/SDL_image", "vendored/SDL_ttf"):
            (self.repo / path).mkdir(parents=True)

        result = self.run_verifier()

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("checkout is", result.stderr)

    def test_missing_dependency_declaration_fails(self) -> None:
        (self.repo / "README.md").write_text(
            f"resource_dasm {self.RESOURCE}\n", encoding="utf-8"
        )
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("phosg commit pin", result.stderr)


if __name__ == "__main__":
    unittest.main()
