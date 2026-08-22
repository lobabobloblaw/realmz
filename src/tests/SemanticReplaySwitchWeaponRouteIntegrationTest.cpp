#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "EventManager.h"
extern "C" {
#include "realmz_orig/structs.h"
}
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

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

extern "C" {
extern WindowPtr gWindow;
extern WindowPtr look;
extern short incombat;
extern short inspell;
extern short monsterturn;
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
extern char charnum;
extern char charselectnew;
extern char charup;
extern char up;
extern char q[110];
extern struct character c[6];
}

// EventManager links the party-selection adapter even though this executable
// delivers only a weapon switch. Keep its unused refresh dependency headless.
extern "C" void updatecontrols(void) {}

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
      (fs::temp_directory_path() / "Realmz Replay Switch Route Root")
          .lexically_normal();
  const fs::path result =
      (fs::temp_directory_path() / "realmz-switch-route-result.json")
          .lexically_normal();
  const std::string json =
      "{"
      "\"schema_version\":3,"
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
      "\"actions\":[{\"ordinal\":0,\"kind\":\"switch_weapon_set\","
      "\"arguments\":{\"combatant\":0}}],"
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}";
  return parse_child_config_v3(json);
}

void prepare_active_combat_window() {
  CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
  CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
  InitGraf(&qd);

  auto& windows = WindowManager::instance();
  windows.create_sdl_window();
  const Rect bounds{.top = 0, .left = 0, .bottom = 600, .right = 800};
  const RGBColor background{0, 0, 0};
  look = windows.create_window(
      "Replay weapon-switch route test",
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

  incombat = 1;
  monsterturn = 0;
  nummon = 0;
  inspell = 0;
  initems = inswap = inbooty = inshop = intemple = indung = 0;
  encountflag = 0;
  head = 1;
  viewtype = 1;
  charnum = 0;
  charselectnew = 0;
  charup = 0;
  up = 0;
  q[0] = 0;
  c[0].stamina = 10;
  c[0].staminamax = 10;
  c[0].inbattle = 1;
  c[0].toggle = 0;
  c[0].armor[15] = 1;
}

void run_route(std::string_view route) {
  ReplayRuntime& runtime = install_replay_runtime(make_config(route));
  auto actions = decode_replay_actions_v3(runtime.config().actions());
  CHECK(actions.size() == 1U);
  CHECK(actions[0].sequence == 1U);
  const auto* switch_weapon =
      std::get_if<SwitchWeaponSetAction>(&actions[0].payload);
  CHECK(switch_weapon != nullptr);
  CHECK(switch_weapon->combatant == 0);
  runtime.start_action_plan(std::move(actions));
  prepare_active_combat_window();

  EventRecord event{};
  CHECK(GetNextSemanticGameplayEvent(
      everyEvent, &event, REALMZ_SEMANTIC_INPUT_COMBAT));
  CHECK(event.what == keyDown);
  CHECK(event.message == kReplaySwitchWeaponKeyMessage);
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
    std::cerr << "usage: SemanticReplaySwitchWeaponRouteIntegrationTest "
                 "classic|semantic\n";
    return 2;
  }
  try {
    run_route(argv[1]);
    std::cout << "SemanticReplaySwitchWeaponRouteIntegrationTest "
              << argv[1] << " passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplaySwitchWeaponRouteIntegrationTest failed "
              << "after " << checks_run << " checks: " << error.what()
              << '\n';
    return 1;
  }
}
