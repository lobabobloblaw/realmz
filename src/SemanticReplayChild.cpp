#include "SemanticReplayChild.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <phosg/Strings.hh>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "UserDataPaths.hpp"
#include "replay/ReplayActionDecoder.hpp"
#include "replay/ReplayChildConfig.hpp"
#include "replay/ReplayCompletion.hpp"
#include "replay/ReplayOutputOracle.hpp"
#include "replay/ReplayResultWriter.hpp"
#include "replay/ReplayRuntime.hpp"
#include "replay/ReplaySlotSelection.h"
#include "replay/SemanticReplayChildSession.hpp"
#include "replay/LegacyReplayStateSource.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr std::size_t kMaximumDiagnosticBytes = 512;
constexpr std::string_view kReplayEngineIdentityV1 =
    "Realmz-8.1.0-native-replay-v1";
constexpr std::string_view kReplayEngineIdentityV2 =
    "Realmz-8.1.0-native-replay-v2";
constexpr std::string_view kReplayEngineIdentityV3 =
    "Realmz-8.1.0-native-replay-v3";

[[nodiscard]] std::string_view replay_engine_identity(
    std::uint32_t schema_version) {
  if (schema_version == 1U) {
    return kReplayEngineIdentityV1;
  }
  if (schema_version == 2U) {
    return kReplayEngineIdentityV2;
  }
  if (schema_version == 3U) {
    return kReplayEngineIdentityV3;
  }
  throw realmz::replay::ReplayConfigError(
      "native replay schema version is unsupported");
}

void write_bounded_diagnostic(
    std::string_view prefix, std::string_view detail) noexcept {
  std::fwrite(prefix.data(), 1, prefix.size(), stderr);
  const std::size_t detail_size =
      std::min(detail.size(), kMaximumDiagnosticBytes);
  std::fwrite(detail.data(), 1, detail_size, stderr);
  std::fputc('\n', stderr);
}

void require_physical_directory(
    const std::filesystem::path& path,
    std::string_view description) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    throw realmz::replay::ReplayConfigError(
        std::string(description) + " must be a physical directory");
  }
}

void require_fresh_output_slot(
    const realmz::replay::ReplayChildConfig& config) {
  const auto save_root = config.user_data_root() / "Save";
  const auto input_root =
      save_root / (std::string("Game ") + config.input_slot());
  const auto output_root =
      save_root / (std::string("Game ") + config.output_slot());
  require_physical_directory(config.user_data_root(), "user_data_root");
  require_physical_directory(save_root, "user_data_root/Save");
  require_physical_directory(input_root, "configured input slot");

  std::error_code output_error;
  const auto output_status =
      std::filesystem::symlink_status(output_root, output_error);
  if ((!output_error &&
          output_status.type() != std::filesystem::file_type::not_found) ||
      (output_error &&
          output_error != std::errc::no_such_file_or_directory)) {
    throw realmz::replay::ReplayConfigError(
        "configured output slot must be fresh and nonexistent");
  }
}

} // namespace

