#include "replay/ReplayChildConfig.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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
void check_config_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayConfigError&) {
    return;
  }
  throw std::runtime_error("expected ReplayConfigError");
}

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string result = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20U) {
          constexpr char digits[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(digits[character >> 4U]);
          result.push_back(digits[character & 0x0FU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] std::string path_utf8(const fs::path& path) {
  const std::u8string encoded = path.u8string();
  std::string result;
  result.reserve(encoded.size());
  for (const char8_t byte : encoded) {
    result.push_back(static_cast<char>(byte));
  }
  return result;
}

using Fields = std::vector<std::pair<std::string, std::string>>;

[[nodiscard]] std::string object_json(const Fields& fields) {
  std::string result = "{";
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0) {
      result.push_back(',');
    }
    result += json_string(fields[index].first);
    result.push_back(':');
    result += fields[index].second;
  }
  result.push_back('}');
  return result;
}

[[nodiscard]] std::string valid_actions_json() {
  return R"json([
    {"ordinal":0,"kind":"move_party","arguments":{
      "actor_id":4,"enabled":true,"label":"North gate"
    }},
    {"ordinal":1,"kind":"center","arguments":{}}
  ])json";
}

[[nodiscard]] Fields valid_fields() {
  const fs::path user_root =
      (fs::temp_directory_path() / fs::path(u8"R\u00e9almz Replay Root"))
          .lexically_normal();
  const fs::path result_path =
      (fs::temp_directory_path() / "realmz-replay-result.json")
          .lexically_normal();
  return {
      {"schema_version", "1"},
      {"run_id", "\"0123456789abcdef0123456789abcdef\""},
      {"child_nonce", "\"fedcba9876543210fedcba9876543210\""},
      {"replay_route", "\"semantic\""},
      {"presentation_mode", "\"remastered\""},
      {"user_data_root", json_string(path_utf8(user_root))},
      {"user_data_root_policy", "\"set_once_before_toolbox_init\""},
      {"preferences_write_policy", "\"disabled\""},
      {"input_slot", "\"A\""},
      {"input_slot_policy",
       "\"user_data_root_only_no_bundled_fallback\""},
      {"output_slot", "\"B\""},
      {"output_slot_policy", "\"fresh_nonexistent\""},
      {"actions", valid_actions_json()},
      {"settlement_barrier", "\"next_semantic_gameplay_poll\""},
      {"result_path", json_string(path_utf8(result_path))},
      {"rng_seed", "\"0123456789abcdef\""},
      {"rng_stream", "\"fedcba9876543210\""},
  };
}

void set_field(Fields& fields, std::string_view name, std::string value) {
  for (auto& [field_name, field_value] : fields) {
    if (field_name == name) {
      field_value = std::move(value);
      return;
    }
  }
  throw std::runtime_error("test field not found");
}

[[nodiscard]] std::string config_with(
    std::string_view name,
    std::string value) {
  Fields fields = valid_fields();
  set_field(fields, name, std::move(value));
  return object_json(fields);
}

void test_valid_config_and_immutable_accessors() {
  const ReplayChildConfig config = parse_child_config_v1(
      object_json(valid_fields()));
  CHECK(config.schema_version() == 1);
  CHECK(config.run_id() == "0123456789abcdef0123456789abcdef");
  CHECK(config.child_nonce() == "fedcba9876543210fedcba9876543210");
  CHECK(config.replay_route() == ReplayRoute::semantic);
  CHECK(config.presentation_mode() == ReplayPresentationMode::remastered);
  CHECK(config.user_data_root().is_absolute());
  CHECK(config.user_data_root().filename() == fs::path(u8"R\u00e9almz Replay Root"));
  CHECK(config.user_data_root_policy() ==
      ReplayUserDataRootPolicy::set_once_before_toolbox_init);
  CHECK(config.preferences_write_policy() ==
      ReplayPreferencesWritePolicy::disabled);
  CHECK(config.input_slot() == 'A');
  CHECK(config.input_slot_policy() ==
      ReplayInputSlotPolicy::user_data_root_only_no_bundled_fallback);
  CHECK(config.output_slot() == 'B');
  CHECK(config.output_slot_policy() ==
      ReplayOutputSlotPolicy::fresh_nonexistent);
  CHECK(config.actions().size() == 2);
  CHECK(config.actions()[0].ordinal == 0);
  CHECK(config.actions()[0].kind == "move_party");
  CHECK(std::get<std::int64_t>(config.actions()[0].arguments.at("actor_id")) == 4);
  CHECK(std::get<bool>(config.actions()[0].arguments.at("enabled")));
  CHECK(std::get<std::string>(config.actions()[0].arguments.at("label")) ==
      "North gate");
  CHECK(config.settlement_barrier() ==
      ReplaySettlementBarrier::next_semantic_gameplay_poll);
  CHECK(config.result_path().is_absolute());
  CHECK(config.rng_seed() == 0x0123456789ABCDEFULL);
  CHECK(config.rng_stream() == 0xFEDCBA9876543210ULL);
  CHECK(config.rng_seed_token() == "0123456789abcdef");
  CHECK(config.rng_stream_token() == "fedcba9876543210");
  CHECK(canonical_hex64(config.rng_seed()) == config.rng_seed_token());
  CHECK(to_string(config.replay_route()) == "semantic");
  CHECK(to_string(config.presentation_mode()) == "remastered");

  // Copying preserves shared immutable storage and its validated values.
  const ReplayChildConfig copy = config;
  CHECK(&copy.actions() == &config.actions());
  CHECK(copy.run_id() == config.run_id());
}

