#include "replay/ReplayResultWriter.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace realmz::replay {
namespace {

constexpr std::uint64_t kMaximumProtocolInteger =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

[[nodiscard]] bool is_printable_ascii(std::string_view value) noexcept {
  return std::ranges::all_of(value, [](unsigned char byte) {
    return byte >= 0x20U && byte <= 0x7EU;
  });
}

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string encoded;
  encoded.reserve(value.size() + 2U);
  encoded.push_back('"');
  for (const char byte : value) {
    if (byte == '"' || byte == '\\') {
      encoded.push_back('\\');
    }
    encoded.push_back(byte);
  }
  encoded.push_back('"');
  return encoded;
}

void validate_completed_result(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  if (result.process_id == 0 || result.process_id > kMaximumProtocolInteger) {
    throw ReplayResultError("replay result process_id is outside v1 bounds");
  }
  if (result.engine_identity.empty() ||
      result.engine_identity.size() > 256U ||
      !is_printable_ascii(result.engine_identity) ||
      result.engine_identity.front() == ' ' ||
      result.engine_identity.back() == ' ') {
    throw ReplayResultError(
        "replay result engine_identity must be 1-256 trimmed printable "
        "ASCII bytes");
  }
  if (result.settled_action_count != config.actions().size() ||
      result.settled_action_count > kMaximumReplayActions) {
    throw ReplayResultError(
        "replay result settled_action_count does not match the action plan");
  }
  if (result.rng_draw_count > kMaximumProtocolInteger) {
    throw ReplayResultError("replay result rng_draw_count is outside v1 bounds");
  }
}

void validate_result_path(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.lexically_normal() != path ||
      !path.has_filename()) {
    throw ReplayResultError(
        "replay result path must be normalized, absolute, and name a file");
  }
}

void require_config_version(
    const ReplayChildConfig& config,
    std::uint32_t expected) {
  if (config.schema_version() != expected) {
    throw ReplayResultError(
        "replay result writer does not match the child config schema");
  }
}

[[nodiscard]] std::string encode_replay_child_result(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result,
    std::uint32_t schema_version) {
  require_config_version(config, schema_version);
  validate_completed_result(config, result);

  std::string encoded;
  encoded.reserve(768U);
  encoded += "{\"schema_version\":" + std::to_string(schema_version);
  encoded += ",\"run_id\":" + json_string(config.run_id());
  encoded += ",\"child_nonce\":" + json_string(config.child_nonce());
  encoded += ",\"replay_route\":" +
      json_string(to_string(config.replay_route()));
  encoded += ",\"presentation_mode\":" +
      json_string(to_string(config.presentation_mode()));
  encoded += ",\"process_id\":" + std::to_string(result.process_id);
  encoded += ",\"status\":\"completed\"";
  encoded += ",\"engine_identity\":" + json_string(result.engine_identity);
  encoded += ",\"settled_action_count\":" +
      std::to_string(result.settled_action_count);
  encoded += ",\"state_sha256\":" +
      json_string(sha256_hex(result.state_sha256));
  encoded += ",\"save_tree_sha256\":" +
      json_string(sha256_hex(result.save_tree_sha256));
  encoded += ",\"rng_draw_count\":" +
      std::to_string(result.rng_draw_count);
  encoded += ",\"rng_seed\":" + json_string(config.rng_seed_token());
  encoded += ",\"rng_stream\":" + json_string(config.rng_stream_token());
  encoded += "}\n";
  return encoded;
}

#ifdef _WIN32

[[noreturn]] void throw_windows_error(
    std::string_view context,
    DWORD error) {
  throw ReplayResultError(
      std::string(context) + ": Windows error " + std::to_string(error));
}

