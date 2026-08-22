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

[[nodiscard]] bool interiors_overlap(
    PhysicalRect first,
    PhysicalRect second) noexcept {
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
      .spell_points = MeterModel{
          .current = static_cast<int32_t>(index),
          .maximum = 12,
          .fill_fraction = static_cast<double>(index) / 12.0,
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
      .armor_class = static_cast<int16_t>(-3 + static_cast<int>(index)),
      .auxiliary_vital = index % 2U == 0U
          ? PartyAuxiliaryVitalKind::attack_cadence
          : PartyAuxiliaryVitalKind::spell_points,
      .attack_cadence_half_units = static_cast<int32_t>(index),
  };
}

[[nodiscard]] PartyStatusModel default_status() {
  return {
      .active_effects = {},
      .fatigue = MeterModel{
          .current = 0,
          .maximum = 135,
          .fill_fraction = 0.0,
          .state = StateTokenModel{
              .identifier = "party.fatigue.fresh",
              .label = "Fresh",
              .emphasis = StateEmphasis::positive,
              .marker = StateMarker::check,
          },
      },
      .pooled_money = {0, 0, 0},
  };
}

[[nodiscard]] PartyEffectModel effect(
    PartyEffectKind kind,
    int16_t raw_value,
    std::string identifier,
    std::string label) {
  return {
      .kind = kind,
      .raw_value = raw_value,
      .state = StateTokenModel{
          .identifier = std::move(identifier),
          .label = std::move(label),
          .emphasis = StateEmphasis::information,
          .marker = StateMarker::condition,
      },
  };
}

template <typename MemberRange>
[[nodiscard]] PartyRailModel party_rail(
    const MemberRange& members,
    PartyStatusModel status = default_status()) {
  return {
      .members = std::vector<PartyRailMemberModel>(
          members.begin(), members.end()),
      .status = std::move(status),
  };
}