void test_explicit_schema_version_dispatch() {
  const std::string v1_json = object_json(valid_fields());
  CHECK(parse_child_config(v1_json).schema_version() == 1U);
  CHECK(parse_child_config_v1(v1_json).schema_version() == 1U);
  check_config_error([&] {
    static_cast<void>(parse_child_config_v2(v1_json));
  });

  Fields v2_fields = valid_fields();
  set_field(v2_fields, "schema_version", "2");
  const std::string v2_json = object_json(v2_fields);
  CHECK(parse_child_config(v2_json).schema_version() == 2U);
  CHECK(parse_child_config_v2(v2_json).schema_version() == 2U);
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(v2_json));
  });

  for (const std::string version : {"0", "3", "-1", "true", "\"2\""}) {
    check_config_error([&] {
      static_cast<void>(parse_child_config(
          config_with("schema_version", version)));
    });
  }
}

void test_strict_json_and_exact_fields() {
  for (const std::string invalid : {
           "",
           "null",
           "[]",
           "{} trailing",
           "{\"schema_version\":01}",
           "{\"schema_version\":NaN}",
           "{\"x\":\"\\uD800\"}",
           "{\"x\":\"\\uDC00\"}",
           "{\"x\":\"\\q\"}",
       }) {
    check_config_error([&] { static_cast<void>(parse_child_config_v1(invalid)); });
  }

  std::string invalid_utf8 = "{\"x\":\"";
  invalid_utf8.push_back(static_cast<char>(0xC0));
  invalid_utf8 += "\"}";
  check_config_error(
      [&] { static_cast<void>(parse_child_config_v1(invalid_utf8)); });

  Fields duplicate = valid_fields();
  duplicate.push_back(duplicate.front());
  check_config_error(
      [&] { static_cast<void>(parse_child_config_v1(object_json(duplicate))); });

  Fields unknown = valid_fields();
  unknown.emplace_back("unexpected", "true");
  check_config_error(
      [&] { static_cast<void>(parse_child_config_v1(object_json(unknown))); });

  const Fields complete = valid_fields();
  for (std::size_t omitted = 0; omitted < complete.size(); ++omitted) {
    Fields fields = complete;
    fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(omitted));
    check_config_error(
        [&] { static_cast<void>(parse_child_config_v1(object_json(fields))); });
  }

  std::string nested = "0";
  for (std::size_t depth = 0; depth < 40; ++depth) {
    nested = "[" + nested + "]";
  }
  check_config_error([&] { static_cast<void>(parse_child_config_v1(nested)); });

  const std::string oversized(kMaximumChildConfigBytes + 1, ' ');
  check_config_error(
      [&] { static_cast<void>(parse_child_config_v1(oversized)); });
}

