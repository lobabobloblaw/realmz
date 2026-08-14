#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

#include "presentation/AdaptiveShell.hpp"
#include "presentation/RemasteredInputMapper.hpp"

using namespace realmz::presentation;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " +
        expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

[[nodiscard]] bool approximately_equal(double lhs, double rhs) {
  return std::abs(lhs - rhs) <=
      1e-9 * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

constexpr ShellRegionId kFallback{1};
constexpr ShellRegionId kParty{2};
constexpr ShellRegionId kActions{3};
constexpr ShellRegionId kOverlay{4};

RemasteredInputMapper make_mapper(double backing_scale = 1.0) {
  const std::array regions{
      ShellHitRegion{kOverlay, {1000.0, 100.0, 100.0, 100.0}},
      ShellHitRegion{kParty, {1000.0, 0.0, 440.0, 600.0}},
      ShellHitRegion{kActions, {0.0, 720.0, 1000.0, 180.0}},
  };
  return RemasteredInputMapper(
      {0.0, 0.0, 1440.0, 900.0},
      ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
      regions,
      kFallback,
      backing_scale);
}

void test_classic_frame_is_the_only_legacy_target() {
  const auto mapper = make_mapper();

  const auto origin = mapper.map_window_point({20.0, 0.0});
  CHECK(std::holds_alternative<LegacyPointerTarget>(origin));
  CHECK(std::get<LegacyPointerTarget>(origin).classic_point ==
      (LogicalPoint{0.0, 0.0}));

  const auto center = mapper.map_window_point({500.0, 360.0});
  CHECK(std::holds_alternative<LegacyPointerTarget>(center));
  const auto center_point =
      std::get<LegacyPointerTarget>(center).classic_point;
  CHECK(approximately_equal(center_point.x, 400.0));
  CHECK(approximately_equal(center_point.y, 300.0));

  const auto almost_edge = mapper.map_window_point({
      std::nextafter(980.0, 20.0),
      std::nextafter(720.0, 0.0),
  });
  CHECK(std::holds_alternative<LegacyPointerTarget>(almost_edge));
  const auto almost_edge_point =
      std::get<LegacyPointerTarget>(almost_edge).classic_point;
  CHECK(almost_edge_point.x < 800.0);
  CHECK(almost_edge_point.y < 600.0);

  // Half-open far edges belong to the shell, never to the legacy engine.
  const auto right_edge = mapper.map_window_point({980.0, 100.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(right_edge));
  CHECK(std::get<ShellPointerTarget>(right_edge).region == kFallback);
  const auto bottom_edge = mapper.map_window_point({500.0, 720.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(bottom_edge));
  CHECK(std::get<ShellPointerTarget>(bottom_edge).region == kActions);

  // Even an overlapping shell region cannot claim the designated frame.
  const std::array overlap{
      ShellHitRegion{kOverlay, {0.0, 0.0, 1440.0, 900.0}},
  };
  const RemasteredInputMapper overlap_mapper(
      {0.0, 0.0, 1440.0, 900.0},
      ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
      overlap,
      kFallback);
  CHECK(std::holds_alternative<LegacyPointerTarget>(
      overlap_mapper.map_window_point({500.0, 360.0})));
}

void test_shell_regions_are_semantic_and_deterministic() {
  const auto mapper = make_mapper();

  const auto overlay = mapper.map_window_point({1050.0, 150.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(overlay));
  const auto& overlay_target = std::get<ShellPointerTarget>(overlay);
  CHECK(overlay_target.region == kOverlay);
  CHECK(overlay_target.local_point == (LogicalPoint{50.0, 50.0}));

  // Regions are front-to-back: the overlay wins over the party rail.
  const auto party = mapper.map_window_point({1100.0, 200.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(party));
  CHECK(std::get<ShellPointerTarget>(party).region == kParty);

  // At the overlay's half-open edge, the underlying party rail wins.
  const auto shared_edge = mapper.map_window_point({1100.0, 100.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(shared_edge));
  CHECK(std::get<ShellPointerTarget>(shared_edge).region == kParty);

  const auto gap = mapper.map_window_point({990.0, 700.0});
  CHECK(std::holds_alternative<ShellPointerTarget>(gap));
  CHECK(std::get<ShellPointerTarget>(gap).region == kFallback);

  CHECK(std::holds_alternative<OutsideWindowTarget>(
      mapper.map_window_point({1440.0, 10.0})));
  CHECK(std::holds_alternative<OutsideWindowTarget>(
      mapper.map_window_point({10.0, 900.0})));
  CHECK(std::holds_alternative<OutsideWindowTarget>(
      mapper.map_window_point({-0.001, 10.0})));
  CHECK(std::holds_alternative<OutsideWindowTarget>(
      mapper.map_window_point({NAN, 10.0})));
}

void test_crop_and_reverse_native_anchors() {
  const RemasteredInputMapper mapper(
      {0.0, 0.0, 1024.0, 768.0},
      ClassicFramePlacement{
          .destination = {20.0, 30.0, 800.0, 600.0},
          .source_crop = {100.0, 50.0, 400.0, 300.0},
      },
      {},
      kFallback,
      2.0);

  const auto mapped = mapper.map_window_point({420.0, 330.0});
  CHECK(std::holds_alternative<LegacyPointerTarget>(mapped));
  CHECK(std::get<LegacyPointerTarget>(mapped).classic_point ==
      (LogicalPoint{300.0, 200.0}));

  const auto native_anchor = mapper.classic_to_window_point({300.0, 200.0});
  CHECK(native_anchor == (LogicalPoint{420.0, 330.0}));
  CHECK(!mapper.classic_to_window_point({99.999, 200.0}));
  CHECK(!mapper.classic_to_window_point({500.0, 200.0}));

  const auto native_text_area = mapper.classic_to_window_rect(
      {120.0, 60.0, 100.0, 20.0});
  CHECK(native_text_area ==
      (LogicalRect{60.0, 50.0, 200.0, 40.0}));
  CHECK(mapper.classic_rect_to_backing_pixels(
      {120.0, 60.0, 100.0, 20.0}) ==
      (PhysicalRect{120, 100, 400, 80}));
  CHECK(!mapper.classic_to_window_rect({490.0, 60.0, 20.0, 20.0}));

  // Rectangle edges may land exactly on the crop edge; hit-test points may not.
  CHECK(mapper.classic_to_window_rect({100.0, 50.0, 400.0, 300.0}) ==
      (LogicalRect{20.0, 30.0, 800.0, 600.0}));
}

void test_real_gameplay_crop_preserves_classic_desktop_offset() {
  const RemasteredInputMapper mapper(
      {0.0, 0.0, 1024.0, 768.0},
      ClassicFramePlacement{
          .destination = {24.0, 24.0, 720.0, 624.0},
          .source_crop = kClassicGameplayCrop,
      },
      {},
      kFallback,
      2.0);

  CHECK(kClassicGameplayCrop == (LogicalRect{0.0, 20.0, 480.0, 416.0}));
  const auto origin = mapper.map_window_point({24.0, 24.0});
  CHECK(std::holds_alternative<LegacyPointerTarget>(origin));
  CHECK(std::get<LegacyPointerTarget>(origin).classic_point ==
      (LogicalPoint{0.0, 20.0}));
  CHECK(mapper.classic_to_window_point({0.0, 20.0}) ==
      (LogicalPoint{24.0, 24.0}));
  CHECK(mapper.classic_to_window_point({240.0, 228.0}) ==
      (LogicalPoint{384.0, 336.0}));

  // The composited framebuffer and EventRecord use Classic desktop/global
  // coordinates. Y=0 is window-local lookrect space and must not be accepted
  // here; GlobalToLocal subtracts the 20-point WIND 131 origin later.
  CHECK(!mapper.classic_to_window_point({0.0, 0.0}));
  const auto far_corner = mapper.classic_to_window_point({479.999, 435.999});
  CHECK(far_corner.has_value());
  CHECK(std::get<LegacyPointerTarget>(
      mapper.map_window_point(*far_corner)).classic_point.y >= 435.998);
}

void test_backing_pixels_are_independent_of_window_points() {
  const auto one_x = make_mapper(1.0);
  const auto two_x = make_mapper(2.0);

  const auto one_target = one_x.map_window_point({500.0, 360.0});
  const auto two_target = two_x.map_backing_pixel({1000, 720});
  CHECK(one_target == two_target);
  CHECK(two_x.window_point_to_backing_pixel({500.0, 360.0}) ==
      (PhysicalPoint{1000, 720}));
  CHECK(two_x.window_rect_to_backing_pixels({20.0, 0.0, 960.0, 720.0}) ==
      (PhysicalRect{40, 0, 1920, 1440}));

  const auto fractional = make_mapper(1.5);
  const auto target = fractional.map_backing_pixel({750, 540});
  CHECK(target == one_target);
}

void test_pointer_capture_preserves_the_pressed_route() {
  const auto mapper = make_mapper();

  const auto legacy_capture = mapper.begin_pointer_capture({500.0, 360.0});
  CHECK(legacy_capture.has_value());
  CHECK(std::holds_alternative<LegacyPointerCapture>(*legacy_capture));

  // Uncaptured routing at this point belongs to shell chrome. A drag that
  // started over Classic must instead deliver an out-of-crop release to the
  // legacy loop so its pressed control can cancel and clear its state.
  CHECK(std::holds_alternative<ShellPointerTarget>(
      mapper.map_window_point({1000.0, 800.0})));
  const auto captured_legacy = mapper.map_captured_window_point(
      {1000.0, 800.0}, *legacy_capture);
  CHECK(std::holds_alternative<LegacyPointerTarget>(captured_legacy));
  const auto captured_classic_point =
      std::get<LegacyPointerTarget>(captured_legacy).classic_point;
  CHECK(captured_classic_point.x > 800.0);
  CHECK(captured_classic_point.y > 600.0);

  const auto captured_left = mapper.map_captured_window_point(
      {-10.0, 360.0}, *legacy_capture);
  CHECK(std::holds_alternative<LegacyPointerTarget>(captured_left));
  CHECK(std::get<LegacyPointerTarget>(captured_left).classic_point.x < 0.0);

  // A modal transition may install a differently placed full-frame mapper
  // between down and up. The capture token, not the current mapper, owns the
  // down-time transform.
  const RemasteredInputMapper modal_mapper(
      {0.0, 0.0, 1440.0, 900.0},
      ClassicFramePlacement{{120.0, 60.0, 800.0, 600.0}},
      {}, kFallback);
  const auto across_recomposite = modal_mapper.map_captured_window_point(
      {1000.0, 800.0}, *legacy_capture);
  CHECK(std::holds_alternative<LegacyPointerTarget>(across_recomposite));
  CHECK(std::get<LegacyPointerTarget>(across_recomposite).classic_point ==
      captured_classic_point);

  const auto shell_capture = mapper.begin_pointer_capture({1050.0, 150.0});
  CHECK(shell_capture.has_value());
  CHECK(std::holds_alternative<ShellPointerCapture>(*shell_capture));
  CHECK(std::get<ShellPointerCapture>(*shell_capture).region == kOverlay);
  const auto captured_shell = mapper.map_captured_window_point(
      {900.0, 500.0}, *shell_capture);
  CHECK(std::holds_alternative<ShellPointerTarget>(captured_shell));
  CHECK(std::get<ShellPointerTarget>(captured_shell).region == kOverlay);
  CHECK(std::get<ShellPointerTarget>(captured_shell).local_point ==
      (LogicalPoint{-100.0, 400.0}));

  const auto fallback_capture = mapper.begin_pointer_capture({990.0, 700.0});
  CHECK(fallback_capture.has_value());
  CHECK(std::holds_alternative<ShellPointerCapture>(*fallback_capture));
  CHECK(std::get<ShellPointerCapture>(*fallback_capture).region == kFallback);
  const auto fallback_release = mapper.map_captured_window_point(
      {1500.0, 950.0}, *fallback_capture);
  CHECK(std::holds_alternative<ShellPointerTarget>(fallback_release));
  CHECK(std::get<ShellPointerTarget>(fallback_release).region == kFallback);
  CHECK(std::get<ShellPointerTarget>(fallback_release).local_point ==
      (LogicalPoint{1500.0, 950.0}));

  CHECK(!mapper.begin_pointer_capture({1440.0, 100.0}));
  CHECK(std::holds_alternative<OutsideWindowTarget>(
      mapper.map_captured_window_point({NAN, 10.0}, *legacy_capture)));
}

template <typename Callable>
void check_invalid(Callable&& callable) {
  bool rejected = false;
  try {
    callable();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);
}

void test_configuration_validation() {
  check_invalid([] {
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{{20.0, 0.0, 960.0, 600.0}},
        {}, kFallback);
  });
  check_invalid([] {
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{
            .destination = {20.0, 0.0, 960.0, 720.0},
            .source_crop = {1.0, 0.0, 800.0, 600.0},
        },
        {}, kFallback);
  });
  check_invalid([] {
    const std::array regions{
        ShellHitRegion{{0}, {1000.0, 0.0, 400.0, 600.0}},
    };
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
        regions, kFallback);
  });
  check_invalid([] {
    const std::array regions{
        ShellHitRegion{kParty, {1400.0, 0.0, 100.0, 100.0}},
    };
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
        regions, kFallback);
  });
  check_invalid([] {
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
        {}, ShellRegionId{0});
  });
  check_invalid([] {
    (void)RemasteredInputMapper(
        {0.0, 0.0, 1440.0, 900.0},
        ClassicFramePlacement{{20.0, 0.0, 960.0, 720.0}},
        {}, kFallback, 0.0);
  });
}

} // namespace

int main() {
  try {
    test_classic_frame_is_the_only_legacy_target();
    test_shell_regions_are_semantic_and_deterministic();
    test_crop_and_reverse_native_anchors();
    test_real_gameplay_crop_preserves_classic_desktop_offset();
    test_backing_pixels_are_independent_of_window_points();
    test_pointer_capture_preserves_the_pressed_route();
    test_configuration_validation();
    std::cout << "Remastered input mapper tests passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
