#include "UserDataMigration.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace realmz::userdata {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kManifestHeader =
    "source_relative_path\tsha256\tsize\tresult\n";
constexpr std::string_view kManifestName = "manifest.tsv";
constexpr std::string_view kMarkerName = "complete.marker";
constexpr std::string_view kBackupPayloadDirectory = "Files";
constexpr std::string_view kBackupRootDirectory = "Import Backups";

class Sha256 {
public:
  void update(const uint8_t* data, size_t size) noexcept {
    for (size_t z = 0; z < size; z++) {
      this->buffer_[this->buffer_size_++] = data[z];
      if (this->buffer_size_ == this->buffer_.size()) {
        this->transform(this->buffer_.data());
        this->bit_count_ += 512;
        this->buffer_size_ = 0;
      }
    }
  }

  void update(std::string_view data) noexcept {
    this->update(
        reinterpret_cast<const uint8_t*>(data.data()), data.size());
  }

  [[nodiscard]] std::array<uint8_t, 32> finish() noexcept {
    const uint64_t final_bit_count =
        this->bit_count_ + static_cast<uint64_t>(this->buffer_size_) * 8;

    this->buffer_[this->buffer_size_++] = 0x80;
    if (this->buffer_size_ > 56) {
      while (this->buffer_size_ < 64) {
        this->buffer_[this->buffer_size_++] = 0;
      }
      this->transform(this->buffer_.data());
      this->buffer_size_ = 0;
    }
    while (this->buffer_size_ < 56) {
      this->buffer_[this->buffer_size_++] = 0;
    }
    for (size_t z = 0; z < 8; z++) {
      this->buffer_[63 - z] =
          static_cast<uint8_t>((final_bit_count >> (z * 8)) & 0xFF);
    }
    this->transform(this->buffer_.data());

    std::array<uint8_t, 32> digest{};
    for (size_t z = 0; z < this->state_.size(); z++) {
      digest[z * 4] = static_cast<uint8_t>(this->state_[z] >> 24);
      digest[z * 4 + 1] = static_cast<uint8_t>(this->state_[z] >> 16);
      digest[z * 4 + 2] = static_cast<uint8_t>(this->state_[z] >> 8);
      digest[z * 4 + 3] = static_cast<uint8_t>(this->state_[z]);
    }
    return digest;
  }

private:
  static constexpr std::array<uint32_t, 64> constants_ = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
      0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
      0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };

  static constexpr uint32_t rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32 - count));
  }

  void transform(const uint8_t* block) noexcept {
    std::array<uint32_t, 64> words{};
    for (size_t z = 0; z < 16; z++) {
      words[z] =
          (static_cast<uint32_t>(block[z * 4]) << 24) |
          (static_cast<uint32_t>(block[z * 4 + 1]) << 16) |
          (static_cast<uint32_t>(block[z * 4 + 2]) << 8) |
          static_cast<uint32_t>(block[z * 4 + 3]);
    }
    for (size_t z = 16; z < words.size(); z++) {
      const uint32_t s0 = rotate_right(words[z - 15], 7) ^
          rotate_right(words[z - 15], 18) ^ (words[z - 15] >> 3);
      const uint32_t s1 = rotate_right(words[z - 2], 17) ^
          rotate_right(words[z - 2], 19) ^ (words[z - 2] >> 10);
      words[z] = words[z - 16] + s0 + words[z - 7] + s1;
    }

    uint32_t a = this->state_[0];
    uint32_t b = this->state_[1];
    uint32_t c = this->state_[2];
    uint32_t d = this->state_[3];
    uint32_t e = this->state_[4];
    uint32_t f = this->state_[5];
    uint32_t g = this->state_[6];
    uint32_t h = this->state_[7];

    for (size_t z = 0; z < words.size(); z++) {
      const uint32_t sum1 = rotate_right(e, 6) ^
          rotate_right(e, 11) ^ rotate_right(e, 25);
      const uint32_t choice = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + sum1 + choice + constants_[z] + words[z];
      const uint32_t sum0 = rotate_right(a, 2) ^
          rotate_right(a, 13) ^ rotate_right(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    this->state_[0] += a;
    this->state_[1] += b;
    this->state_[2] += c;
    this->state_[3] += d;
    this->state_[4] += e;
    this->state_[5] += f;
    this->state_[6] += g;
    this->state_[7] += h;
  }

  std::array<uint32_t, 8> state_ = {
      0x6a09e667,
      0xbb67ae85,
      0x3c6ef372,
      0xa54ff53a,
      0x510e527f,
      0x9b05688c,
      0x1f83d9ab,
      0x5be0cd19,
  };
  std::array<uint8_t, 64> buffer_{};
  size_t buffer_size_ = 0;
  uint64_t bit_count_ = 0;
};

[[nodiscard]] std::string hex_digest(const std::array<uint8_t, 32>& digest) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const uint8_t value : digest) {
    out << std::setw(2) << static_cast<unsigned int>(value);
  }
  return out.str();
}

