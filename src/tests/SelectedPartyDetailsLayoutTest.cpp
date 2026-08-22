#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "presentation/SelectedPartyDetailsLayout.hpp"

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

[[nodiscard]] bool interiors_overlap(
    PhysicalRect first,
    PhysicalRect second) noexcept {
  return std::max(first.x, second.x) < std::min(first.right(), second.right()) &&
      std::max(first.y, second.y) < std::min(first.bottom(), second.bottom());
}

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
  size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    size_t length = 0;
    if (lead <= 0x7FU) {
      length = 1;
    } else if ((lead >= 0xC2U) && (lead <= 0xDFU)) {
      length = 2;
    } else if ((lead >= 0xE0U) && (lead <= 0xEFU)) {
      length = 3;
    } else if ((lead >= 0xF0U) && (lead <= 0xF4U)) {
      length = 4;
    } else {
      return false;
    }
    if (index + length > text.size()) {
      return false;
    }
    for (size_t offset = 1; offset < length; ++offset) {
      const auto continuation =
          static_cast<unsigned char>(text[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
    }
    index += length;
  }
  return true;
}

[[nodiscard]] TypographyModel typography(double scale = 1.0) {
  return {
      .scale = scale,
      .caption = {13.0 * scale, 17.0 * scale},
      .body = {16.0 * scale, 22.0 * scale},
      .heading = {20.0 * scale, 26.0 * scale},
  };
}

[[nodiscard]] StateTokenModel state(
    std::string identifier,
    std::string label,
    StateEmphasis emphasis,
    StateMarker marker) {
  return {
      .identifier = std::move(identifier),
      .label = std::move(label),
      .emphasis = emphasis,
      .marker = marker,
  };
}

[[nodiscard]] SelectedPartyDetailsModel selected_details() {
  return {
      .member = 2,
      .name = "Bryn",
      .level = 12,
      .armor_class = -4,
      .movement = 3,
      .movement_maximum = 12,
      .stamina = {
          .current = 4,
          .maximum = 20,
          .fill_fraction = 0.2,
          .state = state(
              "stamina.critical",
              "Critical",
              StateEmphasis::critical,
              StateMarker::stop),
      },
      .spell_points = {
          .current = 0,
          .maximum = 9,
          .fill_fraction = 0.0,
          .state = state(
              "spell_points.depleted",
              "Depleted",
              StateEmphasis::critical,
              StateMarker::stop),
      },
      .states = {
          state(
              "status.unconscious",
              "Unconscious",
              StateEmphasis::critical,
              StateMarker::stop),
          state(
              "status.condition.3",
              "Cursed",
              StateEmphasis::caution,
              StateMarker::condition),
          state(
              "status.condition.7",
              "Shielded from Hits",
              StateEmphasis::caution,
              StateMarker::condition),
      },
      .conscious = false,
  };
}

void verify_style(const TextStyleModel& style, LogicalRect bounds) {
  CHECK(std::isfinite(style.point_size));
  CHECK(std::isfinite(style.line_height));
  CHECK(style.point_size >= 8.0);
  CHECK(style.line_height >= 10.0);
  CHECK(style.line_height <= bounds.height);
}

void verify_meter(
    const SelectedPartyDetailsLayout& layout,
    const SelectedPartyDetailsMeterLayout& meter) {
  CHECK(layout.panel_bounds.contains(meter.bounds));
  CHECK(meter.bounds.contains(meter.label_bounds));
  CHECK(meter.bounds.contains(meter.track_bounds));
  CHECK(meter.bounds.contains(meter.value_bounds));
  CHECK(!interiors_overlap(meter.label_bounds, meter.track_bounds));
  CHECK(!interiors_overlap(meter.label_bounds, meter.value_bounds));
  CHECK(!interiors_overlap(meter.track_bounds, meter.value_bounds));
  CHECK(meter.track_bounds.width >= 8.0);
  CHECK(meter.state_marker_text == "[X]");
  CHECK(meter.label_text.find("[X]") != std::string::npos);
  CHECK(!meter.accessibility_text.empty());
  CHECK(meter.fill_fraction >= 0.0);
  CHECK(meter.fill_fraction <= 1.0);
  verify_style(meter.label_text_style, meter.label_bounds);
  verify_style(meter.value_text_style, meter.value_bounds);
}

