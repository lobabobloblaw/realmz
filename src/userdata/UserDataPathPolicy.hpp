#pragma once

#include <filesystem>
#include <string_view>

namespace realmz::userdata {

// Converts a Classic Mac path to a host-relative path. Explicit Classic paths
// begin with ':'. Callers may opt into treating an unprefixed name as local,
// which is needed for Toolbox APIs that already imply the current volume.
// Rooted paths and dot/parent components are rejected before any filesystem
// operation can interpret them.
[[nodiscard]] std::filesystem::path safe_relative_path_for_classic_path(
    std::string_view classic_path,
    bool implicitly_local = false);

// Joins an already-relative path below an explicit absolute root and verifies
// the result remains lexically confined to that root.
[[nodiscard]] std::filesystem::path confined_path_below_root(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path);

// fopen modes beginning with 'w' or 'a', or containing '+', can mutate data.
// Invalid/empty modes are treated as non-writing here and rejected by the
// actual open boundary.
[[nodiscard]] bool fopen_mode_can_write(std::string_view mode) noexcept;

// Read-only opens use a user override when one exists and otherwise fall back
// to bundled data. Every write-capable mode is forced to user storage.
[[nodiscard]] bool should_open_from_user_storage(
    std::string_view mode,
    bool user_file_exists) noexcept;

} // namespace realmz::userdata