struct FileDigest {
  std::string sha256;
  uintmax_t size = 0;
};

[[nodiscard]] FileDigest digest_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw UserDataMigrationError(
        "Could not open file for hashing: " + path.string());
  }

  Sha256 hash;
  uintmax_t size = 0;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      hash.update(
          reinterpret_cast<const uint8_t*>(buffer.data()),
          static_cast<size_t>(count));
      size += static_cast<uintmax_t>(count);
    }
  }
  if (input.bad()) {
    throw UserDataMigrationError(
        "Could not finish hashing file: " + path.string());
  }
  return {hex_digest(hash.finish()), size};
}

[[nodiscard]] std::string digest_text(std::string_view text) {
  Sha256 hash;
  hash.update(text);
  return hex_digest(hash.finish());
}

[[nodiscard]] bool is_safe_relative_path(const fs::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_path()) {
    return false;
  }
  for (const auto& component : path) {
    if ((component == ".") || (component == "..") || component.empty()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_ancestor_or_same(
    const fs::path& ancestor,
    const fs::path& candidate) {
  auto ancestor_it = ancestor.begin();
  auto candidate_it = candidate.begin();
  for (; ancestor_it != ancestor.end(); ++ancestor_it, ++candidate_it) {
    if ((candidate_it == candidate.end()) || (*ancestor_it != *candidate_it)) {
      return false;
    }
  }
  return true;
}

void reject_parent_traversal(const fs::path& path, std::string_view name) {
  for (const auto& component : path) {
    if (component == "..") {
      throw UserDataMigrationError(
          std::string(name) + " must not contain parent traversal");
    }
  }
}

[[nodiscard]] fs::path normalize_and_validate_root(
    const fs::path& input,
    std::string_view name,
    bool must_exist) {
  reject_parent_traversal(input, name);
  std::error_code error;
  fs::path path = fs::absolute(input, error).lexically_normal();
  if (error || !path.is_absolute()) {
    throw UserDataMigrationError(
        "Could not resolve " + std::string(name) + " to an absolute path");
  }

  fs::path current;
  bool missing_component = false;
  for (const auto& component : path) {
    current /= component;
    if (missing_component) {
      continue;
    }
    error.clear();
    const auto status = fs::symlink_status(current, error);
    if (error == std::errc::no_such_file_or_directory) {
      missing_component = true;
      continue;
    }
    if (error) {
      throw UserDataMigrationError(
          "Could not inspect " + std::string(name) +
          " component " + current.string() + ": " + error.message());
    }
    if (fs::is_symlink(status)) {
      throw UserDataMigrationError(
          std::string(name) + " contains a symlink component: " +
          current.string());
    }
    if (!fs::is_directory(status)) {
      throw UserDataMigrationError(
          std::string(name) + " contains a non-directory component: " +
          current.string());
    }
  }
  if (must_exist && missing_component) {
    throw UserDataMigrationError(
        std::string(name) + " must be an existing directory");
  }
  return path;
}

struct ValidatedRoots {
  fs::path source;
  fs::path destination;
};

[[nodiscard]] ValidatedRoots validate_distinct_roots(
    const fs::path& source_root,
    const fs::path& destination_root) {
  ValidatedRoots roots{
      normalize_and_validate_root(source_root, "source root", true),
      normalize_and_validate_root(
          destination_root, "destination root", false),
  };

  std::error_code error;
  if (fs::exists(roots.destination) &&
      fs::equivalent(roots.source, roots.destination, error) && !error) {
    throw UserDataMigrationError(
        "Source and destination roots refer to the same directory");
  }

  const fs::path canonical_source = fs::weakly_canonical(roots.source);
  const fs::path canonical_destination = fs::weakly_canonical(roots.destination);
  if (is_ancestor_or_same(canonical_source, canonical_destination) ||
      is_ancestor_or_same(canonical_destination, canonical_source)) {
    throw UserDataMigrationError(
        "Source and destination roots must be distinct and non-overlapping");
  }
  return roots;
}

struct SourceFile {
  fs::path relative_path;
  fs::path source_path;
  FileDigest digest;
};

[[nodiscard]] std::vector<SourceFile> build_source_census(
    const fs::path& source_root) {
  std::vector<SourceFile> files;
  constexpr std::array<std::string_view, 2> allowed_directories = {
      "Character Files",
      "Save",
  };

  for (const auto directory_name : allowed_directories) {
    const fs::path tree_root = source_root / directory_name;
    std::error_code error;
    const auto tree_status = fs::symlink_status(tree_root, error);
    if (error == std::errc::no_such_file_or_directory) {
      continue;
    }
    if (error) {
      throw UserDataMigrationError(
          "Could not inspect source tree: " + tree_root.string());
    }
    if (fs::is_symlink(tree_status)) {
      throw UserDataMigrationError(
          "Source tree must not be a symlink: " + tree_root.string());
    }
    if (!fs::is_directory(tree_status)) {
      throw UserDataMigrationError(
          "Source tree is not a directory: " + tree_root.string());
    }

    fs::recursive_directory_iterator iterator(tree_root);
    const fs::recursive_directory_iterator end;
    for (; iterator != end; ++iterator) {
      const fs::path entry_path = iterator->path();
      const auto status = iterator->symlink_status();
      if (fs::is_symlink(status)) {
        throw UserDataMigrationError(
            "Symlinks are not permitted in imported data: " +
            entry_path.string());
      }
      if (fs::is_directory(status)) {
        continue;
      }
      if (!fs::is_regular_file(status)) {
        throw UserDataMigrationError(
            "Only regular files may be imported: " + entry_path.string());
      }

      const fs::path relative_path = entry_path.lexically_relative(source_root);
      if (!is_safe_relative_path(relative_path) ||
          (relative_path.begin()->generic_string() != directory_name)) {
        throw UserDataMigrationError(
            "Source path escapes the selected import trees: " +
            entry_path.string());
      }
      files.emplace_back(SourceFile{
          relative_path,
          entry_path,
          digest_file(entry_path),
      });
    }
  }

  std::sort(files.begin(), files.end(),
      [](const SourceFile& a, const SourceFile& b) {
        return a.relative_path.generic_string() <
            b.relative_path.generic_string();
      });
  return files;
}

[[nodiscard]] std::string import_id_for_census(
    const std::vector<SourceFile>& files) {
  Sha256 hash;
  hash.update("Realmz user-data import v1\n");
  for (const auto& file : files) {
    const std::string path = file.relative_path.generic_string();
    const std::string record = std::to_string(path.size()) + ":" + path +
        "\n" + file.digest.sha256 + "\n" +
        std::to_string(file.digest.size) + "\n";
    hash.update(record);
  }
  return "import-" + hex_digest(hash.finish());
}

[[nodiscard]] std::string encode_manifest_path(const fs::path& path) {
  static constexpr char hex[] = "0123456789ABCDEF";
  const std::string input = path.generic_string();
  std::string output;
  output.reserve(input.size());
  for (const unsigned char value : input) {
    const bool safe =
        ((value >= 'a') && (value <= 'z')) ||
        ((value >= 'A') && (value <= 'Z')) ||
        ((value >= '0') && (value <= '9')) ||
        (value == '-') || (value == '_') || (value == '.') ||
        (value == '/');
    if (safe) {
      output.push_back(static_cast<char>(value));
    } else {
      output.push_back('%');
      output.push_back(hex[value >> 4]);
      output.push_back(hex[value & 0x0F]);
    }
  }
  return output;
}

[[nodiscard]] int hex_value(char value) noexcept {
  if ((value >= '0') && (value <= '9')) {
    return value - '0';
  }
  if ((value >= 'A') && (value <= 'F')) {
    return value - 'A' + 10;
  }
  return -1;
}

[[nodiscard]] fs::path decode_manifest_path(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (size_t z = 0; z < input.size(); z++) {
    if (input[z] != '%') {
      output.push_back(input[z]);
      continue;
    }
    if ((z + 2 >= input.size()) ||
        (hex_value(input[z + 1]) < 0) || (hex_value(input[z + 2]) < 0)) {
      throw UserDataMigrationError("Manifest contains an invalid path escape");
    }
    output.push_back(static_cast<char>(
        (hex_value(input[z + 1]) << 4) | hex_value(input[z + 2])));
    z += 2;
  }
  const fs::path path = fs::path(output);
  if (!is_safe_relative_path(path)) {
    throw UserDataMigrationError("Manifest contains an unsafe relative path");
  }
  return path;
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw UserDataMigrationError("Could not read file: " + path.string());
  }
  std::ostringstream output;
  output << input.rdbuf();
  if (input.bad()) {
    throw UserDataMigrationError("Could not finish reading file: " + path.string());
  }
  return output.str();
}

void write_new_text_file(const fs::path& path, std::string_view contents) {
  // C11's x mode maps to exclusive creation. Unlike an existence check
  // followed by std::ofstream(trunc), this cannot overwrite a regular file or
  // follow a symlink raced into the staging name by another process.
  errno = 0;
#ifdef _WIN32
  std::FILE* output = _wfopen(path.c_str(), L"wbx");
#else
  std::FILE* output = std::fopen(path.c_str(), "wbx");
#endif
  if (!output) {
    throw UserDataMigrationError(
        "Could not exclusively create file " + path.string() + ": " +
        std::strerror(errno));
  }

  const size_t written = std::fwrite(
      contents.data(), 1, contents.size(), output);
  const bool write_failed =
      (written != contents.size()) || (std::fflush(output) != 0);
  const bool close_failed = std::fclose(output) != 0;
  if (write_failed || close_failed) {
    std::error_code ignored;
    fs::remove(path, ignored);
    throw UserDataMigrationError(
        "Could not finish writing file: " + path.string());
  }
}

void require_safe_existing_regular_file(
    const fs::path& path,
    std::string_view description) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error || fs::is_symlink(status) || !fs::is_regular_file(status)) {
    throw UserDataMigrationError(
        std::string(description) + " is missing or unsafe: " + path.string());
  }
}

