#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "presentation/PartyRailLayout.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

[[nodiscard]] bool interiors_overlap(
    LogicalRect first,
    LogicalRect second) noexcept {
  return std::max(first.x, second.x) < std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) < std::min(first.bottom(), second.bottom());
}

[[nodiscard]] bool contains(
    PhysicalRect outer,
    PhysicalRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
      inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

void check_near(double actual, double expected) {
  constexpr double kTolerance = 1e-9;
  CHECK(std::abs(actual - expected) <= kTolerance);
}

[[nodiscard]] TypographyModel typography(double scale) {
  return {
      .scale = scale,
      .caption = {13.0 * scale, 17.0 * scale},
      .body = {16.0 * scale, 22.0 * scale},
      .heading = {20.0 * scale, 26.0 * scale},
  };
}

[[nodiscard]] PartyRailMemberModel member(size_t index) {
  return {
      .id = static_cast<PartyMemberId>(100U + index),
      .name = "Member " + std::to_string(index + 1U),
      .level = static_cast<int16_t>(index + 1U),
      .portrait_id = static_cast<int32_t>(257U + index),
      .stamina = MeterModel{
          .current = static_cast<int32_t>(10U + index),
          .maximum = 20,
          .fill_fraction = (10.0 + static_cast<double>(index)) / 20.0,
      },
      .states = {
          StateTokenModel{
              .identifier = "status.condition." + std::to_string(index),
              .label = "Condition " + std::to_string(index),
              .emphasis = StateEmphasis::caution,
              .marker = StateMarker::condition,
          },
      },
      .conscious = true,
  };
}

void verify_text_style(
    const TextStyleModel& style,
    const TextStyleModel& requested,
    LogicalRect bounds) {
  CHECK(std::isfinite(style.point_size));
  CHECK(std::isfinite(style.line_height));
  CHECK(style.point_size > 0.0);
  CHECK(style.line_height > 0.0);
  CHECK(style.point_size <= requested.point_size);
  CHECK(style.line_height <= requested.line_height);
  CHECK(style.line_height <= bounds.height);
}

void verify_practical_representative_text_style(
    const TextStyleModel& style) {
  constexpr double kMinimumEffectivePointSize = 8.0;
  constexpr double kMinimumEffectiveLineHeight = 10.0;
  CHECK(style.point_size >= kMinimumEffectivePointSize);
  CHECK(style.line_height >= kMinimumEffectiveLineHeight);
}

void verify_layout(
    LogicalRect panel,
    size_t member_count,
    double text_scale) {
  std::vector<PartyRailMemberModel> members;
  members.reserve(member_count);
  for (size_t index = 0; index < member_count; ++index) {
    members.emplace_back(member(index));
  }
  const auto type = typography(text_scale);
  const auto before = members;
  const auto layout = compute_party_rail_layout({panel, members, type});

  CHECK(layout.panel_bounds == panel);
  CHECK(layout.heading_text == "PARTY");
  CHECK(panel.contains(layout.heading_bounds));
  verify_text_style(
      layout.heading_text_style, type.heading, layout.heading_bounds);
  CHECK(layout.members.size() == member_count);
  CHECK(members == before);

  for (size_t index = 0; index < member_count; ++index) {
    const auto& placed = layout.members[index];
    CHECK(placed.member_index == index);
    CHECK(placed.member_id == members[index].id);
    CHECK(placed.name_text == members[index].name);
    CHECK(placed.level_text == "Lv " + std::to_string(members[index].level));
    CHECK(panel.contains(placed.card_bounds));
    CHECK(placed.card_bounds.contains(placed.portrait_bounds));
    CHECK(placed.card_bounds.contains(placed.name_bounds));
    CHECK(placed.card_bounds.contains(placed.level_bounds));
    CHECK(placed.card_bounds.contains(placed.stamina_meter_bounds));
    CHECK(placed.card_bounds.contains(placed.stamina_value_bounds));
    CHECK(placed.card_bounds.contains(placed.state_bounds));
    check_near(placed.portrait_bounds.width, 44.0);
    check_near(placed.portrait_bounds.height, 44.0);
    check_near(
        placed.portrait_bounds.y + placed.portrait_bounds.height / 2.0,
        placed.card_bounds.y + placed.card_bounds.height / 2.0);
    CHECK(placed.portrait_bounds.right() < placed.name_bounds.x);
    CHECK(placed.portrait_bounds.right() < placed.stamina_meter_bounds.x);
    CHECK(placed.portrait_bounds.right() < placed.state_bounds.x);
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.name_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.level_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.stamina_meter_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.stamina_value_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.level_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_meter_bounds, placed.stamina_value_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.stamina_meter_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_meter_bounds, placed.state_bounds));
    check_near(placed.name_bounds.x, placed.stamina_meter_bounds.x);
    check_near(placed.name_bounds.x, placed.state_bounds.x);
    check_near(placed.level_bounds.right(), placed.state_bounds.right());
    check_near(
        placed.stamina_value_bounds.right(), placed.state_bounds.right());
    for (const double backing_scale : {1.0, 2.0}) {
      const BackingTransform transform(backing_scale);
      const auto physical_card = transform.to_physical(placed.card_bounds);
      const auto physical_portrait =
          transform.to_physical(placed.portrait_bounds);
      CHECK(contains(physical_card, physical_portrait));
      CHECK(physical_portrait.width ==
          static_cast<int32_t>(44.0 * backing_scale));
      CHECK(physical_portrait.height ==
          static_cast<int32_t>(44.0 * backing_scale));
    }
    verify_text_style(
        placed.name_text_style, type.body, placed.name_bounds);
    verify_text_style(
        placed.level_text_style, type.caption, placed.level_bounds);
    verify_text_style(
        placed.stamina_value_text_style,
        type.caption,
        placed.stamina_value_bounds);
    verify_text_style(
        placed.state_text_style, type.caption, placed.state_bounds);
    // These representative strings fit at a practical floor even for six
    // members in the shorter 1360x768 wide rail at the minimum text scale.
    verify_practical_representative_text_style(placed.name_text_style);
    verify_practical_representative_text_style(placed.level_text_style);
    verify_practical_representative_text_style(
        placed.stamina_value_text_style);
    verify_practical_representative_text_style(placed.state_text_style);
    CHECK(placed.state_tokens.size() == members[index].states.size());
    CHECK(placed.state_tokens[0].identifier ==
        members[index].states[0].identifier);
    CHECK(placed.state_tokens[0].label == members[index].states[0].label);
    CHECK(placed.state_tokens[0].marker == StateMarker::condition);
    CHECK(placed.state_tokens[0].marker_text == "[*]");
    CHECK(placed.state_tokens[0].emphasis == StateEmphasis::caution);
    CHECK(placed.state_tokens[0].render_text ==
        "[*] " + members[index].states[0].label);
    CHECK(placed.state_text == placed.state_tokens[0].render_text);
    if (index > 0U) {
      CHECK(layout.members[index - 1U].card_bounds.y < placed.card_bounds.y);
      CHECK(!interiors_overlap(
          layout.members[index - 1U].card_bounds, placed.card_bounds));
      const double card_gap = placed.card_bounds.y -
          layout.members[index - 1U].card_bounds.bottom();
      CHECK(card_gap >= 3.0);
      CHECK(card_gap <= 4.0);
    }
  }
  CHECK(layout == compute_party_rail_layout({panel, members, type}));
}

