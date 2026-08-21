#include "replay/ReplayOutputOracle.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace realmz::replay {

namespace {

[[noreturn]] void output_error(std::string message) {
  throw ReplayOutputError(std::move(message));
}

[[nodiscard]] bool byte_order_less(
    const std::string& left,
    const std::string& right) noexcept {
  return std::lexicographical_compare(
      left.begin(),
      left.end(),
      right.begin(),
      right.end(),
      [](char left_byte, char right_byte) {
        return static_cast<unsigned char>(left_byte) <
            static_cast<unsigned char>(right_byte);
      });
}

[[nodiscard]] std::vector<std::string> expected_names() {
  std::vector<std::string> names;
  names.reserve(kReplayOutputFileNames.size());
  for (const std::string_view name : kReplayOutputFileNames) {
    names.emplace_back(name);
  }
  std::sort(names.begin(), names.end(), byte_order_less);
  return names;
}

void append_big_endian(Sha256& hash, std::uint64_t value, std::size_t bytes) {
  std::array<std::byte, 8> encoded{};
  for (std::size_t index = 0; index < bytes; ++index) {
    const unsigned shift = static_cast<unsigned>((bytes - index - 1) * 8);
    encoded[index] = static_cast<std::byte>((value >> shift) & 0xFFU);
  }
  hash.update(std::span(encoded).first(bytes));
}

[[nodiscard]] Sha256Digest tree_digest(
    const std::array<ReplayOutputFile, kReplayOutputFileNames.size()>& files) {
  constexpr std::string_view domain =
      "realmz.semantic-replay.output-tree.v1";
  Sha256 hash;
  hash.update(domain);
  constexpr std::array<std::byte, 1> terminator = {std::byte{0}};
  hash.update(terminator);
  append_big_endian(hash, files.size(), 4);
  for (const ReplayOutputFile& file : files) {
    append_big_endian(hash, file.relative_name.size(), 2);
    hash.update(file.relative_name);
    append_big_endian(hash, file.byte_size, 8);
    hash.update(std::as_bytes(std::span(file.content_sha256)));
  }
  return hash.finalize();
}

#ifndef _WIN32

class UniqueFileDescriptor final {
public:
  explicit UniqueFileDescriptor(int descriptor = -1) noexcept
      : descriptor_(descriptor) {}

  ~UniqueFileDescriptor() {
    if (descriptor_ >= 0) {
      static_cast<void>(::close(descriptor_));
    }
  }

  UniqueFileDescriptor(const UniqueFileDescriptor&) = delete;
  UniqueFileDescriptor& operator=(const UniqueFileDescriptor&) = delete;

  UniqueFileDescriptor(UniqueFileDescriptor&& other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}