void ensure_safe_directory(
    const fs::path& path,
    std::vector<fs::path>& created_directories) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (!error) {
    if (fs::is_symlink(status) || !fs::is_directory(status)) {
      throw UserDataMigrationError(
          "Destination path component is not a safe directory: " +
          path.string());
    }
    return;
  }
  if (error != std::errc::no_such_file_or_directory) {
    throw UserDataMigrationError(
        "Could not inspect destination directory: " + path.string());
  }
  error.clear();
  if (!fs::create_directory(path, error) || error) {
    throw UserDataMigrationError(
        "Could not create destination directory: " + path.string());
  }
  created_directories.emplace_back(path);
}

void ensure_safe_absolute_directory_tree(
    const fs::path& absolute_path,
    std::vector<fs::path>& created_directories) {
  if (!absolute_path.is_absolute()) {
    throw UserDataMigrationError(
        "Destination directory tree must use an absolute path");
  }
  fs::path current = absolute_path.root_path();
  ensure_safe_directory(current, created_directories);
  for (const auto& component : absolute_path.relative_path()) {
    current /= component;
    ensure_safe_directory(current, created_directories);
  }
}

void ensure_safe_relative_directory_tree(
    const fs::path& root,
    const fs::path& relative,
    std::vector<fs::path>& created_directories) {
  if (!relative.empty() && !is_safe_relative_path(relative)) {
    throw UserDataMigrationError("Unsafe destination directory path");
  }
  ensure_safe_directory(root, created_directories);
  fs::path current = root;
  for (const auto& component : relative) {
    current /= component;
    ensure_safe_directory(current, created_directories);
  }
}

