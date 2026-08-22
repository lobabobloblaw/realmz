#include "FileManager.h"

#include <SDL3/SDL_storage.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <phosg/Strings.hh>

#if !defined(_WIN32)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "StringConvert.hpp"
#include "UserDataPaths.hpp"
#include "replay/ReplayRuntime.hpp"
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

static bool replay_requires_user_only_read(
    const std::filesystem::path& relative_path) {
  const auto* runtime = realmz::replay::installed_replay_runtime();
  return runtime &&
      (runtime->read_policy_for_user_relative_path(relative_path) ==
          realmz::replay::ReplayReadPolicy::user_data_only);
}

#if defined(_WIN32)
static bool replay_path_components_are_physical(
    const std::filesystem::path& relative_path,
    bool final_is_directory) {
  std::filesystem::path candidate(user_basepath());
  for (auto component = relative_path.begin();
       component != relative_path.end(); ++component) {
    candidate /= *component;
    std::error_code status_error;
    const auto status =
        std::filesystem::symlink_status(candidate, status_error);
    if (status_error || std::filesystem::is_symlink(status)) {
      return false;
    }
    const bool final = std::next(component) == relative_path.end();
    if ((!final || final_is_directory) &&
        !std::filesystem::is_directory(status)) {
      return false;
    }
    if (final && !final_is_directory &&
        !std::filesystem::is_regular_file(status)) {
      return false;
    }
  }
  return true;
}
#endif

#if !defined(_WIN32)
static int open_replay_path_no_follow(
    const std::filesystem::path& relative_path,
    bool final_is_directory) {
  int descriptor = open(user_basepath().c_str(),
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0) {
    return -1;
  }

  for (auto component = relative_path.begin();
       component != relative_path.end(); ++component) {
    const bool final = std::next(component) == relative_path.end();
    int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (!final || final_is_directory) {
      flags |= O_DIRECTORY;
    }
    const std::string name = component->string();
    const int next_descriptor = openat(descriptor, name.c_str(), flags);
    close(descriptor);
    descriptor = next_descriptor;
    if (descriptor < 0) {
      return -1;
    }
  }

  struct stat status;
  if ((fstat(descriptor, &status) != 0) ||
      (final_is_directory ? !S_ISDIR(status.st_mode)
                          : !S_ISREG(status.st_mode))) {
    close(descriptor);
    return -1;
  }
  return descriptor;
}
#endif

static FILE* open_replay_input_file_no_follow(
    const std::filesystem::path& relative_path,
    const char* mode) {
#if !defined(_WIN32)
  const int descriptor = open_replay_path_no_follow(relative_path, false);
  if (descriptor < 0) {
    fm_log.error_f(
        "Could not securely open replay input {}: {}",
        relative_path.string(), std::strerror(errno));
    return nullptr;
  }
  FILE* file = fdopen(descriptor, mode);
  if (!file) {
    close(descriptor);
  }
  return file;
#else
  // The compatibility fallback still rejects every static reparse/symlink
  // component. POSIX builds additionally use descriptor-relative O_NOFOLLOW
  // traversal above to close path-swap races.
  if (!replay_path_components_are_physical(relative_path, false)) {
    return nullptr;
  }
  const std::string host_path = realmz::userdata::confined_path_below_root(
      user_basepath(), relative_path).string();
  return fopen(host_path.c_str(), mode);
#endif
}

static bool list_replay_input_directory_no_follow(
    const std::filesystem::path& relative_path,
    std::vector<std::string>& names) {
#if !defined(_WIN32)
  const int descriptor = open_replay_path_no_follow(relative_path, true);
  if (descriptor < 0) {
    fm_log.error_f(
        "Could not securely open replay input directory {}: {}",
        relative_path.string(), std::strerror(errno));
    return false;
  }
  DIR* directory = fdopendir(descriptor);
  if (!directory) {
    close(descriptor);
    return false;
  }
  errno = 0;
  while (const dirent* entry = readdir(directory)) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..") {
      names.emplace_back(name);
    }
  }
  const bool success = errno == 0;
  closedir(directory);
  if (!success) {
    names.clear();
  }
  return success;
#else
  if (!replay_path_components_are_physical(relative_path, true)) {
    return false;
  }
  const auto host_path = realmz::userdata::confined_path_below_root(
      user_basepath(), relative_path);
  for (const auto& item : std::filesystem::directory_iterator{host_path}) {
    names.emplace_back(item.path().filename().string());
  }
  return true;