  UniqueFileDescriptor& operator=(UniqueFileDescriptor&& other) noexcept {
    if (this != &other) {
      if (descriptor_ >= 0) {
        static_cast<void>(::close(descriptor_));
      }
      descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept {
    return descriptor_;
  }

  [[nodiscard]] int release() noexcept {
    return std::exchange(descriptor_, -1);
  }

private:
  int descriptor_;
};

struct PosixSnapshot final {
  dev_t device{};
  ino_t inode{};
  mode_t mode{};
  nlink_t link_count{};
  uid_t user{};
  gid_t group{};
  off_t size{};
  std::int64_t modified_seconds = 0;
  long modified_nanoseconds = 0;
  std::int64_t changed_seconds = 0;
  long changed_nanoseconds = 0;

  bool operator==(const PosixSnapshot&) const = default;
};

[[nodiscard]] PosixSnapshot snapshot_from_stat(const struct stat& status) {
#if defined(__APPLE__)
  const auto modified_seconds = status.st_mtimespec.tv_sec;
  const auto modified_nanoseconds = status.st_mtimespec.tv_nsec;
  const auto changed_seconds = status.st_ctimespec.tv_sec;
  const auto changed_nanoseconds = status.st_ctimespec.tv_nsec;
#else
  const auto modified_seconds = status.st_mtim.tv_sec;
  const auto modified_nanoseconds = status.st_mtim.tv_nsec;
  const auto changed_seconds = status.st_ctim.tv_sec;
  const auto changed_nanoseconds = status.st_ctim.tv_nsec;
#endif
  return PosixSnapshot{
      .device = status.st_dev,
      .inode = status.st_ino,
      .mode = status.st_mode,
      .link_count = status.st_nlink,
      .user = status.st_uid,
      .group = status.st_gid,
      .size = status.st_size,
      .modified_seconds = static_cast<std::int64_t>(modified_seconds),
      .modified_nanoseconds = modified_nanoseconds,
      .changed_seconds = static_cast<std::int64_t>(changed_seconds),
      .changed_nanoseconds = changed_nanoseconds,
  };
}

[[noreturn]] void posix_error(std::string_view operation) {
  const int error_number = errno;
  output_error(
      std::string(operation) + ": " + std::strerror(error_number));
}

[[nodiscard]] PosixSnapshot snapshot_descriptor(
    int descriptor,
    std::string_view label) {
  struct stat status {};
  if (::fstat(descriptor, &status) != 0) {
    posix_error(std::string("cannot inspect ") + std::string(label));
  }
  return snapshot_from_stat(status);
}

[[nodiscard]] std::vector<std::string> read_directory_names(
    int directory_descriptor,
    std::size_t maximum_names,
    std::string_view label) {
  const int enumeration_descriptor = ::openat(
      directory_descriptor,
      ".",
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (enumeration_descriptor < 0) {
    posix_error(std::string("cannot enumerate ") + std::string(label));
  }
  UniqueFileDescriptor owned_descriptor(enumeration_descriptor);
  const int directory_file_descriptor = owned_descriptor.release();
  DIR* directory = ::fdopendir(directory_file_descriptor);
  if (directory == nullptr) {
    const int open_error = errno;
    static_cast<void>(::close(directory_file_descriptor));
    errno = open_error;
    posix_error(std::string("cannot enumerate ") + std::string(label));
  }

  std::vector<std::string> names;
  while (true) {
    errno = 0;
    const dirent* entry = ::readdir(directory);
    if (entry == nullptr) {
      const int read_error = errno;
      static_cast<void>(::closedir(directory));
      if (read_error != 0) {
        errno = read_error;
        posix_error(std::string("cannot enumerate ") + std::string(label));
      }
      break;
    }
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    if (names.size() == maximum_names) {
      static_cast<void>(::closedir(directory));
      output_error(std::string(label) + " has too many entries");
    }
    names.emplace_back(name);
  }
  std::sort(names.begin(), names.end(), byte_order_less);
  return names;
}

[[nodiscard]] bool has_exact_child_name(
    int directory_descriptor,
    std::string_view wanted,
    std::string_view label) {
  constexpr std::size_t maximum_parent_entries = 65536;
  const std::vector<std::string> names = read_directory_names(
      directory_descriptor, maximum_parent_entries, label);
  return std::find(names.begin(), names.end(), wanted) != names.end();
}

[[nodiscard]] UniqueFileDescriptor open_root_directory(
    const std::filesystem::path& root) {
  const int descriptor = ::open(
      root.c_str(),
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0) {
    posix_error("cannot open replay user-data root");
  }
  UniqueFileDescriptor result(descriptor);
  const PosixSnapshot snapshot =
      snapshot_descriptor(result.get(), "replay user-data root");
  if (!S_ISDIR(snapshot.mode)) {
    output_error("replay user-data root is not a physical directory");
  }
  return result;
}

[[nodiscard]] UniqueFileDescriptor open_exact_directory_child(
    int parent_descriptor,
    std::string_view child_name,
    std::string_view parent_label,
    std::string_view child_label) {
  const PosixSnapshot parent_before =
      snapshot_descriptor(parent_descriptor, parent_label);
  if (!has_exact_child_name(
          parent_descriptor, child_name, parent_label)) {
    output_error(
        std::string(child_label) + " is missing or has non-exact casing");
  }

  struct stat child_status {};
  const std::string native_name(child_name);
  if (::fstatat(
          parent_descriptor,
          native_name.c_str(),
          &child_status,
          AT_SYMLINK_NOFOLLOW) != 0) {
    posix_error(std::string("cannot inspect ") + std::string(child_label));
  }
  if (!S_ISDIR(child_status.st_mode)) {
    output_error(std::string(child_label) + " is not a physical directory");
  }

  const int descriptor = ::openat(
      parent_descriptor,
      native_name.c_str(),
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0) {
    posix_error(std::string("cannot open ") + std::string(child_label));
  }
  UniqueFileDescriptor result(descriptor);
  if (snapshot_descriptor(result.get(), child_label) !=
      snapshot_from_stat(child_status)) {
    output_error(std::string(child_label) + " changed while it was opened");
  }
  if (snapshot_descriptor(parent_descriptor, parent_label) != parent_before) {
    output_error(std::string(parent_label) + " changed during traversal");
  }
  return result;
}

[[nodiscard]] PosixSnapshot snapshot_named_child(
    int parent_descriptor,
    std::string_view child_name,
    std::string_view child_label) {
  struct stat child_status {};
  const std::string native_name(child_name);
  if (::fstatat(
          parent_descriptor,
          native_name.c_str(),
          &child_status,
          AT_SYMLINK_NOFOLLOW) != 0) {
    posix_error(std::string("cannot recheck ") + std::string(child_label));
  }
  return snapshot_from_stat(child_status);
}

struct PosixHashedFile final {
  ReplayOutputFile output;
  PosixSnapshot snapshot;
};

[[nodiscard]] PosixHashedFile hash_regular_file(
    int slot_descriptor,
    const std::string& name,
    std::uint64_t maximum_file_bytes,
    std::uint64_t remaining_total_bytes) {
  struct stat path_status {};
  if (::fstatat(
          slot_descriptor,
          name.c_str(),
          &path_status,
          AT_SYMLINK_NOFOLLOW) != 0) {
    posix_error("cannot inspect replay output file " + name);
  }
  if (!S_ISREG(path_status.st_mode)) {
    output_error("replay output entry is not a physical regular file: " + name);
  }
  if (path_status.st_size < 0) {
    output_error("replay output file has a negative size: " + name);
  }
  const std::uint64_t declared_size =
      static_cast<std::uint64_t>(path_status.st_size);
  if (declared_size > maximum_file_bytes) {
    output_error("replay output file exceeds the per-file byte limit: " + name);
  }
  if (declared_size > remaining_total_bytes) {
    output_error("replay output tree exceeds the aggregate byte limit");
  }

  const int descriptor = ::openat(
      slot_descriptor,
      name.c_str(),
      O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0) {
    posix_error("cannot open replay output file " + name);
  }
  UniqueFileDescriptor file(descriptor);
  const PosixSnapshot before = snapshot_descriptor(file.get(), name);
  if (!S_ISREG(before.mode) || before != snapshot_from_stat(path_status)) {
    output_error("replay output file changed while it was opened: " + name);
  }
  if (before.link_count != 1) {
    output_error("replay output file has a hardlink alias: " + name);
  }
  if (name == "Data I1" && declared_size != kReplayOutputDataI1Bytes) {
    output_error("replay output Data I1 has a non-retail byte size");
  }

  Sha256 hash;
  std::array<std::byte, 64U * 1024U> buffer{};
  std::uint64_t bytes_read = 0;
  while (true) {
    const ssize_t count = ::read(file.get(), buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      posix_error("cannot read replay output file " + name);
    }
    if (count == 0) {
      break;
    }
    const std::uint64_t chunk_size = static_cast<std::uint64_t>(count);
    if (chunk_size > declared_size - std::min(bytes_read, declared_size)) {
      output_error("replay output file grew while it was read: " + name);
    }
    bytes_read += chunk_size;
    hash.update(std::span(buffer).first(static_cast<std::size_t>(count)));
  }

  const PosixSnapshot after = snapshot_descriptor(file.get(), name);
  if (after != before || bytes_read != declared_size) {
    output_error("replay output file changed while it was read: " + name);
  }
  return PosixHashedFile{
      .output = ReplayOutputFile{
          .relative_name = name,
          .byte_size = bytes_read,
          .content_sha256 = hash.finalize(),
      },
      .snapshot = after,
  };
}

[[nodiscard]] VerifiedReplayOutput verify_platform_output(
    const std::filesystem::path& root,
    char output_slot,
    ReplayOutputLimits limits) {
  UniqueFileDescriptor root_directory = open_root_directory(root);
  const PosixSnapshot root_before = snapshot_descriptor(
      root_directory.get(), "replay user-data root");
  UniqueFileDescriptor save_directory = open_exact_directory_child(
      root_directory.get(), "Save", "replay user-data root", "Save directory");
  const PosixSnapshot save_before =
      snapshot_descriptor(save_directory.get(), "Save directory");
  const std::string slot_name = std::string("Game ") + output_slot;
  UniqueFileDescriptor slot_directory = open_exact_directory_child(
      save_directory.get(),
      slot_name,
      "Save directory",
      "replay output slot");

  const PosixSnapshot directory_before =
      snapshot_descriptor(slot_directory.get(), "replay output slot");
  const std::vector<std::string> wanted_names = expected_names();
  const std::vector<std::string> names = read_directory_names(
      slot_directory.get(),
      kReplayOutputFileNames.size(),
      "replay output slot");
  if (names != wanted_names) {
    output_error(
        "replay output slot does not contain the exact required file set");
  }

  VerifiedReplayOutput result;
  std::array<PosixSnapshot, kReplayOutputFileNames.size()> file_snapshots{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    PosixHashedFile hashed = hash_regular_file(
        slot_directory.get(),
        names[index],
        limits.maximum_file_bytes,
        limits.maximum_total_bytes - result.total_bytes);
    result.files[index] = std::move(hashed.output);
    file_snapshots[index] = hashed.snapshot;
    result.total_bytes += result.files[index].byte_size;
  }

  const std::vector<std::string> final_names = read_directory_names(
      slot_directory.get(),
      kReplayOutputFileNames.size(),
      "replay output slot");
  const PosixSnapshot directory_after =
      snapshot_descriptor(slot_directory.get(), "replay output slot");
  if (final_names != names || directory_after != directory_before) {
    output_error("replay output slot changed while it was verified");
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (snapshot_named_child(
            slot_directory.get(), names[index], names[index]) !=
        file_snapshots[index]) {
      output_error(
          "replay output file no longer names the verified object: " +
          names[index]);
    }
  }
  if (snapshot_descriptor(save_directory.get(), "Save directory") !=
          save_before ||
      snapshot_named_child(
          save_directory.get(), slot_name, "replay output slot") !=
          directory_after) {
    output_error("Save directory no longer names the verified output slot");
  }
  if (snapshot_descriptor(root_directory.get(), "replay user-data root") !=
          root_before ||
      snapshot_named_child(
          root_directory.get(), "Save", "Save directory") != save_before) {
    output_error("replay user-data root no longer names the verified Save directory");
  }
  result.tree_sha256 = tree_digest(result.files);
  return result;
}

#else

class UniqueWindowsHandle final {
public:
  explicit UniqueWindowsHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : handle_(handle) {}

  ~UniqueWindowsHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(::CloseHandle(handle_));
    }
  }

  UniqueWindowsHandle(const UniqueWindowsHandle&) = delete;
  UniqueWindowsHandle& operator=(const UniqueWindowsHandle&) = delete;

  UniqueWindowsHandle(UniqueWindowsHandle&& other) noexcept
      : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}

