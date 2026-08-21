#include "UserDataPaths.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <mutex>

#include "AppIdentity.hpp"

namespace realmz::app {
namespace {

struct UserDataRootState final {
  std::mutex mutex;
  std::optional<std::filesystem::path> replay_root;
  bool getter_called = false;
};

UserDataRootState& user_data_root_state() {
  static UserDataRootState state;
  return state;
}

} // namespace

bool set_semantic_replay_user_data_root(
    const std::filesystem::path& root) {
  if (root.empty() || !root.is_absolute() ||
      root.lexically_normal() != root) {
    return false;
  }
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(root, status_error);
  if (status_error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    return false;
  }
  auto& state = user_data_root_state();
  std::lock_guard lock(state.mutex);
  if (state.getter_called || state.replay_root.has_value()) {
    return false;
  }
  state.replay_root = root;
  return true;
}

std::optional<std::filesystem::path> remastered_user_data_root() {
  {
    auto& state = user_data_root_state();
    std::lock_guard lock(state.mutex);
    state.getter_called = true;
    if (state.replay_root.has_value()) {
      return state.replay_root;
    }
  }

  char* raw_path = SDL_GetPrefPath(
      kPreferenceOrganization.data(), kPreferenceApplication.data());
  if (!raw_path) {
    return std::nullopt;
  }
  std::filesystem::path path(raw_path);
  SDL_free(raw_path);
  return path;
}

} // namespace realmz::app
