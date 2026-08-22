import math
from pathlib import Path
import sys
import unittest


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))
import prepare_style_proof_references as PNG  # noqa: E402


DARK_SCRIM = (12, 13, 17, 186)
SELECTED_SCRIM = (240, 227, 190, 144)
SELECTED_INK = (22, 24, 35)
TEXT_PALETTE = {
    "heading": (224, 205, 165),
    "body": (222, 222, 218),
    "muted": (164, 164, 158),
    "selected": (231, 188, 105),
    "information": (154, 194, 207),
    "positive": (154, 190, 131),
    "caution": (231, 188, 105),
    "critical": (221, 125, 112),
    "inactive": (164, 164, 158),
    "focus": (250, 232, 174),
}
OUTPUT_ROOT = (
    REPO / "assets/remastered/style-proof/generation/outputs"
)
MATERIALS = {
    "highlight": OUTPUT_ROOT / "01_ui_material_ppat_128.png",
    "neutral": OUTPUT_ROOT / "02_ui_material_ppat_129.png",
    "shadow": OUTPUT_ROOT / "03_ui_material_ppat_130.png",
    "panel": OUTPUT_ROOT / "04_ui_material_ppat_131.png",
}


def _linear_component(component: int) -> float:
    encoded = component / 255.0
    if encoded <= 0.04045:
        return encoded / 12.92
    return math.pow((encoded + 0.055) / 1.055, 2.4)


def _relative_luminance(color: tuple[int, int, int]) -> float:
    red, green, blue = (_linear_component(value) for value in color)
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def _minimum_contrasts(
    path: Path,
    scrim: tuple[int, int, int, int],
    colors: dict[str, tuple[int, int, int]],
) -> dict[str, float]:
    text_luminance = {
        name: _relative_luminance(color) for name, color in colors.items()
    }
    minimum = {name: math.inf for name in colors}
    for pixel in _decoded_pixels(path):
        background = _relative_luminance(_sdl_blend(pixel, scrim))
        for name, foreground in text_luminance.items():
            lighter = max(background, foreground)
            darker = min(background, foreground)
            minimum[name] = min(
                minimum[name], (lighter + 0.05) / (darker + 0.05)
            )
    return minimum


def _sdl_blend(
    destination: tuple[int, int, int], source: tuple[int, int, int, int]
) -> tuple[int, int, int]:
    # SDL's default SRGB renderer blends these UNORM color bytes. The +127
    # models nearest-integer alpha interpolation for the exhaustive bound.
    alpha = source[3]
    return tuple(
        (source[channel] * alpha + destination[channel] * (255 - alpha) + 127)
        // 255
        for channel in range(3)
    )


def _decoded_pixels(path: Path):
    image = PNG.decode_png(path.read_bytes(), str(path))
    if image.width != 512 or image.height != 512:
        raise AssertionError(
            f"{path.name} physical dimensions changed from 512x512"
        )
    for offset in range(0, len(image.pixels), 4):
        red, green, blue, alpha = image.pixels[offset : offset + 4]
        if alpha != 255:
            raise AssertionError(f"{path.name} is no longer an opaque tile")
        yield red, green, blue


class ShellMaterialContrastTest(unittest.TestCase):
    def test_runtime_constants_remain_bound_to_this_pixel_contract(self):
        source = (REPO / "src/WindowManager.cpp").read_text(encoding="utf-8")
        compact_source = "".join(source.split())
        self.assertEqual(
            1,
            source.count(
                f"SDL_Color{{{SELECTED_SCRIM[0]}, {SELECTED_SCRIM[1]}, "
                f"{SELECTED_SCRIM[2]}, {SELECTED_SCRIM[3]}}}"
            ),
        )
        self.assertEqual(
            1,
            source.count(
                f"SDL_Color{{{DARK_SCRIM[0]}, {DARK_SCRIM[1]}, "
                f"{DARK_SCRIM[2]}, {DARK_SCRIM[3]}}}"
            ),
        )
        self.assertEqual(
            1,
            source.count(
                f"kSelectedMaterialInk{{{SELECTED_INK[0]}, "
                f"{SELECTED_INK[1]}, {SELECTED_INK[2]}, 255}}"
            ),
        )
        expected_palette_bindings = (
            "constexprSDL_ColorkHeading{224,205,165,255};",
            "constexprSDL_ColorkBody{222,222,218,255};",
            "constexprSDL_ColorkMuted{164,164,158,255};",
            "constexprSDL_ColorkSelected{231,188,105,255};",
            "caseStateEmphasis::neutral:return{222,222,218,255};",
            "caseStateEmphasis::information:return{154,194,207,255};",
            "caseStateEmphasis::positive:return{154,190,131,255};",
            "caseStateEmphasis::caution:return{231,188,105,255};",
            "caseStateEmphasis::critical:return{221,125,112,255};",
            "caseStateEmphasis::inactive:return{164,164,158,255};",
            "SDL_Colorcolor=SDL_Color{250,232,174,255}",
        )
        for binding in expected_palette_bindings:
            with self.subTest(binding=binding):
                self.assertEqual(1, compact_source.count(binding))

    def test_selected_material_has_uniform_dark_ink_contrast(self):
        minimum = _minimum_contrasts(
            MATERIALS["highlight"],
            SELECTED_SCRIM,
            {"selected_ink": SELECTED_INK},
        )["selected_ink"]
        self.assertGreaterEqual(
            minimum,
            4.5,
            f"selected shell material contrast fell to {minimum:.4f}:1",
        )

    def test_dark_material_states_cover_every_shell_text_color(self):
        for role in ("neutral", "shadow", "panel"):
            minimum_by_color = _minimum_contrasts(
                MATERIALS[role], DARK_SCRIM, TEXT_PALETTE
            )
            for name in TEXT_PALETTE:
                with self.subTest(role=role, color=name):
                    minimum = minimum_by_color[name]
                    self.assertGreaterEqual(
                        minimum,
                        4.5,
                        f"{role}/{name} contrast fell to {minimum:.4f}:1",
                    )


if __name__ == "__main__":
    unittest.main()