void require_safe_relative_directory_tree(
    const fs::path& root,
    const fs::path& relative) {
  if (!relative.empty() && !is_safe_relative_path(relative)) {
    throw UserDataMigrationError("Unsafe destination directory path");
  }
  fs::path current = root;
  const auto require_directory = [](const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) || !fs::is_directory(status)) {
      throw UserDataMigrationError(
          "Expected a safe existing directory: " + path.string());
    }
  };
  require_directory(current);
  for (const auto& component : relative) {
    current /= component;
    require_directory(current);
  }
}

[[nodiscard]] fs::path unique_temporary_path(
    const fs::path& parent,
    std::string_view purpose) {
  static std::atomic<uint64_t> sequence = 0;
  for (size_t attempt = 0; attempt < 1024; attempt++) {
    const auto value = sequence.fetch_add(1, std::memory_order_relaxed);
    const fs::path candidate = parent /
        (".realmz-import-" + std::string(purpose) + "-" +
            std::to_string(value) + ".tmp");
    std::error_code error;
    const auto status = fs::symlink_status(candidate, error);
    if (error == std::errc::no_such_file_or_directory) {
      return candidate;
    }
    if (error) {
      throw UserDataMigrationError(
          "Could not inspect temporary path: " + candidate.string());
    }
    if (fs::is_symlink(status)) {
      throw UserDataMigrationError(
          "Refusing an unsafe temporary path: " + candidate.string());
    }
  }
  throw UserDataMigrationError("Could not allocate a unique temporary path");
}