template <typename MemberRange>
[[nodiscard]] PartyRailLayout compute_test_layout(
    LogicalRect panel,
    const MemberRange& members,
    TypographyModel type,
    ScreenContext screen = ScreenContext::exploration,
    PartyStatusModel status = default_status()) {
  const auto model = party_rail(members, std::move(status));
  return compute_party_rail_layout({
      .party_panel = panel,
      .party_rail = model,
      .screen = screen,
      .typography = type,
  });
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
    double text_scale,
    ScreenContext screen) {
  std::vector<PartyRailMemberModel> members;
  members.reserve(member_count);
  for (size_t index = 0; index < member_count; ++index) {
    members.emplace_back(member(index));
  }
  const auto type = typography(text_scale);
  const auto model = party_rail(members);
  const auto before = model;
  const auto layout = compute_party_rail_layout({
      .party_panel = panel,
      .party_rail = model,
      .screen = screen,
      .typography = type,
  });

  CHECK(layout.panel_bounds == panel);
  CHECK(layout.heading_text == "PARTY");
  CHECK(panel.contains(layout.heading_bounds));
  CHECK(panel.contains(layout.status.bounds));
  CHECK(layout.status.bounds.contains(layout.heading_bounds));
  CHECK(layout.status.bounds.contains(layout.status.effects_bounds));
  CHECK(layout.status.effects_text == "EFFECTS None");
  CHECK(layout.status.effect_tokens.empty());
  CHECK(!interiors_overlap(
      layout.status.bounds, layout.members.front().card_bounds));
  verify_text_style(
      layout.heading_text_style, type.heading, layout.heading_bounds);
  verify_text_style(
      layout.status.effects_text_style,
      type.caption,
      layout.status.effects_bounds);
  verify_practical_representative_text_style(layout.heading_text_style);
  verify_practical_representative_text_style(
      layout.status.effects_text_style);
  CHECK(!interiors_overlap(
      layout.heading_bounds, layout.status.effects_bounds));
  const bool show_world_resources = screen != ScreenContext::combat;
  if (show_world_resources) {
    CHECK(layout.status.fatigue_meter_bounds.has_value());
    CHECK(layout.status.fatigue_bounds.has_value());
    CHECK(layout.status.pooled_money_bounds.has_value());
    CHECK(layout.status.bounds.contains(
        *layout.status.fatigue_meter_bounds));
    CHECK(layout.status.bounds.contains(*layout.status.fatigue_bounds));
    CHECK(layout.status.bounds.contains(
        *layout.status.pooled_money_bounds));
    CHECK(!interiors_overlap(
        *layout.status.fatigue_meter_bounds,
        *layout.status.fatigue_bounds));
    CHECK(layout.status.fatigue_text == "FAT 0/135  [+] Fresh");
    CHECK(layout.status.pooled_money_text ==
        (panel.width < 300.0
            ? "POOL G 0  GM 0  J 0"
            : "POOL GOLD 0  GEMS 0  JEWELRY 0"));
    CHECK(layout.status.accessibility_text ==
        "party effects none; fatigue 0 of 135, band Fresh; pooled money "
        "gold 0, gems 0, jewelry 0");
    CHECK(layout.status.fatigue_state_token.has_value());
    CHECK(layout.status.fatigue_state_token->label == "Fresh");
    CHECK(layout.status.fatigue_fill_fraction == 0.0);
    CHECK(layout.status.fatigue_meter_available);
    verify_text_style(
        layout.status.fatigue_text_style,
        type.caption,
        *layout.status.fatigue_bounds);
    verify_text_style(
        layout.status.pooled_money_text_style,
        type.caption,
        *layout.status.pooled_money_bounds);
    verify_practical_representative_text_style(
        layout.status.fatigue_text_style);
    verify_practical_representative_text_style(
        layout.status.pooled_money_text_style);
    CHECK(!interiors_overlap(
        layout.status.effects_bounds, *layout.status.fatigue_bounds));
    CHECK(!interiors_overlap(
        *layout.status.fatigue_bounds,
        *layout.status.pooled_money_bounds));
  } else {
    CHECK(!layout.status.fatigue_meter_bounds.has_value());
    CHECK(!layout.status.fatigue_bounds.has_value());
    CHECK(!layout.status.pooled_money_bounds.has_value());
    CHECK(layout.status.fatigue_text.empty());
    CHECK(layout.status.pooled_money_text.empty());
    CHECK(!layout.status.fatigue_state_token.has_value());
    CHECK(layout.status.accessibility_text == "party effects none");
  }
  for (const double backing_scale : {0.75, 1.0, 2.0}) {
    const BackingTransform transform(backing_scale);
    const auto physical_status = transform.to_physical(layout.status.bounds);
    const auto physical_heading =
        transform.to_physical(layout.heading_bounds);
    const auto physical_effects =
        transform.to_physical(layout.status.effects_bounds);
    for (const auto child : {physical_heading, physical_effects}) {
      CHECK(contains(physical_status, child));
    }
    CHECK(!interiors_overlap(physical_heading, physical_effects));
    CHECK(!interiors_overlap(
        physical_status,
        transform.to_physical(layout.members.front().card_bounds)));
    if (show_world_resources) {
      const auto physical_fatigue_meter =
          transform.to_physical(*layout.status.fatigue_meter_bounds);
      const auto physical_fatigue =
          transform.to_physical(*layout.status.fatigue_bounds);
      const auto physical_money =
          transform.to_physical(*layout.status.pooled_money_bounds);
      for (const auto child : {
               physical_fatigue_meter,
               physical_fatigue,
               physical_money,
           }) {
        CHECK(contains(physical_status, child));
      }
      CHECK(!interiors_overlap(physical_effects, physical_fatigue));
      CHECK(!interiors_overlap(physical_fatigue_meter, physical_fatigue));
      CHECK(!interiors_overlap(physical_fatigue, physical_money));
    }
  }
  CHECK(layout.members.size() == member_count);
  CHECK(model == before);

  for (size_t index = 0; index < member_count; ++index) {
    const auto& placed = layout.members[index];
    CHECK(placed.member_index == index);
    CHECK(placed.member_id == members[index].id);
    CHECK(placed.name_text == members[index].name);
    CHECK(placed.level_text == "Lv " + std::to_string(members[index].level));
    CHECK(placed.armor_class_text ==
        "AC " + std::to_string(members[index].armor_class));
    CHECK(placed.stamina_value_text ==
        "ST " + std::to_string(members[index].stamina.current) + "/" +
            std::to_string(members[index].stamina.maximum));
    if (members[index].auxiliary_vital ==
        PartyAuxiliaryVitalKind::spell_points) {
      CHECK(placed.auxiliary_vital_text ==
          "SP " + std::to_string(members[index].spell_points.current) +
              "/" + std::to_string(members[index].spell_points.maximum));
    } else {
      const int32_t half_units = members[index].attack_cadence_half_units;
      const int32_t numerator = half_units % 2 == 0
          ? half_units / 2
          : half_units;
      const int32_t denominator = half_units % 2 == 0 ? 1 : 2;
      CHECK(placed.auxiliary_vital_text ==
          "ATK " + std::to_string(numerator) + "/" +
              std::to_string(denominator));
    }
    CHECK(panel.contains(placed.card_bounds));
    CHECK(placed.card_bounds.height >= 44.0);
    CHECK(placed.card_bounds.contains(placed.portrait_bounds));
    CHECK(placed.card_bounds.contains(placed.name_bounds));
    CHECK(placed.card_bounds.contains(placed.level_bounds));
    CHECK(placed.card_bounds.contains(placed.armor_class_bounds));
    CHECK(placed.card_bounds.contains(placed.stamina_meter_bounds));
    CHECK(placed.card_bounds.contains(placed.stamina_value_bounds));
    CHECK(placed.card_bounds.contains(placed.auxiliary_vital_bounds));
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
        placed.portrait_bounds, placed.armor_class_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.stamina_meter_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.stamina_value_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.auxiliary_vital_bounds));
    CHECK(!interiors_overlap(
        placed.portrait_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.level_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.armor_class_bounds));
    CHECK(!interiors_overlap(
        placed.level_bounds, placed.armor_class_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_meter_bounds, placed.stamina_value_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_meter_bounds, placed.auxiliary_vital_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_value_bounds, placed.auxiliary_vital_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.stamina_meter_bounds));
    CHECK(!interiors_overlap(placed.name_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_meter_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(
        placed.stamina_value_bounds, placed.state_bounds));
    CHECK(!interiors_overlap(
        placed.auxiliary_vital_bounds, placed.state_bounds));
    CHECK(placed.name_bounds.y == placed.level_bounds.y);
    CHECK(placed.level_bounds.y == placed.armor_class_bounds.y);
    CHECK(placed.name_bounds.bottom() <= placed.stamina_value_bounds.y);
    CHECK(placed.stamina_value_bounds.bottom() <= placed.state_bounds.y);
    check_near(placed.name_bounds.x, placed.stamina_meter_bounds.x);
    check_near(placed.name_bounds.x, placed.state_bounds.x);
    check_near(
        placed.armor_class_bounds.right(), placed.state_bounds.right());
    check_near(
        placed.auxiliary_vital_bounds.right(), placed.state_bounds.right());
    for (const double backing_scale : {0.75, 1.0, 2.0}) {
      const BackingTransform transform(backing_scale);
      const auto physical_card = transform.to_physical(placed.card_bounds);
      for (const auto child : {
               placed.portrait_bounds,
               placed.name_bounds,
               placed.level_bounds,
               placed.armor_class_bounds,
               placed.stamina_meter_bounds,
               placed.stamina_value_bounds,
               placed.auxiliary_vital_bounds,
               placed.state_bounds,
           }) {
        CHECK(contains(physical_card, transform.to_physical(child)));
      }
      const auto physical_portrait =
          transform.to_physical(placed.portrait_bounds);
      CHECK(physical_portrait.width ==
          static_cast<int32_t>(std::lround(44.0 * backing_scale)));
      CHECK(physical_portrait.height ==
          static_cast<int32_t>(std::lround(44.0 * backing_scale)));
      const std::array physical_row_one{
          transform.to_physical(placed.name_bounds),
          transform.to_physical(placed.level_bounds),
          transform.to_physical(placed.armor_class_bounds),
      };
      const std::array physical_row_two{
          transform.to_physical(placed.stamina_meter_bounds),
          transform.to_physical(placed.stamina_value_bounds),
          transform.to_physical(placed.auxiliary_vital_bounds),
      };
      for (size_t first = 0; first < physical_row_one.size(); ++first) {
        for (size_t second = first + 1U;
             second < physical_row_one.size();
             ++second) {
          CHECK(!interiors_overlap(
              physical_row_one[first], physical_row_one[second]));
          CHECK(!interiors_overlap(
              physical_row_two[first], physical_row_two[second]));
        }
        for (const auto row_two : physical_row_two) {
          CHECK(!interiors_overlap(physical_row_one[first], row_two));
        }
      }
      const auto physical_state = transform.to_physical(placed.state_bounds);
      for (const auto row_two : physical_row_two) {
        CHECK(!interiors_overlap(row_two, physical_state));
      }
    }
    verify_text_style(
        placed.name_text_style, type.body, placed.name_bounds);
    verify_text_style(
        placed.level_text_style, type.caption, placed.level_bounds);
    verify_text_style(
        placed.armor_class_text_style,
        type.caption,
        placed.armor_class_bounds);
    verify_text_style(
        placed.stamina_value_text_style,
        type.caption,
        placed.stamina_value_bounds);
    verify_text_style(
        placed.auxiliary_vital_text_style,
        type.caption,
        placed.auxiliary_vital_bounds);
    verify_text_style(
        placed.state_text_style, type.caption, placed.state_bounds);
    // These representative strings fit at a practical floor even for six
    // members in the shorter 1360x768 wide rail at the minimum text scale.
    verify_practical_representative_text_style(placed.name_text_style);
    verify_practical_representative_text_style(placed.level_text_style);
    verify_practical_representative_text_style(
        placed.armor_class_text_style);
    verify_practical_representative_text_style(
        placed.stamina_value_text_style);
    verify_practical_representative_text_style(
        placed.auxiliary_vital_text_style);
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
    CHECK(placed.accessibility_text.find(members[index].name) !=
        std::string::npos);
    CHECK(placed.accessibility_text.find(members[index].states[0].label) !=
        std::string::npos);
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
  CHECK(layout == compute_party_rail_layout({
      .party_panel = panel,
      .party_rail = model,
      .screen = screen,
      .typography = type,
  }));
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
        verify_layout(
            panel, count, scale, ScreenContext::exploration);
        verify_layout(panel, count, scale, ScreenContext::combat);
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
  const auto approved_layout = compute_test_layout(
      panel,
      std::span<const PartyRailMemberModel>(&approved, 1U),
      type);
  const auto unavailable_layout = compute_test_layout(
      panel,
      std::span<const PartyRailMemberModel>(&unavailable, 1U),
      type);

  // Asset selection belongs to the renderer. Geometry always reserves the
  // same slot so a code-native fallback cannot cause a layout jump.
  CHECK(approved_layout.members[0].portrait_bounds ==
      unavailable_layout.members[0].portrait_bounds);
  CHECK(approved_layout.members[0].name_bounds ==
      unavailable_layout.members[0].name_bounds);
  CHECK(approved_layout.members[0].level_bounds ==
      unavailable_layout.members[0].level_bounds);
  CHECK(approved_layout.members[0].armor_class_bounds ==
      unavailable_layout.members[0].armor_class_bounds);
  CHECK(approved_layout.members[0].stamina_meter_bounds ==
      unavailable_layout.members[0].stamina_meter_bounds);
  CHECK(approved_layout.members[0].stamina_value_bounds ==
      unavailable_layout.members[0].stamina_value_bounds);
  CHECK(approved_layout.members[0].auxiliary_vital_bounds ==
      unavailable_layout.members[0].auxiliary_vital_bounds);
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
  const auto layout = compute_test_layout(
      {1016.4, 24.0, 319.6, 336.256},
      std::span<const PartyRailMemberModel>(&one, 1U),
      typography(1.0));
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
  CHECK(layout.members[0].accessibility_text ==
      "Member 1, level 1, armor class -3, stamina 10 of 20, "
      "attack cadence 0 over 1, states Selected, Known, Ready, Low, "
      "Unconscious, Unavailable, Poisoned, Neutral");
  for (const auto& state : one.states) {
    CHECK(layout.members[0].accessibility_text.find(state.label) !=
        std::string::npos);
  }
  verify_practical_representative_text_style(
      layout.members[0].state_text_style);
}

