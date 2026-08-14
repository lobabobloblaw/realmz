#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Geometry.hpp"
#include "PartyRailModel.hpp"

namespace realmz::presentation {

// Renderer-ready state presentation. marker_text and render_text are stable,
// code-native, non-color signals; emphasis remains available for styling.
struct PartyRailRenderableStateToken {
  std::string identifier;
  std::string label;
  std::string marker_text;
  std::string render_text;
  StateMarker marker = StateMarker::none;
  StateEmphasis emphasis = StateEmphasis::neutral;

  bool operator==(const PartyRailRenderableStateToken&) const = default;
};

struct PartyRailMemberLayout {
  size_t member_index = 0;
  PartyMemberId member_id = 0;

  LogicalRect card_bounds;
  LogicalRect name_bounds;
  LogicalRect level_bounds;
  LogicalRect stamina_meter_bounds;
  LogicalRect stamina_value_bounds;
  LogicalRect state_bounds;

  std::string name_text;
  std::string level_text;
  std::string stamina_value_text;
  // A deterministic one-line summary; state_tokens remains the complete,
  // unelided semantic sequence for accessible or expanded renderers.
  std::string state_text;

  TextStyleModel name_text_style;
  TextStyleModel level_text_style;
  TextStyleModel stamina_value_text_style;
  TextStyleModel state_text_style;
  std::vector<PartyRailRenderableStateToken> state_tokens;

  bool operator==(const PartyRailMemberLayout&) const = default;
};

struct PartyRailLayout {
  LogicalRect panel_bounds;
  LogicalRect heading_bounds;
  std::string heading_text;
  TextStyleModel heading_text_style;
  std::vector<PartyRailMemberLayout> members;

  bool operator==(const PartyRailLayout&) const = default;
};

struct PartyRailLayoutRequest {
  LogicalRect party_panel;
  std::span<const PartyRailMemberModel> members;
  TypographyModel typography;
};

// Lays out one to six members. A valid request always produces exactly one
// ordered, contained card per input member; malformed or unsupported requests
// throw std::invalid_argument instead of returning a partial rail.
[[nodiscard]] PartyRailLayout compute_party_rail_layout(
    const PartyRailLayoutRequest& request);

} // namespace realmz::presentation
