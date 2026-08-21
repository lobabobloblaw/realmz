#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "EventManager.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "QuickDraw.h"
#include "WindowManager.hpp"
#include "replay/ReplayActionDecoder.hpp"
#include "replay/ReplayRuntime.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
extern WindowPtr gWindow;
extern WindowPtr look;
extern short incombat;
extern short inspell;
extern short nummon;
extern char head;
extern char encountflag;
extern char viewtype;
extern Boolean initems;
extern Boolean inswap;
extern Boolean inbooty;
extern Boolean inshop;
extern Boolean intemple;
extern Boolean indung;
}

namespace fs = std::filesystem;
using namespace realmz::presentation;
using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

struct ExpectedMovement final {
  std::string_view command;
  MovementCommand movement;
  std::uint32_t classic_message;
};

constexpr std::array kOutdoorMovements{
    ExpectedMovement{"north", MovementCommand::north, 0x00007E1EU},
    ExpectedMovement{"northeast", MovementCommand::northeast, 0x00005C39U},
    ExpectedMovement{"east", MovementCommand::east, 0x00007C1DU},
    ExpectedMovement{"southeast", MovementCommand::southeast, 0x00005533U},
    ExpectedMovement{"south", MovementCommand::south, 0x00007D1FU},
    ExpectedMovement{"southwest", MovementCommand::southwest, 0x00005331U},
    ExpectedMovement{"west", MovementCommand::west, 0x00007B1CU},
    ExpectedMovement{"northwest", MovementCommand::northwest, 0x00005937U},
};

constexpr std::array kDungeonMovements{
    ExpectedMovement{
        "step_forward", MovementCommand::step_forward, 0x00007E1EU},
    ExpectedMovement{
        "step_backward", MovementCommand::step_backward, 0x00007D1FU},
    ExpectedMovement{"turn_left", MovementCommand::turn_left, 0x00007B1CU},
    ExpectedMovement{"turn_right", MovementCommand::turn_right, 0x00007C1DU},
};

[[nodiscard]] std::span<const ExpectedMovement> movements_for_surface(
    std::string_view surface) {
  if (surface == "outdoor") {
    return kOutdoorMovements;
  }
  if (surface == "dungeon") {
    return kDungeonMovements;
  }
  throw std::invalid_argument("unknown replay test surface");
}

[[nodiscard]] RealmzSemanticInputSurface semantic_surface_for(
    std::string_view surface) {
  if (surface == "outdoor") {
    return REALMZ_SEMANTIC_INPUT_EXPLORATION;
  }
  if (surface == "dungeon") {
    return REALMZ_SEMANTIC_INPUT_DUNGEON;
  }
  throw std::invalid_argument("unknown replay test surface");
}

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string result = "\"";
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] std::string path_utf8(const fs::path& path) {
  const std::u8string encoded = path.u8string();
  std::string result;
  result.reserve(encoded.size());
  for (const char8_t byte : encoded) {
    result.push_back(static_cast<char>(byte));
  }
  return result;
}

[[nodiscard]] std::string actions_json(
    std::span<const ExpectedMovement> movements) {
  std::string result = "[";
  for (std::size_t index = 0; index < movements.size(); ++index) {
    if (index != 0U) {
      result.push_back(',');
    }
    result +=
        "{\"ordinal\":" + std::to_string(index) +
        ",\"kind\":\"move_party\",\"arguments\":{\"command\":" +
        json_string(movements[index].command) + "}}";
  }
  result.push_back(']');
  return result;
}

