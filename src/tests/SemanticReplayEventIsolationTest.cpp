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

#include "presentation/UIAction.hpp"
#include "replay/ReplayRuntime.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
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
  static constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20U) {
          result += "\\u00";
          result.push_back(hex[character >> 4U]);
          result.push_back(hex[character & 0x0FU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
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

[[nodiscard]] ReplayChildConfig make_config() {
  const fs::path root =
      (fs::temp_directory_path() / "Realmz Replay Event Root")
          .lexically_normal();
  const fs::path result =
      (fs::temp_directory_path() / "realmz-event-result.json")
          .lexically_normal();
  const std::string json =
      "{"
      "\"schema_version\":1,"
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":\"semantic\","
      "\"presentation_mode\":\"remastered\","
      "\"user_data_root\":" + json_string(path_utf8(root)) + ","
      "\"user_data_root_policy\":\"set_once_before_toolbox_init\","
      "\"preferences_write_policy\":\"disabled\","
      "\"input_slot\":\"A\","
      "\"input_slot_policy\":"
      "\"user_data_root_only_no_bundled_fallback\","
      "\"output_slot\":\"B\","
      "\"output_slot_policy\":\"fresh_nonexistent\","
      "\"actions\":[{\"ordinal\":0,\"kind\":\"center\","
      "\"arguments\":{}}],"
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}";
  return parse_child_config_v1(json);
}

class SdlEvents final {
public:
  SdlEvents() {
    if (!SDL_Init(SDL_INIT_EVENTS)) {
      throw std::runtime_error(
          std::string("SDL event initialization failed: ") + SDL_GetError());
    }
  }

  ~SdlEvents() {
    SDL_Quit();
  }
};

void push_user_event(std::uint32_t type, std::int32_t code) {
  SDL_Event event{};
  event.type = type;
  event.user.code = code;
  CHECK(SDL_PushEvent(&event));
}

void require_user_event_still_pending(
    std::uint32_t type,
    std::int32_t code) {
  SDL_Event observed{};
  CHECK(SDL_PeepEvents(
            &observed, 1, SDL_GETEVENT, type, type) == 1);
  CHECK(observed.type == type);
  CHECK(observed.user.code == code);
}

void require_replay_null_event(
    const EventRecord& event,
    std::uint32_t expected_tick) {
  CHECK(event.what == nullEvent);
  CHECK(event.message == 0U);
  CHECK(event.when == expected_tick);
  CHECK(event.where.h == 0);
  CHECK(event.where.v == 0);
  CHECK(event.modifiers == 0U);
  CHECK(event.window_port == nullptr);
  CHECK(std::all_of(
      std::begin(event.text), std::end(event.text),
      [](char value) { return value == '\0'; }));
}

void test_replay_event_isolation() {
  SdlEvents sdl;
  const std::uint32_t user_event_type = SDL_RegisterEvents(1);
  // SDL3 documents zero as the registration-failure sentinel.
  CHECK(user_event_type != 0U);
  CHECK(user_event_type >= SDL_EVENT_USER);
  CHECK(user_event_type <= SDL_EVENT_LAST);

  PushMenuEvent(2, 3);
  EventRecord ordinary{};
  CHECK(GetNextEvent(everyEvent, &ordinary));
  CHECK(ordinary.what == mouseDown);
  CHECK(ordinary.where.v == -2);
  CHECK(ordinary.where.h == -3);

  // This ordinary event predates replay publication. Raw replay polling must
  // ignore it just as it ignores later native and SDL input.
  PushMenuEvent(4, 7);
  ReplayRuntime& replay = install_replay_runtime(make_config());
  CHECK(installed_replay_runtime() == &replay);

  const std::uint32_t first_tick = TickCount();
  const std::uint32_t second_tick = TickCount();
  CHECK(first_tick == 0U);
  CHECK(second_tick == 1U);

  push_user_event(user_event_type, 101);
  EventRecord event{};
  CHECK(!GetNextEvent(everyEvent, &event));
  require_replay_null_event(event, 2U);
  require_user_event_still_pending(user_event_type, 101);

  push_user_event(user_event_type, 202);
  FlushEvents(everyEvent, 0);
  require_user_event_still_pending(user_event_type, 202);

  push_user_event(user_event_type, 303);
  event = {};
  CHECK(!WaitNextEvent(everyEvent, &event, 5000, nullptr));
  require_replay_null_event(event, 3U);
  require_user_event_still_pending(user_event_type, 303);

  Point requested{.v = 123, .h = 456};
  SetMouseLocation(&requested);
  Point observed{.v = -1, .h = -1};
  GetMouseGlobal(&observed);
  CHECK(observed.h == 0);
  CHECK(observed.v == 0);
  observed = {.v = -1, .h = -1};
  GetMouse(&observed);
  CHECK(observed.h == 0);
  CHECK(observed.v == 0);
  CHECK(!Button());
  CHECK(!StillDown());

  // The replay branch is deliberately a no-op and must be safe without an
  // event pump or host-scheduler delay.
  SystemTask();
  CHECK(TickCount() == 4U);

  // A tagged command is invisible to raw/modal polling and survives both a
  // raw poll and FlushEvents. The guarded semantic poll observes its original
  // enqueue tick even if late gameplay-context validation rejects the command.
  const std::uint32_t movement =
      realmz::presentation::semantic_movement_tag(
          realmz::presentation::MovementCommand::north,
          REALMZ_SEMANTIC_INPUT_EXPLORATION);
  CHECK(movement != 0U);
  CHECK(PushSemanticMovementEvent(movement));
  event = {};
  CHECK(!GetNextEvent(everyEvent, &event));
  require_replay_null_event(event, 6U);
  FlushEvents(everyEvent, 0);

  event = {};
  static_cast<void>(GetNextSemanticGameplayEvent(
      everyEvent, &event, REALMZ_SEMANTIC_INPUT_EXPLORATION));
  CHECK(event.when == 5U);
  CHECK(event.modifiers == 0U);

  event = {};
  CHECK(!GetNextSemanticGameplayEvent(
      everyEvent, &event, REALMZ_SEMANTIC_INPUT_EXPLORATION));
  CHECK(event.what == nullEvent);
  CHECK(event.when == 7U);
  CHECK(event.modifiers == 0U);
}

} // namespace

int main() {
  try {
    test_replay_event_isolation();
    std::cout << "SemanticReplayEventIsolationTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayEventIsolationTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
