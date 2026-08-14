#include "FileManager.h"

#include <SDL3/SDL_storage.h>
#include <algorithm>
#include <filesystem>
#include <phosg/Strings.hh>

#include "StringConvert.hpp"
#include "UserDataPaths.hpp"
#include "userdata/UserDataPathPolicy.hpp"

static phosg::PrefixedLogger fm_log("[FileManager] ");

static const std::string& user_basepath() {
  static const std::string path = [] {
    const auto root = realmz::app::remastered_user_data_root();
    if (!root.has_value()) {
      throw std::runtime_error(
          "Could not resolve the Realmz Remastered user-data directory");
    }
    return root->string();
  }();
  return path;
}

std::string normalize_mac_path(const std::string& mac_path, bool implicitly_local) {
  return realmz::userdata::safe_relative_path_for_classic_path(
      mac_path, implicitly_local).string();
}

std::string userdata_filename_for_mac_filename(const std::string& mac_path, bool implicitly_local = false) {
  return realmz::userdata::confined_path_below_root(
      user_basepath(), normalize_mac_path(mac_path, implicitly_local)).string();
}

std::string
host_filename_for_mac_filename(const std::string& mac_path, bool implicitly_local) {
  std::string ret = normalize_mac_path(mac_path, implicitly_local);

  auto base_path = SDL_GetBasePath();
  if (!base_path) {
    fm_log.error_f("Failed to get SDL base path: {}", SDL_GetError());
    return "";
  }

  return realmz::userdata::confined_path_below_root(base_path, ret).string();
}

std::string host_filename_for_FSSpec(const FSSpec* fsp) {
  // We only support relative paths (see above) and only references to the
  // default volume and no directory
  if (fsp->vRefNum != 0) {
    throw std::runtime_error(std::format("FSSpec vRefNum is not zero (received {})", fsp->vRefNum));
  }
  if ((fsp->parID != 0) && (fsp->parID != 1)) {
    throw std::runtime_error(std::format("FSSpec parID is not 0 or 1 (received {})", fsp->parID));
  }

  // Most uses of FSSpecs in Realmz have a parID of 0. The only time it is 1 is
  // in pref.c, where FindFolder is called to locate the System Preferences folder.
  // To signify this, FindFolder sets parID to the 1 sentinel value. In this case,
  // we want to use the userdata folder for storing preferences. Otherwise, we should
  // use the base directory of the executable to load game resource files.
  if (fsp->parID == 1) {
    return userdata_filename_for_mac_filename(string_for_pstr<64>(fsp->name), true);
  } else {
    return host_filename_for_mac_filename(string_for_pstr<64>(fsp->name), false);
  }
}

OSErr GetVInfo(int16_t drvNum, StringPtr volName, int16_t* vRefNum, int32_t* freeBytes) {
  fm_log.info_f("Volume info requested for drive {}", drvNum);

  // Return fake volume info
  strcpy(reinterpret_cast<char*>(volName), "Macintosh HD");
  *vRefNum = 0;
  *freeBytes = 1024 * 1024 * 1024; // 1GB
  return noErr;
}

void GetFInfo(const Str63 fName, int16_t vRefNum, FInfo* fInfo) {
  auto filename = string_for_pstr<64>(fName);
  fm_log.info_f("Finder info requested for file {} (on volume {})", filename, vRefNum);

  // Return fake Finder info (Realmz doesn't use it anyway)
  fInfo->fdType = 0x31313131; // '1111'
  fInfo->fdCreator = 0x31313131; // '1111'
  fInfo->fdFlags = 0;
  fInfo->fdLocation.h = 0;
  fInfo->fdLocation.v = 0;
  fInfo->fdFldr = 0;
}

OSErr FSpGetFInfo(const FSSpec* spec, FInfo* fndrInfo) {
  auto filename = string_for_pstr<64>(spec->name);
  fm_log.info_f("Finder info requested for file {} (on volume {}) via FSSpec", filename, spec->vRefNum);
  GetFInfo(spec->name, spec->vRefNum, fndrInfo);
  return 0;
}

OSErr FSpSetFInfo(const FSSpec* spec, const FInfo* fndrInfo) {
  auto filename = string_for_pstr<64>(spec->name);
  fm_log.info_f("Skipping Finder info write for file {} (on volume {}): type={:08X} creator={:08X} flags={:04X} loc.h={} loc.v={} folder={}",
      filename,
      spec->vRefNum,
      fndrInfo->fdType,
      fndrInfo->fdCreator,
      fndrInfo->fdFlags,
      fndrInfo->fdLocation.h,
      fndrInfo->fdLocation.v,
      fndrInfo->fdFldr);
  // Ignore writes of Finder info
  return 0;
}

OSErr FSpDelete(const FSSpec* spec) {
  auto filename = string_for_pstr<64>(spec->name);
  fm_log.info_f("Skipping delete of file {} (on volume {})", filename, spec->vRefNum);
  // TODO: We probably should have an allow-list of files that can be safely
  // deleted, instead of just ignoring all deletes.
  return 0;
}

OSErr FSMakeFSSpec(int16_t vRefNum, int32_t dirID, ConstStr255Param fileName, FSSpecPtr spec) {
  memcpy(spec->name, fileName, fileName[0] + 1);
  spec->vRefNum = vRefNum;
  spec->parID = dirID;
  return 0;
}