void test_attack_cadence_format_is_exhaustive_and_reduced() {
  auto one = member(0);
  one.auxiliary_vital = PartyAuxiliaryVitalKind::attack_cadence;
  one.spell_points.current = 99;
  one.spell_points.maximum = 99;
  constexpr std::array<std::string_view, 20> expected_fractions{
      "0/1", "1/2", "1/1", "3/2", "2/1",
      "5/2", "3/1", "7/2", "4/1", "9/2",
      "5/1", "11/2", "6/1", "13/2", "7/1",
      "15/2", "8/1", "17/2", "9/1", "19/2",
  };
  constexpr std::array out_of_range{
      -1,
      20,
      21,
      std::numeric_limits<int32_t>::max(),
      std::numeric_limits<int32_t>::min(),
  };

  for (int32_t half_units = 0; half_units <= 19; ++half_units) {
    one.attack_cadence_half_units = half_units;
    const auto layout = compute_test_layout(
        {1016.4, 24.0, 319.6, 336.256},
        std::span<const PartyRailMemberModel>(&one, 1U),
        typography(1.0));
    const auto fraction = expected_fractions[
        static_cast<size_t>(half_units)];
    CHECK(layout.members[0].auxiliary_vital_text == "ATK " +
        std::string(fraction));
    const auto separator = fraction.find('/');
    CHECK(layout.members[0].accessibility_text.find(
        "attack cadence " + std::string(fraction.substr(0U, separator)) +
            " over " + std::string(fraction.substr(separator + 1U))) !=
        std::string::npos);
  }

  for (const int32_t half_units : out_of_range) {
    one.attack_cadence_half_units = half_units;
    const auto layout = compute_test_layout(
        {1016.4, 24.0, 319.6, 336.256},
        std::span<const PartyRailMemberModel>(&one, 1U),
        typography(1.0));
    CHECK(layout.members[0].auxiliary_vital_text == "ATK > 10");
    CHECK(layout.members[0].accessibility_text.find(
        "attack cadence greater than 10") != std::string::npos);
  }
}

