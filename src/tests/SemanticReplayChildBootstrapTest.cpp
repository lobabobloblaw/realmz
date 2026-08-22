#include "FileManager.hpp"
#include "MemoryManager.h"
#include "PortPrefs.hpp"
#include "SemanticReplayChild.h"
#include "UserDataPaths.hpp"
#include "replay/ReplayRuntime.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = fs::temp_directory_path() /
        ("realmz-semantic-bootstrap-test-" + std::to_string(suffix));
    if (!fs::create_directory(path_)) {
      throw std::runtime_error("could not create temporary test directory");
    }
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const noexcept {
    return path_;
  }

private:
  fs::path path_;
};

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string result = "\"";
  for (const unsigned char character : value) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(static_cast<char>(character));
  }
  result.push_back('"');
  return result;
}

void write_text(const fs::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not create test file: " + path.string());
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) {
    throw std::runtime_error("could not write test file: " + path.string());
  }
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not read test file: " + path.string());
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string child_config(
    const fs::path& user_root,
    const fs::path& result_path,
    std::uint32_t schema_version) {
  return
      "{"
      "\"schema_version\":" + std::to_string(schema_version) + ","
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":\"semantic\","
      "\"presentation_mode\":\"remastered\","
      "\"user_data_root\":" + json_string(user_root.string()) + ","
      "\"user_data_root_policy\":\"set_once_before_toolbox_init\","
      "\"preferences_write_policy\":\"disabled\","
      "\"input_slot\":\"A\","
      "\"input_slot_policy\":"
      "\"user_data_root_only_no_bundled_fallback\","
      "\"output_slot\":\"B\","
      "\"output_slot_policy\":\"fresh_nonexistent\","
      "\"actions\":[{\"ordinal\":0,\"kind\":\"center\","
      "\"arguments\":{}}],"
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(result_path.string()) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}";
}

void test_configured_bootstrap_policies(std::uint32_t schema_version) {
  TemporaryDirectory temporary;
  const fs::path user_root = temporary.path() / "user-root";
  const fs::path input_root = user_root / "Save" / "Game A";
  const fs::path output_root = user_root / "Save" / "Game B";
  const fs::path result_path = temporary.path() / "result.json";
  const fs::path config_path = temporary.path() / "config.json";
  fs::create_directories(input_root);
  write_text(input_root / "state.dat", "staged-input");

  CHECK(RealmzConfigureSemanticReplayChild(
            (temporary.path() / "missing.json").string().c_str()) ==
      REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT);
  CHECK(!RealmzSemanticReplayChildIsActive());

  write_text(
      config_path, child_config(user_root, result_path, schema_version));
  CHECK(RealmzConfigureSemanticReplayChild(config_path.string().c_str()) == 0);
  CHECK(RealmzSemanticReplayChildIsActive());
  const auto selected_root = realmz::app::remastered_user_data_root();
  CHECK(selected_root.has_value());
  CHECK(*selected_root == user_root);

  const fs::path port_preferences = user_root / "port_settings.json";
  write_text(port_preferences, "ambient-preferences-must-be-ignored");
  PortPrefs preferences = load_port_prefs();
  CHECK(preferences.presentation_mode ==
      realmz::presentation::PresentationMode::remastered);
  save_port_prefs(preferences);
  CHECK(read_text(port_preferences) == "ambient-preferences-must-be-ignored");

  FILE* input = mac_fopen(":Save:Game A:state.dat", "rb");
  CHECK(input != nullptr);
  char buffer[32] = {};
  CHECK(std::fread(buffer, 1, sizeof(buffer), input) == 12);
  CHECK(std::string_view(buffer, 12) == "staged-input");
  CHECK(std::fclose(input) == 0);
  CHECK(mac_fopen(":Save:Game A:state.dat", "r+b") == nullptr);
  CHECK(mac_fopen(":Save:Game A:missing.dat", "rb") == nullptr);

  auto* runtime = realmz::replay::installed_replay_runtime();
  CHECK(runtime != nullptr);
  CHECK(runtime->config().schema_version() == schema_version);
  CHECK(runtime->read_policy_for_user_relative_path(
            fs::path("save") / "game a" / "state.dat") ==
      realmz::replay::ReplayReadPolicy::user_data_only);
  CHECK(mac_fopen(":save:game a:state.dat", "r+b") == nullptr);
  std::error_code alias_error;
  const bool alternate_case_aliases_input = fs::equivalent(
      input_root, user_root / "save" / "game a", alias_error);
  FILE* alternate_case_input =
      mac_fopen(":save:game a:state.dat", "rb");
  if (alternate_case_aliases_input && !alias_error) {
    CHECK(alternate_case_input != nullptr);
    char alternate_buffer[32] = {};
    CHECK(std::fread(
              alternate_buffer, 1, sizeof(alternate_buffer),
              alternate_case_input) == 12);
    CHECK(std::string_view(alternate_buffer, 12) == "staged-input");
    CHECK(std::fclose(alternate_case_input) == 0);
  } else {
    CHECK(alternate_case_input == nullptr);
  }

  const fs::path outside = temporary.path() / "outside";
  fs::create_directory(outside);
  write_text(outside / "secret.dat", "outside");
  std::error_code link_error;
  fs::create_directory_symlink(outside, input_root / "linked", link_error);
  if (!link_error) {
    CHECK(mac_fopen(":Save:Game A:linked:secret.dat", "rb") == nullptr);
    CHECK(mac_list_directory(":Save:Game A:linked").empty());
  }

  CHECK(Random() == 22067);
  CHECK(runtime->rng_draw_count() == 1);

  CHECK(RealmzRunSemanticReplayChild() ==
      REALMZ_SEMANTIC_REPLAY_ACTION_ERROR_EXIT);
  CHECK(!fs::exists(result_path));
  CHECK(!fs::exists(output_root));
}