void test_all_supported_counts_sizes_and_scales() {
  // Exact party-panel rectangles produced at the representative 1024x768
  // compact and 1360x768 wide shell sizes by ResponsiveLayout.
  constexpr std::array panels{
      LogicalRect{738.88, 24.0, 261.12, 565.76},
      LogicalRect{1016.4, 24.0, 319.6, 336.256},
  };
  constexpr std::array scales{0.75, 1.0, 2.0};
  for (const auto panel : panels) {
    for (const auto scale : scales) {
      for (size_t count = 1; count <= 6; ++count) {
        verify_layout(panel, count, scale);
      }
    }
  }
}

void test_portrait_slot_is_stable_across_asset_identities() {
  auto approved = member(0);
  auto unavailable = approved;
  approved.portrait_id = 257;
  unavailable.portrait_id = -32768;
  const auto panel = LogicalRect{738.88, 24.0, 261.12, 565.76};
  const auto type = typography(1.0);
  const auto approved_layout = compute_party_rail_layout({
      panel,
      std::span<const PartyRailMemberModel>(&approved, 1U),
      type,
  });
  const auto unavailable_layout = compute_party_rail_layout({
      panel,
      std::span<const PartyRailMemberModel>(&unavailable, 1U),
      type,
  });

  // Asset selection belongs to the renderer. Geometry always reserves the
  // same slot so a code-native fallback cannot cause a layout jump.
  CHECK(approved_layout.members[0].portrait_bounds ==
      unavailable_layout.members[0].portrait_bounds);
  CHECK(approved_layout.members[0].name_bounds ==
      unavailable_layout.members[0].name_bounds);
  CHECK(approved_layout.members[0].level_bounds ==
      unavailable_layout.members[0].level_bounds);
  CHECK(approved_layout.members[0].stamina_meter_bounds ==
      unavailable_layout.members[0].stamina_meter_bounds);
  CHECK(approved_layout.members[0].stamina_value_bounds ==
      unavailable_layout.members[0].stamina_value_bounds);
  CHECK(approved_layout.members[0].state_bounds ==
      unavailable_layout.members[0].state_bounds);
}

