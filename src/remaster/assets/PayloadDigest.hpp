#pragma once

#include <string>
#include <string_view>

namespace realmz::remaster::assets {

// Returns the lowercase SHA-256 digest of the exact resource-fork payload.
// Live selection passes immutable ResourceFile::Resource::data here before a
// mutable Classic Handle is exposed to callers.
[[nodiscard]] std::string payloadSha256Hex(std::string_view payload);

} // namespace realmz::remaster::assets
