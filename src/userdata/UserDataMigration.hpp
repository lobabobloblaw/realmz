#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace realmz::userdata {

enum class FileImportResult {
  imported,
  skipped_existing,
};

enum class MigrationRunStatus {
  completed,
  already_completed,
};

// Checkpoints are primarily intended for deterministic tests and diagnostics.
// Throwing from a checkpoint aborts the import and exercises the same rollback
// path as an I/O error.
enum class MigrationCheckpoint {
  after_backup_file,
  backups_complete,
  before_destination_file,
  after_destination_file,
  before_commit,
};

struct ImportedFile {
  std::filesystem::path source_relative_path;
  std::string sha256;
  uintmax_t size = 0;
  FileImportResult result = FileImportResult::imported;

  bool operator==(const ImportedFile&) const = default;
};

struct UserDataMigrationOptions {
  std::function<void(
      MigrationCheckpoint,
      const std::filesystem::path& source_relative_path)> checkpoint_hook;
};

struct UserDataMigrationReport {
  MigrationRunStatus status = MigrationRunStatus::completed;
  std::string import_id;
  std::filesystem::path backup_directory;
  std::filesystem::path manifest_path;
  std::filesystem::path completion_marker_path;
  std::vector<ImportedFile> files;
};

class UserDataMigrationError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Imports only the "Character Files" and "Save" trees below source_root.
// source_root is opened read-only. destination_root must be a distinct,
// non-overlapping root; existing destination files are recorded and skipped.
[[nodiscard]] UserDataMigrationReport import_legacy_user_data(
    const std::filesystem::path& source_root,
    const std::filesystem::path& destination_root,
    const UserDataMigrationOptions& options = {});

// Exposed for independent verification of manifests and copied files.
[[nodiscard]] std::string compute_file_sha256(
    const std::filesystem::path& path);

[[nodiscard]] const char* to_string(FileImportResult result) noexcept;

} // namespace realmz::userdata