void verify_selected_layout(
    LogicalRect panel,
    SelectedPartyDetailsLayoutDensity density,
    double text_scale) {
  const auto details = selected_details();
  const auto before = details;
  const auto type = typography(text_scale);
  const auto layout = compute_selected_party_details_layout({
      .details_panel = panel,
      .details = details,
      .typography = type,
      .density = density,
  });

  CHECK(details == before);
  CHECK(layout.panel_bounds == panel);
  CHECK(layout.density == density);
  CHECK(layout.has_selection);
  CHECK(layout.heading_text == "DETAILS");
  CHECK(layout.name_text == "Bryn");
  CHECK(panel.contains(layout.heading_bounds));
  CHECK(panel.contains(layout.name_bounds));
  CHECK(panel.contains(layout.summary_bounds));
  CHECK(panel.contains(layout.state_bounds));
  CHECK(!interiors_overlap(layout.heading_bounds, layout.name_bounds));
  CHECK(!interiors_overlap(layout.heading_bounds, layout.summary_bounds));
  CHECK(!interiors_overlap(layout.name_bounds, layout.summary_bounds));
  CHECK(!interiors_overlap(layout.summary_bounds, layout.stamina.bounds));
  CHECK(!interiors_overlap(layout.summary_bounds, layout.spell_points.bounds));
  CHECK(!interiors_overlap(layout.stamina.bounds, layout.spell_points.bounds));
  CHECK(!interiors_overlap(layout.stamina.bounds, layout.state_bounds));
  CHECK(!interiors_overlap(layout.spell_points.bounds, layout.state_bounds));
  verify_style(layout.heading_text_style, layout.heading_bounds);
  verify_style(layout.name_text_style, layout.name_bounds);
  verify_style(layout.summary_text_style, layout.summary_bounds);
  verify_style(layout.state_text_style, layout.state_bounds);
  verify_meter(layout, layout.stamina);
  verify_meter(layout, layout.spell_points);
  CHECK(layout.stamina.fill_fraction == 0.2);
  CHECK(layout.spell_points.fill_fraction == 0.0);
  CHECK(layout.stamina.value_text == "4 / 20");
  CHECK(layout.spell_points.value_text == "0 / 9");
  CHECK(layout.state_tokens.size() == details.states.size());
  CHECK(layout.visible_state_count + layout.hidden_state_count ==
      layout.state_tokens.size());
  for (size_t index = 0; index < details.states.size(); ++index) {
    CHECK(layout.state_tokens[index].identifier ==
        details.states[index].identifier);
    CHECK(layout.state_tokens[index].label == details.states[index].label);
    CHECK(layout.state_tokens[index].marker == details.states[index].marker);
    CHECK(layout.state_tokens[index].emphasis ==
        details.states[index].emphasis);
    CHECK(layout.state_tokens[index].render_text.find(
        layout.state_tokens[index].marker_text) != std::string::npos);
  }
  const std::array logical_regions{
      layout.heading_bounds,
      layout.name_bounds,
      layout.summary_bounds,
      layout.stamina.bounds,
      layout.spell_points.bounds,
      layout.state_bounds,
  };
  for (const double backing_scale : {1.0, 2.0}) {
    const BackingTransform transform(backing_scale);
    const auto physical_panel = transform.to_physical(panel);
    std::array<PhysicalRect, logical_regions.size()> physical_regions;
    for (size_t index = 0; index < logical_regions.size(); ++index) {
      physical_regions[index] = transform.to_physical(logical_regions[index]);
      CHECK(contains(physical_panel, physical_regions[index]));
      for (size_t prior = 0; prior < index; ++prior) {
        CHECK(!interiors_overlap(
            physical_regions[prior], physical_regions[index]));
      }
    }
    for (const auto* meter : {&layout.stamina, &layout.spell_points}) {
      const auto physical_meter = transform.to_physical(meter->bounds);
      const auto physical_label = transform.to_physical(meter->label_bounds);
      const auto physical_track = transform.to_physical(meter->track_bounds);
      const auto physical_value = transform.to_physical(meter->value_bounds);
      CHECK(contains(physical_meter, physical_label));
      CHECK(contains(physical_meter, physical_track));
      CHECK(contains(physical_meter, physical_value));
      CHECK(!interiors_overlap(physical_label, physical_track));
      CHECK(!interiors_overlap(physical_label, physical_value));
      CHECK(!interiors_overlap(physical_track, physical_value));
    }
  }
  CHECK(layout == compute_selected_party_details_layout({
      .details_panel = panel,
      .details = details,
      .typography = type,
      .density = density,
  }));
}

