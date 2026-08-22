#include "PartyRailLayout.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string_view>

namespace realmz::presentation {
namespace {

constexpr size_t kMaximumPartyMembers = 6;
constexpr double kMinimumPanelWidth = 180.0;
constexpr double kMinimumPanelHeight = 160.0;
constexpr double kPanelInset = 10.0;
constexpr double kHeadingGap = 6.0;
constexpr double kPreferredCardGap = 4.0;
constexpr double kMinimumCardGap = 3.0;
constexpr double kMaximumCardHeight = 78.0;
constexpr double kPortraitSize = 44.0;
constexpr double kPortraitGap = 6.0;
constexpr double kAverageGlyphWidthInEms = 0.58;
constexpr double kMinimumPracticalPointSize = 8.0;

[[nodiscard]] bool valid_text_style(const TextStyleModel& style) noexcept {
  return std::isfinite(style.point_size) &&
      std::isfinite(style.line_height) &&
      (style.point_size > 0.0) && (style.line_height > 0.0);
}

void validate_request(const PartyRailLayoutRequest& request) {
  if (!request.party_panel.is_finite_and_nonnegative() ||
      (request.party_panel.width < kMinimumPanelWidth) ||
      (request.party_panel.height < kMinimumPanelHeight)) {
    throw std::invalid_argument(
        "party panel must be finite and at least 180 by 160 points");
  }
  if (request.members.empty() ||
      (request.members.size() > kMaximumPartyMembers)) {
    throw std::invalid_argument("party rail requires one to six members");
  }
  if (!std::isfinite(request.typography.scale) ||
      (request.typography.scale <= 0.0) ||
      !valid_text_style(request.typography.caption) ||
      !valid_text_style(request.typography.body) ||
      !valid_text_style(request.typography.heading)) {
    throw std::invalid_argument(
        "party rail typography must be finite and positive");
  }
}

[[nodiscard]] TextStyleModel fit_text_style(
    const TextStyleModel& requested,
    LogicalRect bounds,
    size_t character_count) noexcept {
  const double line_to_point = requested.line_height / requested.point_size;
  const double height_limited_points = bounds.height / line_to_point;
  const double width_limited_points = bounds.width /
      (kAverageGlyphWidthInEms *
          static_cast<double>(std::max<size_t>(character_count, 1U)));
  const double point_size = std::min({
      requested.point_size,
      height_limited_points,
      width_limited_points,
  });
  return {
      .point_size = point_size,
      .line_height = std::min(bounds.height, point_size * line_to_point),
  };
}

[[nodiscard]] std::string_view marker_text(StateMarker marker) noexcept {
  switch (marker) {
    case StateMarker::none:
      return "";
    case StateMarker::selection:
      return "[>]";
    case StateMarker::information:
      return "[i]";
    case StateMarker::check:
      return "[+]";
    case StateMarker::alert:
      return "[!]";
    case StateMarker::stop:
      return "[X]";
    case StateMarker::unavailable:
      return "[-]";
    case StateMarker::condition:
      return "[*]";
  }
  return "";
}

[[nodiscard]] PartyRailRenderableStateToken renderable_token(
    const StateTokenModel& token) {
  const std::string marker(marker_text(token.marker));
  const std::string label = token.label.empty() ? "State" : token.label;
  return {
      .identifier = token.identifier,
      .label = label,
      .marker_text = marker,
      .render_text = marker.empty() ? label : marker + " " + label,
      .marker = token.marker,
      .emphasis = token.emphasis,
  };
}

[[nodiscard]] StateTokenModel fallback_state(
    const PartyRailMemberModel& member) {
  if (member.conscious) {
    return {
        .identifier = "status.ready",
        .label = "Ready",
        .emphasis = StateEmphasis::positive,
        .marker = StateMarker::check,
    };
  }
  return {
      .identifier = "status.unconscious",
      .label = "Unconscious",
      .emphasis = StateEmphasis::critical,
      .marker = StateMarker::stop,
  };
}

[[nodiscard]] std::string join_state_text(
    std::span<const PartyRailRenderableStateToken> tokens) {
  std::string result;
  for (const auto& token : tokens) {
    if (!result.empty()) {
      result += "  ";
    }
    result += token.render_text;
  }
  return result;
}

[[nodiscard]] std::string join_accessible_state_text(
    std::span<const PartyRailRenderableStateToken> tokens) {
  std::string result = tokens.size() == 1U ? "state " : "states ";
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (index > 0U) {
      result += ", ";
    }
    result += tokens[index].label;
  }
  return result;
}

struct AttackCadenceText {
  std::string visible;
  std::string accessible;
};

[[nodiscard]] AttackCadenceText attack_cadence_text(
    int32_t half_units) {
  if ((half_units < 0) || (half_units > 19)) {
    return {
        .visible = "> 10",
        .accessible = "greater than 10",
    };
  }
  const int32_t numerator = (half_units % 2 == 0)
      ? half_units / 2
      : half_units;
  const int32_t denominator = (half_units % 2 == 0) ? 1 : 2;
  return {
      .visible = std::format("{}/{}", numerator, denominator),
      .accessible = std::format("{} over {}", numerator, denominator),
  };
}

[[nodiscard]] std::string utf8_prefix(
    std::string_view text,
    size_t byte_budget) {
  size_t end = std::min(byte_budget, text.size());
  if (end == text.size()) {
    return std::string(text);
  }
  while ((end > 0U) &&
      ((static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)) {
    --end;
  }
  return std::string(text.substr(0U, end));
}

// state_tokens always retains the complete semantic sequence. This bounded
// one-line summary prevents an unusually large state set from silently
// reducing visible text below the practical point-size floor.
[[nodiscard]] std::string elided_state_text(
    std::span<const PartyRailRenderableStateToken> tokens,
    LogicalRect bounds) {
  const std::string full_text = join_state_text(tokens);
  const auto maximum_bytes = static_cast<size_t>(std::max(
      1.0,
      std::floor(bounds.width /
          (kAverageGlyphWidthInEms * kMinimumPracticalPointSize))));
  if (full_text.size() <= maximum_bytes) {
    return full_text;
  }

  const std::string suffix = tokens.size() > 1U
      ? " ... (+" + std::to_string(tokens.size() - 1U) + ")"
      : "...";
  if (maximum_bytes <= suffix.size()) {
    return utf8_prefix(tokens.front().render_text, maximum_bytes);
  }
  return utf8_prefix(
      tokens.front().render_text,
      maximum_bytes - suffix.size()) + suffix;
}

[[nodiscard]] PartyRailMemberLayout layout_member(
    const PartyRailLayoutRequest& request,
    size_t member_index,
    LogicalRect card) {
  const auto& member = request.members[member_index];
  constexpr double kHorizontalInset = 6.0;
  constexpr double kVerticalInset = 4.0;
  const double inner_width = card.width - 2.0 * kHorizontalInset;
  const double inner_height = card.height - 2.0 * kVerticalInset;
  const double text_width = inner_width - kPortraitSize - kPortraitGap;
  const double row_gap = std::clamp(inner_height * 0.04, 1.0, 2.0);
  const double row_height = inner_height - 2.0 * row_gap;
  const double name_height = row_height * 0.35;
  const double stamina_row_height = row_height * 0.32;
  const double state_height = row_height - name_height - stamina_row_height;
  if ((card.height < kPortraitSize) || (text_width <= 0.0) ||
      (state_height <= 0.0)) {
    throw std::invalid_argument("party panel is too small for member cards");
  }

  const double level_width = std::clamp(text_width * 0.22, 42.0, 56.0);
  const double armor_class_width =
      std::clamp(text_width * 0.22, 42.0, 56.0);
  const double column_gap = std::clamp(text_width * 0.025, 3.0, 7.0);
  const double name_width = text_width - level_width - armor_class_width -
      2.0 * column_gap;
  const double meter_width = text_width * 0.28;
  const double stamina_value_width = text_width * 0.30;
  const double auxiliary_vital_width = text_width - meter_width -
      stamina_value_width - 2.0 * column_gap;
  if ((name_width <= 0.0) || (meter_width <= 0.0) ||
      (stamina_value_width <= 0.0) || (auxiliary_vital_width <= 0.0)) {
    throw std::invalid_argument("party panel is too narrow for member cards");
  }

  const double left = card.x + kHorizontalInset;
  const LogicalRect portrait_bounds{
      left,
      card.y + (card.height - kPortraitSize) / 2.0,
      kPortraitSize,
      kPortraitSize,
  };
  const double text_left = portrait_bounds.right() + kPortraitGap;
  const double top = card.y + kVerticalInset;
  const LogicalRect name_bounds{text_left, top, name_width, name_height};
  const LogicalRect level_bounds{
      name_bounds.right() + column_gap,
      top,
      level_width,
      name_height,
  };
  const LogicalRect armor_class_bounds{
      level_bounds.right() + column_gap,
      top,
      armor_class_width,
      name_height,
  };
  const double meter_y = top + name_height + row_gap;
  const double meter_height = std::clamp(
      stamina_row_height * 0.50, 5.0, 8.0);
  const double meter_track_y =
      meter_y + (stamina_row_height - meter_height) / 2.0;
  const LogicalRect stamina_meter_bounds{
      text_left,
      meter_track_y,
      meter_width,
      meter_height,
  };
  const LogicalRect stamina_value_bounds{
      stamina_meter_bounds.right() + column_gap,
      meter_y,
      stamina_value_width,
      stamina_row_height,
  };
  const LogicalRect auxiliary_vital_bounds{
      stamina_value_bounds.right() + column_gap,
      meter_y,
      auxiliary_vital_width,
      stamina_row_height,
  };
  const LogicalRect state_bounds{
      text_left,
      meter_y + stamina_row_height + row_gap,
      text_width,
      state_height,
  };

  const std::string name_text = member.name.empty()
      ? std::format("Adventurer {}", member_index + 1U)
      : member.name;
  const std::string level_text = std::format("Lv {}", member.level);
  const std::string armor_class_text =
      std::format("AC {}", member.armor_class);
  const std::string stamina_value_text = std::format(
      "ST {}/{}", member.stamina.current, member.stamina.maximum);

  std::string auxiliary_vital_text;
  std::string accessible_auxiliary_vital;
  switch (member.auxiliary_vital) {
    case PartyAuxiliaryVitalKind::spell_points:
      auxiliary_vital_text = std::format(
          "SP {}/{}",
          member.spell_points.current,
          member.spell_points.maximum);
      accessible_auxiliary_vital = std::format(
          "spell points {} of {}",
          member.spell_points.current,
          member.spell_points.maximum);
      break;
    case PartyAuxiliaryVitalKind::attack_cadence: {
      const auto cadence = attack_cadence_text(
          member.attack_cadence_half_units);
      auxiliary_vital_text = "ATK " + cadence.visible;
      accessible_auxiliary_vital = "attack cadence " + cadence.accessible;
      break;
    }
    default:
      throw std::invalid_argument(
          "party member auxiliary vital kind must be supported");
  }

  std::vector<PartyRailRenderableStateToken> tokens;
  tokens.reserve(std::max<size_t>(member.states.size(), 1U));
  if (member.states.empty()) {
    tokens.emplace_back(renderable_token(fallback_state(member)));
  } else {
    for (const auto& state : member.states) {
      tokens.emplace_back(renderable_token(state));
    }
  }
  const std::string state_text = elided_state_text(tokens, state_bounds);
  const std::string accessibility_text = std::format(
      "{}, level {}, armor class {}, stamina {} of {}, {}, {}",
      name_text,
      member.level,
      member.armor_class,
      member.stamina.current,
      member.stamina.maximum,
      accessible_auxiliary_vital,
      join_accessible_state_text(tokens));

  return {
      .member_index = member_index,
      .member_id = member.id,
      .card_bounds = card,
      .portrait_bounds = portrait_bounds,
      .name_bounds = name_bounds,
      .level_bounds = level_bounds,
      .armor_class_bounds = armor_class_bounds,
      .stamina_meter_bounds = stamina_meter_bounds,
      .stamina_value_bounds = stamina_value_bounds,
      .auxiliary_vital_bounds = auxiliary_vital_bounds,
      .state_bounds = state_bounds,
      .name_text = name_text,
      .level_text = level_text,
      .armor_class_text = armor_class_text,
      .stamina_value_text = stamina_value_text,
      .auxiliary_vital_text = auxiliary_vital_text,
      .state_text = state_text,
      .accessibility_text = accessibility_text,
      .name_text_style = fit_text_style(
          request.typography.body, name_bounds, name_text.size()),
      .level_text_style = fit_text_style(
          request.typography.caption, level_bounds, level_text.size()),
      .armor_class_text_style = fit_text_style(
          request.typography.caption,
          armor_class_bounds,
          armor_class_text.size()),
      .stamina_value_text_style = fit_text_style(
          request.typography.caption,
          stamina_value_bounds,
          stamina_value_text.size()),
      .auxiliary_vital_text_style = fit_text_style(
          request.typography.caption,
          auxiliary_vital_bounds,
          auxiliary_vital_text.size()),
      .state_text_style = fit_text_style(
          request.typography.caption, state_bounds, state_text.size()),
      .state_tokens = std::move(tokens),
  };
}

} // namespace

PartyRailLayout compute_party_rail_layout(
    const PartyRailLayoutRequest& request) {
  validate_request(request);

  const double heading_height = std::clamp(
      request.typography.heading.line_height,
      18.0,
      28.0);
  const LogicalRect heading_bounds{
      request.party_panel.x + kPanelInset,
      request.party_panel.y + kPanelInset,
      request.party_panel.width - 2.0 * kPanelInset,
      heading_height,
  };
  const double cards_top = heading_bounds.bottom() + kHeadingGap;
  const double cards_height = request.party_panel.bottom() - kPanelInset -
      cards_top;
  const auto gap_count = request.members.size() - 1U;
  double card_gap = kPreferredCardGap;
  if (gap_count > 0U) {
    // Preserve the established four-point rhythm whenever possible. Only the
    // shortest six-member wide rail at the largest text scale needs a slightly
    // tighter gap to keep every fixed portrait at its full logical size.
    const double maximum_gap_for_full_portraits =
        (cards_height -
            kPortraitSize * static_cast<double>(request.members.size())) /
        static_cast<double>(gap_count);
    card_gap = std::clamp(
        maximum_gap_for_full_portraits,
        kMinimumCardGap,
        kPreferredCardGap);
  }
  const double total_gaps = card_gap * static_cast<double>(gap_count);
  const double card_height = std::min(
      kMaximumCardHeight,
      (cards_height - total_gaps) /
          static_cast<double>(request.members.size()));
  if (!std::isfinite(card_height) || (card_height <= 12.0)) {
    throw std::invalid_argument("party panel is too short for member cards");
  }

  PartyRailLayout result{
      .panel_bounds = request.party_panel,
      .heading_bounds = heading_bounds,
      .heading_text = "PARTY",
      .heading_text_style = fit_text_style(
          request.typography.heading, heading_bounds, 5U),
  };
  result.members.reserve(request.members.size());
  double card_y = cards_top;
  for (size_t index = 0; index < request.members.size(); ++index) {
    const LogicalRect card{
        request.party_panel.x + kPanelInset,
        card_y,
        request.party_panel.width - 2.0 * kPanelInset,
        card_height,
    };
    result.members.emplace_back(layout_member(request, index, card));
    card_y += card_height + card_gap;
  }
  return result;
}

} // namespace realmz::presentation