namespace realmz::replay {

void record_live_replay_checkpoint(
    ReplayRuntime& runtime,
    const ReplayCheckpoint& checkpoint) {
  runtime.record_checkpoint(
      checkpoint, LegacyReplayStateSource().capture());
}

[[noreturn]] void fail_semantic_replay_child(
    std::string_view detail) noexcept {
  write_bounded_diagnostic(
      "semantic replay child execution failed: ", detail);
  std::fflush(stderr);
  std::_Exit(REALMZ_SEMANTIC_REPLAY_EXECUTION_ERROR_EXIT);
}

[[nodiscard]] static std::uint64_t replay_process_id() noexcept {
#ifdef _WIN32
  return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

[[noreturn]] void complete_semantic_replay_child(
    ReplayRuntime& runtime) noexcept {
  try {
    static_cast<void>(complete_replay(runtime, {
        .engine_identity = std::string(
            replay_engine_identity(runtime.config().schema_version())),
        .process_id = replay_process_id(),
        .save_slot = [&runtime](char slot) {
          // The runner creates a private workspace, but the output pathname
          // still remains ambient mutable state. Recheck it at the last
          // possible boundary before the legacy writer creates anything.
          require_fresh_output_slot(runtime.config());
          return RealmzReplaySaveSlot(slot) == 1;
        },
        .verify_output_slot = [](const std::filesystem::path& root, char slot) {
          return verify_replay_output_slot(root, slot);
        },
        .publish_result = [](const ReplayChildConfig& config,
                                 const ReplayCompletedResult& result) {
          if (config.schema_version() == 1U) {
            write_replay_child_result_v1(config, result);
          } else if (config.schema_version() == 2U) {
            write_replay_child_result_v2(config, result);
          } else if (config.schema_version() == 3U) {
            write_replay_child_result_v3(config, result);
          } else {
            throw ReplayResultError(
                "native replay result schema version is unsupported");
          }
        },
    }));
    std::_Exit(0);
  } catch (const std::exception& error) {
    fail_semantic_replay_child(error.what());
  } catch (...) {
    fail_semantic_replay_child("unknown completion failure");
  }
}

} // namespace realmz::replay

extern "C" int RealmzConfigureSemanticReplayChild(
    const char* config_path) {
  if (!config_path || !config_path[0]) {
    write_bounded_diagnostic(
        "semantic replay child configuration rejected: ",
        "the config path is empty");
    return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
  }

  try {
    auto config = realmz::replay::load_child_config(config_path);
    require_fresh_output_slot(config);
    if (!realmz::app::set_semantic_replay_user_data_root(
            config.user_data_root())) {
      write_bounded_diagnostic(
          "semantic replay child configuration rejected: ",
          "the replay user-data root was not installed before its first use");
      return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
    }
    static_cast<void>(
        realmz::replay::install_replay_runtime(std::move(config)));
    phosg::set_log_level(phosg::LogLevel::L_WARNING);
    return 0;
  } catch (const realmz::replay::ReplayConfigError& error) {
    write_bounded_diagnostic(
        "semantic replay child configuration rejected: ", error.what());
  } catch (const std::exception& error) {
    write_bounded_diagnostic(
        "semantic replay child startup failed: ", error.what());
  } catch (...) {
    write_bounded_diagnostic(
        "semantic replay child startup failed: ", "unknown error");
  }
  return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
}

extern "C" int RealmzSemanticReplayChildIsActive(void) {
  return realmz::replay::installed_replay_runtime() != nullptr;
}

extern "C" void RealmzFailSemanticReplayChild(const char* detail) {
  realmz::replay::fail_semantic_replay_child(
      (detail && detail[0])
          ? std::string_view(detail)
          : std::string_view("unspecified legacy failure"));
}

extern "C" int RealmzRunSemanticReplayChild(void) {
  auto* runtime = realmz::replay::installed_replay_runtime();
  if (!runtime) {
    write_bounded_diagnostic(
        "semantic replay child startup failed: ",
        "no replay runtime is installed");
    return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
  }
  try {
    // Close and start the engine-specific action vocabulary before any save
    // can be loaded or mutated.
    std::vector<realmz::presentation::UIAction> actions;
    if (runtime->config().schema_version() == 1U) {
      actions = realmz::replay::decode_replay_actions_v1(
          runtime->config().actions());
    } else if (runtime->config().schema_version() == 2U) {
      actions = realmz::replay::decode_replay_actions_v2(
          runtime->config().actions());
    } else if (runtime->config().schema_version() == 3U) {
      actions = realmz::replay::decode_replay_actions_v3(
          runtime->config().actions());
    } else {
      throw realmz::replay::ReplayActionDecodeError(
          "native replay action schema version is unsupported");
    }
    runtime->start_action_plan(std::move(actions));
  } catch (const realmz::replay::ReplayActionDecodeError& error) {
    write_bounded_diagnostic(
        "semantic replay child action plan rejected: ", error.what());
    return REALMZ_SEMANTIC_REPLAY_ACTION_ERROR_EXIT;
  } catch (const std::exception& error) {
    write_bounded_diagnostic(
        "semantic replay child action plan failed: ", error.what());
    return REALMZ_SEMANTIC_REPLAY_ACTION_ERROR_EXIT;
  }

  if (RealmzReplayLoadSlot(runtime->input_slot()) != 1) {
    write_bounded_diagnostic(
        "semantic replay child execution failed: ",
        "configured input slot could not be loaded");
    return REALMZ_SEMANTIC_REPLAY_EXECUTION_ERROR_EXIT;
  }
  RealmzReplayEnterLoadedGame();
  write_bounded_diagnostic(
      "semantic replay child execution failed: ",
      "loaded gameplay returned before replay completion");
  return REALMZ_SEMANTIC_REPLAY_EXECUTION_ERROR_EXIT;
}
