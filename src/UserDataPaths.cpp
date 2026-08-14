#include "UserDataPaths.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "AppIdentity.hpp"

namespace realmz::app {

std::optional<std::filesystem::path> remastered_user_data_root() {
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