void test_state_tokens_are_complete_and_non_color() {
  auto one = member(0);
  one.states = {
      {"selected", "Selected", StateEmphasis::information,
          StateMarker::selection},
      {"information", "Known", StateEmphasis::information,
          StateMarker::information},
      {"check", "Ready", StateEmphasis::positive, StateMarker::check},
      {"alert", "Low", StateEmphasis::caution, StateMarker::alert},
      {"stop", "Unconscious", StateEmphasis::critical,
          StateMarker::stop},
      {"unavailable", "Unavailable", StateEmphasis::inactive,
          StateMarker::unavailable},
      {"condition", "Poisoned", StateEmphasis::caution,
          StateMarker::condition},
      {"neutral", "Neutral", StateEmphasis::neutral, StateMarker::none},
  };
  const auto layout = compute_party_rail_layout({
      {1016.4, 24.0, 319.6, 336.256},
      std::span<const PartyRailMemberModel>(&one, 1U),
      typography(1.0),
  });
  CHECK(layout.members[0].state_tokens.size() == one.states.size());
  constexpr std::array<std::string_view, 8> markers{
      "[>]", "[i]", "[+]", "[!]", "[X]", "[-]", "[*]", ""};
  for (size_t index = 0; index < one.states.size(); ++index) {
    const auto& source = one.states[index];
    const auto& rendered = layout.members[0].state_tokens[index];
    CHECK(rendered.marker == source.marker);
    CHECK(rendered.emphasis == source.emphasis);
    CHECK(rendered.label == source.label);
    CHECK(rendered.marker_text == markers[index]);
    CHECK(rendered.render_text.find(source.label) != std::string::npos);
  }
  CHECK(layout.members[0].state_text.find("Selected") != std::string::npos);
  CHECK(layout.members[0].state_text == "[>] Selected ... (+7)");
  CHECK(layout.members[0].state_text.find("Neutral") == std::string::npos);
  CHECK(layout.members[0].state_tokens.back().label == "Neutral");
  verify_practical_representative_text_style(
      layout.members[0].state_text_style);
}

void test_fallbacks_and_text_fitting() {
  auto ready = member(0);
  ready.name =
      "A deliberately long adventurer name that must fit without clipping";
  ready.states.clear();
  auto unconscious = member(1);
  unconscious.name.clear();
  unconscious.states.clear();
  unconscious.conscious = false;
  const std::array members{ready, unconscious};
  const auto type = typography(2.0);
  const auto layout = compute_party_rail_layout({
      {1016.4, 24.0, 319.6, 336.256}, members, type});

  CHECK(layout.members[0].state_tokens.size() == 1);
  CHECK(layout.members[0].state_tokens[0].label == "Ready");
  CHECK(layout.members[0].state_tokens[0].marker == StateMarker::check);
  CHECK(layout.members[0].state_tokens[0].emphasis ==
      StateEmphasis::positive);
  CHECK(layout.members[1].name_text == "Adventurer 2");
  CHECK(layout.members[1].state_tokens.size() == 1);
  CHECK(layout.members[1].state_tokens[0].label == "Unconscious");
  CHECK(layout.members[1].state_tokens[0].marker == StateMarker::stop);
  CHECK(layout.members[1].state_tokens[0].emphasis ==
      StateEmphasis::critical);
  CHECK(layout.members[0].name_text_style.point_size < type.body.point_size);
}

template <typename Function>
void check_invalid_argument(Function&& function) {
  bool threw = false;
  try {
    function();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void test_invalid_requests_never_return_partial_layouts() {
  std::vector<PartyRailMemberModel> members;
  const auto type = typography(1.0);
  check_invalid_argument([&] {
    static_cast<void>(compute_party_rail_layout({
        {0.0, 0.0, 300.0, 400.0}, members, type}));
  });
  for (size_t index = 0; index < 7; ++index) {
    members.emplace_back(member(index));
  }
  check_invalid_argument([&] {
    static_cast<void>(compute_party_rail_layout({
        {0.0, 0.0, 300.0, 400.0}, members, type}));
  });
  members.resize(1);
  check_invalid_argument([&] {
    static_cast<void>(compute_party_rail_layout({
        {0.0, 0.0, 179.0, 400.0}, members, type}));
  });
  auto bad_type = type;
  bad_type.body.point_size = std::numeric_limits<double>::quiet_NaN();
  check_invalid_argument([&] {
    static_cast<void>(compute_party_rail_layout({
        {0.0, 0.0, 300.0, 400.0}, members, bad_type}));
  });

  members.clear();
  for (size_t index = 0; index < 6; ++index) {
    members.emplace_back(member(index));
  }
  check_invalid_argument([&] {
    static_cast<void>(compute_party_rail_layout({
        {0.0, 0.0, 180.0, 160.0}, members, type}));
  });
}

} // namespace

int main() {
  try {
    test_all_supported_counts_sizes_and_scales();
    test_portrait_slot_is_stable_across_asset_identities();
    test_state_tokens_are_complete_and_non_color();
    test_fallbacks_and_text_fitting();
    test_invalid_requests_never_return_partial_layouts();
    std::cout << "PartyRailLayoutTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PartyRailLayoutTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
