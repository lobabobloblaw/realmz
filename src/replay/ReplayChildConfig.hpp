#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace realmz::replay {

inline constexpr std::size_t kMaximumChildConfigBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaximumReplayActions = 4096U;

enum class ReplayRoute {
  classic,
  semantic,
};

enum class ReplayPresentationMode {
  classic,
  remastered,
};

enum class ReplayUserDataRootPolicy {
  set_once_before_toolbox_init,
};

enum class ReplayPreferencesWritePolicy {
  disabled,
};

enum class ReplayInputSlotPolicy {
  user_data_root_only_no_bundled_fallback,
};

enum class ReplayOutputSlotPolicy {
  fresh_nonexistent,
};

enum class ReplaySettlementBarrier {
  next_semantic_gameplay_poll,
};

using ReplayArgumentValue = std::variant<std::string, std::int64_t, bool>;

struct ReplayAction final {
  std::uint16_t ordinal = 0;
  std::string kind;
  std::map<std::string, ReplayArgumentValue> arguments;

  bool operator==(const ReplayAction&) const = default;
};

class ReplayConfigError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// An immutable, validated representation of the private child protocol.
// Instances can only be produced by the strict parser below.
class ReplayChildConfig final {
public:
  ReplayChildConfig(const ReplayChildConfig&) noexcept = default;
  ReplayChildConfig(ReplayChildConfig&&) noexcept = default;
  ReplayChildConfig& operator=(const ReplayChildConfig&) noexcept = default;
  ReplayChildConfig& operator=(ReplayChildConfig&&) noexcept = default;
  ~ReplayChildConfig() = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept;
  [[nodiscard]] const std::string& run_id() const noexcept;
  [[nodiscard]] const std::string& child_nonce() const noexcept;
  [[nodiscard]] ReplayRoute replay_route() const noexcept;
  [[nodiscard]] ReplayPresentationMode presentation_mode() const noexcept;
  [[nodiscard]] const std::filesystem::path& user_data_root() const noexcept;
  [[nodiscard]] ReplayUserDataRootPolicy user_data_root_policy() const noexcept;
  [[nodiscard]] ReplayPreferencesWritePolicy preferences_write_policy() const noexcept;
  [[nodiscard]] char input_slot() const noexcept;
  [[nodiscard]] ReplayInputSlotPolicy input_slot_policy() const noexcept;
  [[nodiscard]] char output_slot() const noexcept;
  [[nodiscard]] ReplayOutputSlotPolicy output_slot_policy() const noexcept;
  [[nodiscard]] const std::vector<ReplayAction>& actions() const noexcept;
  [[nodiscard]] ReplaySettlementBarrier settlement_barrier() const noexcept;
  [[nodiscard]] const std::filesystem::path& result_path() const noexcept;
  [[nodiscard]] std::uint64_t rng_seed() const noexcept;
  [[nodiscard]] std::uint64_t rng_stream() const noexcept;
  [[nodiscard]] const std::string& rng_seed_token() const noexcept;
  [[nodiscard]] const std::string& rng_stream_token() const noexcept;

private:
  struct Storage;

  explicit ReplayChildConfig(std::shared_ptr<const Storage> storage) noexcept;

  std::shared_ptr<const Storage> storage_;

  friend ReplayChildConfig parse_child_config(std::string_view json);
};

// Parses one complete UTF-8 JSON document. The parser rejects duplicate keys,
// unknown or missing fields, unsupported policy values, malformed UTF-8,
// excessive nesting, and all schema/type/range violations. Printable ASCII is
// required for action string arguments so the native and parent validators
// have a dependency-free canonical text intersection.
[[nodiscard]] ReplayChildConfig parse_child_config_v1(std::string_view json);

// V2 retains the complete structural protocol and policies while selecting
// the native v2 action vocabulary and result contract.
[[nodiscard]] ReplayChildConfig parse_child_config_v2(std::string_view json);

// Startup dispatcher accepting exactly the supported schema versions. It
// never infers a vocabulary from action contents.
[[nodiscard]] ReplayChildConfig parse_child_config(std::string_view json);

// Loads at most kMaximumChildConfigBytes and then applies the same parser.
[[nodiscard]] ReplayChildConfig load_child_config_v1(
    const std::filesystem::path& path);
[[nodiscard]] ReplayChildConfig load_child_config_v2(
    const std::filesystem::path& path);
[[nodiscard]] ReplayChildConfig load_child_config(
    const std::filesystem::path& path);

[[nodiscard]] std::string canonical_hex64(std::uint64_t value);

[[nodiscard]] constexpr std::string_view to_string(ReplayRoute value) noexcept {
  switch (value) {
    case ReplayRoute::classic:
      return "classic";
    case ReplayRoute::semantic:
      return "semantic";
  }
  return "classic";
}

[[nodiscard]] constexpr std::string_view to_string(
    ReplayPresentationMode value) noexcept {
  switch (value) {
    case ReplayPresentationMode::classic:
      return "classic";
    case ReplayPresentationMode::remastered:
      return "remastered";
  }
  return "classic";
}

} // namespace realmz::replay
