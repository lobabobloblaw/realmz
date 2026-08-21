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
#include "replay/ReplayRuntime.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

[[nodiscard]] ReplayChildConfig make_config(std::string_view route) {
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
      "\"actions\":[{\"ordinal\":0,\"kind\":\"move_party\","
      "\"arguments\":{\"command\":\"north\"}}],"
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}");
}

void prepare_gameplay_window() {
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
}

void run_route(std::string_view route) {
  ReplayRuntime& runtime = install_replay_runtime(make_config(route));
  runtime.start_action_plan({UIAction{
      .sequence = 1,
      .payload = MovePartyAction{MovementCommand::north},
  }});
  prepare_gameplay_window();

  EventRecord event{};
  CHECK(GetNextSemanticGameplayEvent(
      everyEvent, &event, REALMZ_SEMANTIC_INPUT_EXPLORATION));
  CHECK(event.what == keyDown);
  CHECK(event.message == 0x00007E1EU);
  CHECK(event.when == 0U);
  CHECK(event.where.h == 0);
  CHECK(event.where.v == 0);
  CHECK(event.modifiers == 0U);
  CHECK(event.window_port == nullptr);
  CHECK(runtime.planned_action_count() == 1U);
  CHECK(runtime.settled_action_count() == 0U);
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2 ||
      (std::string_view(argv[1]) != "classic" &&
          std::string_view(argv[1]) != "semantic")) {
    std::cerr << "usage: SemanticReplayRouteIntegrationTest "
                 "classic|semantic\n";
    return 2;
  }
  try {
    run_route(argv[1]);
    std::cout << "SemanticReplayRouteIntegrationTest " << argv[1]
              << " passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayRouteIntegrationTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