void copy_file_and_verify(
    const fs::path& source,
    const fs::path& destination,
    const FileDigest& expected) {
  std::error_code error;
  if (!fs::copy_file(source, destination, fs::copy_options::none, error) || error) {
    throw UserDataMigrationError(
        "Could not copy " + source.string() + " to " +
        destination.string() + ": " + error.message());
  }
  const auto actual = digest_file(destination);
  if ((actual.sha256 != expected.sha256) || (actual.size != expected.size)) {
    throw UserDataMigrationError(
        "Hash or size mismatch after copying to: " + destination.string());
  }
}

void atomic_publish_without_overwrite(
    const fs::path& temporary,
    const fs::path& destination) {
  std::error_code error;
  fs::create_hard_link(temporary, destination, error);
  if (error) {
    throw UserDataMigrationError(
        "Could not atomically publish destination file without overwriting: " +
        destination.string() + ": " + error.message());
  }
  error.clear();
  if (!fs::remove(temporary, error) || error) {
    std::error_code rollback_error;
    fs::remove(destination, rollback_error);
    throw UserDataMigrationError(
        "Could not remove destination staging file: " + temporary.string());
  }
}

[[nodiscard]] std::string serialize_manifest(
    const std::vector<ImportedFile>& files) {
  std::string output(kManifestHeader);
  for (const auto& file : files) {
    output += encode_manifest_path(file.source_relative_path);
    output.push_back('\t');
    output += file.sha256;
    output.push_back('\t');
    output += std::to_string(file.size);
    output.push_back('\t');
    output += to_string(file.result);
    output.push_back('\n');
  }
  return output;
}

[[nodiscard]] std::vector<std::string_view> split_tabs(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.emplace_back(line.substr(start));
      return fields;
    }
    fields.emplace_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