void test_scalar_and_policy_validation() {
  for (const std::string value : {"0", "2", "1.0", "true", "\"1\""}) {
    check_config_error([&] {
      static_cast<void>(parse_child_config_v1(
          config_with("schema_version", value)));
    });
  }
  for (const std::string_view field : {"run_id", "child_nonce"}) {
    for (const std::string value : {
             "\"0123456789abcdef0123456789abcde\"",
             "\"0123456789ABCDEF0123456789ABCDEF\"",
             "\"g123456789abcdef0123456789abcdef\"",
             "32",
         }) {
      check_config_error([&] {
        static_cast<void>(parse_child_config_v1(config_with(field, value)));
      });
    }
  }
  for (const auto& invalid : {
           std::pair<std::string_view, std::string>{"replay_route", "\"other\""},
           {"replay_route", "false"},
           {"presentation_mode", "\"semantic\""},
           {"presentation_mode", "1"},
           {"user_data_root_policy", "\"mutable\""},
           {"preferences_write_policy", "\"enabled\""},
           {"input_slot_policy", "\"fallback\""},
           {"output_slot_policy", "\"overwrite\""},
           {"settlement_barrier", "\"immediate\""},
       }) {
    check_config_error([&] {
      static_cast<void>(parse_child_config_v1(
          config_with(invalid.first, invalid.second)));
    });
  }
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(
        config_with("presentation_mode", "\"classic\"")));
  });
  Fields classic_with_remastered = valid_fields();
  set_field(classic_with_remastered, "replay_route", "\"classic\"");
  check_config_error([&] {
    static_cast<void>(
        parse_child_config_v1(object_json(classic_with_remastered)));
  });
  for (const std::string_view field : {"input_slot", "output_slot"}) {
    for (const std::string value : {
             "\"\"", "\"a\"", "\"K\"", "\"AA\"", "1"}) {
      check_config_error([&] {
        static_cast<void>(parse_child_config_v1(config_with(field, value)));
      });
    }
  }
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(
        config_with("output_slot", "\"A\"")));
  });
  for (const std::string_view field : {"rng_seed", "rng_stream"}) {
    for (const std::string value : {
             "\"0123456789abcde\"",
             "\"0123456789ABCDEF\"",
             "\"0123456789abcdeg\"",
             "0",
         }) {
      check_config_error([&] {
        static_cast<void>(parse_child_config_v1(config_with(field, value)));
      });
    }
  }
}

void test_path_validation() {
  for (const std::string_view field : {"user_data_root", "result_path"}) {
    for (const std::string& value : {
             json_string("relative/path"),
             json_string("/tmp/../escape"),
             json_string("/tmp/./escape"),
             json_string("/tmp/trailing/"),
             json_string(std::string("/tmp/control\npath")),
             std::string("false"),
         }) {
      check_config_error([&] {
        static_cast<void>(parse_child_config_v1(config_with(field, value)));
      });
    }
    const std::string overlong = "/" + std::string(4096, 'x');
    check_config_error([&] {
      static_cast<void>(parse_child_config_v1(
          config_with(field, json_string(overlong))));
    });
  }
}

void test_action_validation_and_bounds() {
  for (const std::string actions : {
           "null",
           "{}",
           R"json([null])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{},"extra":1}])json",
           R"json([{"ordinal":0,"kind":"move"}])json",
           R"json([{"ordinal":1,"kind":"move","arguments":{}}])json",
           R"json([{"ordinal":-1,"kind":"move","arguments":{}}])json",
           R"json([{"ordinal":4096,"kind":"move","arguments":{}}])json",
           R"json([{"ordinal":0.0,"kind":"move","arguments":{}}])json",
           R"json([{"ordinal":false,"kind":"move","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"Move","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"1move","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"move_","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"move__party","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"move-party","arguments":{}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":[]}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"Bad":1}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":null}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":1.0}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":[]}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":{}}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":9007199254740992}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":-9007199254740992}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":"line\nfeed"}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":"caf\u00e9"}}])json",
           R"json([{"ordinal":0,"kind":"move","arguments":{"a":"\u007f"}}])json",
       }) {
    check_config_error([&] {
      static_cast<void>(parse_child_config_v1(config_with("actions", actions)));
    });
  }

  const std::string long_kind(65, 'a');
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(config_with(
        "actions",
        "[{\"ordinal\":0,\"kind\":" + json_string(long_kind) +
            ",\"arguments\":{} }]")));
  });
  const std::string long_value(1025, 'x');
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(config_with(
        "actions",
        "[{\"ordinal\":0,\"kind\":\"move\",\"arguments\":{\"a\":" +
            json_string(long_value) + "}}]")));
  });

  std::string too_many_arguments =
      "[{\"ordinal\":0,\"kind\":\"move\",\"arguments\":{";
  for (std::size_t index = 0; index < 33; ++index) {
    if (index != 0) {
      too_many_arguments.push_back(',');
    }
    too_many_arguments += "\"a" + std::to_string(index) + "\":true";
  }
  too_many_arguments += "}}]";
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(
        config_with("actions", too_many_arguments)));
  });

  std::string too_many_actions = "[";
  for (std::size_t index = 0; index < 4097; ++index) {
    if (index != 0) {
      too_many_actions.push_back(',');
    }
    too_many_actions += "{\"ordinal\":" + std::to_string(index) +
        ",\"kind\":\"move\",\"arguments\":{}}";
  }
  too_many_actions.push_back(']');
  check_config_error([&] {
    static_cast<void>(parse_child_config_v1(
        config_with("actions", too_many_actions)));
  });

  const std::string maximum_value(1024, '~');
  const ReplayChildConfig boundary = parse_child_config_v1(config_with(
      "actions",
      "[{\"ordinal\":0,\"kind\":\"move\",\"arguments\":{"
      "\"minimum\":-9007199254740991,\"maximum\":9007199254740991,"
      "\"text\":" + json_string(maximum_value) + "}}]"));
  CHECK(boundary.actions().size() == 1);
  CHECK(std::get<std::string>(boundary.actions()[0].arguments.at("text")).size() ==
      1024);
}

