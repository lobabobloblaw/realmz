#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

std::size_t checks_run = 0;

void require(bool condition, std::string_view detail) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(std::string(detail));
  }
}

[[nodiscard]] std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not open source file: " + path.string());
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string_view function_body(
    const std::string& source, std::string_view signature) {
  const std::size_t signature_position = source.find(signature);
  if (signature_position == std::string::npos) {
    throw std::runtime_error(
        "missing function signature: " + std::string(signature));
  }
  const std::size_t opening = source.find('{', signature_position);
  if (opening == std::string::npos) {
    throw std::runtime_error(
        "missing function body: " + std::string(signature));
  }
  std::size_t depth = 0;
  for (std::size_t position = opening; position < source.size(); ++position) {
    if (source[position] == '{') {
      ++depth;
    } else if (source[position] == '}') {
      if (--depth == 0) {
        return std::string_view(source).substr(
            opening, position - opening + 1);
      }
    }
  }
  throw std::runtime_error(
      "unterminated function body: " + std::string(signature));
}

void verify_early_child_dispatch(const fs::path& root) {
  const std::string main_source =
      read_file(root / "src/realmz_orig/main.c");
  const std::string_view main_body =
      function_body(main_source, "int main(int argc, char* argvp[])");
  const std::size_t configure =
      main_body.find("RealmzConfigureSemanticReplayChild(argvp[2])");
  const std::size_t toolbox = main_body.find("ToolBoxInit()");
  const std::size_t run = main_body.find("RealmzRunSemanticReplayChild()");
  const std::size_t ambient_loop = main_body.find("MainLoop()");
  require(configure != std::string_view::npos && configure < toolbox,
      "replay config must be installed before ToolBoxInit");
  require(toolbox < run && run < ambient_loop,
      "configured replay must bypass the ambient MainLoop");
  require(main_body.find("(argument_index != 1) || (argc != 3)") !=
          std::string_view::npos,
      "replay child command line must be exact");

  const std::string child_source =
      read_file(root / "src/SemanticReplayChild.cpp");
  const std::string_view configure_body = function_body(
      child_source, "RealmzConfigureSemanticReplayChild(");
  const std::size_t validate_slots =
      configure_body.find("require_fresh_output_slot");
  const std::size_t set_root = configure_body.find(
      "set_semantic_replay_user_data_root");
  const std::size_t install_runtime =
      configure_body.find("install_replay_runtime");
  require(validate_slots != std::string_view::npos &&
          validate_slots < set_root && set_root < install_runtime,
      "slot validation and replay root must precede runtime publication");

  const std::string_view run_body =
      function_body(child_source, "RealmzRunSemanticReplayChild(void)");
  require(run_body.find(
              "REALMZ_SEMANTIC_REPLAY_DRIVER_UNAVAILABLE_EXIT") !=
          std::string_view::npos,
      "bootstrap must fail explicitly until the native driver exists");
  require(run_body.find("result_path") == std::string_view::npos,
      "placeholder bootstrap must not emit a replay result");
}

void verify_isolated_file_policy(const fs::path& root) {
  const std::string file_source = read_file(root / "src/FileManager.cpp");
  const std::string_view open_body =
      function_body(file_source, "FILE* mac_fopen(");
  const std::size_t user_only_branch =
      open_body.find("if (user_only_read)");
  const std::size_t reject_input_write =
      open_body.find("if (write_capable)", user_only_branch);
  const std::size_t secure_input_open =
      open_body.find("open_replay_input_file_no_follow", user_only_branch);
  const std::size_t ordinary_open = open_body.find(
      "return fopen(host_filename.c_str(), mode)");
  require(user_only_branch != std::string_view::npos &&
          reject_input_write != std::string_view::npos &&
          user_only_branch < reject_input_write &&
          reject_input_write < secure_input_open &&
          secure_input_open < ordinary_open,
      "write-capable opens of the replay input subtree must fail closed");
  require(file_source.find("openat(descriptor") != std::string::npos &&
          file_source.find("O_NOFOLLOW") != std::string::npos,
      "replay input reads must use descriptor-relative no-follow traversal");

  const std::string_view list_body =
      function_body(file_source, "mac_list_directory(");
  const std::size_t secure_list =
      list_body.find("list_replay_input_directory_no_follow");
  const std::size_t early_return =
      list_body.find("return ret", secure_list);
  const std::size_t create = list_body.find("SDL_CreateDirectory");
  require(secure_list != std::string_view::npos &&
          early_return != std::string_view::npos &&
          create != std::string_view::npos && early_return < create,
      "listing replay Game A must not create directories");
  require(file_source.find("fdopendir") != std::string::npos,
      "replay input directory listing must stay on the pinned descriptor");
  require(list_body.find(
              "append_directory(host_filename_for_mac_filename") !=
          std::string_view::npos,
      "ordinary content must retain bundled directory fallback");
}