  [[nodiscard]] HANDLE get() const noexcept {
    return handle_;
  }

private:
  HANDLE handle_;
};

struct WindowsSnapshot final {
  DWORD volume = 0;
  DWORD index_high = 0;
  DWORD index_low = 0;
  DWORD attributes = 0;
  DWORD links = 0;
  DWORD size_high = 0;
  DWORD size_low = 0;
  DWORD write_high = 0;
  DWORD write_low = 0;
  std::int64_t change_time = 0;

  bool operator==(const WindowsSnapshot&) const = default;
};

[[noreturn]] void windows_error(std::string message) {
  output_error(std::move(message) + " (Windows error " +
      std::to_string(::GetLastError()) + ")");
}

[[nodiscard]] WindowsSnapshot windows_snapshot(
    HANDLE handle,
    std::string_view label) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (!::GetFileInformationByHandle(handle, &information)) {
    windows_error("cannot inspect " + std::string(label));
  }
  FILE_BASIC_INFO basic_information{};
  if (!::GetFileInformationByHandleEx(
          handle,
          FileBasicInfo,
          &basic_information,
          sizeof(basic_information))) {
    windows_error("cannot inspect " + std::string(label));
  }
  return WindowsSnapshot{
      .volume = information.dwVolumeSerialNumber,
      .index_high = information.nFileIndexHigh,
      .index_low = information.nFileIndexLow,
      .attributes = information.dwFileAttributes,
      .links = information.nNumberOfLinks,
      .size_high = information.nFileSizeHigh,
      .size_low = information.nFileSizeLow,
      .write_high = information.ftLastWriteTime.dwHighDateTime,
      .write_low = information.ftLastWriteTime.dwLowDateTime,
      .change_time = basic_information.ChangeTime.QuadPart,
  };
}

