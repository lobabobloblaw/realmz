#include "SemanticReplayChild.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "UserDataPaths.hpp"
#include "replay/ReplayActionDecoder.hpp"
#include "replay/ReplayChildConfig.hpp"
#include "replay/ReplayRuntime.hpp"

namespace {

constexpr std::size_t kMaximumDiagnosticBytes = 512;

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

extern "C" int RealmzConfigureSemanticReplayChild(
    const char* config_path) {
  if (!config_path || !config_path[0]) {
    write_bounded_diagnostic(
        "semantic replay child configuration rejected: ",
        "the config path is empty");
    return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
  }

  try {
    auto config = realmz::replay::load_child_config_v1(config_path);
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

extern "C" int RealmzRunSemanticReplayChild(void) {
  auto* runtime = realmz::replay::installed_replay_runtime();
  if (!runtime) {
    write_bounded_diagnostic(
        "semantic replay child startup failed: ",
        "no replay runtime is installed");
    return REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT;
  }
  try {
    // Close the engine-specific action vocabulary before a future driver is
    // allowed to load or mutate the configured input save.
    static_cast<void>(realmz::replay::decode_replay_actions_v1(
        runtime->config().actions()));
  } catch (const realmz::replay::ReplayActionDecodeError& error) {
    write_bounded_diagnostic(
        "semantic replay child action plan rejected: ", error.what());
    return REALMZ_SEMANTIC_REPLAY_DRIVER_UNAVAILABLE_EXIT;
  }
  write_bounded_diagnostic(
      "semantic replay child unavailable: ",
      "native save loading, action driving, and result emission are not "
      "implemented");
  return REALMZ_SEMANTIC_REPLAY_DRIVER_UNAVAILABLE_EXIT;
}
