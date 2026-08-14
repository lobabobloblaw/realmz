#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "userdata/UserDataMigration.hpp"

using namespace realmz::userdata;
namespace fs = std::filesystem;

namespace {

int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  checks_run++;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class TemporaryDirectory {
public:
  explicit TemporaryDirectory(std::string_view label) {
    static std::atomic<uint64_t> sequence = 0;
    const auto time_value = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
    for (size_t attempt = 0; attempt < 1024; attempt++) {
      this->path_ = fs::weakly_canonical(fs::temp_directory_path()) /
          ("realmz-userdata-test-" + std::string(label) + "-" +
              std::to_string(time_value) + "-" +
              std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(this->path_, error)) {
        return;
      }
      if (error && (error != std::errc::file_exists)) {
        throw std::runtime_error(
            "could not create temporary directory: " + error.message());
      }
    }
    throw std::runtime_error("could not allocate temporary directory");
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(this->path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] const fs::path& path() const noexcept {
    return this->path_;
  }

private:
  fs::path path_;
};

void write_file(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not create test file: " + path.string());
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output) {
    throw std::runtime_error("could not write test file: " + path.string());
  }
}

[[nodiscard]] std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not read test file: " + path.string());
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

struct TreeRecord {
  fs::file_type type = fs::file_type::none;
  fs::perms permissions = fs::perms::unknown;
  fs::file_time_type modified{};
  uintmax_t size = 0;
  std::string sha256;

  bool operator==(const TreeRecord&) const = default;
};

using TreeSnapshot = std::map<std::string, TreeRecord>;

[[nodiscard]] TreeSnapshot snapshot_tree(const fs::path& root) {
  TreeSnapshot result;
  if (!fs::exists(root)) {
    return result;
  }
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto status = entry.symlink_status();
    TreeRecord record{
        .type = status.type(),
        .permissions = status.permissions(),
        .modified = fs::last_write_time(entry.path()),
    };
    if (fs::is_regular_file(status)) {
      record.size = fs::file_size(entry.path());
      record.sha256 = compute_file_sha256(entry.path());
    } else if (fs::is_symlink(status)) {
      record.sha256 = fs::read_symlink(entry.path()).generic_string();
    }
    result.emplace(
        entry.path().lexically_relative(root).generic_string(),
        std::move(record));
  }
  return result;
}

[[nodiscard]] const ImportedFile& result_for(
    const UserDataMigrationReport& report,
    std::string_view relative_path) {
  const auto found = std::find_if(
      report.files.begin(), report.files.end(),
      [relative_path](const ImportedFile& file) {
        return file.source_relative_path.generic_string() == relative_path;
      });
  if (found == report.files.end()) {
    throw std::runtime_error("missing file result");
  }
  return *found;
}

void expect_migration_failure(
    const fs::path& source,
    const fs::path& destination) {
  bool failed = false;
  try {
    (void)import_legacy_user_data(source, destination);
  } catch (const std::exception&) {
    failed = true;
  }
  CHECK(failed);
}

void check_no_staging_files(const fs::path& root) {
  if (!fs::exists(root)) {
    return;
  }
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    CHECK(entry.path().filename().string().find(".realmz-import-") ==
        std::string::npos);
  }
}