[[nodiscard]] std::vector<ImportedFile> parse_and_validate_manifest(
    std::string_view manifest,
    const std::vector<SourceFile>& census) {
  if (!manifest.starts_with(kManifestHeader)) {
    throw UserDataMigrationError("Import manifest has an invalid header");
  }
  manifest.remove_prefix(kManifestHeader.size());

  std::vector<ImportedFile> results;
  results.reserve(census.size());
  for (const auto& source : census) {
    const size_t newline = manifest.find('\n');
    if (newline == std::string_view::npos) {
      throw UserDataMigrationError("Import manifest is truncated");
    }
    const auto fields = split_tabs(manifest.substr(0, newline));
    manifest.remove_prefix(newline + 1);
    if (fields.size() != 4) {
      throw UserDataMigrationError("Import manifest has an invalid record");
    }
    const fs::path relative_path = decode_manifest_path(fields[0]);
    if ((relative_path != source.relative_path) ||
        (fields[1] != source.digest.sha256)) {
      throw UserDataMigrationError(
          "Import manifest does not match the current source census");
    }
    uintmax_t size = 0;
    const auto parse = std::from_chars(
        fields[2].data(), fields[2].data() + fields[2].size(), size);
    if ((parse.ec != std::errc{}) ||
        (parse.ptr != fields[2].data() + fields[2].size()) ||
        (size != source.digest.size)) {
      throw UserDataMigrationError("Import manifest has an invalid file size");
    }
    FileImportResult result;
    if (fields[3] == "imported") {
      result = FileImportResult::imported;
    } else if (fields[3] == "skipped_existing") {
      result = FileImportResult::skipped_existing;
    } else {
      throw UserDataMigrationError("Import manifest has an invalid result");
    }
    results.emplace_back(ImportedFile{
        relative_path,
        source.digest.sha256,
        source.digest.size,
        result,
    });
  }
  if (!manifest.empty()) {
    throw UserDataMigrationError("Import manifest has unexpected extra records");
  }
  return results;
}

[[nodiscard]] std::string marker_contents(
    std::string_view import_id,
    std::string_view manifest_sha256) {
  return "Realmz user-data import v1\nimport_id\t" +
      std::string(import_id) + "\nmanifest_sha256\t" +
      std::string(manifest_sha256) + "\n";
}

void run_checkpoint(
    const UserDataMigrationOptions& options,
    MigrationCheckpoint checkpoint,
    const fs::path& relative_path = {}) {
  if (options.checkpoint_hook) {
    options.checkpoint_hook(checkpoint, relative_path);
  }
}

[[nodiscard]] bool path_exists_without_following_symlinks(
    const fs::path& path,
    fs::file_status& status) {
  std::error_code error;
  status = fs::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory) {
    return false;
  }
  if (error) {
    throw UserDataMigrationError(
        "Could not inspect path: " + path.string() + ": " + error.message());
  }
  return true;
}

} // namespace

const char* to_string(FileImportResult result) noexcept {
  switch (result) {
    case FileImportResult::imported:
      return "imported";
    case FileImportResult::skipped_existing:
      return "skipped_existing";
  }
  return "imported";
}

std::string compute_file_sha256(const fs::path& path) {
  return digest_file(path).sha256;
}

