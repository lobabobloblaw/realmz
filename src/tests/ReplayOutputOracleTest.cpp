#include "replay/ReplayOutputOracle.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
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
void check_output_error(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const ReplayOutputError&) {
    return;
  }
  throw std::runtime_error("expected ReplayOutputError");
}

template <typename Function>
void check_invalid_argument(Function&& function) {
  ++checks_run;
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("expected std::invalid_argument");
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
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
      path_ = fs::temp_directory_path() /
          ("realmz-output-oracle-" + std::to_string(process_id()) + "-" +
              std::to_string(now) + "-" +
              std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(path_, error)) {
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw fs::filesystem_error(
            "cannot create temporary test directory", path_, error);
      }
    }
    throw std::runtime_error("cannot allocate unique temporary test directory");
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] const fs::path& path() const noexcept {
    return path_;
  }

private:
  fs::path path_;
};

[[nodiscard]] std::array<std::string, kReplayOutputFileNames.size()>
sample_contents() {
  std::array<std::string, kReplayOutputFileNames.size()> result;
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] =
        std::string(index + 1, static_cast<char>('A' + index));
    result[index].push_back('\0');
    result[index].append(kReplayOutputFileNames[index]);
  }
  result[8] = std::string(kReplayOutputDataI1Bytes, 'I');
  return result;
}

void write_bytes(const fs::path& path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot open test file for writing");
  }
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!stream) {
    throw std::runtime_error("cannot write test file");
  }
}

[[nodiscard]] bool create_test_symlink(
    const fs::path& target,
    const fs::path& link,
    bool directory) {
  std::error_code error;
  if (directory) {
    fs::create_directory_symlink(target, link, error);
  } else {
    fs::create_symlink(target, link, error);
  }
  if (!error) {
    ++checks_run;
    return true;
  }
#ifdef _WIN32
  // Windows may require Developer Mode or SeCreateSymbolicLinkPrivilege.
  // Skip only that unavailable test mechanism; unexpected failures remain
  // fatal and the production reparse-point checks are still source-reviewed.
  if ((error.value() == ERROR_PRIVILEGE_NOT_HELD) ||
      (error.value() == ERROR_ACCESS_DENIED) ||
      (error == std::errc::operation_not_permitted) ||
      (error == std::errc::permission_denied)) {
    return false;
  }
#endif
  throw fs::filesystem_error("cannot create test symbolic link", link, error);
}

struct SampleTree final {
  TemporaryDirectory temporary;
  fs::path root;
  fs::path slot;
  std::array<std::string, kReplayOutputFileNames.size()> contents =
      sample_contents();

  explicit SampleTree(bool reverse_creation_order = false) {
    root = temporary.path() / fs::path(u8"Donn\u00e9es Realmz \U0001f332");
    slot = root / "Save" / "Game B";
    fs::create_directories(slot);
    std::vector<std::size_t> order(kReplayOutputFileNames.size());
    std::iota(order.begin(), order.end(), 0);
    if (reverse_creation_order) {
      std::reverse(order.begin(), order.end());
    }
    for (const std::size_t index : order) {
      write_bytes(slot / kReplayOutputFileNames[index], contents[index]);
    }
  }
};

[[nodiscard]] std::uint64_t total_sample_bytes(
    const std::array<std::string, kReplayOutputFileNames.size()>& contents) {
  return std::accumulate(
      contents.begin(),
      contents.end(),
      std::uint64_t{0},
      [](std::uint64_t total, const std::string& value) {
        return total + value.size();
      });
}

void test_valid_tree_and_protocol_digest() {
  SampleTree first;
  const VerifiedReplayOutput verified =
      verify_replay_output_slot(first.root, 'B');
  CHECK(verified.total_bytes == total_sample_bytes(first.contents));
  for (std::size_t index = 0; index < verified.files.size(); ++index) {
    CHECK(verified.files[index].relative_name ==
        kReplayOutputFileNames[index]);
    CHECK(verified.files[index].byte_size == first.contents[index].size());
    CHECK(verified.files[index].content_sha256 == sha256(first.contents[index]));
  }

  // Independently generated from the framing documented in
  // ReplayOutputOracle.hpp. This is a protocol vector, not a platform value.
  CHECK(
      sha256_hex(verified.tree_sha256) ==
      "8a2c944725d30afcd92e3a06edcc053b"
      "b32028f0abf364d83d6b436bd184ffaa");

  SampleTree reverse(true);
  const VerifiedReplayOutput reversed =
      verify_replay_output_slot(reverse.root, 'B');
  CHECK(reversed == verified);
}

void test_content_and_size_mutations_change_the_digest() {
  SampleTree tree;
  const VerifiedReplayOutput original =
      verify_replay_output_slot(tree.root, 'B');

  std::string same_size = tree.contents[3];
  std::fill(same_size.begin(), same_size.end(), 'z');
  write_bytes(tree.slot / "Data D1", same_size);
  const VerifiedReplayOutput changed =
      verify_replay_output_slot(tree.root, 'B');
  CHECK(changed.total_bytes == original.total_bytes);
  CHECK(changed.files[3].byte_size == original.files[3].byte_size);
  CHECK(changed.files[3].content_sha256 != original.files[3].content_sha256);
  CHECK(changed.tree_sha256 != original.tree_sha256);

  write_bytes(tree.slot / "Data D1", "shorter");
  const VerifiedReplayOutput resized =
      verify_replay_output_slot(tree.root, 'B');
  CHECK(resized.total_bytes != original.total_bytes);
  CHECK(resized.files[3].byte_size == 7);
  CHECK(resized.tree_sha256 != changed.tree_sha256);
}