#endif
}

std::filesystem::path
host_path_for_mac_filename(
    const std::string& mac_path, bool implicitly_local) {
  const auto relative_path =
      realmz::userdata::safe_relative_path_for_classic_path(
          mac_path, implicitly_local);
  const auto* base_path = SDL_GetBasePath();
  if (!base_path) {
    fm_log.error_f("Failed to get SDL base path: {}", SDL_GetError());
    return {};
  }

  return realmz::userdata::confined_path_below_root(
      realmz::userdata::path_from_utf8(base_path), relative_path);
}

std::string
host_filename_for_mac_filename(
    const std::string& mac_path, bool implicitly_local) {
  return host_path_for_mac_filename(mac_path, implicitly_local).string();
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

  const std::filesystem::path relative_path =
      normalize_mac_path(filename, false);
  const bool write_capable = realmz::userdata::fopen_mode_can_write(mode);
  const bool user_only_read =
      replay_requires_user_only_read(relative_path);
  if (user_only_read) {
    if (write_capable) {
      fm_log.error_f(
          "Refusing write-capable open of replay input path {}", filename);
      return nullptr;
    }
    fm_log.info_f(
        "Securely opening replay input {} with mode {}", filename, mode);
    return open_replay_input_file_no_follow(relative_path, mode);
  }

  std::string user_filename = realmz::userdata::confined_path_below_root(
      user_basepath(), relative_path).string();
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

  // Every write-capable mode is confined to user storage. Read-only opens use
  // a user override when present and otherwise fall back to bundled data. The
  // replay input subtree returned through the no-follow path above instead.
  if (realmz::userdata::should_open_from_user_storage(
          mode, user_file_exists)) {
    host_filename = user_filename;

    // Reads must not mutate the staged input tree. Write-capable opens retain
    // the ordinary behavior of creating their confined parent directories.
    std::filesystem::path host_path{host_filename};
    if (write_capable &&
        !SDL_CreateDirectory(host_path.parent_path().string().c_str())) {
      fm_log.error_f("Could not create user-data directory {}: {}",
          host_path.parent_path().string(), SDL_GetError());
      return nullptr;
    }

    // Modes that preserve existing content need a private copy when the file
    // currently exists only in the read-only application resources. A mode
    // beginning with 'w' truncates or creates instead and must not be seeded.
    if (write_capable && !user_file_exists &&
        (mode[0] != 'w')) {
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
  const std::filesystem::path relative_path =
      normalize_mac_path(mac_path, false);
  std::string user_filename = realmz::userdata::confined_path_below_root(
      user_basepath(), relative_path).string();
  const bool user_only_read =
      replay_requires_user_only_read(relative_path);
  std::vector<std::string> ret;
  if (user_only_read) {
    static_cast<void>(
        list_replay_input_directory_no_follow(relative_path, ret));
    std::sort(ret.begin(), ret.end());
    fm_log.info_f(
        "Listing replay input directory {} yielded {} items",
        mac_path, ret.size());
    return ret;
  }
  SDL_CreateDirectory(user_filename.c_str());

  // Read the userdata directory first, so user files override bundled ones of
  // the same name (mirrors mac_fopen's read fallback semantics).
  const auto append_directory = [&ret](const std::string& base) {
    if (!std::filesystem::is_directory(base)) {
      fm_log.info_f("Skipping {} because it is not a directory", base);
      return;
    }
    for (const auto& item : std::filesystem::directory_iterator{base}) {
      auto name = item.path().filename().string();
      if (std::find(ret.begin(), ret.end(), name) == ret.end()) {
        ret.emplace_back(std::move(name));
      }
    }
  };
  append_directory(user_filename);
  append_directory(host_filename_for_mac_filename(mac_path, false));
  fm_log.info_f("Listing directory {} yielded {} items", mac_path, ret.size());
  return ret;
}

std::unique_ptr<FILE, void (*)(FILE*)> mac_fopen_unique(const std::string& mac_path, const std::string& mode) {
  return std::unique_ptr<FILE, void (*)(FILE*)>{
      mac_fopen(mac_path.c_str(), mode.c_str()), +[](FILE* f) -> void { fclose(f); }};
}
