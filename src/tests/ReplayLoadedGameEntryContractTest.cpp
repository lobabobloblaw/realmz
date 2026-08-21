#include "realmz_orig/time-scale.h"

#include <array>
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
    } else if (source[position] == '}' && --depth == 0) {
      return std::string_view(source).substr(
          opening, position - opening + 1);
    }
  }
  throw std::runtime_error(
      "unterminated function body: " + std::string(signature));
}

[[nodiscard]] std::size_t require_after(
    std::string_view body,
    std::string_view token,
    std::size_t previous,
    std::string_view detail) {
  const std::size_t position = body.find(token, previous);
  require(position != std::string_view::npos && position >= previous, detail);
  return position + token.size();
}

void verify_shared_setup(std::string_view setup) {
  std::size_t position = 0;
  for (const std::string_view operation : {
           "EnableItem(gScenario, 0)",
           "EnableItem(gParty, 0)",
           "EnableItem(gParty, 1)",
           "EnableItem(gParty, 2)",
           "EnableItem(gParty, 3)",
           "EnableItem(gParty, 7)",
           "DisableItem(gParty, 5)",
           "EnableItem(gGame, 3)",
           "DisableItem(gGame, 1)",
           "EnableItem(gBeast, 0)",
           "EnableItem(gFile, 3)",
           "EnableItem(gFile, 4)",
           "EnableItem(gFile, 2)",
           "DisableItem(gFile, 1)",
           "TextFont(defaultfont)",
           "DrawMenuBar()",
           "if (partycondition[PARTY_COND_TORCH_LIT])",
           "loaddark((partycondition[PARTY_COND_TORCH_LIT] / 30) + 1)",
           "else",
           "loaddark(0)",
       }) {
    position = require_after(
        setup, operation, position,
        "shared post-load operations must retain their legacy order");
  }

  require(setup.find("mainscreeninit") == std::string_view::npos,
      "shared setup must leave gameplay entry to its caller");
  require(setup.find("revertgame") == std::string_view::npos,
      "shared setup must not alter interactive revert control flow");
  require(setup.find("load(") == std::string_view::npos,
      "shared setup must not perform another load");
}

void verify_callers(const std::string& source) {
  const std::string_view replay = function_body(
      source, "void RealmzReplayEnterLoadedGame(void)");
  const std::size_t replay_setup = replay.find("prepare_loaded_game()");
  const std::size_t replay_entry = replay.find("mainscreeninit(0, 0)");
  require(replay_setup != std::string_view::npos &&
          replay_entry != std::string_view::npos &&
          replay_setup < replay_entry,
      "replay entry must apply shared setup before entering the main screen");
  require(replay.find("revertgame") == std::string_view::npos,
      "replay entry must not inherit interactive revert branching");
  require(replay.find("load(") == std::string_view::npos,
      "replay entry must operate only after an explicit replay load");

  const std::string_view ordinary =
      function_body(source, "short HandleMenuChoice(void)");
  const std::size_t label = ordinary.find("playsaved:");
  const std::size_t ordinary_setup =
      ordinary.find("prepare_loaded_game()", label);
  const std::size_t revert_check = ordinary.find("if (!revertgame)", label);
  const std::size_t ordinary_entry =
      ordinary.find("mainscreeninit(0, 0)", label);
  const std::size_t revert_return = ordinary.find("return (0)", revert_check);
  require(label != std::string_view::npos &&
          ordinary_setup != std::string_view::npos &&
          revert_check != std::string_view::npos &&
          ordinary_entry != std::string_view::npos &&
          revert_return != std::string_view::npos &&
          label < ordinary_setup && ordinary_setup < revert_check &&
          revert_check < ordinary_entry && ordinary_entry < revert_return,
      "interactive load/revert must preserve setup, entry, and return order");
}

void verify_header(const fs::path& root) {
  const std::string header =
      read_file(root / "src/replay/ReplaySlotSelection.h");
  require(header.find("void RealmzReplayEnterLoadedGame(void);") !=
          std::string::npos,
      "the replay post-load entry point must be declared in the C ABI header");
  require(header.find("extern \"C\"") != std::string::npos,
      "the replay header must retain C linkage for C++ callers");
}

void verify_dungeon_time_scale(const fs::path& root) {
  std::array<short, 20> base_scale{};
  require(RealmzTimeScaleForLocation(1, -1, nullptr, 0) == 1,
      "a cold dungeon load must select indoor time without tile metadata");
  require(RealmzTimeScaleForLocation(1, 255, nullptr, 0) == 1,
      "dungeon time must not depend on the signedness of lastpix");
  require(RealmzTimeScaleForLocation(
              0, -1, base_scale.data(), base_scale.size()) == 5 &&
          RealmzTimeScaleForLocation(
              0, 20, base_scale.data(), base_scale.size()) == 5 &&
          RealmzTimeScaleForLocation(
              0, 255, base_scale.data(), base_scale.size()) == 5,
      "invalid outdoor tile indices must retain the default time scale");
  require(RealmzTimeScaleForLocation(0, 0, nullptr, 20) == 5,
      "missing outdoor tile metadata must retain the default time scale");
  base_scale[0] = 1;
  base_scale[19] = 2;
  require(RealmzTimeScaleForLocation(
              0, 0, base_scale.data(), base_scale.size()) == 1 &&
          RealmzTimeScaleForLocation(
              0, 19, base_scale.data(), base_scale.size()) == 1,
      "valid nonzero tile metadata must select indoor time");
  base_scale[0] = 0;
  require(RealmzTimeScaleForLocation(
              0, 0, base_scale.data(), base_scale.size()) == 5,
      "valid zero tile metadata must retain outdoor time");

  const std::string source =
      read_file(root / "src/realmz_orig/textbox-time.c");
  const std::string_view body =
      function_body(source, "short timeclick(unsigned char number");
  require(body.find("short scale = RealmzTimeScaleForLocation(") !=
              std::string_view::npos,
      "timeclick must use the tested location time-scale policy");
  require(body.find("basescale[lastpix]") == std::string_view::npos,
      "timeclick must not retain a direct basescale lookup");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: ReplayLoadedGameEntryContractTest <repository-root>");
    }

    const fs::path root = fs::weakly_canonical(argv[1]);
    const std::string source =
        read_file(root / "src/realmz_orig/handlemenuchoice.c");
    verify_shared_setup(function_body(
        source, "static void prepare_loaded_game(void)"));
    verify_callers(source);
    verify_header(root);
    verify_dungeon_time_scale(root);
    std::cout << "ReplayLoadedGameEntryContractTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplayLoadedGameEntryContractTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