void test_file_loading_and_byte_bound() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path directory = fs::temp_directory_path() /
      ("realmz-replay-config-test-" + std::to_string(suffix));
  fs::create_directory(directory);
  struct Cleanup final {
    fs::path path;
    ~Cleanup() {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{directory};

  const fs::path valid_path = directory / "child-config.json";
  {
    std::ofstream output(valid_path, std::ios::binary);
    output << object_json(valid_fields());
  }
  CHECK(load_child_config_v1(valid_path).schema_version() == 1);
  CHECK(load_child_config(valid_path).schema_version() == 1);
  check_config_error([&] {
    static_cast<void>(load_child_config_v1(directory / "missing.json"));
  });
  check_config_error(
      [&] { static_cast<void>(load_child_config_v1(directory)); });

  const fs::path symlink_path = directory / "child-config-link.json";
  std::error_code symlink_error;
  fs::create_symlink(valid_path, symlink_path, symlink_error);
  if (!symlink_error) {
    check_config_error(
        [&] { static_cast<void>(load_child_config_v1(symlink_path)); });
  }

#if !defined(_WIN32)
  const fs::path fifo_path = directory / "child-config.fifo";
  if (mkfifo(fifo_path.c_str(), 0600) != 0) {
    throw std::runtime_error("could not create config FIFO regression fixture");
  }
  const auto fifo_start = std::chrono::steady_clock::now();
  check_config_error(
      [&] { static_cast<void>(load_child_config_v1(fifo_path)); });
  CHECK(std::chrono::steady_clock::now() - fifo_start <
      std::chrono::seconds(2));

  const fs::path mutating_path = directory / "mutating-config.json";
  {
    std::string contents = object_json(valid_fields());
    contents.resize(kMaximumChildConfigBytes - 1, ' ');
    std::ofstream output(mutating_path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }
  const int mutation_descriptor = open(mutating_path.c_str(), O_WRONLY);
  if (mutation_descriptor < 0) {
    throw std::runtime_error("could not open config mutation fixture");
  }
  std::atomic<bool> stop_mutating = false;
  std::atomic<bool> mutation_observed = false;
  std::atomic<bool> mutation_failed = false;
  std::thread mutator([&] {
    char value = '\t';
    while (!stop_mutating.load(std::memory_order_acquire)) {
      const ssize_t written = pwrite(
          mutation_descriptor,
          &value,
          1,
          static_cast<off_t>(kMaximumChildConfigBytes - 2));
      if (written != 1) {
        mutation_failed.store(true, std::memory_order_release);
        return;
      }
      mutation_observed.store(true, std::memory_order_release);
      value = value == '\t' ? ' ' : '\t';
    }
  });
  while (!mutation_observed.load(std::memory_order_acquire) &&
      !mutation_failed.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  bool mutation_rejected = false;
  try {
    static_cast<void>(load_child_config_v1(mutating_path));
  } catch (const ReplayConfigError&) {
    mutation_rejected = true;
  }
  stop_mutating.store(true, std::memory_order_release);
  mutator.join();
  static_cast<void>(close(mutation_descriptor));
  CHECK(!mutation_failed.load(std::memory_order_acquire));
  CHECK(mutation_rejected);
#endif

  const fs::path oversized_path = directory / "oversized.json";
  {
    std::ofstream output(oversized_path, std::ios::binary);
    const std::string block(64U * 1024U, ' ');
    for (std::size_t index = 0; index < 64; ++index) {
      output.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    output.put(' ');
  }
  check_config_error(
      [&] { static_cast<void>(load_child_config_v1(oversized_path)); });
}

} // namespace

int main() {
  try {
    test_valid_config_and_immutable_accessors();
    test_explicit_schema_version_dispatch();
    test_strict_json_and_exact_fields();
    test_scalar_and_policy_validation();
    test_path_validation();
    test_action_validation_and_bounds();
    test_file_loading_and_byte_bound();
    std::cout << "ReplayChildConfigTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayChildConfigTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