UserDataMigrationReport import_legacy_user_data(
    const fs::path& source_root,
    const fs::path& destination_root,
    const UserDataMigrationOptions& options) {
  if (source_root.empty() || destination_root.empty()) {
    throw UserDataMigrationError(
        "Source and destination roots must be explicit nonempty paths");
  }

  const auto roots = validate_distinct_roots(source_root, destination_root);
  const auto& source = roots.source;
  const auto& destination = roots.destination;
  const auto census = build_source_census(source);
  const std::string import_id = import_id_for_census(census);

  const fs::path backup_root = destination / kBackupRootDirectory;
  const fs::path backup_directory = backup_root / import_id;
  const fs::path manifest_path = backup_directory / kManifestName;
  const fs::path marker_path = backup_directory / kMarkerName;
  const fs::path backup_payload = backup_directory / kBackupPayloadDirectory;

  std::vector<fs::path> created_directories;
  std::vector<fs::path> created_destination_files;
  created_destination_files.reserve(census.size());
  std::vector<fs::path> temporary_files;
  bool owns_backup_directory = false;

  try {
    ensure_safe_absolute_directory_tree(destination, created_directories);
    ensure_safe_relative_directory_tree(
        destination,
        fs::path(kBackupRootDirectory),
        created_directories);

    fs::file_status backup_status;
    if (path_exists_without_following_symlinks(
            backup_directory, backup_status)) {
      if (fs::is_symlink(backup_status) || !fs::is_directory(backup_status)) {
        throw UserDataMigrationError(
            "Import backup path is not a safe directory");
      }
      require_safe_existing_regular_file(
          manifest_path, "Import manifest");
      require_safe_existing_regular_file(
          marker_path, "Import completion marker");
      const std::string manifest = read_text_file(manifest_path);
      const std::string expected_marker = marker_contents(
          import_id, digest_text(manifest));
      if (read_text_file(marker_path) != expected_marker) {
        throw UserDataMigrationError(
            "Import completion marker does not match its manifest");
      }
      for (const auto& source : census) {
        require_safe_relative_directory_tree(
            backup_payload, source.relative_path.parent_path());
        const fs::path backup_path = backup_payload / source.relative_path;
        require_safe_existing_regular_file(backup_path, "Imported backup");
        const auto backup_digest = digest_file(backup_path);
        if ((backup_digest.sha256 != source.digest.sha256) ||
            (backup_digest.size != source.digest.size)) {
          throw UserDataMigrationError(
              "Imported backup no longer matches its recorded hash");
        }
      }
      return UserDataMigrationReport{
          .status = MigrationRunStatus::already_completed,
          .import_id = import_id,
          .backup_directory = backup_directory,
          .manifest_path = manifest_path,
          .completion_marker_path = marker_path,
          .files = parse_and_validate_manifest(manifest, census),
      };
    }

    std::error_code error;
    if (!fs::create_directory(backup_directory, error) || error) {
      throw UserDataMigrationError(
          "Could not reserve import backup directory: " + error.message());
    }
    owns_backup_directory = true;
    ensure_safe_relative_directory_tree(
        backup_directory,
        fs::path(kBackupPayloadDirectory),
        created_directories);

    // Back up and verify the entire census before attempting any destination
    // file. Destination copies are made from these verified backups, never by
    // reopening source files.
    for (const auto& source : census) {
      const fs::path backup_path = backup_payload / source.relative_path;
      ensure_safe_relative_directory_tree(
          backup_payload,
          source.relative_path.parent_path(),
          created_directories);
      const fs::path backup_temporary = unique_temporary_path(
          backup_path.parent_path(), "backup");
      temporary_files.emplace_back(backup_temporary);
      copy_file_and_verify(
          source.source_path, backup_temporary, source.digest);
      atomic_publish_without_overwrite(backup_temporary, backup_path);
      temporary_files.pop_back();
      const auto published_digest = digest_file(backup_path);
      if ((published_digest.sha256 != source.digest.sha256) ||
          (published_digest.size != source.digest.size)) {
        throw UserDataMigrationError(
            "Published backup did not retain its verified hash");
      }
      run_checkpoint(
          options,
          MigrationCheckpoint::after_backup_file,
          source.relative_path);
    }
    run_checkpoint(options, MigrationCheckpoint::backups_complete);

    std::vector<ImportedFile> file_results;
    file_results.reserve(census.size());
    for (const auto& source : census) {
      const fs::path destination_file = destination / source.relative_path;
      ensure_safe_relative_directory_tree(
          destination,
          source.relative_path.parent_path(),
          created_directories);

      fs::file_status destination_status;
      if (path_exists_without_following_symlinks(
              destination_file, destination_status)) {
        if (fs::is_symlink(destination_status)) {
          throw UserDataMigrationError(
              "Destination file must not be a symlink: " +
              destination_file.string());
        }
        if (!fs::is_regular_file(destination_status)) {
          throw UserDataMigrationError(
              "Destination collision is not a regular file: " +
              destination_file.string());
        }
        file_results.emplace_back(ImportedFile{
            source.relative_path,
            source.digest.sha256,
            source.digest.size,
            FileImportResult::skipped_existing,
        });
        continue;
      }

      run_checkpoint(
          options,
          MigrationCheckpoint::before_destination_file,
          source.relative_path);
      const fs::path destination_temporary = unique_temporary_path(
          destination_file.parent_path(), "destination");
      temporary_files.emplace_back(destination_temporary);
      copy_file_and_verify(
          backup_payload / source.relative_path,
          destination_temporary,
          source.digest);
      atomic_publish_without_overwrite(
          destination_temporary, destination_file);
      temporary_files.pop_back();
      created_destination_files.emplace_back(destination_file);
      const auto destination_digest = digest_file(destination_file);
      if ((destination_digest.sha256 != source.digest.sha256) ||
          (destination_digest.size != source.digest.size)) {
        throw UserDataMigrationError(
            "Destination copy did not retain its verified hash");
      }
      file_results.emplace_back(ImportedFile{
          source.relative_path,
          source.digest.sha256,
          source.digest.size,
          FileImportResult::imported,
      });
      run_checkpoint(
          options,
          MigrationCheckpoint::after_destination_file,
          source.relative_path);
    }

    const std::string manifest = serialize_manifest(file_results);
    const fs::path manifest_temporary = unique_temporary_path(
        backup_directory, "manifest");
    temporary_files.emplace_back(manifest_temporary);
    write_new_text_file(manifest_temporary, manifest);
    if (digest_file(manifest_temporary).sha256 != digest_text(manifest)) {
      throw UserDataMigrationError("Manifest hash verification failed");
    }
    atomic_publish_without_overwrite(manifest_temporary, manifest_path);
    temporary_files.pop_back();

    run_checkpoint(options, MigrationCheckpoint::before_commit);
    // Revalidate the entire backup set at the commit boundary. This also
    // covers skipped destination collisions, for which the copy loop never
    // needs to reopen the backup payload.
    for (const auto& source : census) {
      const fs::path backup_path = backup_payload / source.relative_path;
      require_safe_existing_regular_file(backup_path, "Imported backup");
      const auto backup_digest = digest_file(backup_path);
      if ((backup_digest.sha256 != source.digest.sha256) ||
          (backup_digest.size != source.digest.size)) {
        throw UserDataMigrationError(
            "Imported backup changed before migration commit");
      }
    }
    const std::string marker = marker_contents(
        import_id, digest_text(manifest));
    const fs::path marker_temporary = unique_temporary_path(
        backup_directory, "marker");
    temporary_files.emplace_back(marker_temporary);
    write_new_text_file(marker_temporary, marker);
    atomic_publish_without_overwrite(marker_temporary, marker_path);
    temporary_files.pop_back();

    return UserDataMigrationReport{
        .status = MigrationRunStatus::completed,
        .import_id = import_id,
        .backup_directory = backup_directory,
        .manifest_path = manifest_path,
        .completion_marker_path = marker_path,
        .files = std::move(file_results),
    };

  } catch (...) {
    const std::exception_ptr original_error = std::current_exception();
    std::vector<std::string> rollback_errors;
    const auto remove_one = [&](const fs::path& path) {
      std::error_code error;
      fs::remove(path, error);
      if (error) {
        rollback_errors.emplace_back(
            path.string() + ": " + error.message());
      }
    };
    for (auto iterator = temporary_files.rbegin();
         iterator != temporary_files.rend(); ++iterator) {
      remove_one(*iterator);
    }
    for (auto iterator = created_destination_files.rbegin();
         iterator != created_destination_files.rend(); ++iterator) {
      remove_one(*iterator);
    }
    if (owns_backup_directory) {
      std::error_code error;
      fs::remove_all(backup_directory, error);
      if (error) {
        rollback_errors.emplace_back(
            backup_directory.string() + ": " + error.message());
      }
    }
    for (auto iterator = created_directories.rbegin();
         iterator != created_directories.rend(); ++iterator) {
      remove_one(*iterator);
    }

    if (!rollback_errors.empty()) {
      std::string original_detail = "non-standard exception";
      try {
        std::rethrow_exception(original_error);
      } catch (const std::exception& error) {
        original_detail = error.what();
      } catch (...) {
      }
      std::string detail =
          "Migration failed (" + original_detail +
          "); rollback was incomplete";
      for (const auto& rollback_error : rollback_errors) {
        detail += "\n- " + rollback_error;
      }
      throw UserDataMigrationError(std::move(detail));
    }
    std::rethrow_exception(original_error);
  }
}

} // namespace realmz::userdata
