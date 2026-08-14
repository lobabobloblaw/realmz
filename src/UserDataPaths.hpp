#pragma once

#include <filesystem>
#include <optional>

namespace realmz::app {

// Returns the fork's isolated writable root. On macOS this is
// ~/Library/Application Support/Realmz Remastered. SDL creates the directory
// when it is first requested.
[[nodiscard]] std::optional<std::filesystem::path>
remastered_user_data_root();

} // namespace realmz::app
