#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "userdata/UserDataPathPolicy.hpp"

namespace fs = std::filesystem;
using namespace realmz::userdata;

namespace {

size_t checks_run = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    ++checks_run;                                                               \
    if (!(condition)) {                                                         \
      throw std::runtime_error(                                                 \
          std::string("check failed: ") + #condition);                        \
    }                                                                           \
  } while (false)

template <typename Function>
void check_invalid(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("expected std::invalid_argument");
}

void test_classic_path_conversion_and_confinement() {
  const std::string utf8_path = "Realmz-\xC3\xA9-\xE7\x95\x8C";
  const auto native_utf8_path = path_from_utf8(utf8_path);
  const auto encoded_native_path = native_utf8_path.generic_u8string();
  CHECK(std::string(
            reinterpret_cast<const char*>(encoded_native_path.data()),
            encoded_native_path.size()) == utf8_path);

  CHECK(safe_relative_path_for_classic_path(
            ":Character Files:Hero One") ==
      fs::path("Character Files") / "Hero One");
  CHECK(safe_relative_path_for_classic_path(
            "ChicagoFLF.ttf", true) == fs::path("ChicagoFLF.ttf"));
  CHECK(safe_relative_path_for_classic_path(
            ":Character Files:") == fs::path("Character Files"));

  const fs::path root = fs::temp_directory_path() / "Realmz Remastered";
  const fs::path result = confined_path_below_root(
      root, fs::path("Save") / "Party One");
  CHECK(result == root / "Save" / "Party One");
  CHECK(result != fs::path("/Save/Party One"));

  // SDL_GetBasePath() includes a trailing separator. That spelling must remain
  // confined without becoming a false negative in the component comparison.
  const fs::path trailing_root(root.string() + fs::path::preferred_separator);
  const fs::path bundled_font = confined_path_below_root(
      trailing_root, fs::path("Black Chancery.ttf"));
  CHECK(bundled_font == root / "Black Chancery.ttf");
}

void test_unsafe_paths_are_rejected() {
  check_invalid([] {
    static_cast<void>(safe_relative_path_for_classic_path(""));
  });
  check_invalid([] {
    static_cast<void>(safe_relative_path_for_classic_path(":"));
  });
  check_invalid([] {
    static_cast<void>(
        safe_relative_path_for_classic_path("Character Files"));
  });
  check_invalid([] {
    static_cast<void>(safe_relative_path_for_classic_path(":/tmp/escape"));
  });
  check_invalid([] {
    static_cast<void>(safe_relative_path_for_classic_path("::escape"));
  });
  check_invalid([] {
    static_cast<void>(
        safe_relative_path_for_classic_path(":folder::escape"));
  });
  check_invalid([] {
    static_cast<void>(safe_relative_path_for_classic_path(":../escape"));
  });
  check_invalid([] {
    static_cast<void>(
        safe_relative_path_for_classic_path(":folder:.:escape"));
  });
  check_invalid([] {
    static_cast<void>(
        safe_relative_path_for_classic_path(":folder:..:escape"));
  });
  check_invalid([] {
    static_cast<void>(
        safe_relative_path_for_classic_path("/tmp/escape", true));
  });

  const fs::path root = fs::temp_directory_path() / "Realmz Remastered";
  check_invalid([&] {
    static_cast<void>(
        confined_path_below_root(root, fs::path("/tmp/escape")));
  });
  check_invalid([&] {
    static_cast<void>(
        confined_path_below_root(root, fs::path("../escape")));
  });
  check_invalid([&] {
    static_cast<void>(
        confined_path_below_root(root, fs::path("./escape")));
  });
  check_invalid([&] {
    static_cast<void>(
        confined_path_below_root("relative-root", "Save/Game"));
  });
}

void test_fopen_mode_routing() {
  for (const std::string_view mode : {"w", "wb", "w+", "w+b", "wx",
           "a", "ab", "a+", "a+b", "r+", "rb+", "r+b"}) {
    CHECK(fopen_mode_can_write(mode));
    CHECK(should_open_from_user_storage(mode, false));
  }

  for (const std::string_view mode : {"r", "rb"}) {
    CHECK(!fopen_mode_can_write(mode));
    CHECK(!should_open_from_user_storage(mode, false));
    CHECK(should_open_from_user_storage(mode, true));
  }
  CHECK(!fopen_mode_can_write(""));
}

} // namespace

int main() {
  try {
    test_classic_path_conversion_and_confinement();
    test_unsafe_paths_are_rejected();
    test_fopen_mode_routing();
    std::cout << "UserDataPathPolicyTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "UserDataPathPolicyTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