void test_caster_and_signed_vitals_are_exact() {
  auto caster = member(0);
  caster.armor_class = -12;
  caster.stamina.current = -9;
  caster.stamina.maximum = -2;
  caster.spell_points.current = 0;
  caster.spell_points.maximum = 37;
  caster.auxiliary_vital = PartyAuxiliaryVitalKind::spell_points;
  caster.attack_cadence_half_units = 19;
  auto layout = compute_test_layout(
      {738.88, 24.0, 261.12, 565.76},
      std::span<const PartyRailMemberModel>(&caster, 1U),
      typography(2.0));
  CHECK(layout.members[0].armor_class_text == "AC -12");
  CHECK(layout.members[0].stamina_value_text == "ST -9/-2");
  CHECK(layout.members[0].auxiliary_vital_text == "SP 0/37");
  CHECK(layout.members[0].accessibility_text ==
      "Member 1, level 1, armor class -12, stamina -9 of -2, "
      "spell points 0 of 37, state Condition 0");

  caster.spell_points.current = -8;
  caster.spell_points.maximum = -1;
  layout = compute_test_layout(
      {738.88, 24.0, 261.12, 565.76},
      std::span<const PartyRailMemberModel>(&caster, 1U),
      typography(0.75));
  CHECK(layout.members[0].auxiliary_vital_text == "SP -8/-1");
  CHECK(layout.members[0].accessibility_text.find(
      "spell points -8 of -1") != std::string::npos);
  verify_practical_representative_text_style(
      layout.members[0].armor_class_text_style);
  verify_practical_representative_text_style(
      layout.members[0].stamina_value_text_style);
  verify_practical_representative_text_style(
      layout.members[0].auxiliary_vital_text_style);
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
  const auto layout = compute_test_layout(
      {1016.4, 24.0, 319.6, 336.256}, members, type);

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

[[nodiscard]] PartyStatusModel extreme_active_status() {
  auto status = default_status();
  status.active_effects = {
      effect(PartyEffectKind::waterworld,
          std::numeric_limits<int16_t>::min(),
          "party.effect.waterworld", "Waterworld"),
      effect(PartyEffectKind::dragon_hide, 2,
          "party.effect.dragon_hide", "Dragon Hide"),
      effect(PartyEffectKind::discover_secret, -3,
          "party.effect.discover_secret", "Discover Secret"),
      effect(PartyEffectKind::wizard_eye, 4,
          "party.effect.wizard_eye", "Wizard Eye"),
      effect(PartyEffectKind::search, -5,
          "party.effect.search", "Search"),
      effect(PartyEffectKind::free_fall_levitate, 6,
          "party.effect.free_fall_levitate", "Free Fall / Levitate"),
      effect(PartyEffectKind::sentry, -7,
          "party.effect.sentry", "Sentry"),
      effect(PartyEffectKind::charm_resistance,
          std::numeric_limits<int16_t>::max(),
          "party.effect.charm_resistance", "Charm Resistance"),
  };
  status.fatigue = MeterModel{
      .current = std::numeric_limits<int32_t>::min(),
      .maximum = 135,
      .fill_fraction = 1.0,
      .state = StateTokenModel{
          .identifier = "party.fatigue.critical",
          .label = "Critical",
          .emphasis = StateEmphasis::critical,
          .marker = StateMarker::alert,
      },
  };
  status.pooled_money = {
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max(),
      -1234567890,
  };
  return status;
}

void test_party_status_is_complete_responsive_and_screen_specific() {
  const std::array members{member(0)};
  const auto status = extreme_active_status();
  const LogicalRect compact_panel{738.88, 24.0, 261.12, 565.76};
  const auto exploration = compute_test_layout(
      compact_panel,
      members,
      typography(2.0),
      ScreenContext::exploration,
      status);

  CHECK(exploration.status.effect_tokens.size() == 8U);
  CHECK(exploration.status.effects_text ==
      "EFFECTS [*] Waterworld ... (+7)");
  for (size_t index = 0; index < status.active_effects.size(); ++index) {
    CHECK(exploration.status.effect_tokens[index].kind ==
        status.active_effects[index].kind);
    CHECK(exploration.status.effect_tokens[index].raw_value ==
        status.active_effects[index].raw_value);
    CHECK(exploration.status.effect_tokens[index].state.identifier ==
        status.active_effects[index].state.identifier);
    CHECK(exploration.status.effect_tokens[index].state.label ==
        status.active_effects[index].state.label);
    CHECK(exploration.status.accessibility_text.find(
        status.active_effects[index].state.label) != std::string::npos);
  }
  CHECK(exploration.status.fatigue_meter_bounds.has_value());
  CHECK(exploration.status.fatigue_bounds.has_value());
  CHECK(exploration.status.pooled_money_bounds.has_value());
  CHECK(exploration.status.fatigue_text ==
      "FAT -2147483648/135  [!] Critical");
  CHECK(exploration.status.pooled_money_text ==
      "POOL G -2147483648  GM 2147483647  J -1234567890");
  CHECK(exploration.status.fatigue_state_token->identifier ==
      "party.fatigue.critical");
  CHECK(exploration.status.fatigue_fill_fraction == 1.0);
  CHECK(exploration.status.fatigue_meter_available);
  CHECK(exploration.status.accessibility_text.find(
      "fatigue -2147483648 of 135, band Critical") !=
      std::string::npos);
  CHECK(exploration.status.accessibility_text.find(
      "pooled money gold -2147483648, gems 2147483647, jewelry "
      "-1234567890") != std::string::npos);
  verify_practical_representative_text_style(
      exploration.status.effects_text_style);
  verify_practical_representative_text_style(
      exploration.status.fatigue_text_style);
  verify_practical_representative_text_style(
      exploration.status.pooled_money_text_style);

  const auto dungeon = compute_test_layout(
      compact_panel,
      members,
      typography(2.0),
      ScreenContext::dungeon,
      status);
  CHECK(dungeon.status == exploration.status);
  CHECK(dungeon.members == exploration.members);

  auto combat_status = status;
  combat_status.fatigue = MeterModel{};
  const auto combat = compute_test_layout(
      {1016.4, 24.0, 319.6, 336.256},
      members,
      typography(1.0),
      ScreenContext::combat,
      combat_status);
  CHECK(combat.status.effect_tokens == exploration.status.effect_tokens);
  CHECK(combat.status.effects_text ==
      "EFFECTS [*] Waterworld ... (+7)");
  CHECK(!combat.status.fatigue_meter_bounds.has_value());
  CHECK(!combat.status.fatigue_bounds.has_value());
  CHECK(!combat.status.pooled_money_bounds.has_value());
  CHECK(combat.status.fatigue_text.empty());
  CHECK(combat.status.pooled_money_text.empty());
  CHECK(!combat.status.fatigue_state_token.has_value());
  CHECK(combat.status.accessibility_text ==
      "party effects Waterworld, Dragon Hide, Discover Secret, Wizard Eye, "
      "Search, Free Fall / Levitate, Sentry, Charm Resistance");
  CHECK(combat.status.accessibility_text.find("fatigue") ==
      std::string::npos);
  CHECK(combat.status.accessibility_text.find("money") ==
      std::string::npos);
  CHECK(!interiors_overlap(
      combat.status.bounds, combat.members[0].card_bounds));
  CHECK(combat.members[0].card_bounds.height >= 44.0);
  CHECK(combat == compute_test_layout(
      {1016.4, 24.0, 319.6, 336.256},
      members,
      typography(1.0),
      ScreenContext::combat,
      combat_status));
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
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 300.0, 400.0}, members, type));
  });
  for (size_t index = 0; index < 7; ++index) {
    members.emplace_back(member(index));
  }
  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 300.0, 400.0}, members, type));
  });
  members.resize(1);
  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 179.0, 400.0}, members, type));
  });
  auto bad_type = type;
  bad_type.body.point_size = std::numeric_limits<double>::quiet_NaN();
  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 300.0, 400.0}, members, bad_type));
  });
  auto invalid_vital = members[0];
  invalid_vital.auxiliary_vital =
      static_cast<PartyAuxiliaryVitalKind>(127);
  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 300.0, 400.0},
        std::span<const PartyRailMemberModel>(&invalid_vital, 1U),
        type));
  });

  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 300.0, 400.0},
        members,
        type,
        ScreenContext::inventory));
  });

  const auto check_bad_status = [&](PartyStatusModel status) {
    check_invalid_argument([&] {
      static_cast<void>(compute_test_layout(
          {0.0, 0.0, 300.0, 400.0},
          members,
          type,
          ScreenContext::exploration,
          std::move(status)));
    });
  };
  auto bad_status = default_status();
  bad_status.active_effects = {
      effect(PartyEffectKind::waterworld, 0,
          "party.effect.waterworld", "Waterworld"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(static_cast<PartyEffectKind>(0), 1,
          "party.effect.invalid", "Invalid"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(static_cast<PartyEffectKind>(9), 1,
          "party.effect.invalid", "Invalid"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  for (size_t index = 0; index < 9U; ++index) {
    bad_status.active_effects.emplace_back(effect(
        PartyEffectKind::waterworld,
        1,
        "party.effect.too_many." + std::to_string(index),
        "Too Many " + std::to_string(index)));
  }
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(PartyEffectKind::search, 1,
          "party.effect.search", "Search"),
      effect(PartyEffectKind::search, 2,
          "party.effect.search.duplicate", "Search Again"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(PartyEffectKind::search, 1,
          "party.effect.search", "Search"),
      effect(PartyEffectKind::dragon_hide, 2,
          "party.effect.dragon_hide", "Dragon Hide"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(PartyEffectKind::search, 1, "", "Search"),
  };
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.active_effects = {
      effect(PartyEffectKind::search, 1, "party.effect.search", ""),
  };
  check_bad_status(bad_status);

  bad_status = default_status();
  bad_status.fatigue.maximum = 134;
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.fill_fraction =
      std::numeric_limits<double>::quiet_NaN();
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.fill_fraction =
      std::numeric_limits<double>::infinity();
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.fill_fraction = -0.01;
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.fill_fraction = 1.01;
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.state.identifier.clear();
  check_bad_status(bad_status);
  bad_status = default_status();
  bad_status.fatigue.state.label.clear();
  check_bad_status(bad_status);

  members.clear();
  for (size_t index = 0; index < 6; ++index) {
    members.emplace_back(member(index));
  }
  check_invalid_argument([&] {
    static_cast<void>(compute_test_layout(
        {0.0, 0.0, 180.0, 160.0}, members, type));
  });
}

} // namespace

int main() {
  try {
    test_all_supported_counts_sizes_and_scales();
    test_portrait_slot_is_stable_across_asset_identities();
    test_state_tokens_are_complete_and_non_color();
    test_attack_cadence_format_is_exhaustive_and_reduced();
    test_caster_and_signed_vitals_are_exact();
    test_fallbacks_and_text_fitting();
    test_party_status_is_complete_responsive_and_screen_specific();
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
