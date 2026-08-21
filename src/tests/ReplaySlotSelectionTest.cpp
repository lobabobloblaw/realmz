#include "replay/ReplaySlotSelection.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

void test_complete_char_domain() {
  for (int value = std::numeric_limits<char>::min();
       value <= std::numeric_limits<char>::max(); ++value) {
    const char slot = static_cast<char>(value);
    const short expected = ((value >= 'A') && (value <= 'J'))
        ? static_cast<short>(4 + (value - 'A'))
        : 0;
    require(RealmzReplayLegacyChoiceForSlot(slot) == expected,
        "slot mapping must accept exactly uppercase ASCII A through J");
  }
  require(RealmzReplayLegacyChoiceForSlot('A') == 4,
      "slot A must map to legacy choice 4");
  require(RealmzReplayLegacyChoiceForSlot('J') == 13,
      "slot J must map to legacy choice 13");
}

void test_load_source_contract(const fs::path& root) {
  const std::string source =
      read_file(root / "src/realmz_orig/loadsavedgame.c");
  const std::string_view ordinary = function_body(source, "short load(void)");
  const std::string_view replay =
      function_body(source, "short RealmzReplayLoadSlot(char slot)");
  const std::string_view selected =
      function_body(source, "static short load_selected(short choice)");

  const std::size_t chooser = ordinary.find("fileprep(1)");
  const std::size_t redraw = ordinary.find("DrawDialog(background)");
  const std::size_t cancel = ordinary.find("if (!choice)");
  const std::size_t dispatch = ordinary.find("load_selected(choice)");
  require(chooser != std::string_view::npos &&
          redraw != std::string_view::npos &&
          cancel != std::string_view::npos &&
          dispatch != std::string_view::npos &&
          chooser < redraw && redraw < cancel && cancel < dispatch,
      "ordinary load must retain chooser, redraw, cancel, and dispatch order");

  const std::size_t mapping =
      replay.find("RealmzReplayLegacyChoiceForSlot(slot)");
  const std::size_t invalid_return = replay.find("if (!choice)");
  const std::size_t dungeon_update =
      replay.find("needdungeonupdate = TRUE");
  const std::size_t replay_dispatch = replay.find("load_selected(choice)");
  require(mapping != std::string_view::npos &&
          invalid_return != std::string_view::npos &&
          dungeon_update != std::string_view::npos &&
          replay_dispatch != std::string_view::npos &&
          mapping < invalid_return && invalid_return < dungeon_update &&
          dungeon_update < replay_dispatch,
      "replay load must validate before its sole legacy-state side effect");
  require(replay.find("fileprep") == std::string_view::npos,
      "replay load must not invoke the chooser");
  require(replay.find("lastgame") == std::string_view::npos,
      "replay load must not inspect or mutate lastgame");
  require(replay.find("savepref") == std::string_view::npos,
      "replay load must not write preferences");
  require(replay.find("DrawDialog") == std::string_view::npos,
      "replay load must not perform the chooser redraw");

  require(selected.find("fileprep") == std::string_view::npos,
      "selected legacy body must remain chooser-independent");
  require(selected.find("switch (choice)") != std::string_view::npos,
      "selected legacy body must retain the existing slot path switch");
  require(selected.find("Data I1") != std::string_view::npos &&
          selected.find("return (1)") != std::string_view::npos,
      "selected legacy body must retain I1 loading and success return");
}

