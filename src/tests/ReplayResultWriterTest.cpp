#include "replay/ReplayResultWriter.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace realmz::replay;

namespace {

std::size_t checks_run = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks_run;                                                              \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("check failed: ") + #condition);   \
    }                                                                          \
  } while (false)

template <typename Function>
void check_result_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayResultError&) {
    return;
  }
  throw std::runtime_error("expected ReplayResultError");
}

[[nodiscard]] std::uint64_t process_id() noexcept {
#ifdef _WIN32
  return ::GetCurrentProcessId();
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    static std::atomic<std::uint64_t> sequence = 0;
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
      path_ = fs::temp_directory_path() /
          ("realmz-result-writer-" + std::to_string(process_id()) + "-" +
              std::to_string(now) + "-" +
              std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(path_, error)) {
#ifndef _WIN32
        fs::permissions(path_, fs::perms::owner_all, fs::perm_options::replace);
#endif
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw fs::filesystem_error(
            "cannot create temporary test directory", path_, error);
      }
    }
    throw std::runtime_error("cannot allocate temporary test directory");
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  [[nodiscard]] const fs::path& path() const noexcept {
    return path_;
  }

private:
  fs::path path_;
};

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string encoded = "\"";
  for (const char byte : value) {
    if (byte == '"' || byte == '\\') {
      encoded.push_back('\\');
    }
    encoded.push_back(byte);
  }
  encoded.push_back('"');
  return encoded;
}

[[nodiscard]] std::string path_utf8(const fs::path& path) {
  const std::u8string encoded = path.u8string();
  return std::string(
      reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

[[nodiscard]] ReplayChildConfig make_config(
    const fs::path& result_path,
    std::size_t action_count = 1U) {
  const fs::path user_root = result_path.parent_path() / "user root";
  std::string actions = "[";
  for (std::size_t index = 0; index < action_count; ++index) {
    if (index != 0U) {
      actions.push_back(',');
    }
    actions += "{\"ordinal\":" + std::to_string(index) +
        ",\"kind\":\"move_party\","
        "\"arguments\":{\"command\":\"north\"}}";
  }
  actions.push_back(']');
  const std::string json =
      "{"
      "\"schema_version\":1,"
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":\"semantic\","
      "\"presentation_mode\":\"remastered\","
      "\"user_data_root\":" + json_string(path_utf8(user_root)) + ","
      "\"user_data_root_policy\":\"set_once_before_toolbox_init\","
      "\"preferences_write_policy\":\"disabled\","
      "\"input_slot\":\"A\","
      "\"input_slot_policy\":"
      "\"user_data_root_only_no_bundled_fallback\","
      "\"output_slot\":\"B\","
      "\"output_slot_policy\":\"fresh_nonexistent\","
      "\"actions\":" + actions + ","
      "\"settlement_barrier\":\"next_semantic_gameplay_poll\","
      "\"result_path\":" + json_string(path_utf8(result_path)) + ","
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\""
      "}";
  return parse_child_config_v1(json);
}

[[nodiscard]] ReplayCompletedResult make_result() {
  return {
      .process_id = process_id(),
      .engine_identity = "Realmz \\\"test\\\"",
      .settled_action_count = 1,
      .state_sha256 = sha256("state"),
      .save_tree_sha256 = sha256("save"),
      .rng_draw_count = 7,
  };
}

[[nodiscard]] std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read result test file");
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, std::string_view value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output) {
    throw std::runtime_error("cannot write result test fixture");
  }
}

void test_exact_encoding() {
  TemporaryDirectory temporary;
  const ReplayChildConfig config =
      make_config(temporary.path() / "result.json");
  const ReplayCompletedResult result = make_result();
  const std::string encoded = encode_replay_child_result_v1(config, result);
  const std::string expected =
      "{\"schema_version\":1,"
      "\"run_id\":\"0123456789abcdef0123456789abcdef\","
      "\"child_nonce\":\"fedcba9876543210fedcba9876543210\","
      "\"replay_route\":\"semantic\","
      "\"presentation_mode\":\"remastered\","
      "\"process_id\":" + std::to_string(result.process_id) + ","
      "\"status\":\"completed\","
      "\"engine_identity\":\"Realmz \\\\\\\"test\\\\\\\"\","
      "\"settled_action_count\":1,"
      "\"state_sha256\":\"4ba69735ca53765ed6a709edb56c6ea2"
      "36b7193a3b29a6b390c346f0f4340e4e\","
      "\"save_tree_sha256\":\"157dca92e4250458339d4b835250d44c"
      "238f3355e1b7986195188ee434e9baff\","
      "\"rng_draw_count\":7,"
      "\"rng_seed\":\"0123456789abcdef\","
      "\"rng_stream\":\"fedcba9876543210\"}\n";
  ++checks_run;
  if (encoded != expected) {
    throw std::runtime_error(
        "encoded child result mismatch\nactual: " + encoded +
        "expected: " + expected);
  }
  CHECK(encoded.ends_with("}\n"));
}

