#include "replay/ReplayRuntime.hpp"

#include <filesystem>
#include <iostream>
#include <array>
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

template <typename Function>
void check_invalid_argument(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("expected std::invalid_argument");
}

template <typename Function>
void check_logic_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const std::logic_error&) {
    return;
  }
  throw std::runtime_error("expected std::logic_error");
}

template <typename Function>
void check_runtime_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayRuntimeError&) {
    return;
  }
  throw std::runtime_error("expected ReplayRuntimeError");
}

[[nodiscard]] realmz::presentation::UIAction movement(
    realmz::presentation::ActionSequence sequence,
    realmz::presentation::MovementCommand command) {
  return {
      .sequence = sequence,
      .payload = realmz::presentation::MovePartyAction{command},
  };
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

[[nodiscard]] ReplayChildConfig make_config(
    std::string_view route = "semantic",
    std::string_view presentation = "remastered") {
  const fs::path root =
      (fs::temp_directory_path() / "Realmz Replay Runtime Root")
          .lexically_normal();
  const fs::path result =
      (fs::temp_directory_path() / "realmz-runtime-result.json")
          .lexically_normal();
  const std::string json =
      "{"
      "\"schema_version\":1,"
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":" + json_string(route) + ","
      "\"presentation_mode\":" + json_string(presentation) + ","
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

void test_immutable_policy_access() {
  ReplayRuntime runtime(make_config());
  CHECK(runtime.replay_route() == ReplayRoute::semantic);
  CHECK(runtime.presentation_mode() == ReplayPresentationMode::remastered);
  CHECK(runtime.user_data_root().is_absolute());
  CHECK(runtime.input_slot() == 'A');
  CHECK(runtime.output_slot() == 'B');
  CHECK(!runtime.preferences_writes_enabled());
  CHECK(runtime.result_path().is_absolute());
  CHECK(runtime.config().actions().size() == 1);
  CHECK(runtime.config().actions()[0].kind == "center");

  ReplayRuntime classic(make_config("classic", "classic"));
  CHECK(classic.replay_route() == ReplayRoute::classic);
  CHECK(classic.presentation_mode() == ReplayPresentationMode::classic);
}

void test_narrow_user_only_input_read_policy() {
  ReplayRuntime runtime(make_config());
  for (const fs::path& path : {
           fs::path("Save") / "Game A",
           fs::path("Save") / "Game A" / "Party",
           fs::path("Save") / "Game A" / "nested" / "state.dat",
           fs::path("save") / "game a",
           fs::path("SAVE") / "GAME A" / "STATE.DAT",
           fs::path("sAvE") / "gAmE A" / "nested" / "state.dat",
       }) {
    CHECK(runtime.read_policy_for_user_relative_path(path) ==
        ReplayReadPolicy::user_data_only);
  }
  for (const fs::path& path : {
           fs::path("Save"),
           fs::path("Save") / "Game B",
           fs::path("Save") / "Game Aardvark",
           fs::path("save") / "game b",
           fs::path("SAVE") / "GAME AARDVARK",
           fs::path("Data Files"),
           fs::path("Data Files") / "Class Data A",
           fs::path("Scenarios") / "Tutorial",
           fs::path("Black Chancery.ttf"),
       }) {
    CHECK(runtime.read_policy_for_user_relative_path(path) ==
        ReplayReadPolicy::normal_bundled_fallback);
  }

  for (const fs::path& path : {
           fs::path(),
           fs::path("."),
           fs::path(".."),
           fs::path("Save") / ".." / "Data Files",
           runtime.user_data_root() / "Save" / "Game A",
       }) {
    check_invalid_argument([&] {
      static_cast<void>(runtime.read_policy_for_user_relative_path(path));
    });
  }
}

void test_runtime_rng_delegation() {
  ReplayRuntime runtime(make_config());
  CHECK(runtime.rng_draw_count() == 0);
  CHECK(runtime.next_classic_random() == 22067);
  CHECK(runtime.rng_draw_count() == 1);
  CHECK(runtime.next_classic_random() == -16737);
  CHECK(runtime.rng_draw_count() == 2);
}

void test_action_plan_and_state_trace_lifecycle() {
  ReplayRuntime runtime(make_config());
  CHECK(!runtime.action_plan_started());
  CHECK(runtime.settled_action_count() == 0U);
  check_runtime_error([&] {
    static_cast<void>(runtime.next_gameplay_poll());
  });

  runtime.start_action_plan({
      movement(1, realmz::presentation::MovementCommand::north),
      movement(2, realmz::presentation::MovementCommand::east),
  });
  CHECK(runtime.action_plan_started());
  check_logic_error([&] { runtime.start_action_plan({}); });

  ReplayStateSnapshot initial;
  initial.world.party_y = 10;
  ReplayStateSnapshot after_first = initial;
  after_first.world.party_y = 9;
  ReplayStateSnapshot after_second = after_first;
  after_second.world.party_x = 1;
  const std::array expected_post_actions{after_first, after_second};

  auto directive = runtime.next_gameplay_poll();
  CHECK(directive.checkpoint.has_value());
  CHECK(directive.checkpoint->kind == ReplayCheckpointKind::initial);
  runtime.record_checkpoint(*directive.checkpoint, initial);
  CHECK(directive.action != nullptr);
  runtime.acknowledge_action_delivery(
      directive.action->sequence,
      0x00007E1EU,
      {.kind = ReplayObservedEventKind::key_down, .message = 0x00007E1EU});
  CHECK(runtime.settled_action_count() == 0U);

  directive = runtime.next_gameplay_poll();
  CHECK(directive.checkpoint->action_index == 0U);
  runtime.record_checkpoint(*directive.checkpoint, after_first);
  CHECK(runtime.settled_action_count() == 1U);
  CHECK(directive.action != nullptr);
  runtime.acknowledge_action_delivery(
      directive.action->sequence,
      0x00007C1DU,
      {.kind = ReplayObservedEventKind::key_down, .message = 0x00007C1DU});
  CHECK(runtime.settled_action_count() == 1U);

  directive = runtime.next_gameplay_poll();
  CHECK(directive.checkpoint->action_index == 1U);
  runtime.record_checkpoint(*directive.checkpoint, after_second);
  CHECK(directive.finalize);
  CHECK(runtime.settled_action_count() == 2U);
  CHECK(runtime.finalize_state_trace() ==
      replay_state_trace_sha256_v1(initial, expected_post_actions));
}

void test_action_plan_rejection_is_atomic() {
  ReplayRuntime runtime(make_config());
  check_runtime_error([&] {
    runtime.start_action_plan({
        movement(2, realmz::presentation::MovementCommand::north),
    });
  });
  CHECK(!runtime.action_plan_started());
  CHECK(runtime.settled_action_count() == 0U);
}

void test_one_shot_process_installation() {
  CHECK(installed_replay_runtime() == nullptr);
  ReplayRuntime& installed = install_replay_runtime(make_config());
  CHECK(installed_replay_runtime() == &installed);
  CHECK(installed.replay_route() == ReplayRoute::semantic);
  CHECK(installed.config().run_id() ==
      "0123456789abcdef0123456789abcdef");

  check_logic_error([&] {
    static_cast<void>(install_replay_runtime(make_config("classic", "classic")));
  });
  CHECK(installed_replay_runtime() == &installed);
  CHECK(installed.replay_route() == ReplayRoute::semantic);
}

} // namespace

int main() {
  try {
    test_immutable_policy_access();
    test_narrow_user_only_input_read_policy();
    test_runtime_rng_delegation();
    test_action_plan_and_state_trace_lifecycle();
    test_action_plan_rejection_is_atomic();
    test_one_shot_process_installation();
    std::cout << "ReplayRuntimeTest passed (" << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayRuntimeTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