void test_save_source_contract(const fs::path& root) {
  const std::string source =
      read_file(root / "src/realmz_orig/save-direction-order.c");
  const std::string_view ordinary =
      function_body(source, "void save(short mode)");
  const std::string_view replay =
      function_body(source, "short RealmzReplaySaveSlot(char slot)");
  const std::string_view prelude =
      function_body(source, "static void save_prelude(void) {");
  const std::string_view selected = function_body(
      source,
      "static short save_selected(short choice, short preserve_land_sites)");

  const std::size_t ordinary_prelude = ordinary.find("save_prelude()");
  const std::size_t chooser = ordinary.find("fileprep(mode)");
  const std::size_t center = ordinary.find("centerpict()");
  const std::size_t cancel = ordinary.find("if (!choice)");
  const std::size_t ordinary_dispatch =
      ordinary.find("save_selected(choice, FALSE)");
  require(ordinary_prelude != std::string_view::npos &&
          chooser != std::string_view::npos &&
          center != std::string_view::npos &&
          cancel != std::string_view::npos &&
          ordinary_dispatch != std::string_view::npos &&
          ordinary_prelude < chooser && chooser < center && center < cancel &&
          cancel < ordinary_dispatch,
      "ordinary save must retain prelude, chooser, center, cancel, and writer order");

  const std::size_t mapping =
      replay.find("RealmzReplayLegacyChoiceForSlot(slot)");
  const std::size_t invalid = replay.find("if (!choice)");
  const std::size_t invalid_return = replay.find("return (0)", invalid);
  const std::size_t dungeon_update =
      replay.find("needdungeonupdate = TRUE");
  const std::size_t replay_prelude = replay.find("save_prelude()");
  const std::size_t replay_center = replay.find("centerpict()");
  const std::size_t replay_dispatch =
      replay.find("save_selected(choice, TRUE)");
  require(mapping != std::string_view::npos &&
          invalid != std::string_view::npos &&
          invalid_return != std::string_view::npos &&
          dungeon_update != std::string_view::npos &&
          replay_prelude != std::string_view::npos &&
          replay_center != std::string_view::npos &&
          replay_dispatch != std::string_view::npos &&
          mapping < invalid && invalid < invalid_return &&
          invalid_return < dungeon_update && dungeon_update < replay_prelude &&
          replay_prelude < replay_center &&
          replay_center < replay_dispatch,
      "replay save must reject invalid slots before restoring fileprep's "
      "dungeon-update side effect and entering the legacy save path");
  require(replay.find("fileprep") == std::string_view::npos,
      "replay save must not invoke the chooser");
  require(replay.find("lastgame") == std::string_view::npos,
      "replay save must not inspect or mutate lastgame");
  require(replay.find("savepref") == std::string_view::npos,
      "replay save must not write preferences");

  const std::size_t resource_check = prelude.find("CountResources('RLMZ')");
  const std::size_t disk_check = prelude.find("GetVInfo(");
  const std::size_t save_land = prelude.find("saveland(");
  const std::size_t save_shop = prelude.find("saveshop(1");
  const std::size_t save_panel = prelude.find("in()");
  require(resource_check != std::string_view::npos &&
          disk_check != std::string_view::npos &&
          save_land != std::string_view::npos &&
          save_shop != std::string_view::npos &&
          save_panel != std::string_view::npos &&
          resource_check < disk_check && disk_check < save_land &&
          save_land < save_shop && save_shop < save_panel,
      "shared save prelude must retain all legacy side effects in order");
  require(prelude.find("fileprep") == std::string_view::npos,
      "shared save prelude must remain chooser-independent");

  const std::size_t land_data = selected.find("Data A1");
  require(land_data != std::string_view::npos,
      "selected save body must retain Data A1 output");
  const std::string_view land_copy = selected.substr(land_data);
  const std::string_view site_guard =
      "if ((!indung) || preserve_land_sites)";
  const std::size_t site_read_guard = land_copy.find(site_guard);
  const std::size_t site_write_guard = land_copy.find(
      site_guard, site_read_guard == std::string_view::npos
          ? 0
          : site_read_guard + site_guard.size());
  require(site_read_guard != std::string_view::npos &&
          site_write_guard != std::string_view::npos,
      "replay Data A1 must retain both site reads and site writes");
  require(land_copy.find(site_guard, site_write_guard + site_guard.size()) ==
              std::string_view::npos,
      "Data A1 must have exactly two replay site-preservation guards");
  require(selected.find("fileprep") == std::string_view::npos,
      "selected save body must remain chooser-independent");
  require(selected.find("return (1)") != std::string_view::npos,
      "selected save body must report only a normally reached end");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: ReplaySlotSelectionTest <repository-root>");
    }
    test_complete_char_domain();
    const fs::path root = fs::weakly_canonical(argv[1]);
    test_load_source_contract(root);
    test_save_source_contract(root);
    std::cout << "ReplaySlotSelectionTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ReplaySlotSelectionTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