void test_wide_and_compact_geometries() {
  for (const double text_scale : {0.75, 1.0, 1.5, 2.0}) {
    verify_selected_layout(
        {1016.4, 376.256, 319.6, 203.504},
        SelectedPartyDetailsLayoutDensity::wide,
        text_scale);
    verify_selected_layout(
        {758.24, 659.76, 229.76, 68.24},
        SelectedPartyDetailsLayoutDensity::compact,
        text_scale);
  }

  const auto details = selected_details();
  const auto wide = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 280.0, 160.0},
      .details = details,
      .typography = typography(0.75),
      .density = SelectedPartyDetailsLayoutDensity::wide,
  });
  CHECK(wide.summary_text.find("Level 12") != std::string::npos);
  CHECK(wide.stamina.label_text.find("STAMINA") != std::string::npos);
  CHECK(wide.spell_points.label_text.find("SPELL POINTS") !=
      std::string::npos);

  const auto compact = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 200.0, 64.0},
      .details = details,
      .typography = typography(0.75),
      .density = SelectedPartyDetailsLayoutDensity::compact,
  });
  CHECK(compact.summary_text == "L12  A-4  M3/12");
  CHECK(compact.stamina.label_text == "ST [X]");
  CHECK(compact.spell_points.label_text == "SP [X]");
}

void test_utf8_name_elision_stays_well_formed() {
  auto details = selected_details();
  const std::string phrase =
      "\xC3\x89" "owyn \xE2\x80\x94 shield-bearer of Rohan ";
  details.name.clear();
  for (size_t repeat = 0; repeat < 20U; ++repeat) {
    details.name += phrase;
  }
  CHECK(valid_utf8(details.name));

  for (const auto [panel, density] : std::array{
           std::pair{
               LogicalRect{0.0, 0.0, 280.0, 160.0},
               SelectedPartyDetailsLayoutDensity::wide},
           std::pair{
               LogicalRect{0.0, 0.0, 200.0, 64.0},
               SelectedPartyDetailsLayoutDensity::compact},
       }) {
    const auto layout = compute_selected_party_details_layout({
        .details_panel = panel,
        .details = details,
        .typography = typography(2.0),
        .density = density,
    });
    CHECK(layout.name_text.size() < details.name.size());
    CHECK(layout.name_text.ends_with("..."));
    CHECK(valid_utf8(layout.name_text));
    verify_style(layout.name_text_style, layout.name_bounds);
  }
}

void test_zero_through_forty_conditions_keep_exact_hidden_counts() {
  constexpr std::array cases{
      std::pair{
          LogicalRect{0.0, 0.0, 280.0, 160.0},
          SelectedPartyDetailsLayoutDensity::wide},
      std::pair{
          LogicalRect{0.0, 0.0, 200.0, 64.0},
          SelectedPartyDetailsLayoutDensity::compact},
  };
  for (const auto [panel, density] : cases) {
    for (size_t condition_count = 0; condition_count <= 40U;
         ++condition_count) {
      auto details = selected_details();
      details.states.resize(1U);
      for (size_t index = 0; index < condition_count; ++index) {
        details.states.emplace_back(state(
            "status.condition." + std::to_string(index),
            "Condition " + std::to_string(index),
            StateEmphasis::caution,
            StateMarker::condition));
      }
      const auto before = details;
      const auto layout = compute_selected_party_details_layout({
          .details_panel = panel,
          .details = details,
          .typography = typography(2.0),
          .density = density,
      });
      CHECK(details == before);
      CHECK(layout.state_tokens.size() == condition_count + 1U);
      CHECK(layout.visible_state_count + layout.hidden_state_count ==
          condition_count + 1U);
      CHECK(layout.state_tokens.front().identifier ==
          "status.unconscious");
      for (size_t index = 0; index < condition_count; ++index) {
        const size_t token_index = index + 1U;
        CHECK(layout.state_tokens[token_index].identifier ==
            details.states[token_index].identifier);
        CHECK(layout.state_tokens[token_index].label ==
            details.states[token_index].label);
      }
      if (layout.hidden_state_count > 0U) {
        const std::string exact_suffix =
            "+" + std::to_string(layout.hidden_state_count);
        CHECK(layout.state_text.ends_with(exact_suffix));
      } else if (condition_count == 0U) {
        CHECK(layout.state_text == "[X] Unconscious");
      }
      verify_style(layout.state_text_style, layout.state_bounds);
    }
  }
}