void test_missing_extra_and_non_regular_entries_are_rejected() {
  SampleTree tree;
  fs::remove(tree.slot / "Data C1");
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });
  write_bytes(tree.slot / "Data C1", tree.contents[2]);

  write_bytes(tree.slot / ".unexpected", "extra");
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });
  fs::remove(tree.slot / ".unexpected");

  write_bytes(
      tree.slot / "Data I1", std::string(kReplayOutputDataI1Bytes - 1, 'I'));
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });
  write_bytes(tree.slot / "Data I1", tree.contents[8]);

  fs::remove(tree.slot / "Data H1");
  fs::create_directory(tree.slot / "Data H1");
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });

#ifndef _WIN32
  fs::remove_all(tree.slot / "Data H1");
  const fs::path fifo = tree.slot / "Data H1";
  CHECK(::mkfifo(fifo.c_str(), 0600) == 0);
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });
#endif
}

void test_links_and_exact_physical_names_are_rejected() {
  {
    SampleTree tree;
    fs::remove(tree.slot / "Data A1");
    if (create_test_symlink("Data B1", tree.slot / "Data A1", false)) {
      check_output_error([&] {
        static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
      });
    }
  }
  {
    SampleTree tree;
    fs::remove(tree.slot / "Data A1");
    std::error_code error;
    fs::create_hard_link(tree.slot / "Data B1", tree.slot / "Data A1", error);
    CHECK(!error);
    check_output_error([&] {
      static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
    });
  }
  {
    SampleTree tree;
    fs::rename(tree.slot / "Data A1", tree.slot / "data a1");
    check_output_error([&] {
      static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
    });
  }
  {
    SampleTree tree;
    const fs::path physical_slot = tree.root / "Save" / "Physical Slot";
    fs::rename(tree.slot, physical_slot);
    if (create_test_symlink("Physical Slot", tree.slot, true)) {
      check_output_error([&] {
        static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
      });
    }
  }
  {
    SampleTree tree;
    const fs::path physical_root = tree.temporary.path() / "Physical Root";
    fs::rename(tree.root, physical_root);
    if (create_test_symlink(physical_root.filename(), tree.root, true)) {
      check_output_error([&] {
        static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
      });
    }
  }
  {
    SampleTree tree;
    const fs::path save = tree.root / "Save";
    const fs::path physical_save = tree.root / "Physical Save";
    fs::rename(save, physical_save);
    if (create_test_symlink("Physical Save", save, true)) {
      check_output_error([&] {
        static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
      });
    }
  }
}

void test_byte_limits_are_strict_and_inclusive() {
  SampleTree tree;
  const std::uint64_t total = total_sample_bytes(tree.contents);
  const std::uint64_t largest = std::max_element(
      tree.contents.begin(),
      tree.contents.end(),
      [](const std::string& left, const std::string& right) {
        return left.size() < right.size();
      })->size();

  const ReplayOutputLimits exact{
      .maximum_file_bytes = largest,
      .maximum_total_bytes = total,
  };
  CHECK(verify_replay_output_slot(tree.root, 'B', exact).total_bytes == total);

  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(
        tree.root,
        'B',
        ReplayOutputLimits{
            .maximum_file_bytes = largest - 1,
            .maximum_total_bytes = total,
        }));
  });
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(
        tree.root,
        'B',
        ReplayOutputLimits{
            .maximum_file_bytes = largest,
            .maximum_total_bytes = total - 1,
        }));
  });
  CHECK(verify_replay_output_slot(
            tree.root,
            'B',
            ReplayOutputLimits{
                .maximum_file_bytes = total + 1,
                .maximum_total_bytes = total,
            })
            .total_bytes == total);
}

void test_slot_path_validation_and_parent_case() {
  SampleTree tree;
  CHECK(replay_output_slot_path(tree.root, 'A') ==
      tree.root / "Save" / "Game A");
  CHECK(replay_output_slot_path(tree.root, 'J') ==
      tree.root / "Save" / "Game J");
  check_invalid_argument([&] {
    static_cast<void>(replay_output_slot_path("relative", 'B'));
  });
  check_invalid_argument([&] {
    static_cast<void>(replay_output_slot_path(tree.root / "..", 'B'));
  });
  check_invalid_argument([&] {
    static_cast<void>(replay_output_slot_path(tree.root, 'b'));
  });
  check_invalid_argument([&] {
    static_cast<void>(replay_output_slot_path(tree.root, 'K'));
  });

  fs::rename(tree.slot, tree.root / "Save" / "game b");
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(tree.root, 'B'));
  });

  SampleTree save_case;
  fs::rename(save_case.root / "Save", save_case.root / "save");
  check_output_error([&] {
    static_cast<void>(verify_replay_output_slot(save_case.root, 'B'));
  });
}

} // namespace

int main() {
  try {
    test_valid_tree_and_protocol_digest();
    test_content_and_size_mutations_change_the_digest();
    test_missing_extra_and_non_regular_entries_are_rejected();
    test_links_and_exact_physical_names_are_rejected();
    test_byte_limits_are_strict_and_inclusive();
    test_slot_path_validation_and_parent_case();
    std::cout << "ReplayOutputOracleTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayOutputOracleTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