[[nodiscard]] ReplayChildConfig make_config(
    std::string_view route,
    std::span<const ExpectedMovement> movements) {
  const bool semantic = route == "semantic";
  const fs::path root =
      (fs::temp_directory_path() / "Realmz Replay Route Root")
          .lexically_normal();
  const fs::path result =
      (fs::temp_directory_path() / "realmz-route-result.json")
          .lexically_normal();
  return parse_child_config_v1(
      "{"
      "\"schema_version\":1,"
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":" + json_string(route) + ","
      "\"presentation_mode\":" +
          json_string(semantic ? "remastered" : "classic") + ","
      "\"user_data_root\":" + json_string(path_utf8(root)) + ","
      "\"user_data_root_policy\":\"set_once_before_toolbox_init\","
      "\"preferences_write_policy\":\"disabled\","
      "\"input_slot\":\"A\","
      "\"input_slot_policy\":"
      "\"user_data_root_only_no_bundled_fallback\","
      "\"output_slot\":\"B\","
      "\"output_slot_policy\":\"fresh_nonexistent\","
      "\"actions\":" + actions_json(movements) + ","
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}");
}

void prepare_gameplay_window(RealmzSemanticInputSurface surface) {
  CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
  CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
  InitGraf(&qd);

  auto& windows = WindowManager::instance();
  windows.create_sdl_window();
  const Rect bounds{.top = 0, .left = 0, .bottom = 600, .right = 800};
  const RGBColor background{0, 0, 0};
  look = windows.create_window(
      "Replay route test",
      bounds,
      true,
      false,
      0,
      0,
      false,
      background,
      {});
  gWindow = look;
  BringToFront(look);

  incombat = 0;
  inspell = 0;
  nummon = 0;
  head = 1;
  encountflag = 0;
  viewtype = 1;
  initems = inswap = inbooty = inshop = intemple = indung = 0;
  indung = surface == REALMZ_SEMANTIC_INPUT_DUNGEON;
}

void run_route(std::string_view route, std::string_view surface_name) {
  const auto movements = movements_for_surface(surface_name);
  const auto surface = semantic_surface_for(surface_name);
  ReplayRuntime& runtime =
      install_replay_runtime(make_config(route, movements));
  auto actions = decode_replay_actions_v1(runtime.config().actions());
  CHECK(actions.size() == movements.size());
  for (std::size_t index = 0; index < actions.size(); ++index) {
    CHECK(actions[index].sequence == index + 1U);
    const auto* movement = std::get_if<MovePartyAction>(
        &actions[index].payload);
    CHECK(movement != nullptr);
    CHECK(movement->command == movements[index].movement);
  }
  runtime.start_action_plan(std::move(actions));
  prepare_gameplay_window(surface);

  CHECK(runtime.planned_action_count() == movements.size());
  CHECK(runtime.settled_action_count() == 0U);
  for (std::size_t index = 0; index < movements.size(); ++index) {
    EventRecord event{};
    CHECK(GetNextSemanticGameplayEvent(everyEvent, &event, surface));
    CHECK(event.what == keyDown);
    CHECK(event.message == movements[index].classic_message);
    CHECK(event.when == index);
    CHECK(event.where.h == 0);
    CHECK(event.where.v == 0);
    CHECK(event.modifiers == 0U);
    CHECK(event.window_port == nullptr);

    // The following top-level poll settles the previously delivered action.
    // Deliberately stop after the final delivery: one more poll would invoke
    // production child completion, which is outside this delivery-only test.
    CHECK(runtime.settled_action_count() == index);
  }
  CHECK(runtime.settled_action_count() + 1U ==
      runtime.planned_action_count());
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3 ||
      (std::string_view(argv[1]) != "classic" &&
          std::string_view(argv[1]) != "semantic") ||
      (std::string_view(argv[2]) != "outdoor" &&
          std::string_view(argv[2]) != "dungeon")) {
    std::cerr << "usage: SemanticReplayRouteIntegrationTest "
                 "classic|semantic outdoor|dungeon\n";
    return 2;
  }
  try {
    run_route(argv[1], argv[2]);
    std::cout << "SemanticReplayRouteIntegrationTest " << argv[1] << ' '
              << argv[2]
              << " passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayRouteIntegrationTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
