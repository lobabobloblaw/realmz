#include "SelectedPartyDetailsLayout.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace realmz::presentation {
namespace {

constexpr double kMinimumPracticalPointSize = 8.0;
constexpr double kMinimumPracticalLineHeight = 10.0;
constexpr double kAverageGlyphWidthInEms = 0.58;
constexpr double kWideMinimumWidth = 280.0;
constexpr double kWideMinimumHeight = 160.0;
constexpr double kCompactMinimumWidth = 200.0;
constexpr double kCompactMinimumHeight = 64.0;
constexpr double kWideInset = 10.0;
constexpr double kCompactInset = 2.0;

[[nodiscard]] bool valid_density(
    SelectedPartyDetailsLayoutDensity density) noexcept {
  return density == SelectedPartyDetailsLayoutDensity::wide ||
      density == SelectedPartyDetailsLayoutDensity::compact;
}

[[nodiscard]] bool valid_text_style(const TextStyleModel& style) noexcept {
  return std::isfinite(style.point_size) &&
      std::isfinite(style.line_height) &&
      (style.point_size >= kMinimumPracticalPointSize) &&
      (style.line_height >= kMinimumPracticalLineHeight) &&
      (style.line_height >= style.point_size);
}

void validate_request(const SelectedPartyDetailsLayoutRequest& request) {
  if (!valid_density(request.density)) {
    throw std::invalid_argument("selected details density is unsupported");
  }
  if (!request.details_panel.is_finite_and_nonnegative() ||
      !std::isfinite(request.details_panel.right()) ||
      !std::isfinite(request.details_panel.bottom())) {
    throw std::invalid_argument("selected details panel must be finite");
  }
  const bool compact =
      request.density == SelectedPartyDetailsLayoutDensity::compact;
  const double minimum_width = compact
      ? kCompactMinimumWidth
      : kWideMinimumWidth;
  const double minimum_height = compact
      ? kCompactMinimumHeight
      : kWideMinimumHeight;
  if ((request.details_panel.width < minimum_width) ||
      (request.details_panel.height < minimum_height)) {
    throw std::invalid_argument(
        compact
            ? "compact selected details panel must be at least 200 by 64 points"
            : "wide selected details panel must be at least 280 by 160 points");
  }
  if (!std::isfinite(request.typography.scale) ||
      (request.typography.scale <= 0.0) ||
      !valid_text_style(request.typography.caption) ||
      !valid_text_style(request.typography.body) ||
      !valid_text_style(request.typography.heading)) {
    throw std::invalid_argument(
        "selected details typography cannot honor the practical text floor");
  }

  if (!request.details.member || request.details.states.empty()) {
    return;
  }
  const std::string_view expected_identifier = request.details.conscious
      ? "status.conscious"
      : "status.unconscious";
  const auto expected_emphasis = request.details.conscious
      ? StateEmphasis::positive
      : StateEmphasis::critical;
  const auto expected_marker = request.details.conscious
      ? StateMarker::check
      : StateMarker::stop;
  const auto& cue = request.details.states.front();
  if ((cue.identifier != expected_identifier) || cue.label.empty() ||
      (cue.emphasis != expected_emphasis) ||
      (cue.marker != expected_marker)) {
    throw std::invalid_argument(
        "selected details consciousness cue is missing or contradictory");
  }
  for (size_t index = 1; index < request.details.states.size(); ++index) {
    const auto& identifier = request.details.states[index].identifier;
    if ((identifier == "status.conscious") ||
        (identifier == "status.unconscious")) {
      throw std::invalid_argument(
          "selected details consciousness cue must occur exactly once");
    }
  }
}

[[nodiscard]] size_t floor_character_capacity(LogicalRect bounds) noexcept {
  const double capacity = std::max(
      1.0,
      std::floor(bounds.width /
          (kAverageGlyphWidthInEms * kMinimumPracticalPointSize)));
  const double maximum = static_cast<double>(
      std::numeric_limits<size_t>::max());
  if (capacity >= maximum) {
    return std::numeric_limits<size_t>::max();
  }
  return static_cast<size_t>(capacity);
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

[[nodiscard]] std::string elide_to_practical_floor(
    std::string_view text,
    LogicalRect bounds) {
  const size_t capacity = floor_character_capacity(bounds);
  if (text.size() <= capacity) {
    return std::string(text);
  }
  constexpr std::string_view suffix = "...";
  if (capacity <= suffix.size()) {
    return utf8_prefix(text, capacity);
  }
  return utf8_prefix(text, capacity - suffix.size()) + std::string(suffix);
}

[[nodiscard]] TextStyleModel fit_text_style(
    const TextStyleModel& requested,
    LogicalRect bounds,
    size_t character_count) {
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
  const double line_height = std::min(
      bounds.height,
      point_size * line_to_point);
  if (!std::isfinite(point_size) || !std::isfinite(line_height) ||
      (point_size < kMinimumPracticalPointSize) ||
      (line_height < kMinimumPracticalLineHeight)) {
    throw std::invalid_argument(
        "selected details text cannot honor the practical text floor");
  }
  return {
      .point_size = point_size,
      .line_height = line_height,
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

[[nodiscard]] SelectedPartyDetailsRenderableStateToken renderable_token(
    const StateTokenModel& source) {
  const std::string marker(marker_text(source.marker));
  const std::string visible_label = source.label.empty()
      ? "State"
      : source.label;
  return {
      .identifier = source.identifier,
      .label = source.label,
      .marker_text = marker,
      .render_text = marker.empty()
          ? visible_label
          : marker + " " + visible_label,
      .marker = source.marker,
      .emphasis = source.emphasis,
  };
}

[[nodiscard]] std::string joined_token_prefix(
    const std::vector<SelectedPartyDetailsRenderableStateToken>& tokens,
    size_t count) {
  std::string result;
  for (size_t index = 0; index < count; ++index) {
    if (!result.empty()) {
      result += "  ";
    }
    result += tokens[index].render_text;
  }
  return result;
}

struct StateSummary {
  std::string text;
  size_t visible_count = 0;
  size_t hidden_count = 0;
};

[[nodiscard]] int summary_priority(
    const SelectedPartyDetailsRenderableStateToken& token) noexcept {
  if (token.identifier == "status.unconscious") {
    return 0;
  }
  if (token.identifier == "status.conscious") {
    return 1;
  }
  if (token.emphasis == StateEmphasis::critical) {
    return 2;
  }
  if ((token.emphasis == StateEmphasis::caution) ||
      (token.marker == StateMarker::condition)) {
    return 3;
  }
  if (token.emphasis == StateEmphasis::positive) {
    return 4;
  }
  if (token.identifier == "status.selected") {
    return 7;
  }
  if (token.emphasis == StateEmphasis::information) {
    return 5;
  }
  return 6;
}

// Elision is token-based: no semantic token is partially presented as though
// it were complete. The largest ordered prefix that fits is followed by an
// explicit +N count. The full tokens remain in the layout result.
[[nodiscard]] StateSummary state_summary(
    const std::vector<SelectedPartyDetailsRenderableStateToken>& tokens,
    LogicalRect bounds,
    bool conscious) {
  if (tokens.empty()) {
    return {
        .text = conscious ? "[+] Ready" : "[X] Unconscious",
    };
  }

  const size_t capacity = floor_character_capacity(bounds);
  for (size_t visible = tokens.size();; --visible) {
    const size_t hidden = tokens.size() - visible;
    std::string candidate = joined_token_prefix(tokens, visible);
    if (hidden > 0U) {
      if (!candidate.empty()) {
        candidate += "  ";
      }
      candidate += "+" + std::to_string(hidden);
    }
    if (candidate.size() <= capacity) {
      return {
          .text = std::move(candidate),
          .visible_count = visible,
          .hidden_count = hidden,
      };
    }
    if (visible == 0U) {
      break;
    }
  }

  // A valid details panel always has room for this count, but keep the final
  // representation bounded if future minimums change.
  std::string fallback = "+" + std::to_string(tokens.size());
  fallback = elide_to_practical_floor(fallback, bounds);
  return {
      .text = std::move(fallback),
      .hidden_count = tokens.size(),
  };
}

[[nodiscard]] SelectedPartyDetailsMeterLayout meter_layout(
    LogicalRect bounds,
    std::string_view wide_label,
    std::string_view compact_label,
    const MeterModel& meter,
    const TypographyModel& typography,
    SelectedPartyDetailsLayoutDensity density) {
  const bool compact = density == SelectedPartyDetailsLayoutDensity::compact;
  constexpr double kElementGap = 2.0;
  const double label_fraction = compact ? 0.30 : 0.32;
  const double value_fraction = compact ? 0.30 : 0.25;
  const double label_width = bounds.width * label_fraction;
  const double value_width = bounds.width * value_fraction;
  const LogicalRect label_bounds{
      bounds.x,
      bounds.y,
      label_width,
      bounds.height,
  };
  const LogicalRect value_bounds{
      bounds.right() - value_width,
      bounds.y,
      value_width,
      bounds.height,
  };
  const LogicalRect track_bounds{
      label_bounds.right() + kElementGap,
      bounds.y + (bounds.height - 6.0) / 2.0,
      value_bounds.x - kElementGap - (label_bounds.right() + kElementGap),
      6.0,
  };
  if ((track_bounds.width < 8.0) || !bounds.contains(label_bounds) ||
      !bounds.contains(track_bounds) || !bounds.contains(value_bounds)) {
    throw std::invalid_argument("selected details meter row is too narrow");
  }

  const std::string marker(marker_text(meter.state.marker));
  std::string full_label(compact ? compact_label : wide_label);
  if (!marker.empty()) {
    full_label += " " + marker;
  }
  const std::string full_value = meter.maximum > 0
      ? std::format("{} / {}", meter.current, meter.maximum)
      : "N/A";
  std::string accessibility = std::string(wide_label) + " " + full_value;
  if (!meter.state.label.empty()) {
    accessibility += ", " + meter.state.label;
  }

  double fill_fraction = 0.0;
  if (meter.maximum > 0) {
    fill_fraction = std::clamp(
        static_cast<double>(meter.current) /
            static_cast<double>(meter.maximum),
        0.0,
        1.0);
  }
  const std::string visible_label =
      elide_to_practical_floor(full_label, label_bounds);
  const std::string visible_value =
      elide_to_practical_floor(full_value, value_bounds);
  return {
      .bounds = bounds,
      .label_bounds = label_bounds,
      .track_bounds = track_bounds,
      .value_bounds = value_bounds,
      .label_text = visible_label,
      .value_text = visible_value,
      .state_marker_text = marker,
      .accessibility_text = std::move(accessibility),
      .label_text_style = fit_text_style(
          typography.caption, label_bounds, visible_label.size()),
      .value_text_style = fit_text_style(
          typography.caption, value_bounds, visible_value.size()),
      .fill_fraction = fill_fraction,
      .state = meter.state,
  };
}

[[nodiscard]] SelectedPartyDetailsLayout empty_layout(
    const SelectedPartyDetailsLayoutRequest& request,
    LogicalRect inner,
    LogicalRect heading_bounds) {
  constexpr double kGap = 4.0;
  const LogicalRect message_bounds{
      inner.x,
      heading_bounds.bottom() + kGap,
      inner.width,
      inner.bottom() - heading_bounds.bottom() - kGap,
  };
  const std::string message = "Select a party member.";
  return {
      .density = request.density,
      .panel_bounds = request.details_panel,
      .heading_bounds = heading_bounds,
      .heading_text = "DETAILS",
      .heading_text_style = fit_text_style(
          request.typography.heading, heading_bounds, 7U),
      .empty_message_bounds = message_bounds,
      .empty_message_text = message,
      .empty_message_text_style = fit_text_style(
          request.typography.body, message_bounds, message.size()),
  };
}

} // namespace

SelectedPartyDetailsLayout compute_selected_party_details_layout(
    const SelectedPartyDetailsLayoutRequest& request) {
  validate_request(request);
  const bool compact =
      request.density == SelectedPartyDetailsLayoutDensity::compact;
  const double inset = compact ? kCompactInset : kWideInset;
  const LogicalRect inner{
      request.details_panel.x + inset,
      request.details_panel.y + inset,
      request.details_panel.width - 2.0 * inset,
      request.details_panel.height - 2.0 * inset,
  };
  const double heading_height = compact
      ? 14.0
      : std::clamp(request.typography.heading.line_height, 18.0, 26.0);
  const double heading_width = compact ? 48.0 : inner.width;
  const LogicalRect heading_bounds{
      inner.x,
      inner.y,
      heading_width,
      heading_height,
  };
  if (!request.details.member) {
    return empty_layout(request, inner, heading_bounds);
  }

  constexpr double kCompactGap = 2.0;
  constexpr double kWideHeadingGap = 4.0;
  constexpr double kWideRowGap = 3.0;
  constexpr double kWideMeterGap = 2.0;
  LogicalRect name_bounds;
  LogicalRect summary_bounds;
  LogicalRect stamina_bounds;
  LogicalRect spell_points_bounds;
  LogicalRect state_bounds;

  if (compact) {
    constexpr double kHeadingNameGap = 3.0;
    name_bounds = {
        heading_bounds.right() + kHeadingNameGap,
        inner.y,
        inner.right() - heading_bounds.right() - kHeadingNameGap,
        heading_height,
    };
    summary_bounds = {
        inner.x,
        heading_bounds.bottom() + kCompactGap,
        inner.width,
        12.0,
    };
    const double meter_y = summary_bounds.bottom() + kCompactGap;
    constexpr double kMeterColumnGap = 4.0;
    const double meter_width = (inner.width - kMeterColumnGap) / 2.0;
    stamina_bounds = {
        inner.x,
        meter_y,
        meter_width,
        14.0,
    };
    spell_points_bounds = {
        stamina_bounds.right() + kMeterColumnGap,
        meter_y,
        inner.right() - stamina_bounds.right() - kMeterColumnGap,
        14.0,
    };
    state_bounds = {
        inner.x,
        stamina_bounds.bottom() + kCompactGap,
        inner.width,
        inner.bottom() - stamina_bounds.bottom() - kCompactGap,
    };
  } else {
    const double name_y = heading_bounds.bottom() + kWideHeadingGap;
    const double name_height = std::clamp(
        request.typography.body.line_height, 16.0, 22.0);
    name_bounds = {inner.x, name_y, inner.width, name_height};
    const double summary_y = name_bounds.bottom() + kWideRowGap;
    const double summary_height = std::clamp(
        request.typography.caption.line_height, 13.0, 18.0);
    summary_bounds = {
        inner.x,
        summary_y,
        inner.width,
        summary_height,
    };
    const double meter_height = summary_height;
    stamina_bounds = {
        inner.x,
        summary_bounds.bottom() + kWideRowGap,
        inner.width,
        meter_height,
    };
    spell_points_bounds = {
        inner.x,
        stamina_bounds.bottom() + kWideMeterGap,
        inner.width,
        meter_height,
    };
    state_bounds = {
        inner.x,
        spell_points_bounds.bottom() + kWideRowGap,
        inner.width,
        inner.bottom() - spell_points_bounds.bottom() - kWideRowGap,
    };
  }

  if ((name_bounds.width <= 0.0) ||
      (summary_bounds.height < kMinimumPracticalLineHeight) ||
      (state_bounds.height < kMinimumPracticalLineHeight) ||
      !request.details_panel.contains(heading_bounds) ||
      !request.details_panel.contains(name_bounds) ||
      !request.details_panel.contains(summary_bounds) ||
      !request.details_panel.contains(stamina_bounds) ||
      !request.details_panel.contains(spell_points_bounds) ||
      !request.details_panel.contains(state_bounds)) {
    throw std::invalid_argument(
        "selected details panel cannot contain all information rows");
  }

  const std::string full_name = request.details.name.empty()
      ? "Selected party member"
      : request.details.name;
  const std::string name_text =
      elide_to_practical_floor(full_name, name_bounds);
  const std::string full_summary = compact
      ? std::format(
            "L{}  A{}  M{}/{}",
            request.details.level,
            request.details.armor_class,
            request.details.movement,
            request.details.movement_maximum)
      : std::format(
            "Level {}  Armor {}  Movement {}/{}",
            request.details.level,
            request.details.armor_class,
            request.details.movement,
            request.details.movement_maximum);
  const std::string summary_text =
      elide_to_practical_floor(full_summary, summary_bounds);

  std::vector<SelectedPartyDetailsRenderableStateToken> tokens;
  tokens.reserve(request.details.states.size());
  for (const auto& source : request.details.states) {
    tokens.emplace_back(renderable_token(source));
  }
  auto summary_tokens = tokens;
  std::ranges::stable_sort(
      summary_tokens,
      [](const auto& left, const auto& right) {
        return summary_priority(left) < summary_priority(right);
      });
  auto states = state_summary(
      summary_tokens,
      state_bounds,
      request.details.conscious);

  SelectedPartyDetailsLayout result{
      .density = request.density,
      .panel_bounds = request.details_panel,
      .has_selection = true,
      .heading_bounds = heading_bounds,
      .heading_text = "DETAILS",
      .heading_text_style = fit_text_style(
          request.typography.heading, heading_bounds, 7U),
      .name_bounds = name_bounds,
      .name_text = name_text,
      .name_text_style = fit_text_style(
          request.typography.body, name_bounds, name_text.size()),
      .summary_bounds = summary_bounds,
      .summary_text = summary_text,
      .summary_text_style = fit_text_style(
          request.typography.caption, summary_bounds, summary_text.size()),
      .stamina = meter_layout(
          stamina_bounds,
          "STAMINA",
          "ST",
          request.details.stamina,
          request.typography,
          request.density),
      .spell_points = meter_layout(
          spell_points_bounds,
          "SPELL POINTS",
          "SP",
          request.details.spell_points,
          request.typography,
          request.density),
      .state_bounds = state_bounds,
      .state_text = std::move(states.text),
      .state_text_style = {},
      .state_tokens = std::move(tokens),
      .visible_state_count = states.visible_count,
      .hidden_state_count = states.hidden_count,
  };
  result.state_text_style = fit_text_style(
      request.typography.caption,
      result.state_bounds,
      result.state_text.size());
  return result;
}

} // namespace realmz::presentation