OSErr FindFolder(int16_t vRefNum, OSType folderType, Boolean createFolder, int16_t* foundVRefNum, int32_t* foundDirID) {
  // This is only used by Realmz for getting the Preferences folder (which in
  // Classic Mac OS is within the System Folder). Here, we just use the userdata
  // directory instead, and we signal this by setting the directory ID to 1.
  *foundVRefNum = 0;
  *foundDirID = 1;
  return noErr;
}

FILE* mac_fopen(const char* filename, const char* mode) {
  // It seems some codepath in pref.c calls fopen(""). This is never valid (a
  // file cannot have an empty name) so just immediately fail in that case.
  if (!filename || !mode || (filename[0] == '\0') || (mode[0] == '\0')) {
    return nullptr;
  }

  std::string user_filename = userdata_filename_for_mac_filename(filename);
  std::string host_filename{};

  std::error_code status_error;
  const auto user_status = std::filesystem::symlink_status(
      user_filename, status_error);
  if (status_error &&
      (status_error != std::errc::no_such_file_or_directory)) {
    fm_log.error_f("Could not inspect user file {}: {}", user_filename,
        status_error.message());
    return nullptr;
  }
  const bool user_file_exists =
      !status_error && std::filesystem::exists(user_status);
  if (user_file_exists && std::filesystem::is_symlink(user_status)) {
    fm_log.error_f("Refusing user-data symlink {}", user_filename);
    return nullptr;
  }

  const bool write_capable = realmz::userdata::fopen_mode_can_write(mode);

  // Every write-capable mode is confined to user storage. Read-only opens use
  // a user override when present and otherwise fall back to bundled data.
  if (realmz::userdata::should_open_from_user_storage(
          mode, user_file_exists)) {
    host_filename = user_filename;

    // Ensure all parent directories exist
    std::filesystem::path host_path{host_filename};
    if (!SDL_CreateDirectory(host_path.parent_path().string().c_str())) {
      fm_log.error_f("Could not create user-data directory {}: {}",
          host_path.parent_path().string(), SDL_GetError());
      return nullptr;
    }

    // Modes that preserve existing content need a private copy when the file
    // currently exists only in the read-only application resources. A mode
    // beginning with 'w' truncates or creates instead and must not be seeded.
    if (write_capable && !user_file_exists && (mode[0] != 'w')) {
      const std::string bundled_filename =
          host_filename_for_mac_filename(filename, false);
      std::error_code bundled_error;
      if (std::filesystem::is_regular_file(
              bundled_filename, bundled_error) && !bundled_error) {
        std::error_code copy_error;
        std::filesystem::copy_file(
            bundled_filename, user_filename,
            std::filesystem::copy_options::none, copy_error);
        if (copy_error &&
            !std::filesystem::is_regular_file(user_filename)) {
          fm_log.error_f("Could not seed writable user copy {} from {}: {}",
              user_filename, bundled_filename, copy_error.message());
          return nullptr;
        }
      }
    }
  } else {
    // Otherwise, fall back to reading the file from the Realmz application directory
    host_filename = host_filename_for_mac_filename(filename, false);
  }

  // Sometimes bugs may cause Realmz to try to open a directory. For example,
  // when you click on an empty slot in the party select list, it will call
  // fopen with ":Character Files:". Opening directories this way is an
  // implementation detail; the usual pattern is to call opendir + readdir
  // instead of fopen, but fopen still succeeds on some Unix-like systems. To
  // handle this, we need to check if the file is a directory and not try to
  // open it if so, which emulates the Classic Mac OS behavior.
  if (std::filesystem::is_directory(host_filename)) {
    fm_log.info_f("Cannot open file {} (host: {}) because it is a directory", filename, host_filename);
    return nullptr;
  }

  fm_log.info_f("Opening file {} (host: {}) with mode {}", filename, host_filename, mode);
  return fopen(host_filename.c_str(), mode);
}

std::vector<std::string> mac_list_directory(const std::string& mac_path) {
  std::string user_filename = userdata_filename_for_mac_filename(mac_path);
  SDL_CreateDirectory(user_filename.c_str());

  std::vector<std::string> ret;
  // Read the userdata directory first, so user files override bundled ones of
  // the same name (mirrors mac_fopen's read fallback semantics).
  for (const auto& base : {user_filename, host_filename_for_mac_filename(mac_path, false)}) {
    if (!std::filesystem::is_directory(base)) {
      fm_log.info_f("Skipping {} because it is not a directory", base);
      continue;
    }
    for (const auto& item : std::filesystem::directory_iterator{base}) {
      auto name = item.path().filename().string();
      if (std::find(ret.begin(), ret.end(), name) == ret.end()) {
        ret.emplace_back(std::move(name));
      }
    }
  }
  fm_log.info_f("Listing directory {} yielded {} items", mac_path, ret.size());
  return ret;
}

std::unique_ptr<FILE, void (*)(FILE*)> mac_fopen_unique(const std::string& mac_path, const std::string& mode) {
  return std::unique_ptr<FILE, void (*)(FILE*)>{
      mac_fopen(mac_path.c_str(), mode.c_str()), +[](FILE* f) -> void { fclose(f); }};
}
