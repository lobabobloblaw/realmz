#pragma once

#include <filesystem>
#include <optional>

namespace realmz::app {

// Installs the replay process's isolated writable root. This is a one-shot
// startup operation and succeeds only before the first call to
// remastered_user_data_root(). The path must be absolute and nonempty.
[[nodiscard]] bool set_semantic_replay_user_data_root(
    const std::filesystem::path& root);

// Returns the fork's isolated writable root. On macOS this is
// ~/Library/Application Support/Realmz Remastered. SDL creates the directory
// when it is first requested. A replay child instead receives its already
// staged root from set_semantic_replay_user_data_root and does not consult SDL.
[[nodiscard]] std::optional<std::filesystem::path>
remastered_user_data_root();

} // namespace realmz::app
