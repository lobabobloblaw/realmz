#include "replay/ReplayCompletion.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  return std::string(
      reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

[[nodiscard]] ReplayChildConfig make_config() {
  const fs::path root =
      (fs::temp_directory_path() / "Realmz Replay Completion Root")
          .lexically_normal();
  const fs::path result =
      (fs::temp_directory_path() / "realmz-completion-result.json")
          .lexically_normal();
  return parse_child_config_v1(
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
      "\"actions\":[{\"ordinal\":0,\"kind\":\"move_party\","
      "\"arguments\":{\"command\":\"north\"}}],"
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}");
}

[[nodiscard]] UIAction movement() {
  return {
      .sequence = 1,
      .payload = MovePartyAction{MovementCommand::north},
  };
}

void prepare_completed_runtime(
    ReplayRuntime& runtime,
    const ReplayStateSnapshot& initial,
    const ReplayStateSnapshot& settled) {
  runtime.start_action_plan({movement()});
  auto directive = runtime.next_gameplay_poll();
  CHECK(directive.checkpoint.has_value());
  runtime.record_checkpoint(*directive.checkpoint, initial);
  CHECK(directive.action != nullptr);
  runtime.acknowledge_action_delivery(
      directive.action->sequence,
      0x00007E1EU,
      {.kind = ReplayObservedEventKind::key_down, .message = 0x00007E1EU});
  directive = runtime.next_gameplay_poll();
  CHECK(directive.checkpoint.has_value());
  runtime.record_checkpoint(*directive.checkpoint, settled);
  CHECK(directive.finalize);
}

template <typename Function>
void check_completion_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayCompletionError&) {
    return;
  }
  throw std::runtime_error("expected ReplayCompletionError");
}

void test_completion_orders_effects_and_builds_result() {
  ReplayRuntime runtime(make_config());
  ReplayStateSnapshot initial;
  initial.world.world_y = 20;
  ReplayStateSnapshot settled = initial;
  settled.world.world_y = 19;
  prepare_completed_runtime(runtime, initial, settled);

  std::vector<std::string> calls;
  ReplayCompletedResult published;
  const Sha256Digest output_digest = sha256("verified output");
  const auto result = complete_replay(runtime, {
      .engine_identity = "Realmz-8.1.0-native-replay-v1",
      .process_id = 4242,
      .save_slot = [&](char slot) {
        calls.emplace_back("save");
        CHECK(slot == 'B');
        static_cast<void>(runtime.next_classic_random());
        return true;
      },
      .verify_output_slot = [&](const fs::path& root, char slot) {
        calls.emplace_back("verify");
        CHECK(root == runtime.user_data_root());
        CHECK(slot == 'B');
        return VerifiedReplayOutput{.tree_sha256 = output_digest};
      },
      .publish_result = [&](const ReplayChildConfig& config,
                                const ReplayCompletedResult& candidate) {
        calls.emplace_back("publish");
        CHECK(&config == &runtime.config());
        published = candidate;
      },
  });

  CHECK((calls == std::vector<std::string>{"save", "verify", "publish"}));
  CHECK(result.process_id == 4242U);
  CHECK(result.engine_identity == "Realmz-8.1.0-native-replay-v1");
  CHECK(result.settled_action_count == 1U);
  CHECK(result.state_sha256 == replay_state_trace_sha256_v1(
      initial, std::vector<ReplayStateSnapshot>{settled}));
  CHECK(result.save_tree_sha256 == output_digest);
  CHECK(result.rng_draw_count == 1U);
  CHECK(published.process_id == result.process_id);
  CHECK(published.state_sha256 == result.state_sha256);
}

void test_preconditions_fail_before_save() {
  ReplayRuntime runtime(make_config());
  std::size_t saves = 0;
  ReplayCompletionOperations operations{
      .engine_identity = "engine",
      .process_id = 1,
      .save_slot = [&](char) {
        ++saves;
        return true;
      },
      .verify_output_slot = [](const fs::path&, char) {
        return VerifiedReplayOutput{};
      },
      .publish_result = [](const ReplayChildConfig&,
                            const ReplayCompletedResult&) {},
  };
  check_completion_error([&] {
    static_cast<void>(complete_replay(runtime, operations));
  });
  CHECK(saves == 0U);

  runtime.start_action_plan({movement()});
  auto first = runtime.next_gameplay_poll();
  runtime.record_checkpoint(*first.checkpoint, {});
  check_completion_error([&] {
    static_cast<void>(complete_replay(runtime, operations));
  });
  CHECK(saves == 0U);

  runtime.acknowledge_action_delivery(
      first.action->sequence,
      0x00007E1EU,
      {.kind = ReplayObservedEventKind::key_down, .message = 0x00007E1EU});
  CHECK(runtime.settled_action_count() == 0U);
  check_completion_error([&] {
    static_cast<void>(complete_replay(runtime, operations));
  });
  CHECK(saves == 0U);
}

void test_failures_stop_the_pipeline() {
  ReplayStateSnapshot initial;
  ReplayStateSnapshot settled;

  ReplayRuntime save_failure(make_config());
  prepare_completed_runtime(save_failure, initial, settled);
  std::vector<std::string> calls;
  check_completion_error([&] {
    static_cast<void>(complete_replay(save_failure, {
        .engine_identity = "engine",
        .process_id = 1,
        .save_slot = [&](char) {
          calls.emplace_back("save");
          return false;
        },
        .verify_output_slot = [&](const fs::path&, char) {
          calls.emplace_back("verify");
          return VerifiedReplayOutput{};
        },
        .publish_result = [&](const ReplayChildConfig&,
                                  const ReplayCompletedResult&) {
          calls.emplace_back("publish");
        },
    }));
  });
  CHECK((calls == std::vector<std::string>{"save"}));

  ReplayRuntime verify_failure(make_config());
  prepare_completed_runtime(verify_failure, initial, settled);
  calls.clear();
  try {
    static_cast<void>(complete_replay(verify_failure, {
        .engine_identity = "engine",
        .process_id = 1,
        .save_slot = [&](char) {
          calls.emplace_back("save");
          return true;
        },
        .verify_output_slot = [&](const fs::path&, char)
            -> VerifiedReplayOutput {
          calls.emplace_back("verify");
          throw ReplayOutputError("verification failed");
        },
        .publish_result = [&](const ReplayChildConfig&,
                                  const ReplayCompletedResult&) {
          calls.emplace_back("publish");
        },
    }));
    throw std::runtime_error("expected ReplayOutputError");
  } catch (const ReplayOutputError&) {
  }
  CHECK((calls == std::vector<std::string>{"save", "verify"}));
}

void test_missing_operation_and_zero_pid_rejected() {
  ReplayRuntime runtime(make_config());
  ReplayStateSnapshot snapshot;
  prepare_completed_runtime(runtime, snapshot, snapshot);
  check_completion_error([&] {
    static_cast<void>(complete_replay(runtime, {}));
  });
  check_completion_error([&] {
    static_cast<void>(complete_replay(runtime, {
        .engine_identity = "engine",
        .process_id = 0,
        .save_slot = [](char) { return true; },
        .verify_output_slot = [](const fs::path&, char) {
          return VerifiedReplayOutput{};
        },
        .publish_result = [](const ReplayChildConfig&,
                              const ReplayCompletedResult&) {},
    }));
  });
}

} // namespace

int main() {
  try {
    test_completion_orders_effects_and_builds_result();
    test_preconditions_fail_before_save();
    test_failures_stop_the_pipeline();
    test_missing_operation_and_zero_pid_rejected();
    std::cout << "ReplayCompletionTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayCompletionTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