[[nodiscard]] std::vector<std::string> windows_directory_names(
    const std::filesystem::path& directory,
    std::size_t maximum_names,
    std::string_view label) {
  std::vector<std::string> names;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(directory, error), end;
       iterator != end;
       iterator.increment(error)) {
    if (error) {
      output_error("cannot enumerate " + std::string(label));
    }
    if (names.size() == maximum_names) {
      output_error(std::string(label) + " has too many entries");
    }
    const std::u8string encoded = iterator->path().filename().u8string();
    names.emplace_back(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
  }
  if (error) {
    output_error("cannot enumerate " + std::string(label));
  }
  std::sort(names.begin(), names.end(), byte_order_less);
  return names;
}

[[nodiscard]] UniqueWindowsHandle open_windows_object(
    const std::filesystem::path& path,
    bool directory,
    std::string_view label) {
  const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT |
      (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN);
  HANDLE handle = ::CreateFileW(
      path.c_str(),
      directory ? FILE_LIST_DIRECTORY : GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      flags,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    windows_error("cannot open " + std::string(label));
  }
  UniqueWindowsHandle result(handle);
  const WindowsSnapshot snapshot = windows_snapshot(result.get(), label);
  if ((snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      ((snapshot.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
    output_error(std::string(label) +
        (directory ? " is not a physical directory"
                   : " is not a physical regular file"));
  }
  if (!directory && ::GetFileType(result.get()) != FILE_TYPE_DISK) {
    output_error(std::string(label) + " is not a physical regular file");
  }
  return result;
}

void require_exact_windows_child(
    const std::filesystem::path& parent,
    std::string_view child,
    std::string_view label) {
  constexpr std::size_t maximum_parent_entries = 65536;
  const std::vector<std::string> names = windows_directory_names(
      parent, maximum_parent_entries, label);
  if (std::find(names.begin(), names.end(), child) == names.end()) {
    output_error(std::string(label) + " is missing or has non-exact casing");
  }
}

struct WindowsHashedFile final {
  ReplayOutputFile output;
  WindowsSnapshot snapshot;
};

[[nodiscard]] WindowsHashedFile hash_windows_file(
    const std::filesystem::path& path,
    const std::string& name,
    std::uint64_t maximum_file_bytes,
    std::uint64_t remaining_total_bytes) {
  UniqueWindowsHandle file = open_windows_object(path, false, name);
  const WindowsSnapshot before = windows_snapshot(file.get(), name);
  const std::uint64_t declared_size =
      (static_cast<std::uint64_t>(before.size_high) << 32U) | before.size_low;
  if (declared_size > maximum_file_bytes) {
    output_error("replay output file exceeds the per-file byte limit: " + name);
  }
  if (declared_size > remaining_total_bytes) {
    output_error("replay output tree exceeds the aggregate byte limit");
  }
  if (before.links != 1) {
    output_error("replay output file has a hardlink alias: " + name);
  }
  if (name == "Data I1" && declared_size != kReplayOutputDataI1Bytes) {
    output_error("replay output Data I1 has a non-retail byte size");
  }

  Sha256 hash;
  std::array<std::byte, 64U * 1024U> buffer{};
  std::uint64_t bytes_read = 0;
  while (true) {
    DWORD count = 0;
    if (!::ReadFile(
            file.get(),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &count,
            nullptr)) {
      windows_error("cannot read replay output file " + name);
    }
    if (count == 0) {
      break;
    }
    if (count > declared_size - std::min(bytes_read, declared_size)) {
      output_error("replay output file grew while it was read: " + name);
    }
    bytes_read += count;
    hash.update(std::span(buffer).first(count));
  }
  if (windows_snapshot(file.get(), name) != before ||
      bytes_read != declared_size) {
    output_error("replay output file changed while it was read: " + name);
  }
  return WindowsHashedFile{
      .output = ReplayOutputFile{
          .relative_name = name,
          .byte_size = bytes_read,
          .content_sha256 = hash.finalize(),
      },
      .snapshot = before,
  };
}

[[nodiscard]] VerifiedReplayOutput verify_platform_output(
    const std::filesystem::path& root,
    char output_slot,
    ReplayOutputLimits limits) {
  UniqueWindowsHandle root_handle =
      open_windows_object(root, true, "replay user-data root");
  const WindowsSnapshot root_before =
      windows_snapshot(root_handle.get(), "replay user-data root");
  require_exact_windows_child(root, "Save", "replay user-data root");
  const std::filesystem::path save_path = root / "Save";
  UniqueWindowsHandle save_handle =
      open_windows_object(save_path, true, "Save directory");
  const WindowsSnapshot save_before =
      windows_snapshot(save_handle.get(), "Save directory");
  const std::string slot_name = std::string("Game ") + output_slot;
  require_exact_windows_child(save_path, slot_name, "Save directory");
  const std::filesystem::path slot_path = save_path / slot_name;
  UniqueWindowsHandle slot_handle =
      open_windows_object(slot_path, true, "replay output slot");
  if (windows_snapshot(save_handle.get(), "Save directory") != save_before) {
    output_error("Save directory changed during traversal");
  }

  const WindowsSnapshot directory_before =
      windows_snapshot(slot_handle.get(), "replay output slot");
  const std::vector<std::string> wanted_names = expected_names();
  const std::vector<std::string> names = windows_directory_names(
      slot_path, kReplayOutputFileNames.size(), "replay output slot");
  if (names != wanted_names) {
    output_error(
        "replay output slot does not contain the exact required file set");
  }

  VerifiedReplayOutput result;
  std::array<WindowsSnapshot, kReplayOutputFileNames.size()> file_snapshots{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    WindowsHashedFile hashed = hash_windows_file(
        slot_path / names[index],
        names[index],
        limits.maximum_file_bytes,
        limits.maximum_total_bytes - result.total_bytes);
    result.files[index] = std::move(hashed.output);
    file_snapshots[index] = hashed.snapshot;
    result.total_bytes += result.files[index].byte_size;
  }
  const std::vector<std::string> final_names = windows_directory_names(
      slot_path, kReplayOutputFileNames.size(), "replay output slot");
  if (final_names != names ||
      windows_snapshot(slot_handle.get(), "replay output slot") !=
          directory_before) {
    output_error("replay output slot changed while it was verified");
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    UniqueWindowsHandle named_file = open_windows_object(
        slot_path / names[index], false, names[index]);
    if (windows_snapshot(named_file.get(), names[index]) !=
        file_snapshots[index]) {
      output_error(
          "replay output file no longer names the verified object: " +
          names[index]);
    }
  }
  UniqueWindowsHandle named_slot =
      open_windows_object(slot_path, true, "replay output slot");
  if (windows_snapshot(named_slot.get(), "replay output slot") !=
      directory_before) {
    output_error("Save directory no longer names the verified output slot");
  }
  UniqueWindowsHandle named_save =
      open_windows_object(save_path, true, "Save directory");
  if (windows_snapshot(named_save.get(), "Save directory") != save_before) {
    output_error("replay user-data root no longer names the verified Save directory");
  }
  UniqueWindowsHandle named_root =
      open_windows_object(root, true, "replay user-data root");
  if (windows_snapshot(named_root.get(), "replay user-data root") !=
      root_before) {
    output_error("configured path no longer names the replay user-data root");
  }
  result.tree_sha256 = tree_digest(result.files);
  return result;
}

#endif

} // namespace

std::filesystem::path replay_output_slot_path(
    const std::filesystem::path& user_data_root,
    char output_slot) {
  if (user_data_root.empty() || !user_data_root.is_absolute() ||
      user_data_root.lexically_normal() != user_data_root ||
      (user_data_root != user_data_root.root_path() &&
          user_data_root.filename().empty())) {
    throw std::invalid_argument(
        "replay user-data root must be an absolute normalized path");
  }
  if (output_slot < 'A' || output_slot > 'J') {
    throw std::invalid_argument(
        "replay output slot must be an uppercase letter A through J");
  }
  return user_data_root / "Save" / (std::string("Game ") + output_slot);
}

VerifiedReplayOutput verify_replay_output_slot(
    const std::filesystem::path& user_data_root,
    char output_slot,
    ReplayOutputLimits limits) {
  static_cast<void>(replay_output_slot_path(user_data_root, output_slot));
  return verify_platform_output(user_data_root, output_slot, limits);
}

} // namespace realmz::replay