void test_state_elision_is_explicit_and_tokens_stay_complete() {
  auto details = selected_details();
  details.states = {
      state("status.unconscious", "Unconscious", StateEmphasis::critical,
          StateMarker::stop),
      state("information", "Known", StateEmphasis::information,
          StateMarker::information),
      state("check", "Ready", StateEmphasis::positive,
          StateMarker::check),
      state("alert", "Low", StateEmphasis::caution,
          StateMarker::alert),
      state("stop", "Critical wound", StateEmphasis::critical,
          StateMarker::stop),
      state("unavailable", "Unavailable", StateEmphasis::inactive,
          StateMarker::unavailable),
      state("condition", "Poisoned", StateEmphasis::caution,
          StateMarker::condition),
      state("neutral", "Neutral", StateEmphasis::neutral,
          StateMarker::none),
  };
  const auto before = details;
  const auto layout = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 200.0, 64.0},
      .details = details,
      .typography = typography(),
      .density = SelectedPartyDetailsLayoutDensity::compact,
  });

  CHECK(details == before);
  CHECK(layout.state_tokens.size() == details.states.size());
  CHECK(layout.visible_state_count > 0U);
  CHECK(layout.hidden_state_count > 0U);
  CHECK(layout.visible_state_count + layout.hidden_state_count ==
      details.states.size());
  CHECK(layout.state_text.find(
      "+" + std::to_string(layout.hidden_state_count)) !=
      std::string::npos);
  CHECK(layout.state_text.starts_with("[X] Unconscious"));
  constexpr std::array<std::string_view, 8> markers{
      "[X]", "[i]", "[+]", "[!]", "[X]", "[-]", "[*]", ""};
  for (size_t index = 0; index < markers.size(); ++index) {
    CHECK(layout.state_tokens[index].marker_text == markers[index]);
    if (!markers[index].empty()) {
      CHECK(layout.state_tokens[index].render_text.starts_with(markers[index]));
    }
  }
  verify_style(layout.state_text_style, layout.state_bounds);
}

void test_empty_selection_and_state_fallbacks() {
  const SelectedPartyDetailsModel empty;
  for (const auto [panel, density] : std::array{
           std::pair{
               LogicalRect{10.0, 20.0, 280.0, 160.0},
               SelectedPartyDetailsLayoutDensity::wide},
           std::pair{
               LogicalRect{10.0, 20.0, 200.0, 64.0},
               SelectedPartyDetailsLayoutDensity::compact},
       }) {
    const auto layout = compute_selected_party_details_layout({
        .details_panel = panel,
        .details = empty,
        .typography = typography(0.75),
        .density = density,
    });
    CHECK(!layout.has_selection);
    CHECK(layout.heading_text == "DETAILS");
    CHECK(layout.empty_message_text == "Select a party member.");
    CHECK(panel.contains(layout.heading_bounds));
    CHECK(panel.contains(layout.empty_message_bounds));
    CHECK(!interiors_overlap(
        layout.heading_bounds, layout.empty_message_bounds));
    CHECK(layout.state_tokens.empty());
    CHECK(layout.visible_state_count == 0U);
    CHECK(layout.hidden_state_count == 0U);
    verify_style(layout.heading_text_style, layout.heading_bounds);
    verify_style(
        layout.empty_message_text_style, layout.empty_message_bounds);
  }

  auto no_tokens = selected_details();
  no_tokens.states.clear();
  no_tokens.conscious = false;
  const auto unconscious = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 280.0, 160.0},
      .details = no_tokens,
      .typography = typography(),
      .density = SelectedPartyDetailsLayoutDensity::wide,
  });
  CHECK(unconscious.state_text == "[X] Unconscious");
  CHECK(unconscious.state_tokens.empty());

  no_tokens.conscious = true;
  const auto ready = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 280.0, 160.0},
      .details = no_tokens,
      .typography = typography(),
      .density = SelectedPartyDetailsLayoutDensity::wide,
  });
  CHECK(ready.state_text == "[+] Ready");
  CHECK(ready.state_tokens.empty());
}