void verify_deterministic_runtime_hooks(const fs::path& root) {
  const std::string paths_source =
      read_file(root / "src/UserDataPaths.cpp");
  const std::string_view set_root = function_body(
      paths_source, "set_semantic_replay_user_data_root(");
  require(set_root.find("state.getter_called") != std::string_view::npos,
      "replay root installation must fail after the first getter");
  require(set_root.find("symlink_status") != std::string_view::npos,
      "replay root must be a physical directory");

  const std::string memory_source =
      read_file(root / "src/MemoryManager.cpp");
  const std::string_view random_body =
      function_body(memory_source, "int16_t Random(void)");
  require(random_body.find("next_classic_random") <
          random_body.find("phosg::random_object"),
      "Classic Random must delegate only when replay is installed");

  const std::string port_prefs_source =
      read_file(root / "src/PortPrefs.cpp");
  const std::string_view load_prefs =
      function_body(port_prefs_source, "PortPrefs load_port_prefs()");
  require(load_prefs.find("installed_replay_runtime") <
          load_prefs.find("prefs_path()"),
      "replay must bypass ambient port preference reads");
  const std::string_view save_prefs =
      function_body(port_prefs_source, "void save_port_prefs(");
  require(save_prefs.find("preferences_writes_enabled") <
          save_prefs.find("prefs_path()"),
      "replay must bypass port preference writes");

  const std::string legacy_prefs_source =
      read_file(root / "src/realmz_orig/pref.c");
  require(legacy_prefs_source.find(
              "UseResFile(Appl_Rsrc_Fork_Ref_Num)") != std::string::npos,
      "replay legacy preferences must use bundled PRFN defaults");
  const std::size_t native_getpref_position =
      legacy_prefs_source.rfind("void getpref(void)");
  require(native_getpref_position != std::string::npos,
      "native getpref implementation is missing");
  const std::string native_getpref_source =
      legacy_prefs_source.substr(native_getpref_position);
  const std::string_view native_getpref =
      function_body(native_getpref_source, "void getpref(void)");
  require(native_getpref.find("NewHandleWithData") !=
              std::string_view::npos &&
          native_getpref.find("DetachResource") == std::string_view::npos,
      "bundled PRFN defaults must be cloned, not detached from the app fork");
  require(native_getpref.find("UseResFile(oldresfile)") <
          native_getpref.find("prefs = *(PrefHandle)data_handle"),
      "getpref must restore the app resource selection before conversion");
  require(legacy_prefs_source.find(
              "if (RealmzSemanticReplayChildIsActive())\n    return (-1);") !=
          std::string::npos,
      "legacy preference auto-create/open must be disabled");

  const std::string window_source =
      read_file(root / "src/WindowManager.cpp");
  const std::string_view set_mode = function_body(
      window_source, "void WindowManager::set_presentation_mode(");
  require(set_mode.find("forced_replay_presentation_mode") !=
          std::string_view::npos,
      "configured replay presentation mode must be locked");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: SemanticReplayStartupContractTest <repository-root>");
    }
    const fs::path root = fs::weakly_canonical(argv[1]);
    verify_early_child_dispatch(root);
    verify_isolated_file_policy(root);
    verify_deterministic_runtime_hooks(root);
    std::cout << "SemanticReplayStartupContractTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayStartupContractTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