void test_unknown_schema_rejected_before_runtime_installation() {
  TemporaryDirectory temporary;
  const fs::path user_root = temporary.path() / "user-root";
  const fs::path input_root = user_root / "Save" / "Game A";
  const fs::path output_root = user_root / "Save" / "Game B";
  const fs::path result_path = temporary.path() / "result.json";
  const fs::path config_path = temporary.path() / "config-v3.json";
  fs::create_directories(input_root);
  const fs::path sentinel = input_root / "state.dat";
  write_text(sentinel, "unchanged-input");
  write_text(config_path, child_config(user_root, result_path, 3U));

  CHECK(realmz::replay::installed_replay_runtime() == nullptr);
  CHECK(!RealmzSemanticReplayChildIsActive());
  CHECK(RealmzConfigureSemanticReplayChild(config_path.string().c_str()) ==
      REALMZ_SEMANTIC_REPLAY_CONFIG_ERROR_EXIT);
  CHECK(!RealmzSemanticReplayChildIsActive());
  CHECK(realmz::replay::installed_replay_runtime() == nullptr);
  CHECK(read_text(sentinel) == "unchanged-input");
  CHECK(!fs::exists(output_root));
  CHECK(!fs::exists(result_path));
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::string_view scenario = "v1";
    if (argc == 2) {
      scenario = argv[1];
    } else if (argc != 1) {
      std::cerr << "usage: SemanticReplayChildBootstrapTest "
                   "[v1|v2|unknown-version]\n";
      return 2;
    }
    if (scenario == "v1") {
      test_configured_bootstrap_policies(1U);
    } else if (scenario == "v2") {
      test_configured_bootstrap_policies(2U);
    } else if (scenario == "unknown-version") {
      test_unknown_schema_rejected_before_runtime_installation();
    } else {
      std::cerr << "usage: SemanticReplayChildBootstrapTest "
                   "[v1|v2|unknown-version]\n";
      return 2;
    }
    std::cout << "SemanticReplayChildBootstrapTest passed ("
              << scenario << ", " << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayChildBootstrapTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