void test_sha256_vectors() {
  TemporaryDirectory temporary("sha");
  const fs::path empty = temporary.path() / "empty";
  const fs::path abc = temporary.path() / "abc";
  const fs::path save_data = temporary.path() / "save-data";
  write_file(empty, "");
  write_file(abc, "abc");
  write_file(save_data, "save-data");
  CHECK(compute_file_sha256(empty) ==
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(compute_file_sha256(abc) ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(compute_file_sha256(save_data) ==
      "f94a09c1cc080aa8cca9b465292cf7de163fef90bc45b2ef9b7450a4b98b8e57");
}

void test_complete_import_and_backup_order() {
  TemporaryDirectory temporary("complete");
  const fs::path source = temporary.path() / "Legacy Realmz";
  const fs::path destination = temporary.path() / "Realmz Remastered";
  write_file(source / "Character Files" / "Ada", "character-record");
  write_file(source / "Save" / "Game A" / "Data G1", "save-data");
  write_file(source / "Unselected" / "secret", "do-not-import");
  const TreeSnapshot source_before = snapshot_tree(source);

  using TraceEntry = std::pair<MigrationCheckpoint, std::string>;
  std::vector<TraceEntry> trace;
  UserDataMigrationOptions options;
  options.checkpoint_hook = [&](
      MigrationCheckpoint checkpoint,
      const fs::path& relative_path) {
    trace.emplace_back(checkpoint, relative_path.generic_string());
    if (checkpoint == MigrationCheckpoint::before_destination_file) {
      CHECK(!fs::exists(destination / relative_path));
      const fs::path import_root = destination / "Import Backups";
      CHECK(fs::exists(import_root));
      size_t matching_backups = 0;
      for (const auto& import_entry : fs::directory_iterator(import_root)) {
        if (fs::exists(import_entry.path() / "Files" / relative_path)) {
          matching_backups++;
        }
      }
      CHECK(matching_backups == 1);
    }
  };

  const auto report = import_legacy_user_data(source, destination, options);
  CHECK(report.status == MigrationRunStatus::completed);
  CHECK(report.import_id.starts_with("import-"));
  CHECK(report.import_id.size() == 71);
  CHECK(report.files.size() == 2);
  CHECK(report.files[0].source_relative_path ==
      fs::path("Character Files/Ada"));
  CHECK(report.files[1].source_relative_path ==
      fs::path("Save/Game A/Data G1"));
  CHECK(fs::exists(report.manifest_path));
  CHECK(fs::exists(report.completion_marker_path));
  CHECK(read_file(destination / "Character Files" / "Ada") ==
      "character-record");
  CHECK(read_file(destination / "Save" / "Game A" / "Data G1") ==
      "save-data");
  CHECK(!fs::exists(destination / "Unselected"));
  CHECK(read_file(
      report.backup_directory / "Files" / "Character Files" / "Ada") ==
      "character-record");
  CHECK(read_file(
      report.backup_directory / "Files" / "Save" / "Game A" / "Data G1") ==
      "save-data");

  for (const auto& file : report.files) {
    CHECK(file.result == FileImportResult::imported);
    CHECK(compute_file_sha256(source / file.source_relative_path) == file.sha256);
    CHECK(compute_file_sha256(
              report.backup_directory / "Files" /
              file.source_relative_path) == file.sha256);
    CHECK(compute_file_sha256(destination / file.source_relative_path) ==
        file.sha256);
  }

  size_t backup_events = 0;
  bool saw_backups_complete = false;
  bool saw_destination = false;
  for (const auto& [checkpoint, relative] : trace) {
    (void)relative;
    if (checkpoint == MigrationCheckpoint::after_backup_file) {
      CHECK(!saw_destination);
      backup_events++;
    } else if (checkpoint == MigrationCheckpoint::backups_complete) {
      CHECK(backup_events == 2);
      CHECK(!saw_destination);
      saw_backups_complete = true;
    } else if (checkpoint == MigrationCheckpoint::before_destination_file) {
      CHECK(saw_backups_complete);
      saw_destination = true;
    }
  }
  CHECK(backup_events == 2);
  CHECK(saw_backups_complete);

  const std::string manifest = read_file(report.manifest_path);
  CHECK(manifest.starts_with(
      "source_relative_path\tsha256\tsize\tresult\n"));
  CHECK(manifest.find("Character%20Files/Ada\t") != std::string::npos);
  CHECK(manifest.find("Save/Game%20A/Data%20G1\t") != std::string::npos);
  CHECK(manifest.find("\t16\timported\n") != std::string::npos);
  CHECK(manifest.find("\t9\timported\n") != std::string::npos);
  CHECK(snapshot_tree(source) == source_before);
  check_no_staging_files(destination);

  // The destination is an independent copy, not a hard link to its backup.
  write_file(destination / "Character Files" / "Ada", "changed");
  CHECK(read_file(
      report.backup_directory / "Files" / "Character Files" / "Ada") ==
      "character-record");
  CHECK(snapshot_tree(source) == source_before);
}

void test_collisions_and_partial_existing_data() {
  TemporaryDirectory temporary("partial");
  const fs::path source = temporary.path() / "source";
  const fs::path destination = temporary.path() / "destination";
  write_file(source / "Character Files" / "New Hero", "new-character");
  write_file(source / "Save" / "Game A" / "Data G1", "source-save");
  write_file(destination / "Save" / "Game A" / "Data G1", "keep-me");

  const auto report = import_legacy_user_data(source, destination);
  CHECK(result_for(report, "Character Files/New Hero").result ==
      FileImportResult::imported);
  CHECK(result_for(report, "Save/Game A/Data G1").result ==
      FileImportResult::skipped_existing);
  CHECK(read_file(destination / "Character Files" / "New Hero") ==
      "new-character");
  CHECK(read_file(destination / "Save" / "Game A" / "Data G1") ==
      "keep-me");
  CHECK(read_file(
      report.backup_directory / "Files" / "Save" / "Game A" / "Data G1") ==
      "source-save");
  const std::string manifest = read_file(report.manifest_path);
  CHECK(manifest.find("Save/Game%20A/Data%20G1\t") != std::string::npos);
  CHECK(manifest.find("\t11\tskipped_existing\n") != std::string::npos);
}

void test_idempotent_marker_and_deterministic_manifest() {
  TemporaryDirectory temporary("idempotent");
  const fs::path source = temporary.path() / "source";
  const fs::path first_destination = temporary.path() / "destination-a";
  const fs::path second_destination = temporary.path() / "destination-b";
  write_file(source / "Character Files" / "Hero", "hero");
  write_file(source / "Save" / "Game B" / "Data A1", "state");

  const auto first = import_legacy_user_data(source, first_destination);
  // Imported data becomes live user data. A completed marker prevents a later
  // launch from replacing legitimate post-import changes.
  write_file(first_destination / "Save" / "Game B" / "Data A1", "live-state");
  const TreeSnapshot destination_before = snapshot_tree(first_destination);
  size_t repeat_hooks = 0;
  UserDataMigrationOptions options;
  options.checkpoint_hook = [&](MigrationCheckpoint, const fs::path&) {
    repeat_hooks++;
  };
  const auto repeated = import_legacy_user_data(
      source, first_destination, options);
  CHECK(repeated.status == MigrationRunStatus::already_completed);
  CHECK(repeated.import_id == first.import_id);
  CHECK(repeated.backup_directory == first.backup_directory);
  CHECK(repeated.files == first.files);
  CHECK(repeat_hooks == 0);
  CHECK(snapshot_tree(first_destination) == destination_before);

  const auto second = import_legacy_user_data(source, second_destination);
  CHECK(second.import_id == first.import_id);
  CHECK(read_file(second.manifest_path) == read_file(first.manifest_path));
  CHECK(read_file(second.completion_marker_path) ==
      read_file(first.completion_marker_path));
}

void test_root_and_symlink_rejection() {
  TemporaryDirectory temporary("safety");
  const fs::path source = temporary.path() / "source";
  const fs::path destination = temporary.path() / "destination";
  write_file(source / "Character Files" / "Hero", "hero");

  expect_migration_failure(source, source);
  expect_migration_failure(source, source / "nested destination");

  const fs::path source_alias = temporary.path() / "source-alias";
  fs::create_directory_symlink(source, source_alias);
  expect_migration_failure(source_alias, destination);
  CHECK(!fs::exists(destination));

  const fs::path source_ancestor = temporary.path() / "source-ancestor";
  const fs::path source_through_ancestor = source_ancestor / "legacy";
  write_file(
      source_through_ancestor / "Character Files" / "Ancestor Hero",
      "ancestor");
  const fs::path source_ancestor_alias =
      temporary.path() / "source-ancestor-alias";
  fs::create_directory_symlink(source_ancestor, source_ancestor_alias);
  expect_migration_failure(
      source_ancestor_alias / "legacy", destination);
  CHECK(!fs::exists(destination));

  const fs::path destination_ancestor =
      temporary.path() / "destination-ancestor";
  fs::create_directory(destination_ancestor);
  const fs::path destination_ancestor_alias =
      temporary.path() / "destination-ancestor-alias";
  fs::create_directory_symlink(
      destination_ancestor, destination_ancestor_alias);
  expect_migration_failure(
      source, destination_ancestor_alias / "new destination");
  CHECK(!fs::exists(destination_ancestor / "new destination"));

  const fs::path traversing_source =
      temporary.path() / "unused" / ".." / "source";
  expect_migration_failure(traversing_source, destination);
  CHECK(!fs::exists(destination));

  const fs::path outside = temporary.path() / "outside";
  write_file(outside / "stolen", "outside-data");
  fs::create_symlink(
      outside / "stolen",
      source / "Character Files" / "Linked Hero");
  expect_migration_failure(source, destination);
  CHECK(!fs::exists(destination));
  fs::remove(source / "Character Files" / "Linked Hero");

  fs::create_directory(destination);
  fs::create_directory_symlink(outside, destination / "Save");
  write_file(source / "Save" / "Game C" / "Data", "save");
  const TreeSnapshot outside_before = snapshot_tree(outside);
  expect_migration_failure(source, destination);
  CHECK(snapshot_tree(outside) == outside_before);
  CHECK(fs::is_symlink(fs::symlink_status(destination / "Save")));
  CHECK(!fs::exists(destination / "Character Files" / "Hero"));
  CHECK(!fs::exists(destination / "Import Backups"));
}

void test_failure_cleanup_and_source_immutability() {
  TemporaryDirectory temporary("rollback");
  const fs::path source = temporary.path() / "source";
  const fs::path destination =
      temporary.path() / "new parent" / "deeper" / "destination";
  write_file(source / "Character Files" / "A", "alpha");
  write_file(source / "Save" / "Game D" / "Data", "delta");
  const TreeSnapshot source_before = snapshot_tree(source);

  size_t completed_destinations = 0;
  UserDataMigrationOptions options;
  options.checkpoint_hook = [&](
      MigrationCheckpoint checkpoint,
      const fs::path&) {
    if (checkpoint == MigrationCheckpoint::after_destination_file) {
      completed_destinations++;
      if (completed_destinations == 1) {
        throw std::runtime_error("injected failure");
      }
    }
  };

  bool failed = false;
  try {
    (void)import_legacy_user_data(source, destination, options);
  } catch (const std::runtime_error& error) {
    failed = std::string(error.what()) == "injected failure";
  }
  CHECK(failed);
  CHECK(completed_destinations == 1);
  CHECK(snapshot_tree(source) == source_before);
  CHECK(!fs::exists(destination / "Character Files" / "A"));
  CHECK(!fs::exists(destination / "Save" / "Game D" / "Data"));
  CHECK(!fs::exists(destination / "Import Backups"));
  CHECK(!fs::exists(temporary.path() / "new parent"));
  check_no_staging_files(destination);

  const auto recovered = import_legacy_user_data(source, destination);
  CHECK(recovered.status == MigrationRunStatus::completed);
  CHECK(read_file(destination / "Character Files" / "A") == "alpha");
  CHECK(read_file(destination / "Save" / "Game D" / "Data") == "delta");
  CHECK(snapshot_tree(source) == source_before);
}

void test_failure_cleanup_at_each_transaction_phase() {
  constexpr std::array checkpoints{
      MigrationCheckpoint::after_backup_file,
      MigrationCheckpoint::backups_complete,
      MigrationCheckpoint::before_destination_file,
      MigrationCheckpoint::before_commit,
  };

  for (size_t index = 0; index < checkpoints.size(); index++) {
    TemporaryDirectory temporary("phase-" + std::to_string(index));
    const fs::path source = temporary.path() / "source";
    const fs::path destination = temporary.path() / "destination";
    write_file(source / "Character Files" / "Hero", "hero");
    const TreeSnapshot source_before = snapshot_tree(source);
    bool injected = false;
    UserDataMigrationOptions options;
    options.checkpoint_hook = [&](
        MigrationCheckpoint checkpoint,
        const fs::path&) {
      if (!injected && (checkpoint == checkpoints[index])) {
        injected = true;
        throw std::runtime_error("phase failure");
      }
    };

    bool failed = false;
    try {
      (void)import_legacy_user_data(source, destination, options);
    } catch (const std::runtime_error& error) {
      failed = std::string(error.what()) == "phase failure";
    }
    CHECK(injected);
    CHECK(failed);
    CHECK(snapshot_tree(source) == source_before);
    CHECK(!fs::exists(destination));
  }
}

void test_backup_is_revalidated_before_commit() {
  TemporaryDirectory temporary("commit-integrity");
  const fs::path source = temporary.path() / "source";
  const fs::path destination = temporary.path() / "destination";
  const fs::path relative = fs::path("Character Files") / "Existing Hero";
  write_file(source / relative, "source-hero");
  write_file(destination / relative, "keep-existing");

  UserDataMigrationOptions options;
  options.checkpoint_hook = [&](
      MigrationCheckpoint checkpoint,
      const fs::path&) {
    if (checkpoint != MigrationCheckpoint::backups_complete) {
      return;
    }
    const fs::path backup_root = destination / "Import Backups";
    size_t changed = 0;
    for (const auto& import : fs::directory_iterator(backup_root)) {
      const fs::path backup = import.path() / "Files" / relative;
      if (fs::exists(backup)) {
        write_file(backup, "corrupted-after-verification");
        changed++;
      }
    }
    CHECK(changed == 1);
  };

  bool failed = false;
  try {
    (void)import_legacy_user_data(source, destination, options);
  } catch (const UserDataMigrationError&) {
    failed = true;
  }
  CHECK(failed);
  CHECK(read_file(destination / relative) == "keep-existing");
  CHECK(!fs::exists(destination / "Import Backups"));
  check_no_staging_files(destination);
}

void test_corrupt_marker_is_not_trusted() {
  TemporaryDirectory temporary("marker");
  const fs::path source = temporary.path() / "source";
  const fs::path destination = temporary.path() / "destination";
  write_file(source / "Character Files" / "Hero", "hero");
  const auto first = import_legacy_user_data(source, destination);
  write_file(first.completion_marker_path, "forged marker\n");
  const TreeSnapshot before = snapshot_tree(destination);
  expect_migration_failure(source, destination);
  CHECK(snapshot_tree(destination) == before);
}

void test_damaged_or_redirected_backup_is_not_trusted() {
  TemporaryDirectory temporary("backup-integrity");
  const fs::path source = temporary.path() / "source";
  const fs::path destination = temporary.path() / "destination";
  write_file(source / "Character Files" / "Hero", "hero");
  const auto first = import_legacy_user_data(source, destination);
  const fs::path backup =
      first.backup_directory / "Files" / "Character Files" / "Hero";
  write_file(backup, "damaged");
  const TreeSnapshot before = snapshot_tree(destination);
  expect_migration_failure(source, destination);
  CHECK(snapshot_tree(destination) == before);

  fs::remove(backup);
  const fs::path outside = temporary.path() / "outside";
  write_file(outside, "hero");
  fs::create_symlink(outside, backup);
  const TreeSnapshot redirected_before = snapshot_tree(destination);
  expect_migration_failure(source, destination);
  CHECK(snapshot_tree(destination) == redirected_before);
}

} // namespace

int main() {
  try {
    test_sha256_vectors();
    test_complete_import_and_backup_order();
    test_collisions_and_partial_existing_data();
    test_idempotent_marker_and_deterministic_manifest();
    test_root_and_symlink_rejection();
    test_failure_cleanup_and_source_immutability();
    test_failure_cleanup_at_each_transaction_phase();
    test_backup_is_revalidated_before_commit();
    test_corrupt_marker_is_not_trusted();
    test_damaged_or_redirected_backup_is_not_trusted();
    std::cout << "UserDataMigrationTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "UserDataMigrationTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
