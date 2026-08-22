#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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
  // Every card reserves one fixed logical portrait slot. Rendering may fill
  // it with a verified portrait or a code-native fallback without changing
  // the surrounding information geometry.
  LogicalRect portrait_bounds;
  LogicalRect name_bounds;
  LogicalRect level_bounds;
  LogicalRect armor_class_bounds;
  LogicalRect stamina_meter_bounds;
  LogicalRect stamina_value_bounds;
  LogicalRect auxiliary_vital_bounds;
  LogicalRect state_bounds;

  std::string name_text;
  std::string level_text;
  std::string armor_class_text;
  std::string stamina_value_text;
  std::string auxiliary_vital_text;
  // A deterministic one-line summary; state_tokens remains the complete,
  // unelided semantic sequence for accessible or expanded renderers.
  std::string state_text;
  // Complete spoken content for this card. Unlike the compact visible state
  // summary, accessibility_text always includes every semantic state token.
  std::string accessibility_text;

  TextStyleModel name_text_style;
  TextStyleModel level_text_style;
  TextStyleModel armor_class_text_style;
  TextStyleModel stamina_value_text_style;
  TextStyleModel auxiliary_vital_text_style;
  TextStyleModel state_text_style;
  std::vector<PartyRailRenderableStateToken> state_tokens;

  bool operator==(const PartyRailMemberLayout&) const = default;
};

// The raw Classic effect value remains attached to its semantic token. The
// compact ribbon may elide visible tokens, but never this ordered collection.
struct PartyStatusEffectLayout {
  PartyEffectKind kind = PartyEffectKind::waterworld;
  int16_t raw_value = 0;
  PartyRailRenderableStateToken state;
  // Mirrored for renderer-wide emphasis aggregation without reaching back
  // into PartyRailModel or reformatting status content.
  StateEmphasis emphasis = StateEmphasis::neutral;

  bool operator==(const PartyStatusEffectLayout&) const = default;
};

// Renderer-ready, read-only party-wide status. World surfaces expose all three
// rows; combat deliberately leaves the fatigue and pooled-money rows absent.
// accessibility_text is complete semantic layout text, not an OS publication
// claim.
struct PartyStatusLayout {
  LogicalRect bounds;
  LogicalRect effects_bounds;
  std::optional<LogicalRect> fatigue_meter_bounds;
  std::optional<LogicalRect> fatigue_bounds;
  std::optional<LogicalRect> pooled_money_bounds;

  std::string effects_text;
  std::string fatigue_text;
  std::string pooled_money_text;
  std::string accessibility_text;

  TextStyleModel effects_text_style;
  TextStyleModel fatigue_text_style;
  TextStyleModel pooled_money_text_style;
  std::vector<PartyStatusEffectLayout> effect_tokens;
  std::optional<PartyRailRenderableStateToken> fatigue_state_token;
  double fatigue_fill_fraction = 0.0;
  bool fatigue_meter_available = false;

  bool operator==(const PartyStatusLayout&) const = default;
};

struct PartyRailLayout {
  LogicalRect panel_bounds;
  LogicalRect heading_bounds;
  std::string heading_text;
  TextStyleModel heading_text_style;
  PartyStatusLayout status;
  std::vector<PartyRailMemberLayout> members;

  bool operator==(const PartyRailLayout&) const = default;
};

struct PartyRailLayoutRequest {
  LogicalRect party_panel;
  const PartyRailModel& party_rail;
  ScreenContext screen;
  TypographyModel typography;
};

// Lays out one to six members. A valid request always produces exactly one
// ordered, contained card per input member; malformed or unsupported requests
// throw std::invalid_argument instead of returning a partial rail.
[[nodiscard]] PartyRailLayout compute_party_rail_layout(
    const PartyRailLayoutRequest& request);

} // namespace realmz::presentation