void test_validation() {
  TemporaryDirectory temporary;
  const ReplayChildConfig config =
      make_config(temporary.path() / "result.json");

  ReplayCompletedResult result = make_result();
  result.process_id = 0;
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.process_id =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U;
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.engine_identity.clear();
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.engine_identity = " leading";
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.engine_identity = "trailing ";
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.engine_identity = "line\nfeed";
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.engine_identity = std::string(257U, 'x');
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.settled_action_count = 0;
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });
  result = make_result();
  result.rng_draw_count =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U;
  check_result_error([&] {
    static_cast<void>(encode_replay_child_result_v1(config, result));
  });

  result = make_result();
  result.process_id =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  result.engine_identity = std::string(256U, 'x');
  result.rng_draw_count =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  CHECK(!encode_replay_child_result_v1(config, result).empty());

  const ReplayChildConfig zero_config =
      make_config(temporary.path() / "zero-result.json", 0U);
  result = make_result();
  result.settled_action_count = 0U;
  CHECK(!encode_replay_child_result_v1(zero_config, result).empty());

  const ReplayChildConfig maximum_config = make_config(
      temporary.path() / "maximum-result.json", kMaximumReplayActions);
  result = make_result();
  result.settled_action_count = kMaximumReplayActions;
  CHECK(!encode_replay_child_result_v1(maximum_config, result).empty());
}

void test_private_exclusive_write() {
  TemporaryDirectory temporary;
  const fs::path result_path = temporary.path() / "result.json";
  const ReplayChildConfig config = make_config(result_path);
  const ReplayCompletedResult result = make_result();
  const std::string expected = encode_replay_child_result_v1(config, result);

  write_replay_child_result_v1(config, result);
  CHECK(read_file(result_path) == expected);
  CHECK(fs::is_regular_file(result_path));
#ifndef _WIN32
  struct stat status {};
  CHECK(::lstat(result_path.c_str(), &status) == 0);
  CHECK((status.st_mode & 0777) == 0600);
  CHECK(status.st_nlink == 1);
#endif
  check_result_error([&] {
    write_replay_child_result_v1(config, result);
  });
  CHECK(read_file(result_path) == expected);
}

void test_existing_special_leaves_are_not_followed() {
  {
    TemporaryDirectory temporary;
    const fs::path result_path = temporary.path() / "result.json";
    const ReplayChildConfig config = make_config(result_path);
    write_file(result_path, "existing");
    check_result_error([&] {
      write_replay_child_result_v1(config, make_result());
    });
    CHECK(read_file(result_path) == "existing");
  }

#ifndef _WIN32
  {
    TemporaryDirectory temporary;
    const fs::path result_path = temporary.path() / "result.json";
    const fs::path target_path = temporary.path() / "target.json";
    const ReplayChildConfig config = make_config(result_path);
    write_file(target_path, "target");
    CHECK(::symlink(target_path.c_str(), result_path.c_str()) == 0);
    check_result_error([&] {
      write_replay_child_result_v1(config, make_result());
    });
    CHECK(read_file(target_path) == "target");
  }
  {
    TemporaryDirectory temporary;
    const fs::path result_path = temporary.path() / "result.json";
    const ReplayChildConfig config = make_config(result_path);
    CHECK(::mkfifo(result_path.c_str(), 0600) == 0);
    check_result_error([&] {
      write_replay_child_result_v1(config, make_result());
    });
  }
#endif
}

void test_failed_creation_does_not_invent_a_leaf() {
  TemporaryDirectory temporary;
  const fs::path missing_parent = temporary.path() / "missing";
  const fs::path result_path = missing_parent / "result.json";
  const ReplayChildConfig config = make_config(result_path);
  check_result_error([&] {
    write_replay_child_result_v1(config, make_result());
  });
  CHECK(!fs::exists(result_path));
  CHECK(!fs::exists(missing_parent));
}

} // namespace

int main() {
  try {
    test_exact_encoding();
    test_validation();
    test_private_exclusive_write();
    test_existing_special_leaves_are_not_followed();
    test_failed_creation_does_not_invent_a_leaf();
    std::cout << "ReplayResultWriterTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayResultWriterTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
