#include "UserDataPathPolicy.hpp"

#include <stdexcept>
#include <string>

namespace realmz::userdata {

namespace fs = std::filesystem;

namespace {

void validate_relative_path(const fs::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_path()) {
    throw std::invalid_argument("user-data path must be relative");
  }
  for (const auto& component : path) {
    if (component.empty() || (component == ".") || (component == "..")) {
      throw std::invalid_argument(
          "user-data path must not contain empty or dot components");
    }
  }
}

[[nodiscard]] bool is_ancestor_or_same(
    const fs::path& ancestor,
    const fs::path& candidate) noexcept {
  auto ancestor_iterator = ancestor.begin();
  auto candidate_iterator = candidate.begin();
  for (; ancestor_iterator != ancestor.end();
       ++ancestor_iterator, ++candidate_iterator) {
    if ((candidate_iterator == candidate.end()) ||
        (*ancestor_iterator != *candidate_iterator)) {
      return false;
    }
  }
  return true;
}

} // namespace

fs::path path_from_utf8(std::string_view utf8_path) {
  std::u8string encoded;
  encoded.reserve(utf8_path.size());
  for (const unsigned char byte : utf8_path) {
    encoded.push_back(static_cast<char8_t>(byte));
  }
  return fs::path(encoded);
}

fs::path safe_relative_path_for_classic_path(
    std::string_view classic_path,
    bool implicitly_local) {
  if (classic_path.empty()) {
    throw std::invalid_argument("Classic path must not be empty");
  }

  const bool explicitly_local = classic_path.front() == ':';
  if (explicitly_local) {
    classic_path.remove_prefix(1);
  } else if (!implicitly_local) {
    throw std::invalid_argument("absolute Classic paths are not supported");
  }
  if (classic_path.empty()) {
    throw std::invalid_argument("Classic path must name a relative item");
  }

  // In Classic Mac path syntax, an empty component means a parent traversal.
  // Do not let conversion to host separators erase that semantic signal.
  if (classic_path.starts_with(':') ||
      (classic_path.find("::") != std::string_view::npos)) {
    throw std::invalid_argument(
        "Classic path must not contain parent traversal");
  }
  // A single trailing separator names a directory; it is not another path
  // component. Removing it avoids producing an empty host component.
  if (classic_path.ends_with(':')) {
    classic_path.remove_suffix(1);
  }
  if (classic_path.empty()) {
    throw std::invalid_argument("Classic path must name a relative item");
  }

  std::string converted(classic_path);
  for (char& character : converted) {
    if (character == ':') {
      character = fs::path::preferred_separator;
    }
  }

  fs::path result(converted);
  validate_relative_path(result);
  return result;
}

fs::path confined_path_below_root(
    const fs::path& root,
    const fs::path& relative_path) {
  if (root.empty() || !root.is_absolute()) {
    throw std::invalid_argument("user-data root must be an absolute path");
  }
  validate_relative_path(relative_path);

  fs::path normalized_root = root.lexically_normal();
  // SDL_GetBasePath() deliberately returns a path with a trailing separator.
  // std::filesystem retains that separator as an empty final component, which
  // would make the component-wise ancestor check reject every child path.
  // Strip only that syntactic component; never walk above the filesystem root.
  if ((normalized_root != normalized_root.root_path()) &&
      normalized_root.filename().empty()) {
    normalized_root = normalized_root.parent_path();
  }
  const fs::path result = (normalized_root / relative_path).lexically_normal();
  if ((result == normalized_root) ||
      !is_ancestor_or_same(normalized_root, result)) {
    throw std::invalid_argument("user-data path escapes its configured root");
  }
  return result;
}

bool fopen_mode_can_write(std::string_view mode) noexcept {
  if (mode.empty()) {
    return false;
  }
  return (mode.front() == 'w') || (mode.front() == 'a') ||
      (mode.find('+') != std::string_view::npos);
}

bool should_open_from_user_storage(
    std::string_view mode,
    bool user_file_exists) noexcept {
  return user_file_exists || fopen_mode_can_write(mode);
}

} // namespace realmz::userdata