void test_text_floor_and_meter_clamping() {
  auto details = selected_details();
  details.name = std::string(400U, 'A');
  details.stamina.current = 200;
  details.stamina.maximum = 20;
  details.spell_points.current = -5;
  details.spell_points.maximum = 9;
  const auto layout = compute_selected_party_details_layout({
      .details_panel = {0.0, 0.0, 200.0, 64.0},
      .details = details,
      .typography = typography(2.0),
      .density = SelectedPartyDetailsLayoutDensity::compact,
  });
  CHECK(layout.name_text.size() < details.name.size());
  CHECK(layout.name_text.ends_with("..."));
  CHECK(layout.stamina.fill_fraction == 1.0);
  CHECK(layout.spell_points.fill_fraction == 0.0);
  verify_style(layout.name_text_style, layout.name_bounds);
  verify_style(layout.summary_text_style, layout.summary_bounds);
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

void test_invalid_requests_fail_without_partial_layouts() {
  const auto details = selected_details();
  const auto type = typography();
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 279.0, 160.0},
        .details = details,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 200.0, 63.0},
        .details = details,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::compact,
    }));
  });
  auto bad_type = type;
  bad_type.caption.point_size = 7.99;
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = details,
        .typography = bad_type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {
            0.0,
            0.0,
            std::numeric_limits<double>::quiet_NaN(),
            160.0,
        },
        .details = details,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = details,
        .typography = type,
        .density = static_cast<SelectedPartyDetailsLayoutDensity>(99),
    }));
  });
  check_invalid_argument([&] {
    auto missing_cue = details;
    missing_cue.states = {
        state("status.condition.1", "Cursed", StateEmphasis::caution,
            StateMarker::condition),
    };
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = missing_cue,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    auto contradictory = details;
    contradictory.conscious = true;
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = contradictory,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    auto empty_label = details;
    empty_label.states.front().label.clear();
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = empty_label,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    auto wrong_marker = details;
    wrong_marker.states.front().marker = StateMarker::alert;
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = wrong_marker,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    auto duplicate_cue = details;
    duplicate_cue.states.emplace_back(duplicate_cue.states.front());
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {0.0, 0.0, 280.0, 160.0},
        .details = duplicate_cue,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });
  check_invalid_argument([&] {
    static_cast<void>(compute_selected_party_details_layout({
        .details_panel = {
            std::numeric_limits<double>::max(),
            0.0,
            std::numeric_limits<double>::max(),
            160.0,
        },
        .details = details,
        .typography = type,
        .density = SelectedPartyDetailsLayoutDensity::wide,
    }));
  });

  const auto huge_finite_width = compute_selected_party_details_layout({
      .details_panel = {
          0.0,
          0.0,
          std::numeric_limits<double>::max(),
          160.0,
      },
      .details = details,
      .typography = type,
      .density = SelectedPartyDetailsLayoutDensity::wide,
  });
  CHECK(huge_finite_width.has_selection);
  CHECK(std::isfinite(huge_finite_width.panel_bounds.right()));
}

} // namespace

int main() {
  try {
    test_wide_and_compact_geometries();
    test_utf8_name_elision_stays_well_formed();
    test_zero_through_forty_conditions_keep_exact_hidden_counts();
    test_state_elision_is_explicit_and_tokens_stay_complete();
    test_empty_selection_and_state_fallbacks();
    test_text_floor_and_meter_clamping();
    test_invalid_requests_fail_without_partial_layouts();
    std::cout << "SelectedPartyDetailsLayoutTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SelectedPartyDetailsLayoutTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
