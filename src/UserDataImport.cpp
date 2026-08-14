#include "UserDataImport.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>

#include "UserDataPaths.hpp"
#include "userdata/UserDataMigration.hpp"

extern "C" int RealmzImportLegacyUserData(const char* source_root) {
  if (!source_root || (source_root[0] == '\0')) {
    std::fprintf(stderr, "error: a legacy user-data directory is required\n");
    return 2;
  }

  const auto destination = realmz::app::remastered_user_data_root();
  if (!destination.has_value()) {
    std::fprintf(stderr,
        "error: could not resolve the Realmz Remastered user-data directory\n");
    return 2;
  }

  try {
    const auto report = realmz::userdata::import_legacy_user_data(
        std::filesystem::path(source_root), *destination);
    const auto imported = std::count_if(
        report.files.begin(), report.files.end(),
        [](const realmz::userdata::ImportedFile& file) {
          return file.result == realmz::userdata::FileImportResult::imported;
        });
    const auto skipped = report.files.size() -
        static_cast<std::size_t>(imported);
    std::fprintf(stdout,
        "%s: %zu file(s) imported, %zu existing file(s) preserved\n"
        "Verified backup: %s\nManifest: %s\n",
        report.status == realmz::userdata::MigrationRunStatus::already_completed
            ? "Import already verified"
            : "Import complete",
        static_cast<std::size_t>(imported), skipped,
        report.backup_directory.string().c_str(),
        report.manifest_path.string().c_str());
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: user-data import failed: %s\n", error.what());
    return 2;
  }
}
