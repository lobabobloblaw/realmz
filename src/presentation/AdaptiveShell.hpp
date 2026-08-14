#pragma once

#include <optional>
#include <variant>
#include <vector>

#include "GameSnapshot.hpp"
#include "PresentationMode.hpp"
#include "ResponsiveLayout.hpp"

namespace realmz::presentation {

inline constexpr LogicalSize kClassicFrameSize{800.0, 600.0};
inline constexpr LogicalRect kClassicFrameRect{0.0, 0.0, 800.0, 600.0};
// WIND 131 is moved to the Classic desktop's 20-point content origin. The
// framebuffer crop and legacy global input coordinates therefore include that
// vertical offset even though the window-local lookrect starts at zero.
inline constexpr LogicalRect kClassicGameplayCrop{0.0, 20.0, 480.0, 416.0};
inline constexpr double kClassicTileExtent = 32.0;
inline constexpr TileCoverage kClassicGameplayTileCoverage{15, 13};

// Only screens whose legacy framebuffer has a stable, isolated gameplay crop
// can be hosted by the adaptive shell. Other screens remain complete 800x600
// compositions and must be delegated intact until their semantic UI exists.
[[nodiscard]] bool legacy_screen_requires_full_frame_fallback(
    ScreenContext screen) noexcept;

enum class AdaptiveShellRoute {
  classic_full_frame,
  // The responsive chrome is live, but the complete Classic frame remains
  // embedded until every visible control has a semantic UIAction mapping.
  remastered_compatibility_shell,
  remastered_adaptive,
  remastered_legacy_fallback,
};

enum class ShellPanelKind {
  party_rail,
  action_bar,
  details,
  event_log,
  drawer_tabs,
};

enum class ClassicSampling {
  nearest,
};

struct ClearShellCommand {
  LogicalRect destination;
  PhysicalRect backing_destination;

  bool operator==(const ClearShellCommand&) const = default;
};

struct BlitClassicFrameCommand {
  // Source coordinates are pixels in the immutable 800x600 Classic frame.
  LogicalRect source;
  // Destination coordinates are logical window points; backing_destination is
  // the equivalent SDL render-target rectangle.
  LogicalRect destination;
  PhysicalRect backing_destination;
  ClassicSampling sampling = ClassicSampling::nearest;

  bool operator==(const BlitClassicFrameCommand&) const = default;
};

struct DrawShellPanelCommand {
  ShellPanelKind panel = ShellPanelKind::party_rail;
  LogicalRect destination;
  PhysicalRect backing_destination;

  bool operator==(const DrawShellPanelCommand&) const = default;
};

using AdaptiveShellRenderCommand = std::variant<
    ClearShellCommand,
    BlitClassicFrameCommand,
    DrawShellPanelCommand>;

struct ClassicInteractionRegion {
  LogicalRect classic_rect;
  LogicalRect window_rect;

  bool operator==(const ClassicInteractionRegion&) const = default;
};

struct AdaptiveShellRequest {
  PresentationMode mode = PresentationMode::classic;
  ScreenContext screen = ScreenContext::title;
  LogicalSize window_size = kClassicFrameSize;
  double backing_scale = 1.0;
  // Runtime modal/window-stack inspection may require a complete legacy frame
  // even when the underlying engine state still says exploration or combat.
  bool legacy_full_frame_required = false;
  // A gameplay crop would hide the legacy action controls. Keep this false
  // until the shell controls for the active screen are complete and routed
  // through LegacyCommandBridge.
  bool semantic_controls_ready = false;
};

// Pure, renderer-independent output for the SDL host. Commands are ordered
// back-to-front. A host can consume their logical rectangles directly or use
// the precomputed backing rectangles for a high-density render target.
struct AdaptiveShellPlan {
  PresentationMode mode = PresentationMode::classic;
  ScreenContext screen = ScreenContext::title;
  AdaptiveShellRoute route = AdaptiveShellRoute::classic_full_frame;
  LogicalRect window;
  double backing_scale = 1.0;
  std::optional<PresentationLayout> adaptive_layout;
  TileCoverage gameplay_tile_coverage;
  std::vector<ClassicInteractionRegion> classic_interaction_regions;
  std::vector<AdaptiveShellRenderCommand> commands;

  [[nodiscard]] std::optional<LogicalPoint> window_to_classic(
      LogicalPoint point) const noexcept;
  [[nodiscard]] std::optional<LogicalPoint> classic_to_window(
      LogicalPoint point) const noexcept;
};

[[nodiscard]] AdaptiveShellPlan compute_adaptive_shell_plan(
    const AdaptiveShellRequest& request,
    const ResponsiveLayoutConfig& config = {});

} // namespace realmz::presentation
