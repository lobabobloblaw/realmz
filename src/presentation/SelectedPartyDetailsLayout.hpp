#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Geometry.hpp"
#include "PartyRailModel.hpp"

namespace realmz::presentation {

// Wide details panels and compact drawer contents share one deterministic
// layout path. Density changes only presentation geometry and abbreviations;
// it never changes the source model or its semantic state sequence.
enum class SelectedPartyDetailsLayoutDensity {
  wide,
  compact,
};

// Renderer-ready state presentation. render_text always includes a code-native
// marker when the semantic token has one, so color is never the only signal.
struct SelectedPartyDetailsRenderableStateToken {
  std::string identifier;
  std::string label;
  std::string marker_text;
  std::string render_text;
  StateMarker marker = StateMarker::none;
  StateEmphasis emphasis = StateEmphasis::neutral;

  bool operator==(
      const SelectedPartyDetailsRenderableStateToken&) const = default;
};

struct SelectedPartyDetailsMeterLayout {
  LogicalRect bounds;
  LogicalRect label_bounds;
  LogicalRect track_bounds;
  LogicalRect value_bounds;

  std::string label_text;
  std::string value_text;
  std::string state_marker_text;
  // This unelided string remains suitable for accessibility output even when
  // compact visible text must be shortened to preserve the text-size floor.
  std::string accessibility_text;
  TextStyleModel label_text_style;
  TextStyleModel value_text_style;
  double fill_fraction = 0.0;
  StateTokenModel state;

  bool operator==(const SelectedPartyDetailsMeterLayout&) const = default;
};

struct SelectedPartyDetailsLayout {
  SelectedPartyDetailsLayoutDensity density =
      SelectedPartyDetailsLayoutDensity::wide;
  LogicalRect panel_bounds;
  bool has_selection = false;

  LogicalRect heading_bounds;
  std::string heading_text;
  TextStyleModel heading_text_style;

  LogicalRect empty_message_bounds;
  std::string empty_message_text;
  TextStyleModel empty_message_text_style;

  LogicalRect name_bounds;
  std::string name_text;
  TextStyleModel name_text_style;

  LogicalRect summary_bounds;
  std::string summary_text;
  TextStyleModel summary_text_style;

  SelectedPartyDetailsMeterLayout stamina;
  SelectedPartyDetailsMeterLayout spell_points;

  LogicalRect state_bounds;
  // A deterministic, bounded one-line summary. Critical and consciousness
  // states are presented before redundant informational state. When tokens do
  // not fit, this contains an explicit +N count; state_tokens remains complete
  // and unelided in its source-model order.
  std::string state_text;
  TextStyleModel state_text_style;
  std::vector<SelectedPartyDetailsRenderableStateToken> state_tokens;
  size_t visible_state_count = 0;
  size_t hidden_state_count = 0;

  bool operator==(const SelectedPartyDetailsLayout&) const = default;
};

struct SelectedPartyDetailsLayoutRequest {
  LogicalRect details_panel;
  const SelectedPartyDetailsModel& details;
  TypographyModel typography;
  SelectedPartyDetailsLayoutDensity density =
      SelectedPartyDetailsLayoutDensity::wide;
};

// Produces contained, non-overlapping renderer geometry. Malformed geometry,
// unsupported density, a contradictory consciousness token, or typography
// that cannot honor the eight-point practical text floor throws
// std::invalid_argument rather than returning a partial layout. A selected
// model may have no state tokens, in which case the layout synthesizes its
// visible consciousness fallback; otherwise its first token must be the one
// matching consciousness cue and no later consciousness cue may occur. The
// function is read-only and renderer/input independent.
[[nodiscard]] SelectedPartyDetailsLayout
compute_selected_party_details_layout(
    const SelectedPartyDetailsLayoutRequest& request);

} // namespace realmz::presentation