void write_private_exclusive_file(
    const std::filesystem::path& path,
    std::string_view payload) {
  HANDLE handle = ::CreateFileW(
      path.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    throw_windows_error(
        "cannot exclusively create replay result", ::GetLastError());
  }

  auto close_on_failure = [&handle]() noexcept {
    if (handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  };

  BY_HANDLE_FILE_INFORMATION before{};
  if (!::GetFileInformationByHandle(handle, &before) ||
      before.dwFileAttributes &
          (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ||
      before.nNumberOfLinks != 1U || ::GetFileType(handle) != FILE_TYPE_DISK) {
    close_on_failure();
    throw ReplayResultError(
        "new replay result is not one physical regular file");
  }

  std::size_t written = 0;
  while (written < payload.size()) {
    const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
        payload.size() - written,
        std::numeric_limits<DWORD>::max()));
    DWORD actual = 0;
    if (!::WriteFile(
            handle, payload.data() + written, chunk, &actual, nullptr) ||
        actual == 0U) {
      const DWORD error = ::GetLastError();
      close_on_failure();
      throw_windows_error("cannot write replay result", error);
    }
    written += actual;
  }

  LARGE_INTEGER size{};
  BY_HANDLE_FILE_INFORMATION after{};
  if (!::FlushFileBuffers(handle) || !::GetFileSizeEx(handle, &size) ||
      !::GetFileInformationByHandle(handle, &after) ||
      size.QuadPart != static_cast<LONGLONG>(payload.size()) ||
      after.nNumberOfLinks != 1U ||
      after.dwFileAttributes &
          (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ||
      before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
      before.nFileIndexHigh != after.nFileIndexHigh ||
      before.nFileIndexLow != after.nFileIndexLow) {
    close_on_failure();
    throw ReplayResultError(
        "replay result identity or size changed while writing");
  }
  if (!::CloseHandle(handle)) {
    const DWORD error = ::GetLastError();
    handle = INVALID_HANDLE_VALUE;
    throw_windows_error("cannot close replay result", error);
  }
  handle = INVALID_HANDLE_VALUE;
}

#else

[[noreturn]] void throw_posix_error(std::string_view context, int error) {
  throw ReplayResultError(
      std::string(context) + ": " +
      std::generic_category().message(error));
}

void write_private_exclusive_file(
    const std::filesystem::path& path,
    std::string_view payload) {
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  int descriptor = ::open(path.c_str(), flags, 0600);
  if (descriptor < 0) {
    throw_posix_error("cannot exclusively create replay result", errno);
  }

  auto close_on_failure = [&descriptor]() noexcept {
    if (descriptor >= 0) {
      ::close(descriptor);
      descriptor = -1;
    }
  };

  if (::fchmod(descriptor, 0600) != 0) {
    const int error = errno;
    close_on_failure();
    throw_posix_error("cannot make replay result private", error);
  }
  struct stat before {};
  if (::fstat(descriptor, &before) != 0) {
    const int error = errno;
    close_on_failure();
    throw_posix_error("cannot inspect new replay result", error);
  }
  if (!S_ISREG(before.st_mode) || before.st_uid != ::getuid() ||
      (before.st_mode & 0777) != 0600 || before.st_nlink != 1 ||
      before.st_size != 0) {
    close_on_failure();
    throw ReplayResultError(
        "new replay result is not one private physical regular file");
  }

  std::size_t written = 0;
  while (written < payload.size()) {
    const std::size_t remaining = payload.size() - written;
    const std::size_t request = std::min<std::size_t>(
        remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
    const ssize_t actual =
        ::write(descriptor, payload.data() + written, request);
    if (actual < 0 && errno == EINTR) {
      continue;
    }
    if (actual <= 0) {
      const int error = (actual < 0) ? errno : EIO;
      close_on_failure();
      throw_posix_error("cannot write replay result", error);
    }
    written += static_cast<std::size_t>(actual);
  }

  struct stat after {};
  if (::fstat(descriptor, &after) != 0) {
    const int error = errno;
    close_on_failure();
    throw_posix_error("cannot recheck replay result", error);
  }
  if (!S_ISREG(after.st_mode) || after.st_uid != ::getuid() ||
      (after.st_mode & 0777) != 0600 || after.st_nlink != 1 ||
      after.st_dev != before.st_dev || after.st_ino != before.st_ino ||
      after.st_size < 0 ||
      static_cast<std::uint64_t>(after.st_size) != payload.size()) {
    close_on_failure();
    throw ReplayResultError(
        "replay result identity, mode, or size changed while writing");
  }
  struct stat named {};
  if (::lstat(path.c_str(), &named) != 0) {
    const int error = errno;
    close_on_failure();
    throw_posix_error("cannot recheck replay result path", error);
  }
  if (!S_ISREG(named.st_mode) || named.st_uid != after.st_uid ||
      (named.st_mode & 0777) != 0600 || named.st_nlink != 1 ||
      named.st_dev != after.st_dev || named.st_ino != after.st_ino ||
      named.st_size != after.st_size) {
    close_on_failure();
    throw ReplayResultError(
        "replay result path no longer names the written private file");
  }
  if (::close(descriptor) != 0) {
    descriptor = -1;
    throw_posix_error("cannot close replay result", errno);
  }
  descriptor = -1;
}

#endif

} // namespace

std::string encode_replay_child_result_v1(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  return encode_replay_child_result(config, result, 1U);
}

void write_replay_child_result_v1(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  validate_result_path(config.result_path());
  const std::string payload = encode_replay_child_result_v1(config, result);
  write_private_exclusive_file(config.result_path(), payload);
}

std::string encode_replay_child_result_v2(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  return encode_replay_child_result(config, result, 2U);
}

void write_replay_child_result_v2(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  validate_result_path(config.result_path());
  const std::string payload = encode_replay_child_result_v2(config, result);
  write_private_exclusive_file(config.result_path(), payload);
}

std::string encode_replay_child_result_v3(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  return encode_replay_child_result(config, result, 3U);
}

void write_replay_child_result_v3(
    const ReplayChildConfig& config,
    const ReplayCompletedResult& result) {
  validate_result_path(config.result_path());
  const std::string payload = encode_replay_child_result_v3(config, result);
  write_private_exclusive_file(config.result_path(), payload);
}

} // namespace realmz::replay
