#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

int checks_run = 0;

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

// Removes comments and literals while preserving byte positions and newlines.
// Structural checks therefore ignore formatting and cannot be satisfied by a
// comment or diagnostic string that merely names a guarded API.
[[nodiscard]] std::string code_only(std::string_view source) {
  enum class State {
    code,
    line_comment,
    block_comment,
    string_literal,
    character_literal,
  };

  std::string result(source);
  State state = State::code;
  bool escaped = false;
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char current = source[index];
    const char next =
        (index + 1 < source.size()) ? source[index + 1] : '\0';

    if (state == State::code) {
      if (current == '/' && next == '/') {
        result[index] = ' ';
        result[index + 1] = ' ';
        ++index;
        state = State::line_comment;
      } else if (current == '/' && next == '*') {
        result[index] = ' ';
        result[index + 1] = ' ';
        ++index;
        state = State::block_comment;
      } else if (current == '"') {
        result[index] = ' ';
        escaped = false;
        state = State::string_literal;
      } else if (current == '\'') {
        result[index] = ' ';
        escaped = false;
        state = State::character_literal;
      }
      continue;
    }

    if (current != '\n') {
      result[index] = ' ';
    }
    if (state == State::line_comment) {
      if (current == '\n') {
        state = State::code;
      }
    } else if (state == State::block_comment) {
      if (current == '*' && next == '/') {
        result[index + 1] = ' ';
        ++index;
        state = State::code;
      }
    } else {
      const char delimiter =
          (state == State::string_literal) ? '"' : '\'';
      if (!escaped && current == delimiter) {
        state = State::code;
      }
      if (!escaped && current == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
    }
  }
  return result;
}

[[nodiscard]] bool is_identifier_character(char value) noexcept {
  const auto byte = static_cast<unsigned char>(value);
  return std::isalnum(byte) != 0 || value == '_';
}

[[nodiscard]] std::size_t find_identifier(
    std::string_view source,
    std::string_view identifier,
    std::size_t start = 0) noexcept {
  for (std::size_t position = source.find(identifier, start);
       position != std::string_view::npos;
       position = source.find(identifier, position + 1)) {
    const bool left_boundary =
        position == 0 || !is_identifier_character(source[position - 1]);
    const std::size_t end = position + identifier.size();
    const bool right_boundary =
        end == source.size() || !is_identifier_character(source[end]);
    if (left_boundary && right_boundary) {
      return position;
    }
  }
  return std::string_view::npos;
}

[[nodiscard]] std::size_t count_identifier(
    std::string_view source,
    std::string_view identifier) noexcept {
  std::size_t count = 0;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t found = find_identifier(source, identifier, cursor);
    if (found == std::string_view::npos) {
      return count;
    }
    ++count;
    cursor = found + identifier.size();
  }
}

[[nodiscard]] std::size_t skip_whitespace(
    std::string_view source,
    std::size_t position) noexcept {
  while (position < source.size() &&
         std::isspace(static_cast<unsigned char>(source[position])) != 0) {
    ++position;
  }
  return position;
}

[[nodiscard]] std::size_t matching_delimiter(
    std::string_view source,
    std::size_t opening,
    char open,
    char close) {
  require(opening < source.size() && source[opening] == open,
      "delimiter scan did not start on the expected opening character");
  std::size_t depth = 0;
  for (std::size_t position = opening; position < source.size(); ++position) {
    if (source[position] == open) {
      ++depth;
    } else if (source[position] == close) {
      require(depth > 0, "source contains an unmatched closing delimiter");
      --depth;
      if (depth == 0) {
        return position;
      }
    }
  }
  throw std::runtime_error("source contains an unmatched opening delimiter");
}

[[nodiscard]] std::string function_body(
    std::string_view stripped_source,
    std::string_view function_name) {
  std::size_t cursor = 0;
  while (true) {
    const std::size_t name =
        find_identifier(stripped_source, function_name, cursor);
    if (name == std::string_view::npos) {
      throw std::runtime_error(
          "could not find function definition: " +
          std::string(function_name));
    }
    std::size_t position = skip_whitespace(
        stripped_source, name + function_name.size());
    if (position >= stripped_source.size() ||
        stripped_source[position] != '(') {
      cursor = name + function_name.size();
      continue;
    }
    const std::size_t parameters_end = matching_delimiter(
        stripped_source, position, '(', ')');
    position = skip_whitespace(stripped_source, parameters_end + 1);
    if (position < stripped_source.size() &&
        stripped_source[position] == '{') {
      const std::size_t body_end = matching_delimiter(
          stripped_source, position, '{', '}');
      return std::string(
          stripped_source.substr(position, body_end - position + 1));
    }
    cursor = parameters_end + 1;
  }
}

[[nodiscard]] std::string without_whitespace(std::string_view source) {
  std::string result;
  result.reserve(source.size());
  for (const char value : source) {
    if (std::isspace(static_cast<unsigned char>(value)) == 0) {
      result.push_back(value);
    }
  }
  return result;
}

[[nodiscard]] std::string designated_lambda_body(
    std::string_view aggregate,
    std::string_view field) {
  require(count_identifier(aggregate, field) == 1,
      std::string("combat sink aggregate must initialize exactly one .") +
          std::string(field));
  const std::size_t name = find_identifier(aggregate, field);
  require(name > 0 && aggregate[name - 1] == '.',
      std::string("combat sink must use designated field .") +
          std::string(field));
  std::size_t position = skip_whitespace(aggregate, name + field.size());
  require(position < aggregate.size() && aggregate[position] == '=',
      std::string("combat sink field is missing an initializer: ") +
          std::string(field));
  position = skip_whitespace(aggregate, position + 1);
  require(position < aggregate.size() && aggregate[position] == '[',
      std::string("combat sink field must own a lambda: ") +
          std::string(field));
  const std::size_t body_open = aggregate.find('{', position);
  require(body_open != std::string_view::npos,
      std::string("combat sink lambda is missing its body: ") +
          std::string(field));
  const std::size_t body_close = matching_delimiter(
      aggregate, body_open, '{', '}');
  return std::string(
      aggregate.substr(body_open, body_close - body_open + 1));
}

void require_no_semantic_scope_or_consumer(
    std::string_view body,
    std::string_view function_name) {
  require(count_identifier(body, "GetNextSemanticGameplayEvent") == 0,
      std::string(function_name) +
          " must not use the top-level semantic gameplay wrapper");
  require(count_identifier(body, "RealmzBeginSemanticInputSurface") == 0,
      std::string(function_name) + " must not begin a semantic input scope");
  require(count_identifier(body, "RealmzEndSemanticInputSurface") == 0,
      std::string(function_name) + " must not end a semantic input scope");
  require(count_identifier(body, "RealmzConsumeSemanticMovementEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic movement");
  require(count_identifier(
              body, "RealmzConsumeSemanticPartySelectionEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic party selection");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenInventoryEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic inventory");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSpellbookEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic spellbook input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSaveGameEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic save input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenLoadGameEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic load input");
  require(count_identifier(
              body, "RealmzConsumeSemanticGuardCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic guard input");
  require(count_identifier(
              body, "RealmzConsumeSemanticFinishCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic finish input");
  require(count_identifier(
              body, "RealmzConsumeSemanticDelayCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic delay input");
  require(count_identifier(
              body, "RealmzConsumeSemanticCenterActiveCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic center input");
  require(count_identifier(
              body, "RealmzConsumeSemanticSwitchWeaponEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic switch-weapon input");
  require(count_identifier(
              body, "RealmzConsumeSemanticCycleCombatFocusEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic cycle-focus input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatItemsEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic combat-items input");
  require(count_identifier(
              body, "RealmzConsumeSemanticAutoCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Auto input");
  require(count_identifier(
              body, "RealmzConsumeSemanticShowCombatRangeEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic range input");
  require(count_identifier(
              body, "RealmzConsumeSemanticBandageCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Bandage input");
  require(count_identifier(
              body, "RealmzConsumeSemanticUndoCombatantEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Undo input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatSpellbookEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic combat-spellbook input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatTargetingEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic combat-targeting input");
  require(count_identifier(
              body, "RealmzConsumeSemanticEscapeCombatEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Escape input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic scroll-case input");
  require(count_identifier(
              body, "RealmzConsumeSemanticCenterCombatCursorEvent") == 0,
      std::string(function_name) +
          " must not consume tagged center-cursor input");
  require(count_identifier(body, "RealmzApplyPartyMemberSelection") == 0,
      std::string(function_name) +
          " must not apply semantic party selection");
}

void verify_event_manager(const fs::path& repository_root) {
  const std::string source = code_only(read_file(
      repository_root / "src/EventManager.cpp"));
  const std::string push = function_body(
      source, "push_semantic_movement_event");
  const std::string compact_push = without_whitespace(push);

  require(count_identifier(push, "RealmzIsSemanticMovementTag") == 1,
      "semantic movement enqueue must validate exactly one movement tag");
  require(count_identifier(push, "app1Evt") == 1,
      "semantic movement enqueue must use exactly one app1Evt assignment");
  require(count_identifier(push, "keyDown") == 0,
      "EventManager must not convert semantic movement directly to keyDown");
  require(count_identifier(push, "RealmzConsumeSemanticMovementEvent") == 0,
      "EventManager must not translate tagged semantic movement");
  require(compact_push.contains("ev.what=app1Evt;"),
      "semantic movement must be enqueued as app1Evt");
  require(compact_push.contains("ev.message=tagged_message;"),
      "semantic movement must retain its tagged message in the event queue");

  const std::size_t validation = find_identifier(
      push, "RealmzIsSemanticMovementTag");
  const std::size_t enqueue = compact_push.find("event_queue.emplace_back(");
  const std::size_t event_kind = compact_push.find("ev.what=app1Evt;");
  const std::size_t message = compact_push.find("ev.message=tagged_message;");
  require(enqueue != std::string::npos && event_kind != std::string::npos &&
          message != std::string::npos,
      "semantic movement enqueue structure is incomplete");
  // validation is from the whitespace-preserving source, so compare only the
  // enqueue assignments with one another here; the exact single validation
  // above prevents an untagged alternate within this function.
  (void)validation;
  require(enqueue < event_kind && event_kind < message,
      "semantic movement must allocate, tag as app1Evt, then store its tag");

  const std::string wrapper = function_body(
      source, "PushSemanticMovementEvent");
  const std::string compact_wrapper = without_whitespace(wrapper);
  require(compact_wrapper.contains(
              "returnem.push_semantic_movement_event(tagged_message);"),
      "public semantic movement enqueue must delegate to the tagged queue path");
  require(count_identifier(wrapper, "keyDown") == 0,
      "public semantic movement enqueue must not synthesize keyDown");

  const std::string push_selection = function_body(
      source, "push_semantic_party_selection_event");
  const std::string compact_push_selection =
      without_whitespace(push_selection);
  require(count_identifier(
              push_selection, "RealmzIsSemanticPartySelectionTag") == 1,
      "semantic party selection enqueue must validate exactly one tag");
  require(count_identifier(push_selection, "app1Evt") == 1,
      "semantic party selection enqueue must use app1Evt exactly once");
  require(count_identifier(push_selection, "keyDown") == 0 &&
          count_identifier(push_selection, "mouseDown") == 0,
      "semantic party selection enqueue must not synthesize Classic input");
  require(count_identifier(
              push_selection, "RealmzApplyPartyMemberSelection") == 0,
      "semantic party selection enqueue must not mutate legacy selection");
  require(compact_push_selection.contains("ev.what=app1Evt;") &&
          compact_push_selection.contains("ev.message=tagged_message;"),
      "semantic party selection must retain its tagged app1Evt payload");

  const std::string selection_wrapper = function_body(
      source, "PushSemanticPartySelectionEvent");
  require(without_whitespace(selection_wrapper).contains(
              "returnem.push_semantic_party_selection_event(tagged_message);"),
      "public semantic party selection enqueue must delegate to tagged queue");
  require(count_identifier(selection_wrapper, "keyDown") == 0 &&
          count_identifier(selection_wrapper, "mouseDown") == 0,
      "public semantic party selection enqueue must not synthesize Classic input");

  const std::string push_inventory = function_body(
      source, "push_semantic_open_inventory_event");
  const std::string compact_push_inventory =
      without_whitespace(push_inventory);
  require(count_identifier(
              push_inventory, "RealmzIsSemanticOpenInventoryTag") == 1,
      "semantic open inventory enqueue must validate exactly one tag");
  require(count_identifier(push_inventory, "app1Evt") == 1,
      "semantic open inventory enqueue must use app1Evt exactly once");
  require(count_identifier(push_inventory, "keyDown") == 0 &&
          count_identifier(push_inventory, "mouseDown") == 0,
      "semantic open inventory enqueue must not synthesize Classic input");
  require(compact_push_inventory.contains("ev.what=app1Evt;") &&
          compact_push_inventory.contains("ev.message=tagged_message;"),
      "semantic open inventory must retain its tagged app1Evt payload");

  const std::string inventory_wrapper = function_body(
      source, "PushSemanticOpenInventoryEvent");
  require(without_whitespace(inventory_wrapper).contains(
              "returnem.push_semantic_open_inventory_event(tagged_message);"),
      "public semantic open inventory enqueue must delegate to tagged queue");
  require(count_identifier(inventory_wrapper, "keyDown") == 0 &&
          count_identifier(inventory_wrapper, "mouseDown") == 0,
      "public semantic open inventory enqueue must not synthesize Classic input");

  const std::string push_spellbook = function_body(
      source, "push_semantic_open_spellbook_event");
  const std::string compact_push_spellbook =
      without_whitespace(push_spellbook);
  require(count_identifier(
              push_spellbook, "RealmzIsSemanticOpenSpellbookTag") == 1,
      "semantic open spellbook enqueue must validate exactly one tag");
  require(count_identifier(push_spellbook, "app1Evt") == 1,
      "semantic open spellbook enqueue must use app1Evt exactly once");
  require(count_identifier(push_spellbook, "keyDown") == 0 &&
          count_identifier(push_spellbook, "mouseDown") == 0,
      "semantic open spellbook enqueue must not synthesize Classic input");
  require(compact_push_spellbook.contains("ev.what=app1Evt;") &&
          compact_push_spellbook.contains("ev.message=tagged_message;"),
      "semantic open spellbook must retain its tagged app1Evt payload");

  const std::string spellbook_wrapper = function_body(
      source, "PushSemanticOpenSpellbookEvent");
  require(without_whitespace(spellbook_wrapper).contains(
              "returnem.push_semantic_open_spellbook_event(tagged_message);"),
      "public semantic open spellbook enqueue must delegate to tagged queue");
  require(count_identifier(spellbook_wrapper, "keyDown") == 0 &&
          count_identifier(spellbook_wrapper, "mouseDown") == 0,
      "public semantic open spellbook enqueue must not synthesize Classic input");

  const std::string push_save = function_body(
      source, "push_semantic_open_save_game_event");
  const std::string compact_push_save = without_whitespace(push_save);
  require(count_identifier(
              push_save, "RealmzIsSemanticOpenSaveGameTag") == 1,
      "semantic open save enqueue must validate exactly one tag");
  require(count_identifier(push_save, "app1Evt") == 1,
      "semantic open save enqueue must use app1Evt exactly once");
  require(count_identifier(push_save, "keyDown") == 0 &&
          count_identifier(push_save, "mouseDown") == 0,
      "semantic open save enqueue must not synthesize Classic input");
  require(compact_push_save.contains("ev.what=app1Evt;") &&
          compact_push_save.contains("ev.message=tagged_message;"),
      "semantic open save must retain its tagged app1Evt payload");

  const std::string save_wrapper = function_body(
      source, "PushSemanticOpenSaveGameEvent");
  require(without_whitespace(save_wrapper).contains(
              "returnem.push_semantic_open_save_game_event(tagged_message);"),
      "public semantic open save enqueue must delegate to tagged queue");
  require(count_identifier(save_wrapper, "keyDown") == 0 &&
          count_identifier(save_wrapper, "mouseDown") == 0,
      "public semantic open save enqueue must not synthesize Classic input");

  const std::string push_load = function_body(
      source, "push_semantic_open_load_game_event");
  const std::string compact_push_load = without_whitespace(push_load);
  require(count_identifier(
              push_load, "RealmzIsSemanticOpenLoadGameTag") == 1,
      "semantic open load enqueue must validate exactly one tag");
  require(count_identifier(push_load, "app1Evt") == 1,
      "semantic open load enqueue must use app1Evt exactly once");
  require(count_identifier(push_load, "keyDown") == 0 &&
          count_identifier(push_load, "mouseDown") == 0,
      "semantic open load enqueue must not synthesize Classic input");
  require(compact_push_load.contains("ev.what=app1Evt;") &&
          compact_push_load.contains("ev.message=tagged_message;"),
      "semantic open load must retain its tagged app1Evt payload");

  const std::string load_wrapper = function_body(
      source, "PushSemanticOpenLoadGameEvent");
  require(without_whitespace(load_wrapper).contains(
              "returnem.push_semantic_open_load_game_event(tagged_message);"),
      "public semantic open load enqueue must delegate to tagged queue");
  require(count_identifier(load_wrapper, "keyDown") == 0 &&
          count_identifier(load_wrapper, "mouseDown") == 0,
      "public semantic open load enqueue must not synthesize Classic input");

  const std::string push_guard = function_body(
      source, "push_semantic_guard_combatant_event");
  const std::string compact_push_guard = without_whitespace(push_guard);
  require(count_identifier(
              push_guard, "RealmzIsSemanticGuardCombatantTag") == 1,
      "semantic guard enqueue must validate exactly one tag");
  require(count_identifier(push_guard, "app1Evt") == 1,
      "semantic guard enqueue must use app1Evt exactly once");
  require(count_identifier(push_guard, "keyDown") == 0 &&
          count_identifier(push_guard, "mouseDown") == 0,
      "semantic guard enqueue must not synthesize Classic input");
  require(compact_push_guard.contains("ev.what=app1Evt;") &&
          compact_push_guard.contains("ev.message=tagged_message;"),
      "semantic guard must retain its tagged app1Evt payload");

  const std::string guard_wrapper = function_body(
      source, "PushSemanticGuardCombatantEvent");
  require(without_whitespace(guard_wrapper).contains(
              "returnem.push_semantic_guard_combatant_event(tagged_message);"),
      "public semantic guard enqueue must delegate to tagged queue");
  require(count_identifier(guard_wrapper, "keyDown") == 0 &&
          count_identifier(guard_wrapper, "mouseDown") == 0,
      "public semantic guard enqueue must not synthesize Classic input");

  const std::string push_finish = function_body(
      source, "push_semantic_finish_combatant_event");
  const std::string compact_push_finish = without_whitespace(push_finish);
  require(count_identifier(
              push_finish, "RealmzIsSemanticFinishCombatantTag") == 1,
      "semantic finish enqueue must validate exactly one tag");
  require(count_identifier(push_finish, "app1Evt") == 1,
      "semantic finish enqueue must use app1Evt exactly once");
  require(count_identifier(push_finish, "keyDown") == 0 &&
          count_identifier(push_finish, "mouseDown") == 0,
      "semantic finish enqueue must not synthesize Classic input");
  require(compact_push_finish.contains("ev.what=app1Evt;") &&
          compact_push_finish.contains("ev.message=tagged_message;"),
      "semantic finish must retain its tagged app1Evt payload");

  const std::string finish_wrapper = function_body(
      source, "PushSemanticFinishCombatantEvent");
  require(without_whitespace(finish_wrapper).contains(
              "returnem.push_semantic_finish_combatant_event(tagged_message);"),
      "public semantic finish enqueue must delegate to tagged queue");
  require(count_identifier(finish_wrapper, "keyDown") == 0 &&
          count_identifier(finish_wrapper, "mouseDown") == 0,
      "public semantic finish enqueue must not synthesize Classic input");

  const std::string push_delay = function_body(
      source, "push_semantic_delay_combatant_event");
  const std::string compact_push_delay = without_whitespace(push_delay);
  require(count_identifier(
              push_delay, "RealmzIsSemanticDelayCombatantTag") == 1,
      "semantic delay enqueue must validate exactly one tag");
  require(count_identifier(push_delay, "app1Evt") == 1,
      "semantic delay enqueue must use app1Evt exactly once");
  require(count_identifier(push_delay, "keyDown") == 0 &&
          count_identifier(push_delay, "mouseDown") == 0,
      "semantic delay enqueue must not synthesize Classic input");
  require(compact_push_delay.contains("ev.what=app1Evt;") &&
          compact_push_delay.contains("ev.message=tagged_message;"),
      "semantic delay must retain its tagged app1Evt payload");

  const std::string delay_wrapper = function_body(
      source, "PushSemanticDelayCombatantEvent");
  require(without_whitespace(delay_wrapper).contains(
              "returnem.push_semantic_delay_combatant_event(tagged_message);"),
      "public semantic delay enqueue must delegate to tagged queue");
  require(count_identifier(delay_wrapper, "keyDown") == 0 &&
          count_identifier(delay_wrapper, "mouseDown") == 0,
      "public semantic delay enqueue must not synthesize Classic input");

  const std::string push_center = function_body(
      source, "push_semantic_center_active_combatant_event");
  const std::string compact_push_center = without_whitespace(push_center);
  require(count_identifier(
              push_center, "RealmzIsSemanticCenterActiveCombatantTag") == 1,
      "semantic center enqueue must validate exactly one tag");
  require(count_identifier(push_center, "app1Evt") == 1,
      "semantic center enqueue must use app1Evt exactly once");
  require(count_identifier(push_center, "keyDown") == 0 &&
          count_identifier(push_center, "mouseDown") == 0,
      "semantic center enqueue must not synthesize Classic input");
  require(compact_push_center.contains("ev.what=app1Evt;") &&
          compact_push_center.contains("ev.message=tagged_message;"),
      "semantic center must retain its tagged app1Evt payload");

  const std::string center_wrapper = function_body(
      source, "PushSemanticCenterActiveCombatantEvent");
  require(without_whitespace(center_wrapper).contains(
              "returnem.push_semantic_center_active_combatant_event("
              "tagged_message);"),
      "public semantic center enqueue must delegate to tagged queue");
  require(count_identifier(center_wrapper, "keyDown") == 0 &&
          count_identifier(center_wrapper, "mouseDown") == 0,
      "public semantic center enqueue must not synthesize Classic input");

  const std::string push_switch = function_body(
      source, "push_semantic_switch_weapon_event");
  const std::string compact_push_switch = without_whitespace(push_switch);
  require(count_identifier(
              push_switch, "RealmzIsSemanticSwitchWeaponTag") == 1,
      "semantic switch-weapon enqueue must validate exactly one tag");
  require(count_identifier(push_switch, "app1Evt") == 1,
      "semantic switch-weapon enqueue must use app1Evt exactly once");
  require(count_identifier(push_switch, "keyDown") == 0 &&
          count_identifier(push_switch, "mouseDown") == 0,
      "semantic switch-weapon enqueue must not synthesize Classic input");
  require(compact_push_switch.contains("ev.what=app1Evt;") &&
          compact_push_switch.contains("ev.message=tagged_message;"),
      "semantic switch weapon must retain its tagged app1Evt payload");

  const std::string switch_wrapper = function_body(
      source, "PushSemanticSwitchWeaponEvent");
  require(without_whitespace(switch_wrapper).contains(
              "returnem.push_semantic_switch_weapon_event(tagged_message);"),
      "public semantic switch-weapon enqueue must delegate to tagged queue");
  require(count_identifier(switch_wrapper, "keyDown") == 0 &&
          count_identifier(switch_wrapper, "mouseDown") == 0,
      "public semantic switch-weapon enqueue must not synthesize Classic input");

  const std::string push_cycle = function_body(
      source, "push_semantic_cycle_combat_focus_event");
  const std::string compact_push_cycle = without_whitespace(push_cycle);
  require(count_identifier(
              push_cycle, "RealmzIsSemanticCycleCombatFocusTag") == 1,
      "semantic cycle-focus enqueue must validate exactly one tag");
  require(count_identifier(push_cycle, "app1Evt") == 1,
      "semantic cycle-focus enqueue must use app1Evt exactly once");
  require(count_identifier(push_cycle, "keyDown") == 0 &&
          count_identifier(push_cycle, "mouseDown") == 0,
      "semantic cycle-focus enqueue must not synthesize Classic input");
  require(compact_push_cycle.contains("ev.what=app1Evt;") &&
          compact_push_cycle.contains("ev.message=tagged_message;"),
      "semantic cycle focus must retain its tagged app1Evt payload");

  const std::string cycle_wrapper = function_body(
      source, "PushSemanticCycleCombatFocusEvent");
  require(without_whitespace(cycle_wrapper).contains(
              "returnem.push_semantic_cycle_combat_focus_event("
              "tagged_message);"),
      "public semantic cycle-focus enqueue must delegate to tagged queue");
  require(count_identifier(cycle_wrapper, "keyDown") == 0 &&
          count_identifier(cycle_wrapper, "mouseDown") == 0,
      "public semantic cycle-focus enqueue must not synthesize Classic input");

  const std::string push_combat_items = function_body(
      source, "push_semantic_open_combat_items_event");
  const std::string compact_push_combat_items =
      without_whitespace(push_combat_items);
  require(count_identifier(
              push_combat_items, "RealmzIsSemanticOpenCombatItemsTag") == 1,
      "semantic combat-items enqueue must validate exactly one tag");
  require(count_identifier(push_combat_items, "app1Evt") == 1,
      "semantic combat-items enqueue must use app1Evt exactly once");
  require(count_identifier(push_combat_items, "keyDown") == 0 &&
          count_identifier(push_combat_items, "mouseDown") == 0,
      "semantic combat-items enqueue must not synthesize Classic input");
  require(compact_push_combat_items.contains("ev.what=app1Evt;") &&
          compact_push_combat_items.contains("ev.message=tagged_message;"),
      "semantic combat items must retain its tagged app1Evt payload");

  const std::string combat_items_wrapper = function_body(
      source, "PushSemanticOpenCombatItemsEvent");
  require(without_whitespace(combat_items_wrapper).contains(
              "returnem.push_semantic_open_combat_items_event("
              "tagged_message);"),
      "public semantic combat-items enqueue must delegate to tagged queue");
  require(count_identifier(combat_items_wrapper, "keyDown") == 0 &&
          count_identifier(combat_items_wrapper, "mouseDown") == 0,
      "public semantic combat-items enqueue must not synthesize Classic input");

  const std::string push_auto = function_body(
      source, "push_semantic_auto_combatant_event");
  const std::string compact_push_auto = without_whitespace(push_auto);
  require(count_identifier(
              push_auto, "RealmzIsSemanticAutoCombatantTag") == 1,
      "semantic Auto enqueue must validate exactly one tag");
  require(count_identifier(push_auto, "app1Evt") == 1,
      "semantic Auto enqueue must use app1Evt exactly once");
  require(count_identifier(push_auto, "keyDown") == 0 &&
          count_identifier(push_auto, "mouseDown") == 0,
      "semantic Auto enqueue must not synthesize Classic input");
  require(compact_push_auto.contains("ev.what=app1Evt;") &&
          compact_push_auto.contains("ev.message=tagged_message;"),
      "semantic Auto must retain its tagged app1Evt payload");

  const std::string auto_wrapper = function_body(
      source, "PushSemanticAutoCombatantEvent");
  require(without_whitespace(auto_wrapper).contains(
              "returnem.push_semantic_auto_combatant_event("
              "tagged_message);"),
      "public semantic Auto enqueue must delegate to tagged queue");
  require(count_identifier(auto_wrapper, "keyDown") == 0 &&
          count_identifier(auto_wrapper, "mouseDown") == 0,
      "public semantic Auto enqueue must not synthesize Classic input");

  const std::string push_range = function_body(
      source, "push_semantic_show_combat_range_event");
  const std::string compact_push_range = without_whitespace(push_range);
  require(count_identifier(
              push_range, "RealmzIsSemanticShowCombatRangeTag") == 1,
      "semantic Range enqueue must validate exactly one tag");
  require(count_identifier(push_range, "app1Evt") == 1,
      "semantic Range enqueue must use app1Evt exactly once");
  require(count_identifier(push_range, "keyDown") == 0 &&
          count_identifier(push_range, "mouseDown") == 0,
      "semantic Range enqueue must not synthesize Classic input");
  require(compact_push_range.contains("ev.what=app1Evt;") &&
          compact_push_range.contains("ev.message=tagged_message;"),
      "semantic Range must retain its tagged app1Evt payload");

  const std::string range_wrapper = function_body(
      source, "PushSemanticShowCombatRangeEvent");
  require(without_whitespace(range_wrapper).contains(
              "returnem.push_semantic_show_combat_range_event("
              "tagged_message);"),
      "public semantic Range enqueue must delegate to tagged queue");
  require(count_identifier(range_wrapper, "keyDown") == 0 &&
          count_identifier(range_wrapper, "mouseDown") == 0,
      "public semantic Range enqueue must not synthesize Classic input");

  const std::string push_bandage = function_body(
      source, "push_semantic_bandage_combatant_event");
  const std::string compact_push_bandage = without_whitespace(push_bandage);
  require(count_identifier(
              push_bandage, "RealmzIsSemanticBandageCombatantTag") == 1,
      "semantic Bandage enqueue must validate exactly one tag");
  require(count_identifier(push_bandage, "app1Evt") == 1,
      "semantic Bandage enqueue must use app1Evt exactly once");
  require(count_identifier(push_bandage, "keyDown") == 0 &&
          count_identifier(push_bandage, "mouseDown") == 0,
      "semantic Bandage enqueue must not synthesize Classic input");
  require(compact_push_bandage.contains("ev.what=app1Evt;") &&
          compact_push_bandage.contains("ev.message=tagged_message;"),
      "semantic Bandage must retain its tagged app1Evt payload");

  const std::string bandage_wrapper = function_body(
      source, "PushSemanticBandageCombatantEvent");
  require(without_whitespace(bandage_wrapper).contains(
              "returnem.push_semantic_bandage_combatant_event("
              "tagged_message);"),
      "public semantic Bandage enqueue must delegate to tagged queue");
  require(count_identifier(bandage_wrapper, "keyDown") == 0 &&
          count_identifier(bandage_wrapper, "mouseDown") == 0,
      "public semantic Bandage enqueue must not synthesize Classic input");

  const std::string push_undo = function_body(
      source, "push_semantic_undo_combatant_event");
  const std::string compact_push_undo = without_whitespace(push_undo);
  require(count_identifier(
              push_undo, "RealmzIsSemanticUndoCombatantTag") == 1,
      "semantic Undo enqueue must validate exactly one tag");
  require(count_identifier(push_undo, "app1Evt") == 1,
      "semantic Undo enqueue must use app1Evt exactly once");
  require(count_identifier(push_undo, "keyDown") == 0 &&
          count_identifier(push_undo, "mouseDown") == 0,
      "semantic Undo enqueue must not synthesize Classic input");
  require(compact_push_undo.contains("ev.what=app1Evt;") &&
          compact_push_undo.contains("ev.message=tagged_message;"),
      "semantic Undo must retain its tagged app1Evt payload");

  const std::string undo_wrapper = function_body(
      source, "PushSemanticUndoCombatantEvent");
  require(without_whitespace(undo_wrapper).contains(
              "returnem.push_semantic_undo_combatant_event("
              "tagged_message);"),
      "public semantic Undo enqueue must delegate to tagged queue");
  require(count_identifier(undo_wrapper, "keyDown") == 0 &&
          count_identifier(undo_wrapper, "mouseDown") == 0,
      "public semantic Undo enqueue must not synthesize Classic input");

  const std::string push_combat_spellbook = function_body(
      source, "push_semantic_open_combat_spellbook_event");
  const std::string compact_push_combat_spellbook =
      without_whitespace(push_combat_spellbook);
  require(count_identifier(push_combat_spellbook,
              "RealmzIsSemanticOpenCombatSpellbookTag") == 1,
      "semantic combat-spellbook enqueue must validate exactly one tag");
  require(count_identifier(push_combat_spellbook, "app1Evt") == 1,
      "semantic combat-spellbook enqueue must use app1Evt exactly once");
  require(count_identifier(push_combat_spellbook, "keyDown") == 0 &&
          count_identifier(push_combat_spellbook, "mouseDown") == 0,
      "semantic combat-spellbook enqueue must not synthesize Classic input");
  require(compact_push_combat_spellbook.contains("ev.what=app1Evt;") &&
          compact_push_combat_spellbook.contains(
              "ev.message=tagged_message;"),
      "semantic combat-spellbook must retain its tagged app1Evt payload");

  const std::string combat_spellbook_wrapper = function_body(
      source, "PushSemanticOpenCombatSpellbookEvent");
  require(without_whitespace(combat_spellbook_wrapper).contains(
              "returnem.push_semantic_open_combat_spellbook_event("
              "tagged_message);"),
      "public combat-spellbook enqueue must delegate to tagged queue");
  require(count_identifier(combat_spellbook_wrapper, "keyDown") == 0 &&
          count_identifier(combat_spellbook_wrapper, "mouseDown") == 0,
      "public combat-spellbook enqueue must not synthesize Classic input");

  const std::string push_combat_targeting = function_body(
      source, "push_semantic_open_combat_targeting_event");
  const std::string compact_push_combat_targeting =
      without_whitespace(push_combat_targeting);
  require(count_identifier(push_combat_targeting,
              "RealmzIsSemanticOpenCombatTargetingTag") == 1,
      "semantic combat-targeting enqueue must validate exactly one tag");
  require(count_identifier(push_combat_targeting, "app1Evt") == 1,
      "semantic combat-targeting enqueue must use app1Evt exactly once");
  require(count_identifier(push_combat_targeting, "keyDown") == 0 &&
          count_identifier(push_combat_targeting, "mouseDown") == 0,
      "semantic combat-targeting enqueue must not synthesize Classic input");
  require(compact_push_combat_targeting.contains("ev.what=app1Evt;") &&
          compact_push_combat_targeting.contains(
              "ev.message=tagged_message;"),
      "semantic combat-targeting must retain its tagged app1Evt payload");

  const std::string combat_targeting_wrapper = function_body(
      source, "PushSemanticOpenCombatTargetingEvent");
  require(without_whitespace(combat_targeting_wrapper).contains(
              "returnem.push_semantic_open_combat_targeting_event("
              "tagged_message);"),
      "public combat-targeting enqueue must delegate to tagged queue");
  require(count_identifier(combat_targeting_wrapper, "keyDown") == 0 &&
          count_identifier(combat_targeting_wrapper, "mouseDown") == 0,
      "public combat-targeting enqueue must not synthesize Classic input");

  const std::string push_escape = function_body(
      source, "push_semantic_escape_combat_event");
  const std::string compact_push_escape = without_whitespace(push_escape);
  require(count_identifier(
              push_escape, "RealmzIsSemanticEscapeCombatTag") == 1,
      "semantic Escape enqueue must validate exactly one tag");
  require(count_identifier(push_escape, "app1Evt") == 1,
      "semantic Escape enqueue must use app1Evt exactly once");
  require(count_identifier(push_escape, "keyDown") == 0 &&
          count_identifier(push_escape, "mouseDown") == 0,
      "semantic Escape enqueue must not synthesize Classic input");
  require(compact_push_escape.contains("ev.what=app1Evt;") &&
          compact_push_escape.contains("ev.message=tagged_message;"),
      "semantic Escape must retain its tagged app1Evt payload");

  const std::string escape_wrapper = function_body(
      source, "PushSemanticEscapeCombatEvent");
  require(without_whitespace(escape_wrapper).contains(
              "returnem.push_semantic_escape_combat_event("
              "tagged_message);"),
      "public Escape enqueue must delegate to its tagged queue");
  require(count_identifier(escape_wrapper, "keyDown") == 0 &&
          count_identifier(escape_wrapper, "mouseDown") == 0,
      "public Escape enqueue must not synthesize Classic input");

  const std::string push_scroll_case = function_body(
      source, "push_semantic_open_combat_scroll_case_event");
  const std::string compact_push_scroll_case =
      without_whitespace(push_scroll_case);
  require(count_identifier(push_scroll_case,
              "RealmzIsSemanticOpenCombatScrollCaseTag") == 1,
      "semantic scroll-case enqueue must validate exactly one tag");
  require(count_identifier(push_scroll_case, "app1Evt") == 1,
      "semantic scroll-case enqueue must use app1Evt exactly once");
  require(count_identifier(push_scroll_case, "keyDown") == 0 &&
          count_identifier(push_scroll_case, "mouseDown") == 0,
      "semantic scroll-case enqueue must not synthesize Classic input");
  require(compact_push_scroll_case.contains("ev.what=app1Evt;") &&
          compact_push_scroll_case.contains("ev.message=tagged_message;"),
      "semantic scroll-case must retain its tagged app1Evt payload");

  const std::string scroll_case_wrapper = function_body(
      source, "PushSemanticOpenCombatScrollCaseEvent");
  require(without_whitespace(scroll_case_wrapper).contains(
              "returnem.push_semantic_open_combat_scroll_case_event("
              "tagged_message);"),
      "public scroll-case enqueue must delegate to its tagged queue");
  require(count_identifier(scroll_case_wrapper, "keyDown") == 0 &&
          count_identifier(scroll_case_wrapper, "mouseDown") == 0,
      "public scroll-case enqueue must not synthesize Classic input");

  const std::string push_center_cursor = function_body(
      source, "push_semantic_center_combat_cursor_event");
  const std::string compact_push_center_cursor =
      without_whitespace(push_center_cursor);
  require(count_identifier(push_center_cursor,
              "RealmzIsSemanticCenterCombatCursorTag") == 1,
      "semantic center-cursor enqueue must validate exactly one tag");
  require(count_identifier(push_center_cursor, "app1Evt") == 1,
      "semantic center-cursor enqueue must use app1Evt exactly once");
  require(count_identifier(push_center_cursor, "keyDown") == 0 &&
          count_identifier(push_center_cursor, "mouseDown") == 0,
      "semantic center-cursor enqueue must not synthesize Classic input");
  require(compact_push_center_cursor.contains("ev.what=app1Evt;") &&
          compact_push_center_cursor.contains("ev.message=tagged_message;") &&
          compact_push_center_cursor.contains("ev.where={};"),
      "semantic center-cursor must retain only its tagged payload and must "
      "not encode a cell in EventRecord.where");

  const std::string center_cursor_wrapper = function_body(
      source, "PushSemanticCenterCombatCursorEvent");
  require(without_whitespace(center_cursor_wrapper).contains(
              "returnem.push_semantic_center_combat_cursor_event("
              "tagged_message);"),
      "public center-cursor enqueue must delegate to its tagged queue");
  require(count_identifier(center_cursor_wrapper, "keyDown") == 0 &&
          count_identifier(center_cursor_wrapper, "mouseDown") == 0,
      "public center-cursor enqueue must not synthesize Classic input");

  const std::string next_event = function_body(source, "get_next_event");
  const std::string compact_next = without_whitespace(next_event);
  require(count_identifier(
              next_event, "RealmzCurrentSemanticInputSurface") == 1,
      "event dequeue must inspect the active semantic surface exactly once");
  require(count_identifier(next_event, "REALMZ_SEMANTIC_INPUT_NONE") == 1,
      "event dequeue must explicitly recognize the absence of a surface");
  require(count_identifier(next_event, "app1Evt") == 1,
      "event dequeue drop predicate must select tagged app1Evt records");
  require(count_identifier(next_event, "RealmzIsSemanticGameplayTag") == 1,
      "event dequeue drop predicate must validate a generic gameplay tag");
  require(count_identifier(
              next_event, "RealmzSemanticGameplayTagSurface") == 1,
      "event dequeue must compare the gameplay tag's origin surface");
  require(compact_next.contains(
              "if((candidate.what!=app1Evt)||"
              "!RealmzIsSemanticGameplayTag(candidate.message))"
              "{returnfalse;}"),
      "drop predicate must leave ordinary and untagged app1Evt records alone");
  require(compact_next.contains(
              "return(active_surface==REALMZ_SEMANTIC_INPUT_NONE)||"
              "(RealmzSemanticGameplayTagSurface(candidate.message)!="
              "active_surface);"),
      "tagged gameplay input must be dropped when inactive or cross-surface");

  const std::size_t active_surface = compact_next.find(
      "active_surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t erase = compact_next.find(
      "std::erase_if(this->event_queue");
  const std::size_t empty_check = compact_next.find(
      "this->event_queue.empty()");
  const std::size_t pop = compact_next.find(
      "this->event_queue.pop_front()");
  require(active_surface != std::string::npos && erase != std::string::npos &&
          empty_check != std::string::npos && pop != std::string::npos,
      "event dequeue is missing its surface-aware drop structure");
  require(active_surface < erase && erase < empty_check && empty_check < pop,
      "tagged events must be dropped before queue emptiness and dequeue");

  const std::string semantic_wrapper = function_body(
      source, "GetNextSemanticGameplayEvent");
  const std::string compact_semantic = without_whitespace(semantic_wrapper);
  require(count_identifier(
              semantic_wrapper, "RealmzBeginSemanticInputSurface") == 1,
      "semantic gameplay wrapper must begin exactly one scoped poll");
  require(count_identifier(
              semantic_wrapper, "RealmzEndSemanticInputSurface") == 1,
      "semantic gameplay wrapper must end exactly one scoped poll");
  require(count_identifier(
              semantic_wrapper, "RealmzConsumeSemanticMovementEvent") == 1,
      "semantic gameplay wrapper must have one late movement consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticPartySelectionEvent") == 1,
      "semantic gameplay wrapper must have one late selection consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenInventoryEvent") == 1,
      "semantic gameplay wrapper must have one late inventory consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenSpellbookEvent") == 1,
      "semantic gameplay wrapper must have one late spellbook consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenSaveGameEvent") == 1,
      "semantic gameplay wrapper must have one late save consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenLoadGameEvent") == 1,
      "semantic gameplay wrapper must have one late load consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticGuardCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late guard consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticFinishCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late finish consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticDelayCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late delay consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticCenterActiveCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late center consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticSwitchWeaponEvent") == 1,
      "semantic gameplay wrapper must have one late switch-weapon consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticCycleCombatFocusEvent") == 1,
      "semantic gameplay wrapper must have one late cycle-focus consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenCombatItemsEvent") == 1,
      "semantic gameplay wrapper must have one late combat-items consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticAutoCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late Auto consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticShowCombatRangeEvent") == 1,
      "semantic gameplay wrapper must have one late Range consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticBandageCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late Bandage consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticUndoCombatantEvent") == 1,
      "semantic gameplay wrapper must have one late Undo consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenCombatSpellbookEvent") == 1,
      "semantic gameplay wrapper must have one late combat-spellbook consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenCombatTargetingEvent") == 1,
      "semantic gameplay wrapper must have one late combat-targeting consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticEscapeCombatEvent") == 1,
      "semantic gameplay wrapper must have one late Escape consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 1,
      "semantic gameplay wrapper must have one late scroll-case consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticCenterCombatCursorEvent") == 1,
      "semantic gameplay wrapper must have one late center-cursor consumer");
  require(count_identifier(
              semantic_wrapper, "RealmzApplyPartyMemberSelection") == 1,
      "semantic gameplay wrapper must use one narrow selection adapter");
  require(count_identifier(semantic_wrapper, "get_next_event") == 1 &&
          count_identifier(semantic_wrapper, "get_next_semantic_event") == 1,
      "semantic gameplay wrapper must separate its Classic and scoped polls");
  require(count_identifier(semantic_wrapper, "app1Evt") == 22,
      "semantic gameplay wrapper must recognize all twenty-two tagged paths");
  require(count_identifier(semantic_wrapper, "keyDown") == 19,
      "only late movement, inventory, spellbook, guard, finish, delay, center, "
      "switch-weapon, cycle-focus, combat-items, Auto, Range, Bandage, Undo, "
      "combat-spellbook, combat-targeting, Escape, scroll-case, or "
      "center-cursor validation may produce keyDown");
  require(count_identifier(semantic_wrapper, "mouseDown") == 2,
      "only late save/load validation may produce menu mouseDown events");
  require(count_identifier(semantic_wrapper, "MenuSelect") == 0 &&
          count_identifier(semantic_wrapper, "HandleMenuChoice") == 0 &&
          count_identifier(semantic_wrapper, "viewcharacter") == 0 &&
          count_identifier(semantic_wrapper, "buttonchoice") == 0 &&
          count_identifier(semantic_wrapper, "updatemain") == 0 &&
          count_identifier(semantic_wrapper, "updatecontrols") == 0,
      "EventManager must not invoke a Classic menu, portrait click, or modal");
  require(count_identifier(source, "RealmzBeginSemanticInputSurface") == 1,
      "EventManager may begin semantic scope only inside its gameplay wrapper");
  require(count_identifier(source, "RealmzEndSemanticInputSurface") == 1,
      "EventManager may end semantic scope only inside its gameplay wrapper");
  require(count_identifier(source, "RealmzConsumeSemanticMovementEvent") == 1,
      "EventManager may consume semantic movement only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticPartySelectionEvent") == 1,
      "EventManager may consume semantic selection only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenInventoryEvent") == 1,
      "EventManager may consume semantic inventory only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenSpellbookEvent") == 1,
      "EventManager may consume semantic spellbook input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenSaveGameEvent") == 1,
      "EventManager may consume semantic save input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenLoadGameEvent") == 1,
      "EventManager may consume semantic load input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticGuardCombatantEvent") == 1,
      "EventManager may consume semantic guard input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticFinishCombatantEvent") == 1,
      "EventManager may consume semantic finish input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticDelayCombatantEvent") == 1,
      "EventManager may consume semantic delay input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticCenterActiveCombatantEvent") == 1,
      "EventManager may consume semantic center input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticSwitchWeaponEvent") == 1,
      "EventManager may consume semantic switch-weapon input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticCycleCombatFocusEvent") == 1,
      "EventManager may consume semantic cycle-focus input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenCombatItemsEvent") == 1,
      "EventManager may consume semantic combat-items input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticAutoCombatantEvent") == 1,
      "EventManager may consume semantic Auto input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticShowCombatRangeEvent") == 1,
      "EventManager may consume semantic Range input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticBandageCombatantEvent") == 1,
      "EventManager may consume semantic Bandage input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticUndoCombatantEvent") == 1,
      "EventManager may consume semantic Undo input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenCombatSpellbookEvent") == 1,
      "EventManager may consume semantic combat-spellbook input only inside "
      "its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenCombatTargetingEvent") == 1,
      "EventManager may consume semantic combat-targeting input only inside "
      "its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticEscapeCombatEvent") == 1,
      "EventManager may consume semantic Escape input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 1,
      "EventManager may consume semantic scroll-case input only inside its "
      "gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticCenterCombatCursorEvent") == 1,
      "EventManager may consume semantic center-cursor input only inside its "
      "gameplay wrapper");
  require(count_identifier(source, "RealmzApplyPartyMemberSelection") == 1,
      "EventManager may apply semantic selection only inside its gameplay wrapper");

  const std::size_t classic_branch = compact_semantic.find("if(!remastered)");
  const std::size_t first_poll = compact_semantic.find(
      "*ret=em.get_next_event(0)", classic_branch);
  const std::size_t scope_type = compact_semantic.find(
      "structSemanticInputScope", first_poll);
  const std::size_t begin_scope = compact_semantic.find(
      "RealmzBeginSemanticInputSurface(value)", scope_type);
  const std::size_t end_scope = compact_semantic.find(
      "RealmzEndSemanticInputSurface()", begin_scope);
  const std::size_t scope_instance = compact_semantic.find(
      "constSemanticInputScopesemantic_scope(surface)", end_scope);
  const std::size_t scoped_poll = compact_semantic.find(
      "*ret=em.get_next_semantic_event(0)", scope_instance);
  const std::size_t scope_block_close = compact_semantic.find(
      ";}constboolstill_remastered=", scoped_poll);
  const std::size_t tagged_branch = compact_semantic.find(
      "ret->what==app1Evt", scoped_poll);
  const std::size_t late_consume = compact_semantic.find(
      "RealmzConsumeSemanticMovementEvent(", tagged_branch);
  const std::size_t late_keydown = compact_semantic.find(
      "ret->what=keyDown", late_consume);
  const std::size_t rejected_null = compact_semantic.find(
      "ret->what=nullEvent", late_keydown);
  const std::size_t rejected_message = compact_semantic.find(
      "ret->message=0", rejected_null);
  const std::size_t selection_branch = compact_semantic.find(
      "RealmzIsSemanticPartySelectionTag(ret->message)", rejected_message);
  const std::size_t selection_consume = compact_semantic.find(
      "RealmzConsumeSemanticPartySelectionEvent(", selection_branch);
  const std::size_t selection_apply = compact_semantic.find(
      "RealmzApplyPartyMemberSelection(party_member)", selection_consume);
  const std::size_t selection_null = compact_semantic.find(
      "ret->what=nullEvent", selection_apply);
  const std::size_t selection_message = compact_semantic.find(
      "ret->message=0", selection_null);
  const std::size_t inventory_branch = compact_semantic.find(
      "RealmzIsSemanticOpenInventoryTag(ret->message)", selection_message);
  const std::size_t inventory_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenInventoryEvent(", inventory_branch);
  const std::size_t inventory_keydown = compact_semantic.find(
      "ret->what=keyDown", inventory_consume);
  const std::size_t inventory_null = compact_semantic.find(
      "ret->what=nullEvent", inventory_keydown);
  const std::size_t inventory_message = compact_semantic.find(
      "ret->message=0", inventory_null);
  const std::size_t spellbook_branch = compact_semantic.find(
      "RealmzIsSemanticOpenSpellbookTag(ret->message)", inventory_message);
  const std::size_t spellbook_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenSpellbookEvent(", spellbook_branch);
  const std::size_t spellbook_keydown = compact_semantic.find(
      "ret->what=keyDown", spellbook_consume);
  const std::size_t spellbook_null = compact_semantic.find(
      "ret->what=nullEvent", spellbook_keydown);
  const std::size_t spellbook_message = compact_semantic.find(
      "ret->message=0", spellbook_null);
  const std::size_t save_branch = compact_semantic.find(
      "RealmzIsSemanticOpenSaveGameTag(ret->message)", spellbook_message);
  const std::size_t save_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenSaveGameEvent(", save_branch);
  const std::size_t save_mousedown = compact_semantic.find(
      "ret->what=mouseDown", save_consume);
  const std::size_t save_message = compact_semantic.find(
      "ret->message=0", save_mousedown);
  const std::size_t save_menu_id = compact_semantic.find(
      "ret->where.v=static_cast<int16_t>(-menu_id)", save_message);
  const std::size_t save_item_id = compact_semantic.find(
      "ret->where.h=static_cast<int16_t>(-item_id)", save_menu_id);
  const std::size_t save_null = compact_semantic.find(
      "ret->what=nullEvent", save_item_id);
  const std::size_t save_rejected_message = compact_semantic.find(
      "ret->message=0", save_null);
  const std::size_t load_branch = compact_semantic.find(
      "RealmzIsSemanticOpenLoadGameTag(ret->message)",
      save_rejected_message);
  const std::size_t load_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenLoadGameEvent(", load_branch);
  const std::size_t load_mousedown = compact_semantic.find(
      "ret->what=mouseDown", load_consume);
  const std::size_t load_message = compact_semantic.find(
      "ret->message=0", load_mousedown);
  const std::size_t load_menu_id = compact_semantic.find(
      "ret->where.v=static_cast<int16_t>(-menu_id)", load_message);
  const std::size_t load_item_id = compact_semantic.find(
      "ret->where.h=static_cast<int16_t>(-item_id)", load_menu_id);
  const std::size_t load_null = compact_semantic.find(
      "ret->what=nullEvent", load_item_id);
  const std::size_t load_rejected_message = compact_semantic.find(
      "ret->message=0", load_null);
  const std::size_t guard_branch = compact_semantic.find(
      "RealmzIsSemanticGuardCombatantTag(ret->message)",
      load_rejected_message);
  const std::size_t guard_consume = compact_semantic.find(
      "RealmzConsumeSemanticGuardCombatantEvent(", guard_branch);
  const std::size_t guard_keydown = compact_semantic.find(
      "ret->what=keyDown", guard_consume);
  const std::size_t guard_null = compact_semantic.find(
      "ret->what=nullEvent", guard_keydown);
  const std::size_t guard_rejected_message = compact_semantic.find(
      "ret->message=0", guard_null);
  const std::size_t finish_branch = compact_semantic.find(
      "RealmzIsSemanticFinishCombatantTag(ret->message)",
      guard_rejected_message);
  const std::size_t finish_consume = compact_semantic.find(
      "RealmzConsumeSemanticFinishCombatantEvent(", finish_branch);
  const std::size_t finish_keydown = compact_semantic.find(
      "ret->what=keyDown", finish_consume);
  const std::size_t finish_null = compact_semantic.find(
      "ret->what=nullEvent", finish_keydown);
  const std::size_t finish_rejected_message = compact_semantic.find(
      "ret->message=0", finish_null);
  const std::size_t delay_branch = compact_semantic.find(
      "RealmzIsSemanticDelayCombatantTag(ret->message)",
      finish_rejected_message);
  const std::size_t delay_consume = compact_semantic.find(
      "RealmzConsumeSemanticDelayCombatantEvent(", delay_branch);
  const std::size_t delay_keydown = compact_semantic.find(
      "ret->what=keyDown", delay_consume);
  const std::size_t delay_null = compact_semantic.find(
      "ret->what=nullEvent", delay_keydown);
  const std::size_t delay_rejected_message = compact_semantic.find(
      "ret->message=0", delay_null);
  const std::size_t center_branch = compact_semantic.find(
      "RealmzIsSemanticCenterActiveCombatantTag(ret->message)",
      delay_rejected_message);
  const std::size_t center_consume = compact_semantic.find(
      "RealmzConsumeSemanticCenterActiveCombatantEvent(", center_branch);
  const std::size_t center_keydown = compact_semantic.find(
      "ret->what=keyDown", center_consume);
  const std::size_t center_null = compact_semantic.find(
      "ret->what=nullEvent", center_keydown);
  const std::size_t center_rejected_message = compact_semantic.find(
      "ret->message=0", center_null);
  const std::size_t switch_branch = compact_semantic.find(
      "RealmzIsSemanticSwitchWeaponTag(ret->message)",
      center_rejected_message);
  const std::size_t switch_consume = compact_semantic.find(
      "RealmzConsumeSemanticSwitchWeaponEvent(", switch_branch);
  const std::size_t switch_keydown = compact_semantic.find(
      "ret->what=keyDown", switch_consume);
  const std::size_t switch_null = compact_semantic.find(
      "ret->what=nullEvent", switch_keydown);
  const std::size_t switch_rejected_message = compact_semantic.find(
      "ret->message=0", switch_null);
  const std::size_t cycle_branch = compact_semantic.find(
      "RealmzIsSemanticCycleCombatFocusTag(ret->message)",
      switch_rejected_message);
  const std::size_t cycle_consume = compact_semantic.find(
      "RealmzConsumeSemanticCycleCombatFocusEvent(", cycle_branch);
  const std::size_t cycle_keydown = compact_semantic.find(
      "ret->what=keyDown", cycle_consume);
  const std::size_t cycle_null = compact_semantic.find(
      "ret->what=nullEvent", cycle_keydown);
  const std::size_t cycle_rejected_message = compact_semantic.find(
      "ret->message=0", cycle_null);
  const std::size_t combat_items_branch = compact_semantic.find(
      "RealmzIsSemanticOpenCombatItemsTag(ret->message)",
      cycle_rejected_message);
  const std::size_t combat_items_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenCombatItemsEvent(", combat_items_branch);
  const std::size_t combat_items_keydown = compact_semantic.find(
      "ret->what=keyDown", combat_items_consume);
  const std::size_t combat_items_null = compact_semantic.find(
      "ret->what=nullEvent", combat_items_keydown);
  const std::size_t combat_items_rejected_message = compact_semantic.find(
      "ret->message=0", combat_items_null);
  const std::size_t auto_branch = compact_semantic.find(
      "RealmzIsSemanticAutoCombatantTag(ret->message)",
      combat_items_rejected_message);
  const std::size_t auto_consume = compact_semantic.find(
      "RealmzConsumeSemanticAutoCombatantEvent(", auto_branch);
  const std::size_t auto_keydown = compact_semantic.find(
      "ret->what=keyDown", auto_consume);
  const std::size_t auto_null = compact_semantic.find(
      "ret->what=nullEvent", auto_keydown);
  const std::size_t auto_rejected_message = compact_semantic.find(
      "ret->message=0", auto_null);
  const std::size_t range_branch = compact_semantic.find(
      "RealmzIsSemanticShowCombatRangeTag(ret->message)",
      auto_rejected_message);
  const std::size_t range_consume = compact_semantic.find(
      "RealmzConsumeSemanticShowCombatRangeEvent(", range_branch);
  const std::size_t range_keydown = compact_semantic.find(
      "ret->what=keyDown", range_consume);
  const std::size_t range_null = compact_semantic.find(
      "ret->what=nullEvent", range_keydown);
  const std::size_t range_rejected_message = compact_semantic.find(
      "ret->message=0", range_null);
  const std::size_t bandage_branch = compact_semantic.find(
      "RealmzIsSemanticBandageCombatantTag(ret->message)",
      range_rejected_message);
  const std::size_t bandage_consume = compact_semantic.find(
      "RealmzConsumeSemanticBandageCombatantEvent(", bandage_branch);
  const std::size_t bandage_keydown = compact_semantic.find(
      "ret->what=keyDown", bandage_consume);
  const std::size_t bandage_null = compact_semantic.find(
      "ret->what=nullEvent", bandage_keydown);
  const std::size_t bandage_rejected_message = compact_semantic.find(
      "ret->message=0", bandage_null);
  const std::size_t undo_branch = compact_semantic.find(
      "RealmzIsSemanticUndoCombatantTag(ret->message)",
      bandage_rejected_message);
  const std::size_t undo_consume = compact_semantic.find(
      "RealmzConsumeSemanticUndoCombatantEvent(", undo_branch);
  const std::size_t undo_keydown = compact_semantic.find(
      "ret->what=keyDown", undo_consume);
  const std::size_t undo_null = compact_semantic.find(
      "ret->what=nullEvent", undo_keydown);
  const std::size_t undo_rejected_message = compact_semantic.find(
      "ret->message=0", undo_null);
  const std::size_t combat_spellbook_branch = compact_semantic.find(
      "RealmzIsSemanticOpenCombatSpellbookTag(ret->message)",
      undo_rejected_message);
  const std::size_t combat_spellbook_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenCombatSpellbookEvent(",
      combat_spellbook_branch);
  const std::size_t combat_spellbook_keydown = compact_semantic.find(
      "ret->what=keyDown", combat_spellbook_consume);
  const std::size_t combat_spellbook_null = compact_semantic.find(
      "ret->what=nullEvent", combat_spellbook_keydown);
  const std::size_t combat_spellbook_rejected_message = compact_semantic.find(
      "ret->message=0", combat_spellbook_null);
  const std::size_t combat_targeting_branch = compact_semantic.find(
      "RealmzIsSemanticOpenCombatTargetingTag(ret->message)",
      combat_spellbook_rejected_message);
  const std::size_t combat_targeting_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenCombatTargetingEvent(",
      combat_targeting_branch);
  const std::size_t combat_targeting_keydown = compact_semantic.find(
      "ret->what=keyDown", combat_targeting_consume);
  const std::size_t combat_targeting_null = compact_semantic.find(
      "ret->what=nullEvent", combat_targeting_keydown);
  const std::size_t combat_targeting_rejected_message = compact_semantic.find(
      "ret->message=0", combat_targeting_null);
  const std::size_t escape_branch = compact_semantic.find(
      "RealmzIsSemanticEscapeCombatTag(ret->message)",
      combat_targeting_rejected_message);
  const std::size_t escape_consume = compact_semantic.find(
      "RealmzConsumeSemanticEscapeCombatEvent(", escape_branch);
  const std::size_t escape_keydown = compact_semantic.find(
      "ret->what=keyDown", escape_consume);
  const std::size_t escape_null = compact_semantic.find(
      "ret->what=nullEvent", escape_keydown);
  const std::size_t escape_rejected_message = compact_semantic.find(
      "ret->message=0", escape_null);
  const std::size_t scroll_case_branch = compact_semantic.find(
      "RealmzIsSemanticOpenCombatScrollCaseTag(ret->message)",
      escape_rejected_message);
  const std::size_t scroll_case_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenCombatScrollCaseEvent(",
      scroll_case_branch);
  const std::size_t scroll_case_keydown = compact_semantic.find(
      "ret->what=keyDown", scroll_case_consume);
  const std::size_t scroll_case_null = compact_semantic.find(
      "ret->what=nullEvent", scroll_case_keydown);
  const std::size_t scroll_case_rejected_message = compact_semantic.find(
      "ret->message=0", scroll_case_null);
  const std::size_t center_cursor_branch = compact_semantic.find(
      "RealmzIsSemanticCenterCombatCursorTag(ret->message)",
      scroll_case_rejected_message);
  const std::size_t center_cursor_consume = compact_semantic.find(
      "RealmzConsumeSemanticCenterCombatCursorEvent(",
      center_cursor_branch);
  const std::size_t center_cursor_stage = compact_semantic.find(
      "stage_semantic_center_combat_cursor_cell(absolute_x,absolute_y);",
      center_cursor_consume);
  const std::size_t center_cursor_keydown = compact_semantic.find(
      "ret->what=keyDown", center_cursor_stage);
  const std::size_t center_cursor_null = compact_semantic.find(
      "ret->what=nullEvent", center_cursor_keydown);
  const std::size_t center_cursor_rejected_message = compact_semantic.find(
      "ret->message=0", center_cursor_null);
  require(classic_branch != std::string::npos &&
          first_poll != std::string::npos &&
          scope_type != std::string::npos &&
          begin_scope != std::string::npos &&
          end_scope != std::string::npos &&
          scope_instance != std::string::npos &&
          scoped_poll != std::string::npos &&
          scope_block_close != std::string::npos &&
          tagged_branch != std::string::npos &&
          late_consume != std::string::npos &&
          late_keydown != std::string::npos &&
          rejected_null != std::string::npos &&
          rejected_message != std::string::npos &&
          selection_branch != std::string::npos &&
          selection_consume != std::string::npos &&
          selection_apply != std::string::npos &&
          selection_null != std::string::npos &&
          selection_message != std::string::npos &&
          inventory_branch != std::string::npos &&
          inventory_consume != std::string::npos &&
          inventory_keydown != std::string::npos &&
          inventory_null != std::string::npos &&
          inventory_message != std::string::npos &&
          spellbook_branch != std::string::npos &&
          spellbook_consume != std::string::npos &&
          spellbook_keydown != std::string::npos &&
          spellbook_null != std::string::npos &&
          spellbook_message != std::string::npos &&
          save_branch != std::string::npos &&
          save_consume != std::string::npos &&
          save_mousedown != std::string::npos &&
          save_message != std::string::npos &&
          save_menu_id != std::string::npos &&
          save_item_id != std::string::npos &&
          save_null != std::string::npos &&
          save_rejected_message != std::string::npos &&
          load_branch != std::string::npos &&
          load_consume != std::string::npos &&
          load_mousedown != std::string::npos &&
          load_message != std::string::npos &&
          load_menu_id != std::string::npos &&
          load_item_id != std::string::npos &&
          load_null != std::string::npos &&
          load_rejected_message != std::string::npos &&
          guard_branch != std::string::npos &&
          guard_consume != std::string::npos &&
          guard_keydown != std::string::npos &&
          guard_null != std::string::npos &&
          guard_rejected_message != std::string::npos &&
          finish_branch != std::string::npos &&
          finish_consume != std::string::npos &&
          finish_keydown != std::string::npos &&
          finish_null != std::string::npos &&
          finish_rejected_message != std::string::npos &&
          delay_branch != std::string::npos &&
          delay_consume != std::string::npos &&
          delay_keydown != std::string::npos &&
          delay_null != std::string::npos &&
          delay_rejected_message != std::string::npos &&
          center_branch != std::string::npos &&
          center_consume != std::string::npos &&
          center_keydown != std::string::npos &&
          center_null != std::string::npos &&
          center_rejected_message != std::string::npos &&
          switch_branch != std::string::npos &&
          switch_consume != std::string::npos &&
          switch_keydown != std::string::npos &&
          switch_null != std::string::npos &&
          switch_rejected_message != std::string::npos &&
          cycle_branch != std::string::npos &&
          cycle_consume != std::string::npos &&
          cycle_keydown != std::string::npos &&
          cycle_null != std::string::npos &&
          cycle_rejected_message != std::string::npos &&
          combat_items_branch != std::string::npos &&
          combat_items_consume != std::string::npos &&
          combat_items_keydown != std::string::npos &&
          combat_items_null != std::string::npos &&
          combat_items_rejected_message != std::string::npos &&
          auto_branch != std::string::npos &&
          auto_consume != std::string::npos &&
          auto_keydown != std::string::npos &&
          auto_null != std::string::npos &&
          auto_rejected_message != std::string::npos &&
          range_branch != std::string::npos &&
          range_consume != std::string::npos &&
          range_keydown != std::string::npos &&
          range_null != std::string::npos &&
          range_rejected_message != std::string::npos &&
          bandage_branch != std::string::npos &&
          bandage_consume != std::string::npos &&
          bandage_keydown != std::string::npos &&
          bandage_null != std::string::npos &&
          bandage_rejected_message != std::string::npos &&
          undo_branch != std::string::npos &&
          undo_consume != std::string::npos &&
          undo_keydown != std::string::npos &&
          undo_null != std::string::npos &&
          undo_rejected_message != std::string::npos &&
          combat_spellbook_branch != std::string::npos &&
          combat_spellbook_consume != std::string::npos &&
          combat_spellbook_keydown != std::string::npos &&
          combat_spellbook_null != std::string::npos &&
          combat_spellbook_rejected_message != std::string::npos &&
          combat_targeting_branch != std::string::npos &&
          combat_targeting_consume != std::string::npos &&
          combat_targeting_keydown != std::string::npos &&
          combat_targeting_null != std::string::npos &&
          combat_targeting_rejected_message != std::string::npos &&
          escape_branch != std::string::npos &&
          escape_consume != std::string::npos &&
          escape_keydown != std::string::npos &&
          escape_null != std::string::npos &&
          escape_rejected_message != std::string::npos &&
          scroll_case_branch != std::string::npos &&
          scroll_case_consume != std::string::npos &&
          scroll_case_keydown != std::string::npos &&
          scroll_case_null != std::string::npos &&
          scroll_case_rejected_message != std::string::npos &&
          center_cursor_branch != std::string::npos &&
          center_cursor_consume != std::string::npos &&
          center_cursor_stage != std::string::npos &&
          center_cursor_keydown != std::string::npos &&
          center_cursor_null != std::string::npos &&
          center_cursor_rejected_message != std::string::npos,
      "semantic gameplay wrapper is missing its centralized fail-closed route");
  require(classic_branch < first_poll && first_poll < scope_type &&
          scope_type < begin_scope && begin_scope < end_scope &&
          end_scope < scope_instance && scope_instance < scoped_poll &&
          scoped_poll < scope_block_close &&
          scope_block_close < tagged_branch && tagged_branch < late_consume &&
          late_consume < late_keydown && late_keydown < rejected_null &&
          rejected_null < rejected_message &&
          rejected_message < selection_branch &&
          selection_branch < selection_consume &&
          selection_consume < selection_apply &&
          selection_apply < selection_null &&
          selection_null < selection_message &&
          selection_message < inventory_branch &&
          inventory_branch < inventory_consume &&
          inventory_consume < inventory_keydown &&
          inventory_keydown < inventory_null &&
          inventory_null < inventory_message &&
          inventory_message < spellbook_branch &&
          spellbook_branch < spellbook_consume &&
          spellbook_consume < spellbook_keydown &&
          spellbook_keydown < spellbook_null &&
          spellbook_null < spellbook_message &&
          spellbook_message < save_branch &&
          save_branch < save_consume &&
          save_consume < save_mousedown &&
          save_mousedown < save_message &&
          save_message < save_menu_id &&
          save_menu_id < save_item_id &&
          save_item_id < save_null &&
          save_null < save_rejected_message &&
          save_rejected_message < load_branch &&
          load_branch < load_consume &&
          load_consume < load_mousedown &&
          load_mousedown < load_message &&
          load_message < load_menu_id &&
          load_menu_id < load_item_id &&
          load_item_id < load_null &&
          load_null < load_rejected_message &&
          load_rejected_message < guard_branch &&
          guard_branch < guard_consume &&
          guard_consume < guard_keydown &&
          guard_keydown < guard_null &&
          guard_null < guard_rejected_message &&
          guard_rejected_message < finish_branch &&
          finish_branch < finish_consume &&
          finish_consume < finish_keydown &&
          finish_keydown < finish_null &&
          finish_null < finish_rejected_message &&
          finish_rejected_message < delay_branch &&
          delay_branch < delay_consume &&
          delay_consume < delay_keydown &&
          delay_keydown < delay_null &&
          delay_null < delay_rejected_message &&
          delay_rejected_message < center_branch &&
          center_branch < center_consume &&
          center_consume < center_keydown &&
          center_keydown < center_null &&
          center_null < center_rejected_message &&
          center_rejected_message < switch_branch &&
          switch_branch < switch_consume &&
          switch_consume < switch_keydown &&
          switch_keydown < switch_null &&
          switch_null < switch_rejected_message &&
          switch_rejected_message < cycle_branch &&
          cycle_branch < cycle_consume &&
          cycle_consume < cycle_keydown &&
          cycle_keydown < cycle_null &&
          cycle_null < cycle_rejected_message &&
          cycle_rejected_message < combat_items_branch &&
          combat_items_branch < combat_items_consume &&
          combat_items_consume < combat_items_keydown &&
          combat_items_keydown < combat_items_null &&
          combat_items_null < combat_items_rejected_message &&
          combat_items_rejected_message < auto_branch &&
          auto_branch < auto_consume &&
          auto_consume < auto_keydown &&
          auto_keydown < auto_null &&
          auto_null < auto_rejected_message &&
          auto_rejected_message < range_branch &&
          range_branch < range_consume &&
          range_consume < range_keydown &&
          range_keydown < range_null &&
          range_null < range_rejected_message &&
          range_rejected_message < bandage_branch &&
          bandage_branch < bandage_consume &&
          bandage_consume < bandage_keydown &&
          bandage_keydown < bandage_null &&
          bandage_null < bandage_rejected_message &&
          bandage_rejected_message < undo_branch &&
          undo_branch < undo_consume &&
          undo_consume < undo_keydown &&
          undo_keydown < undo_null &&
          undo_null < undo_rejected_message &&
          undo_rejected_message < combat_spellbook_branch &&
          combat_spellbook_branch < combat_spellbook_consume &&
          combat_spellbook_consume < combat_spellbook_keydown &&
          combat_spellbook_keydown < combat_spellbook_null &&
          combat_spellbook_null < combat_spellbook_rejected_message &&
          combat_spellbook_rejected_message < combat_targeting_branch &&
          combat_targeting_branch < combat_targeting_consume &&
          combat_targeting_consume < combat_targeting_keydown &&
          combat_targeting_keydown < combat_targeting_null &&
          combat_targeting_null < combat_targeting_rejected_message &&
          combat_targeting_rejected_message < escape_branch &&
          escape_branch < escape_consume &&
          escape_consume < escape_keydown &&
          escape_keydown < escape_null &&
          escape_null < escape_rejected_message &&
          escape_rejected_message < scroll_case_branch &&
          scroll_case_branch < scroll_case_consume &&
          scroll_case_consume < scroll_case_keydown &&
          scroll_case_keydown < scroll_case_null &&
          scroll_case_null < scroll_case_rejected_message &&
          scroll_case_rejected_message < center_cursor_branch &&
          center_cursor_branch < center_cursor_consume &&
          center_cursor_consume < center_cursor_stage &&
          center_cursor_stage < center_cursor_keydown &&
          center_cursor_keydown < center_cursor_null &&
          center_cursor_null < center_cursor_rejected_message,
      "semantic wrapper must scope only its poll and translate afterward");
  require(scope_block_close < range_branch && range_branch < range_consume &&
          range_consume < range_keydown,
      "Range must leave semantic gameplay scope before its late Classic key "
      "handoff, so showrange's later raw WaitNextEvent cannot inherit shell "
      "route eligibility");
  require(scope_block_close < bandage_branch &&
          bandage_branch < bandage_consume &&
          bandage_consume < bandage_keydown,
      "Bandage must leave semantic gameplay scope before its lowercase b "
      "handoff, so getchoice's raw WaitNextEvent cannot inherit shell route "
      "eligibility");
  require(scope_block_close < undo_branch && undo_branch < undo_consume &&
          undo_consume < undo_keydown,
      "Undo must leave semantic gameplay scope before its lowercase u "
      "handoff, leaving every condition check and mutation in Classic");
  require(scope_block_close < combat_spellbook_branch &&
          combat_spellbook_branch < combat_spellbook_consume &&
          combat_spellbook_consume < combat_spellbook_keydown,
      "Combat spellbook must leave semantic gameplay scope before its "
      "lowercase s handoff, leaving cancast and the spell modal in Classic");
  require(scope_block_close < combat_targeting_branch &&
          combat_targeting_branch < combat_targeting_consume &&
          combat_targeting_consume < combat_targeting_keydown,
      "Combat targeting must leave semantic gameplay scope before its "
      "lowercase t handoff, leaving quiver choice and targeting in Classic");
  require(scope_block_close < escape_branch &&
          escape_branch < escape_consume &&
          escape_consume < escape_keydown,
      "Escape must leave semantic gameplay scope before its lowercase e "
      "handoff, leaving range checks, confirmation, and mutation in Classic");
  require(scope_block_close < scroll_case_branch &&
          scroll_case_branch < scroll_case_consume &&
          scroll_case_consume < scroll_case_keydown,
      "Use Scroll must leave semantic gameplay scope before its lowercase l "
      "handoff, leaving the raw chooser, targeting, costs, and effects in "
      "Classic");
  require(scope_block_close < center_cursor_branch &&
          center_cursor_branch < center_cursor_consume &&
          center_cursor_consume < center_cursor_stage &&
          center_cursor_stage < center_cursor_keydown,
      "Center Cursor must leave semantic gameplay scope before its lowercase "
      "m handoff and stage its absolute cell only after late validation");
  require(compact_semantic.contains(
              "if(!remastered){*ret=em.get_next_event(0);"
              "return(ret->what!=nullEvent);}"),
      "Classic mode must remain the ordinary inactive-surface dequeue route");

  const std::string get_next = function_body(source, "GetNextEvent");
  const std::string wait_next = function_body(source, "WaitNextEvent");
  const std::string compact_wait_next = without_whitespace(wait_next);
  require(count_identifier(get_next, "get_next_event") == 1,
      "GetNextEvent must use the guarded EventManager dequeue path");
  require(count_identifier(wait_next, "get_next_event") == 1,
      "WaitNextEvent must use the guarded EventManager dequeue path");
  require(count_identifier(
              get_next, "clear_pending_semantic_center_combat_cursor_cell") ==
          1 &&
          count_identifier(wait_next,
              "clear_pending_semantic_center_combat_cursor_cell") == 1 &&
          count_identifier(semantic_wrapper,
              "clear_pending_semantic_center_combat_cursor_cell") == 1,
      "every ordinary, raw, or semantic event poll must clear a stale staged "
      "center-cursor cell before dequeuing another event");
  require(compact_wait_next.contains(
              "*ret=em.get_next_event(sleep);"
              "return(ret->what!=nullEvent);"),
      "raw WaitNextEvent callers must still use the surface-aware dequeue, "
      "which makes queued gameplay tags inert outside an active scope");
  require(count_identifier(source, "push_semantic_key_event") == 0,
      "obsolete direct semantic key enqueue path must remain absent");

  const std::string cancel = function_body(
      source, "CancelSemanticGameplayInput");
  const std::string compact_cancel = without_whitespace(cancel);
  require(count_identifier(
              cancel, "RealmzInvalidateSemanticInputBoundary") == 1,
      "semantic cancellation must invalidate boundary authorization once");
  require(count_identifier(
              cancel, "discard_semantic_gameplay_events") == 1,
      "semantic cancellation must discard queued gameplay input once");
  require(count_identifier(
              cancel, "clear_pending_semantic_center_combat_cursor_cell") == 1,
      "semantic cancellation must clear a staged center-cursor cell once");
  const std::string take_cursor = function_body(
      source, "TakeSemanticCenterCombatCursorCell");
  const std::string compact_take_cursor = without_whitespace(take_cursor);
  require(count_identifier(take_cursor,
              "clear_pending_semantic_center_combat_cursor_cell") == 1 &&
          compact_take_cursor.contains(
              "constautopending=pending_semantic_center_combat_cursor_cell;") &&
          compact_take_cursor.contains("if(!pending||!absolute_x||!absolute_y)") &&
          compact_take_cursor.contains("*absolute_x=pending->x;") &&
          compact_take_cursor.contains("*absolute_y=pending->y;"),
      "the Classic center-cursor cell handoff must clear before validating "
      "outputs and return the staged absolute coordinates at most once");
  const std::string flush = function_body(source, "FlushEvents");
  require(count_identifier(flush,
              "clear_pending_semantic_center_combat_cursor_cell") == 1,
      "Classic event flushing must also make any staged semantic cursor cell "
      "inert");
  const std::size_t invalidate = compact_cancel.find(
      "RealmzInvalidateSemanticInputBoundary()");
  const std::size_t discard = compact_cancel.find(
      "em.discard_semantic_gameplay_events()");
  require(invalidate != std::string::npos && discard != std::string::npos,
      "semantic cancellation is missing invalidation or queue discard");
  require(invalidate < discard,
      "semantic cancellation must invalidate before discarding queued input");

  const std::string discard_events = function_body(
      source, "discard_semantic_gameplay_events");
  const std::string compact_discard = without_whitespace(discard_events);
  require(count_identifier(discard_events, "app1Evt") == 1 &&
          count_identifier(
              discard_events, "RealmzIsSemanticGameplayTag") == 1,
      "semantic queue discard must target only tagged app1Evt gameplay input");
  require(compact_discard.contains(
              "return(candidate.what==app1Evt)&&"
              "RealmzIsSemanticGameplayTag(candidate.message);"),
      "semantic queue discard predicate must be fail-closed and tag-specific");

  const std::string compatibility_cancel = function_body(
      source, "CancelSemanticMovementInput");
  require(without_whitespace(compatibility_cancel).contains(
              "CancelSemanticGameplayInput();"),
      "legacy movement cancellation name must delegate to the full stream");
}

void verify_party_selection_adapter(const fs::path& repository_root) {
  const std::string source = code_only(read_file(
      repository_root / "src/presentation/LegacyPartySelection.c"));
  const std::string apply = function_body(
      source, "RealmzApplyPartyMemberSelection");
  const std::string compact = without_whitespace(apply);

  require(count_identifier(apply, "updatecontrols") == 1,
      "party selection adapter must update controls exactly once on change");
  require(count_identifier(apply, "viewcharacter") == 0 &&
          count_identifier(apply, "buttonchoice") == 0 &&
          count_identifier(apply, "updatemain") == 0 &&
          count_identifier(apply, "GetNextEvent") == 0 &&
          count_identifier(apply, "WaitNextEvent") == 0,
      "party selection adapter must not enter a modal or nested event loop");
  const std::size_t unchanged = compact.find(
      "if((int)charselectnew==(int)member)");
  const std::size_t assignment = compact.find(
      "charselectnew=(char)member;", unchanged);
  const std::size_t update = compact.find("updatecontrols();", assignment);
  require(unchanged != std::string::npos &&
          assignment != std::string::npos && update != std::string::npos,
      "party selection adapter is missing idempotence, assignment, or refresh");
  require(unchanged < assignment && assignment < update,
      "party selection adapter must no-op before mutation and then refresh");
}

void verify_mode_switch_cancellation(const fs::path& repository_root) {
  const std::string source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string set_mode = function_body(source, "set_presentation_mode");
  const std::string compact = without_whitespace(set_mode);
  require(count_identifier(set_mode, "CancelSemanticGameplayInput") == 1,
      "presentation mode switch must cancel semantic gameplay exactly once");
  require(count_identifier(set_mode, "presentation_host") >= 2,
      "presentation mode switch must compare and then mutate its host mode");
  const std::size_t cancel = compact.find("CancelSemanticGameplayInput()");
  const std::size_t mutate = compact.find(
      "this->presentation_host.set_mode(mode)");
  const std::size_t select_assets = compact.find(
      "realmz::remaster::assets::setResourcePresentationMode(mode)", mutate);
  const std::size_t refresh_assets = compact.find(
      "RealmzRefreshPresentationAssets()", select_assets);
  const std::size_t recomposite = compact.find(
      "this->recomposite_all()", refresh_assets);
  require(cancel != std::string::npos && mutate != std::string::npos,
      "presentation mode switch is missing cancellation or host mutation");
  require(cancel < mutate,
      "semantic input must be cancelled before presentation mode mutation");
  require(select_assets != std::string::npos &&
          refresh_assets != std::string::npos &&
          recomposite != std::string::npos,
      "presentation mode switch must select, refresh, and recompose assets");
  require(mutate < select_assets && select_assets < refresh_assets &&
          refresh_assets < recomposite,
      "presentation assets must refresh after mode selection and before recomposition");
}

void verify_window_manager_named_combat_sinks(
    const fs::path& repository_root) {
  const std::string source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string create_window = function_body(source, "create_sdl_window");
  require(count_identifier(
              create_window, "RuntimeLegacyCommandBridge") == 1,
      "window creation must construct exactly one runtime legacy bridge");

  const std::size_t bridge = find_identifier(
      create_window, "RuntimeLegacyCommandBridge");
  const std::size_t invocation_open = create_window.find(
      '(', bridge + std::string_view("RuntimeLegacyCommandBridge").size());
  require(invocation_open != std::string::npos,
      "runtime legacy bridge construction is missing its argument list");
  const std::size_t invocation_close = matching_delimiter(
      create_window, invocation_open, '(', ')');
  const std::string invocation = create_window.substr(
      invocation_open, invocation_close - invocation_open + 1);

  require(count_identifier(
              invocation, "RuntimeLegacyCombatActionSinks") == 1,
      "window creation must construct exactly one named combat sink bundle");
  const std::size_t sinks_name = find_identifier(
      invocation, "RuntimeLegacyCombatActionSinks");
  const std::size_t sinks_open = skip_whitespace(
      invocation,
      sinks_name + std::string_view("RuntimeLegacyCombatActionSinks").size());
  require(sinks_open < invocation.size() && invocation[sinks_open] == '{',
      "runtime combat sink bundle is missing its aggregate initializer");
  const std::size_t sinks_close = matching_delimiter(
      invocation, sinks_open, '{', '}');
  const std::string combat_sinks = invocation.substr(
      sinks_open, sinks_close - sinks_open + 1);

  for (const auto identifier : {
           "legacy_key_message_for_guard_combatant",
           "semantic_guard_combatant_tag",
           "PushSemanticGuardCombatantEvent",
           "legacy_key_message_for_finish_combatant",
           "semantic_finish_combatant_tag",
           "PushSemanticFinishCombatantEvent",
           "legacy_key_message_for_delay_combatant",
           "semantic_delay_combatant_tag",
           "PushSemanticDelayCombatantEvent",
           "legacy_key_message_for_center_active_combatant",
           "semantic_center_active_combatant_tag",
           "PushSemanticCenterActiveCombatantEvent",
           "legacy_key_message_for_switch_weapon",
           "semantic_switch_weapon_tag",
           "PushSemanticSwitchWeaponEvent",
           "legacy_key_message_for_cycle_combat_focus",
           "semantic_cycle_combat_focus_tag",
           "PushSemanticCycleCombatFocusEvent",
           "legacy_key_message_for_open_combat_items",
           "semantic_open_combat_items_tag",
           "PushSemanticOpenCombatItemsEvent",
           "legacy_key_message_for_auto_combatant",
           "semantic_auto_combatant_tag",
           "PushSemanticAutoCombatantEvent",
           "legacy_key_message_for_show_combat_range",
           "semantic_show_combat_range_tag",
           "PushSemanticShowCombatRangeEvent",
           "legacy_key_message_for_bandage_combatant",
           "semantic_bandage_combatant_tag",
           "PushSemanticBandageCombatantEvent",
           "legacy_key_message_for_undo_combatant",
           "semantic_undo_combatant_tag",
           "PushSemanticUndoCombatantEvent",
           "legacy_key_message_for_open_combat_spellbook",
           "semantic_open_combat_spellbook_tag",
           "PushSemanticOpenCombatSpellbookEvent",
           "legacy_key_message_for_open_combat_targeting",
           "semantic_open_combat_targeting_tag",
           "PushSemanticOpenCombatTargetingEvent",
           "legacy_key_message_for_escape_combat",
           "semantic_escape_combat_tag",
           "PushSemanticEscapeCombatEvent",
           "legacy_key_message_for_open_combat_scroll_case",
           "semantic_open_combat_scroll_case_tag",
           "PushSemanticOpenCombatScrollCaseEvent",
           "legacy_key_message_for_center_combat_cursor",
           "semantic_center_combat_cursor_tag",
           "PushSemanticCenterCombatCursorEvent",
       }) {
    require(count_identifier(invocation, identifier) == 1,
        std::string("runtime legacy bridge construction must contain exactly ") +
            "one " + identifier);
  }

  const auto verify_field = [&combat_sinks](
                                std::string_view field,
                                std::string_view mapper,
                                std::string_view tag,
                                std::string_view push) {
    const std::string body = designated_lambda_body(combat_sinks, field);
    for (const auto identifier : {mapper, tag, push}) {
      require(count_identifier(body, identifier) == 1,
          std::string("named combat sink .") + std::string(field) +
              " must own exactly one " + std::string(identifier));
    }
  };
  verify_field(
      "guard_combatant",
      "legacy_key_message_for_guard_combatant",
      "semantic_guard_combatant_tag",
      "PushSemanticGuardCombatantEvent");
  verify_field(
      "finish_combatant",
      "legacy_key_message_for_finish_combatant",
      "semantic_finish_combatant_tag",
      "PushSemanticFinishCombatantEvent");
  verify_field(
      "delay_combatant",
      "legacy_key_message_for_delay_combatant",
      "semantic_delay_combatant_tag",
      "PushSemanticDelayCombatantEvent");
  verify_field(
      "center_active_combatant",
      "legacy_key_message_for_center_active_combatant",
      "semantic_center_active_combatant_tag",
      "PushSemanticCenterActiveCombatantEvent");
  verify_field(
      "switch_weapon",
      "legacy_key_message_for_switch_weapon",
      "semantic_switch_weapon_tag",
      "PushSemanticSwitchWeaponEvent");
  verify_field(
      "cycle_combat_focus",
      "legacy_key_message_for_cycle_combat_focus",
      "semantic_cycle_combat_focus_tag",
      "PushSemanticCycleCombatFocusEvent");
  verify_field(
      "open_combat_items",
      "legacy_key_message_for_open_combat_items",
      "semantic_open_combat_items_tag",
      "PushSemanticOpenCombatItemsEvent");
  verify_field(
      "auto_combatant",
      "legacy_key_message_for_auto_combatant",
      "semantic_auto_combatant_tag",
      "PushSemanticAutoCombatantEvent");
  verify_field(
      "show_combat_range",
      "legacy_key_message_for_show_combat_range",
      "semantic_show_combat_range_tag",
      "PushSemanticShowCombatRangeEvent");
  verify_field(
      "bandage_combatant",
      "legacy_key_message_for_bandage_combatant",
      "semantic_bandage_combatant_tag",
      "PushSemanticBandageCombatantEvent");
  verify_field(
      "undo_combatant",
      "legacy_key_message_for_undo_combatant",
      "semantic_undo_combatant_tag",
      "PushSemanticUndoCombatantEvent");
  verify_field(
      "open_combat_spellbook",
      "legacy_key_message_for_open_combat_spellbook",
      "semantic_open_combat_spellbook_tag",
      "PushSemanticOpenCombatSpellbookEvent");
  verify_field(
      "open_combat_targeting",
      "legacy_key_message_for_open_combat_targeting",
      "semantic_open_combat_targeting_tag",
      "PushSemanticOpenCombatTargetingEvent");
  verify_field(
      "escape_combat",
      "legacy_key_message_for_escape_combat",
      "semantic_escape_combat_tag",
      "PushSemanticEscapeCombatEvent");
  verify_field(
      "open_combat_scroll_case",
      "legacy_key_message_for_open_combat_scroll_case",
      "semantic_open_combat_scroll_case_tag",
      "PushSemanticOpenCombatScrollCaseEvent");
  verify_field(
      "center_combat_cursor",
      "legacy_key_message_for_center_combat_cursor",
      "semantic_center_combat_cursor_tag",
      "PushSemanticCenterCombatCursorEvent");
}

void verify_window_manager_shell_dispatch_freshness(
    const fs::path& repository_root) {
  const std::string source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string dispatch = function_body(
      source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);

  const std::size_t switch_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::SwitchWeaponSetAction>"
      "(&control.payload)");
  const std::size_t cycle_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::CycleCombatFocusAction>"
      "(&control.payload)",
      switch_payload);
  const std::size_t combat_items_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::OpenCombatItemsAction>"
      "(&control.payload)",
      cycle_payload);
  const std::size_t auto_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::AutoCombatantAction>"
      "(&control.payload)",
      combat_items_payload);
  const std::size_t range_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::ShowCombatRangeAction>"
      "(&control.payload)",
      auto_payload);
  const std::size_t bandage_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::BandageCombatantAction>"
      "(&control.payload)",
      range_payload);
  const std::size_t undo_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::UndoCombatantAction>"
      "(&control.payload)",
      bandage_payload);
  const std::size_t combat_spellbook_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::OpenCombatSpellbookAction>"
      "(&control.payload)",
      undo_payload);
  const std::size_t combat_targeting_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::OpenCombatTargetingAction>"
      "(&control.payload)",
      combat_spellbook_payload);
  const std::size_t escape_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::EscapeCombatAction>"
      "(&control.payload)",
      combat_targeting_payload);
  const std::size_t scroll_case_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::OpenCombatScrollCaseAction>"
      "(&control.payload)",
      escape_payload);
  const std::size_t center_cursor_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::CenterCombatCursorAction>"
      "(&control.payload)",
      scroll_case_payload);
  const std::size_t ordinary_branch = compact_dispatch.find(
      "}else{constautolive_control=std::ranges::find_if(",
      center_cursor_payload);
  const std::size_t current_controls = compact_dispatch.find(
      "this->remastered_shell_controls,", ordinary_branch);
  const std::size_t exact_enabled_descriptor = compact_dispatch.find(
      "returncandidate.enabled&&candidate==control;", current_controls);
  const std::size_t fresh_route = compact_dispatch.find(
      "!this->remastered_shell_keyboard_route_is_eligible()",
      exact_enabled_descriptor);
  const std::size_t descriptor_missing = compact_dispatch.find(
      "live_control==this->remastered_shell_controls.end()", fresh_route);
  const std::size_t bridge_missing = compact_dispatch.find(
      "!this->runtime_legacy_command_bridge", descriptor_missing);
  const std::size_t secondary_action_guard = compact_dispatch.find(
      "((switch_weapon||cycle_focus||combat_items)&&", bridge_missing);
  const std::size_t switch_guard = compact_dispatch.find(
      "(switch_weapon&&", secondary_action_guard);
  const std::size_t switch_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "switch_weapon_set",
      switch_guard);
  const std::size_t cycle_guard = compact_dispatch.find(
      "(cycle_focus&&", switch_kind);
  const std::size_t cycle_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "cycle_combat_focus",
      cycle_guard);
  const std::size_t combat_items_guard = compact_dispatch.find(
      "(combat_items&&", cycle_kind);
  const std::size_t combat_items_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_items",
      combat_items_guard);
  const std::size_t secondary_page = compact_dispatch.find(
      "this->remastered_combat_action_page!="
      "realmz::presentation::CombatActionPage::secondary",
      combat_items_kind);
  const std::size_t secondary_layout = compact_dispatch.find(
      "this->adaptive_shell_plan->adaptive_layout->action_bar"
      ".contains(control.bounds)",
      secondary_page);
  const std::size_t utility_action_guard = compact_dispatch.find(
      "((auto_combatant||show_combat_range||bandage_combatant||"
      "undo_combatant)&&",
      secondary_layout);
  const std::size_t auto_guard = compact_dispatch.find(
      "(auto_combatant&&", utility_action_guard);
  const std::size_t auto_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "auto_combatant",
      auto_guard);
  const std::size_t range_guard = compact_dispatch.find(
      "(show_combat_range&&", auto_kind);
  const std::size_t range_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "show_combat_range",
      range_guard);
  const std::size_t bandage_guard = compact_dispatch.find(
      "(bandage_combatant&&", range_kind);
  const std::size_t bandage_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "bandage_combatant",
      bandage_guard);
  const std::size_t undo_guard = compact_dispatch.find(
      "(undo_combatant&&", bandage_kind);
  const std::size_t undo_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "undo_combatant",
      undo_guard);
  const std::size_t utility_page = compact_dispatch.find(
      "this->remastered_combat_action_page!="
      "realmz::presentation::CombatActionPage::utility",
      undo_kind);
  const std::size_t utility_layout = compact_dispatch.find(
      "this->adaptive_shell_plan->adaptive_layout->action_bar"
      ".contains(control.bounds)",
      utility_page);
  const std::size_t special_action_guard = compact_dispatch.find(
      "((open_combat_spellbook||open_combat_targeting||escape_combat||"
      "open_combat_scroll_case||center_combat_cursor)&&",
      utility_layout);
  const std::size_t combat_spellbook_guard = compact_dispatch.find(
      "(open_combat_spellbook&&", special_action_guard);
  const std::size_t combat_spellbook_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_spellbook",
      combat_spellbook_guard);
  const std::size_t combat_targeting_guard = compact_dispatch.find(
      "(open_combat_targeting&&", combat_spellbook_kind);
  const std::size_t combat_targeting_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_targeting",
      combat_targeting_guard);
  const std::size_t escape_guard = compact_dispatch.find(
      "(escape_combat&&", combat_targeting_kind);
  const std::size_t escape_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "escape_combat",
      escape_guard);
  const std::size_t scroll_case_guard = compact_dispatch.find(
      "(open_combat_scroll_case&&", escape_kind);
  const std::size_t scroll_case_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_scroll_case",
      scroll_case_guard);
  const std::size_t center_cursor_guard = compact_dispatch.find(
      "(center_combat_cursor&&", scroll_case_kind);
  const std::size_t center_cursor_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "center_combat_cursor",
      center_cursor_guard);
  const std::size_t special_page = compact_dispatch.find(
      "this->remastered_combat_action_page!="
      "realmz::presentation::CombatActionPage::special",
      center_cursor_kind);
  const std::size_t special_layout = compact_dispatch.find(
      "this->adaptive_shell_plan->adaptive_layout->action_bar"
      ".contains(control.bounds)",
      special_page);
  const std::size_t reject = compact_dispatch.find(
      "return;", special_layout);
  const std::size_t action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", reject);
  const std::size_t bridge_dispatch = compact_dispatch.find(
      "this->runtime_legacy_command_bridge->dispatch(action)", action);
  require(switch_payload != std::string::npos &&
          cycle_payload != std::string::npos &&
          combat_items_payload != std::string::npos &&
          auto_payload != std::string::npos &&
          range_payload != std::string::npos &&
          bandage_payload != std::string::npos &&
          undo_payload != std::string::npos &&
          combat_spellbook_payload != std::string::npos &&
          combat_targeting_payload != std::string::npos &&
          escape_payload != std::string::npos &&
          scroll_case_payload != std::string::npos &&
          center_cursor_payload != std::string::npos &&
          ordinary_branch != std::string::npos &&
          current_controls != std::string::npos &&
          exact_enabled_descriptor != std::string::npos &&
          fresh_route != std::string::npos &&
          descriptor_missing != std::string::npos &&
          bridge_missing != std::string::npos &&
          secondary_action_guard != std::string::npos &&
          switch_guard != std::string::npos &&
          switch_kind != std::string::npos &&
          cycle_guard != std::string::npos &&
          cycle_kind != std::string::npos &&
          combat_items_guard != std::string::npos &&
          combat_items_kind != std::string::npos &&
          secondary_page != std::string::npos &&
          secondary_layout != std::string::npos &&
          utility_action_guard != std::string::npos &&
          auto_guard != std::string::npos &&
          auto_kind != std::string::npos &&
          range_guard != std::string::npos &&
          range_kind != std::string::npos &&
          bandage_guard != std::string::npos &&
          bandage_kind != std::string::npos &&
          undo_guard != std::string::npos &&
          undo_kind != std::string::npos &&
          utility_page != std::string::npos &&
          utility_layout != std::string::npos &&
          special_action_guard != std::string::npos &&
          combat_spellbook_guard != std::string::npos &&
          combat_spellbook_kind != std::string::npos &&
          combat_targeting_guard != std::string::npos &&
          combat_targeting_kind != std::string::npos &&
          escape_guard != std::string::npos &&
          escape_kind != std::string::npos &&
          scroll_case_guard != std::string::npos &&
          scroll_case_kind != std::string::npos &&
          center_cursor_guard != std::string::npos &&
          center_cursor_kind != std::string::npos &&
          special_page != std::string::npos &&
          special_layout != std::string::npos &&
          reject != std::string::npos &&
          action != std::string::npos &&
          bridge_dispatch != std::string::npos,
      "bridge-bound shell dispatch must retain its live descriptor, fresh "
      "route, and Weapon/Cycle Focus/Combat Items/Auto/Range/Bandage/Undo/"
      "Cast Spell/Target/Escape/Use Scroll/Center Cursor page-layout "
      "rejection gate");
  require(switch_payload < cycle_payload &&
          cycle_payload < combat_items_payload &&
          combat_items_payload < auto_payload &&
          auto_payload < range_payload &&
          range_payload < bandage_payload &&
          bandage_payload < undo_payload &&
          undo_payload < combat_spellbook_payload &&
          combat_spellbook_payload < combat_targeting_payload &&
          combat_targeting_payload < escape_payload &&
          escape_payload < scroll_case_payload &&
          scroll_case_payload < center_cursor_payload &&
          center_cursor_payload < ordinary_branch &&
          ordinary_branch < current_controls &&
          current_controls < exact_enabled_descriptor &&
          exact_enabled_descriptor < fresh_route &&
          fresh_route < descriptor_missing &&
          descriptor_missing < bridge_missing &&
          bridge_missing < secondary_action_guard &&
          secondary_action_guard < switch_guard &&
          switch_guard < switch_kind && switch_kind < cycle_guard &&
          cycle_guard < cycle_kind && cycle_kind < combat_items_guard &&
          combat_items_guard < combat_items_kind &&
          combat_items_kind < secondary_page &&
          secondary_page < secondary_layout &&
          secondary_layout < utility_action_guard &&
          utility_action_guard < auto_guard && auto_guard < auto_kind &&
          auto_kind < range_guard && range_guard < range_kind &&
          range_kind < bandage_guard && bandage_guard < bandage_kind &&
          bandage_kind < undo_guard && undo_guard < undo_kind &&
          undo_kind < utility_page && utility_page < utility_layout &&
          utility_layout < special_action_guard &&
          special_action_guard < combat_spellbook_guard &&
          combat_spellbook_guard < combat_spellbook_kind &&
          combat_spellbook_kind < combat_targeting_guard &&
          combat_targeting_guard < combat_targeting_kind &&
          combat_targeting_kind < escape_guard &&
          escape_guard < escape_kind &&
          escape_kind < scroll_case_guard &&
          scroll_case_guard < scroll_case_kind &&
          scroll_case_kind < center_cursor_guard &&
          center_cursor_guard < center_cursor_kind &&
          center_cursor_kind < special_page &&
          special_page < special_layout && special_layout < reject &&
          reject < action && action < bridge_dispatch,
      "cached shell descriptors and stale Weapon/Cycle Focus/Combat Items/"
      "Auto/Range/Bandage/Undo/Cast Spell/Target/Escape/Use Scroll/Center "
      "Cursor routes must be "
      "rejected before any runtime "
      "legacy bridge dispatch");

  const std::string action_source = code_only(read_file(
      repository_root / "src/presentation/UIAction.hpp"));
  const std::size_t transition_name = find_identifier(
      action_source, "is_valid_combat_action_page_transition");
  require(transition_name != std::string::npos,
      "shared combat page transition predicate is missing");
  const std::size_t transition_open = action_source.find(
      '{', transition_name);
  require(transition_open != std::string::npos,
      "shared combat page transition predicate has no body");
  const std::size_t transition_close = matching_delimiter(
      action_source, transition_open, '{', '}');
  const std::string transition = action_source.substr(
      transition_open, transition_close - transition_open + 1);
  const std::string compact_transition = without_whitespace(transition);
  const std::size_t secondary_case = compact_transition.find(
      "caseCombatActionPage::secondary:");
  const std::size_t secondary_to_utility = compact_transition.find(
      "to==CombatActionPage::utility", secondary_case);
  const std::size_t utility_case = compact_transition.find(
      "caseCombatActionPage::utility:", secondary_to_utility);
  const std::size_t utility_to_secondary = compact_transition.find(
      "to==CombatActionPage::secondary", utility_case);
  const std::size_t utility_to_special = compact_transition.find(
      "to==CombatActionPage::special", utility_to_secondary);
  const std::size_t special_case = compact_transition.find(
      "caseCombatActionPage::special:", utility_to_special);
  const std::size_t special_to_utility = compact_transition.find(
      "returnto==CombatActionPage::utility;", special_case);
  require(secondary_case != std::string::npos &&
          secondary_to_utility != std::string::npos &&
          utility_case != std::string::npos &&
          utility_to_secondary != std::string::npos &&
          utility_to_special != std::string::npos &&
          special_case != std::string::npos &&
          special_to_utility != std::string::npos &&
          secondary_case < secondary_to_utility &&
          secondary_to_utility < utility_case &&
          utility_case < utility_to_secondary &&
          utility_to_secondary < utility_to_special &&
          utility_to_special < special_case &&
          special_case < special_to_utility,
      "shared combat page transition predicate must permit only the bounded "
      "primary-secondary-utility-special linear path");
  const std::size_t shared_transition_call = compact_dispatch.find(
      "is_valid_combat_action_page_transition("
      "this->remastered_combat_action_page,combat_page->page)");
  const std::size_t transition_rejection = compact_dispatch.find(
      "!valid_transition", shared_transition_call);
  const std::size_t page_assignment = compact_dispatch.find(
      "this->remastered_combat_action_page=combat_page->page", action);
  require(count_identifier(
              dispatch, "is_valid_combat_action_page_transition") == 1 &&
          shared_transition_call != std::string::npos &&
          transition_rejection != std::string::npos &&
          page_assignment != std::string::npos &&
          shared_transition_call < transition_rejection &&
          transition_rejection < action && action < page_assignment &&
          page_assignment < bridge_dispatch,
      "WindowManager page dispatch must reject through the shared transition "
      "predicate before mutating linear combat page state");

  const std::string composition = function_body(
      source, "present_remastered_frame");
  const std::string compact_composition = without_whitespace(composition);
  const std::size_t composed_bandage_eligibility = compact_composition.find(
      "constboolbandage_combatant_available=bandage_combatant&&"
      "snapshot.combat&&snapshot.combat->bandage_available&&");
  const std::size_t composed_undo_eligibility = compact_composition.find(
      "constboolundo_combatant_available=undo_combatant&&"
      "snapshot.combat&&snapshot.combat->undo_available&&",
      composed_bandage_eligibility);
  const std::size_t composed_combat_spellbook_eligibility =
      compact_composition.find(
          "constboolopen_combat_spellbook_available="
          "open_combat_spellbook&&snapshot.combat&&"
          "snapshot.combat->cast_spell_available&&",
          composed_undo_eligibility);
  const std::size_t composed_combat_targeting_eligibility =
      compact_composition.find(
          "constboolopen_combat_targeting_available="
          "open_combat_targeting&&snapshot.combat&&"
          "snapshot.combat->target_available&&",
          composed_combat_spellbook_eligibility);
  const std::size_t composed_escape_eligibility = compact_composition.find(
      "constboolescape_combat_available=escape_combat&&"
      "escape_combat_action->can_invoke()&&snapshot_context_matches&&"
      "realmz::presentation::legacy_key_message_for_escape_combat(",
      composed_combat_targeting_eligibility);
  const std::size_t composed_scroll_case_eligibility =
      compact_composition.find(
          "constboolopen_combat_scroll_case_available="
          "open_combat_scroll_case&&snapshot.combat&&"
          "snapshot.combat->use_scroll_available&&",
          composed_escape_eligibility);
  const std::size_t composed_page = compact_composition.find(
      ".combat_action_page=shell_model->combat_action_page",
      composed_scroll_case_eligibility);
  const std::size_t composed_auto = compact_composition.find(
      ".auto_combatant=auto_combatant", composed_page);
  const std::size_t composed_auto_available = compact_composition.find(
      ".auto_combatant_available=auto_combatant_available", composed_auto);
  const std::size_t composed_range = compact_composition.find(
      ".show_combat_range_combatant=show_combat_range_combatant",
      composed_auto_available);
  const std::size_t composed_range_available = compact_composition.find(
      ".show_combat_range_available=show_combat_range_available",
      composed_range);
  const std::size_t composed_bandage = compact_composition.find(
      ".bandage_combatant=bandage_combatant",
      composed_range_available);
  const std::size_t composed_bandage_available = compact_composition.find(
      ".bandage_combatant_available=bandage_combatant_available",
      composed_bandage);
  const std::size_t composed_undo = compact_composition.find(
      ".undo_combatant=undo_combatant",
      composed_bandage_available);
  const std::size_t composed_undo_available = compact_composition.find(
      ".undo_combatant_available=undo_combatant_available",
      composed_undo);
  const std::size_t composed_combat_spellbook = compact_composition.find(
      ".open_combat_spellbook=open_combat_spellbook",
      composed_undo_available);
  const std::size_t composed_combat_spellbook_available =
      compact_composition.find(
          ".open_combat_spellbook_available="
          "open_combat_spellbook_available",
          composed_combat_spellbook);
  const std::size_t composed_combat_targeting = compact_composition.find(
      ".open_combat_targeting=open_combat_targeting",
      composed_combat_spellbook_available);
  const std::size_t composed_combat_targeting_available =
      compact_composition.find(
          ".open_combat_targeting_available="
          "open_combat_targeting_available",
          composed_combat_targeting);
  const std::size_t composed_escape = compact_composition.find(
      ".escape_combat=escape_combat",
      composed_combat_targeting_available);
  const std::size_t composed_escape_available = compact_composition.find(
      ".escape_combat_available=escape_combat_available",
      composed_escape);
  const std::size_t composed_scroll_case = compact_composition.find(
      ".open_combat_scroll_case=open_combat_scroll_case",
      composed_escape_available);
  const std::size_t composed_scroll_case_available =
      compact_composition.find(
          ".open_combat_scroll_case_available="
          "open_combat_scroll_case_available",
          composed_scroll_case);
  const std::size_t live_controls = compact_composition.find(
      "constboolevery_enabled_control_is_live=std::ranges::all_of(",
      composed_scroll_case_available);
  const std::size_t live_page = compact_composition.find(
      "std::get_if<realmz::presentation::SetCombatActionPageAction>"
      "(&control.payload)",
      live_controls);
  const std::size_t live_page_transition = compact_composition.find(
      "is_valid_combat_action_page_transition("
      "current_combat_action_page,page->page)",
      live_page);
  const std::size_t live_auto = compact_composition.find(
      "std::get_if<realmz::presentation::AutoCombatantAction>"
      "(&control.payload)",
      live_page_transition);
  const std::size_t live_auto_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::auto_combatant",
      live_auto);
  const std::size_t live_auto_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!=auto_combatant->combatant",
      live_auto_kind);
  const std::size_t live_auto_mapper = compact_composition.find(
      "legacy_key_message_for_auto_combatant(", live_auto_actor);
  const std::size_t live_auto_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "auto_combatant->combatant,",
      live_auto_mapper);
  const std::size_t live_auto_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(auto_combatant->combatant))",
      live_auto_combatant);
  const std::size_t live_auto_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_auto_member);
  const std::size_t live_auto_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_auto_party_kind);
  const std::size_t live_range = compact_composition.find(
      "std::get_if<realmz::presentation::ShowCombatRangeAction>"
      "(&control.payload)",
      live_auto_stamina);
  const std::size_t live_range_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "show_combat_range",
      live_range);
  const std::size_t live_range_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!="
      "show_combat_range->combatant",
      live_range_kind);
  const std::size_t live_range_mapper = compact_composition.find(
      "legacy_key_message_for_show_combat_range(", live_range_actor);
  const std::size_t live_range_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "show_combat_range->combatant,",
      live_range_mapper);
  const std::size_t live_range_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(show_combat_range->combatant))",
      live_range_combatant);
  const std::size_t live_range_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_range_member);
  const std::size_t live_range_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_range_party_kind);
  const std::size_t live_bandage = compact_composition.find(
      "std::get_if<realmz::presentation::BandageCombatantAction>"
      "(&control.payload)",
      live_range_stamina);
  const std::size_t live_bandage_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "bandage_combatant",
      live_bandage);
  const std::size_t live_bandage_available = compact_composition.find(
      "!snapshot.combat->bandage_available", live_bandage_kind);
  const std::size_t live_bandage_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!="
      "bandage_combatant->combatant",
      live_bandage_available);
  const std::size_t live_bandage_mapper = compact_composition.find(
      "legacy_key_message_for_bandage_combatant(", live_bandage_actor);
  const std::size_t live_bandage_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "bandage_combatant->combatant,",
      live_bandage_mapper);
  const std::size_t live_bandage_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(bandage_combatant->combatant))",
      live_bandage_combatant);
  const std::size_t live_bandage_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_bandage_member);
  const std::size_t live_bandage_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_bandage_party_kind);
  const std::size_t live_undo = compact_composition.find(
      "std::get_if<realmz::presentation::UndoCombatantAction>"
      "(&control.payload)",
      live_bandage_stamina);
  const std::size_t live_undo_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::undo_combatant",
      live_undo);
  const std::size_t live_undo_available = compact_composition.find(
      "!snapshot.combat->undo_available", live_undo_kind);
  const std::size_t live_undo_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!=undo_combatant->combatant",
      live_undo_available);
  const std::size_t live_undo_mapper = compact_composition.find(
      "legacy_key_message_for_undo_combatant(", live_undo_actor);
  const std::size_t live_undo_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "undo_combatant->combatant,",
      live_undo_mapper);
  const std::size_t live_undo_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(undo_combatant->combatant))",
      live_undo_combatant);
  const std::size_t live_undo_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_undo_member);
  const std::size_t live_undo_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_undo_party_kind);
  const std::size_t live_combat_spellbook = compact_composition.find(
      "std::get_if<realmz::presentation::OpenCombatSpellbookAction>"
      "(&control.payload)",
      live_undo_stamina);
  const std::size_t live_combat_spellbook_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_spellbook",
      live_combat_spellbook);
  const std::size_t live_combat_spellbook_available =
      compact_composition.find(
          "!snapshot.combat->cast_spell_available",
          live_combat_spellbook_kind);
  const std::size_t live_combat_spellbook_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!="
      "open_combat_spellbook->combatant",
      live_combat_spellbook_available);
  const std::size_t live_combat_spellbook_mapper = compact_composition.find(
      "legacy_key_message_for_open_combat_spellbook(",
      live_combat_spellbook_actor);
  const std::size_t live_combat_spellbook_combatant =
      compact_composition.find(
          "std::ranges::find(snapshot.combat->combatants,"
          "open_combat_spellbook->combatant,",
          live_combat_spellbook_mapper);
  const std::size_t live_combat_spellbook_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_spellbook->combatant))",
      live_combat_spellbook_combatant);
  const std::size_t live_combat_spellbook_party_kind =
      compact_composition.find(
          "combatant->kind=="
          "realmz::presentation::CombatantKind::party_member",
          live_combat_spellbook_member);
  const std::size_t live_combat_spellbook_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_combat_spellbook_party_kind);
  const std::size_t live_combat_targeting = compact_composition.find(
      "std::get_if<realmz::presentation::OpenCombatTargetingAction>"
      "(&control.payload)",
      live_combat_spellbook_stamina);
  const std::size_t live_combat_targeting_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_targeting",
      live_combat_targeting);
  const std::size_t live_combat_targeting_available =
      compact_composition.find(
          "!snapshot.combat->target_available",
          live_combat_targeting_kind);
  const std::size_t live_combat_targeting_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!="
      "open_combat_targeting->combatant",
      live_combat_targeting_available);
  const std::size_t live_combat_targeting_mapper = compact_composition.find(
      "legacy_key_message_for_open_combat_targeting(",
      live_combat_targeting_actor);
  const std::size_t live_combat_targeting_combatant =
      compact_composition.find(
          "std::ranges::find(snapshot.combat->combatants,"
          "open_combat_targeting->combatant,",
          live_combat_targeting_mapper);
  const std::size_t live_combat_targeting_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_targeting->combatant))",
      live_combat_targeting_combatant);
  const std::size_t live_combat_targeting_party_kind =
      compact_composition.find(
          "combatant->kind=="
          "realmz::presentation::CombatantKind::party_member",
          live_combat_targeting_member);
  const std::size_t live_combat_targeting_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_combat_targeting_party_kind);
  const std::size_t live_escape = compact_composition.find(
      "std::get_if<realmz::presentation::EscapeCombatAction>"
      "(&control.payload)",
      live_combat_targeting_stamina);
  const std::size_t live_escape_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::escape_combat",
      live_escape);
  const std::size_t live_escape_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!=escape_combat->combatant",
      live_escape_kind);
  const std::size_t live_escape_mapper = compact_composition.find(
      "legacy_key_message_for_escape_combat(", live_escape_actor);
  const std::size_t live_escape_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "escape_combat->combatant,",
      live_escape_mapper);
  const std::size_t live_escape_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(escape_combat->combatant))",
      live_escape_combatant);
  const std::size_t live_escape_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_escape_member);
  const std::size_t live_escape_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_escape_party_kind);
  const std::size_t live_scroll_case = compact_composition.find(
      "std::get_if<realmz::presentation::OpenCombatScrollCaseAction>"
      "(&control.payload)",
      live_escape_stamina);
  const std::size_t live_scroll_case_kind = compact_composition.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_combat_scroll_case",
      live_scroll_case);
  const std::size_t live_scroll_case_available = compact_composition.find(
      "!snapshot.combat->use_scroll_available", live_scroll_case_kind);
  const std::size_t live_scroll_case_actor = compact_composition.find(
      "snapshot.combat->acting_combatant!="
      "open_combat_scroll_case->combatant",
      live_scroll_case_available);
  const std::size_t live_scroll_case_mapper = compact_composition.find(
      "legacy_key_message_for_open_combat_scroll_case(",
      live_scroll_case_actor);
  const std::size_t live_scroll_case_combatant = compact_composition.find(
      "std::ranges::find(snapshot.combat->combatants,"
      "open_combat_scroll_case->combatant,",
      live_scroll_case_mapper);
  const std::size_t live_scroll_case_member = compact_composition.find(
      "snapshot.party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_scroll_case->combatant))",
      live_scroll_case_combatant);
  const std::size_t live_scroll_case_party_kind = compact_composition.find(
      "combatant->kind=="
      "realmz::presentation::CombatantKind::party_member",
      live_scroll_case_member);
  const std::size_t live_scroll_case_stamina = compact_composition.find(
      "combatant->stamina.current>0", live_scroll_case_party_kind);
  require(composed_bandage_eligibility != std::string::npos &&
          composed_undo_eligibility != std::string::npos &&
          composed_combat_spellbook_eligibility != std::string::npos &&
          composed_combat_targeting_eligibility != std::string::npos &&
          composed_escape_eligibility != std::string::npos &&
          composed_scroll_case_eligibility != std::string::npos &&
          composed_page != std::string::npos &&
          composed_auto != std::string::npos &&
          composed_auto_available != std::string::npos &&
          composed_range != std::string::npos &&
          composed_range_available != std::string::npos &&
          composed_bandage != std::string::npos &&
          composed_bandage_available != std::string::npos &&
          composed_undo != std::string::npos &&
          composed_undo_available != std::string::npos &&
          composed_combat_spellbook != std::string::npos &&
          composed_combat_spellbook_available != std::string::npos &&
          composed_combat_targeting != std::string::npos &&
          composed_combat_targeting_available != std::string::npos &&
          composed_escape != std::string::npos &&
          composed_escape_available != std::string::npos &&
          composed_scroll_case != std::string::npos &&
          composed_scroll_case_available != std::string::npos &&
          live_controls != std::string::npos &&
          live_page != std::string::npos &&
          live_page_transition != std::string::npos &&
          live_auto != std::string::npos &&
          live_auto_kind != std::string::npos &&
          live_auto_actor != std::string::npos &&
          live_auto_mapper != std::string::npos &&
          live_auto_combatant != std::string::npos &&
          live_auto_member != std::string::npos &&
          live_auto_party_kind != std::string::npos &&
          live_auto_stamina != std::string::npos &&
          live_range != std::string::npos &&
          live_range_kind != std::string::npos &&
          live_range_actor != std::string::npos &&
          live_range_mapper != std::string::npos &&
          live_range_combatant != std::string::npos &&
          live_range_member != std::string::npos &&
          live_range_party_kind != std::string::npos &&
          live_range_stamina != std::string::npos &&
          live_bandage != std::string::npos &&
          live_bandage_kind != std::string::npos &&
          live_bandage_available != std::string::npos &&
          live_bandage_actor != std::string::npos &&
          live_bandage_mapper != std::string::npos &&
          live_bandage_combatant != std::string::npos &&
          live_bandage_member != std::string::npos &&
          live_bandage_party_kind != std::string::npos &&
          live_bandage_stamina != std::string::npos &&
          live_undo != std::string::npos &&
          live_undo_kind != std::string::npos &&
          live_undo_available != std::string::npos &&
          live_undo_actor != std::string::npos &&
          live_undo_mapper != std::string::npos &&
          live_undo_combatant != std::string::npos &&
          live_undo_member != std::string::npos &&
          live_undo_party_kind != std::string::npos &&
          live_undo_stamina != std::string::npos &&
          live_combat_spellbook != std::string::npos &&
          live_combat_spellbook_kind != std::string::npos &&
          live_combat_spellbook_available != std::string::npos &&
          live_combat_spellbook_actor != std::string::npos &&
          live_combat_spellbook_mapper != std::string::npos &&
          live_combat_spellbook_combatant != std::string::npos &&
          live_combat_spellbook_member != std::string::npos &&
          live_combat_spellbook_party_kind != std::string::npos &&
          live_combat_spellbook_stamina != std::string::npos &&
          live_combat_targeting != std::string::npos &&
          live_combat_targeting_kind != std::string::npos &&
          live_combat_targeting_available != std::string::npos &&
          live_combat_targeting_actor != std::string::npos &&
          live_combat_targeting_mapper != std::string::npos &&
          live_combat_targeting_combatant != std::string::npos &&
          live_combat_targeting_member != std::string::npos &&
          live_combat_targeting_party_kind != std::string::npos &&
          live_combat_targeting_stamina != std::string::npos &&
          live_escape != std::string::npos &&
          live_escape_kind != std::string::npos &&
          live_escape_actor != std::string::npos &&
          live_escape_mapper != std::string::npos &&
          live_escape_combatant != std::string::npos &&
          live_escape_member != std::string::npos &&
          live_escape_party_kind != std::string::npos &&
          live_escape_stamina != std::string::npos &&
          live_scroll_case != std::string::npos &&
          live_scroll_case_kind != std::string::npos &&
          live_scroll_case_available != std::string::npos &&
          live_scroll_case_actor != std::string::npos &&
          live_scroll_case_mapper != std::string::npos &&
          live_scroll_case_combatant != std::string::npos &&
          live_scroll_case_member != std::string::npos &&
          live_scroll_case_party_kind != std::string::npos &&
          live_scroll_case_stamina != std::string::npos,
      "combat utility/special composition must retain requests and live "
      "actor/capability validation");
  require(composed_bandage_eligibility < composed_undo_eligibility &&
          composed_undo_eligibility < composed_combat_spellbook_eligibility &&
          composed_combat_spellbook_eligibility <
              composed_combat_targeting_eligibility &&
          composed_combat_targeting_eligibility < composed_escape_eligibility &&
          composed_escape_eligibility < composed_scroll_case_eligibility &&
          composed_scroll_case_eligibility < composed_page &&
          composed_page < composed_auto &&
          composed_auto < composed_auto_available &&
          composed_auto_available < composed_range &&
          composed_range < composed_range_available &&
          composed_range_available < composed_bandage &&
          composed_bandage < composed_bandage_available &&
          composed_bandage_available < composed_undo &&
          composed_undo < composed_undo_available &&
          composed_undo_available < composed_combat_spellbook &&
          composed_combat_spellbook < composed_combat_spellbook_available &&
          composed_combat_spellbook_available < composed_combat_targeting &&
          composed_combat_targeting < composed_combat_targeting_available &&
          composed_combat_targeting_available < composed_escape &&
          composed_escape < composed_escape_available &&
          composed_escape_available < composed_scroll_case &&
          composed_scroll_case < composed_scroll_case_available &&
          composed_scroll_case_available < live_controls &&
          live_controls < live_page && live_page < live_page_transition &&
          live_page_transition < live_auto && live_auto < live_auto_kind &&
          live_auto_kind < live_auto_actor &&
          live_auto_actor < live_auto_mapper &&
          live_auto_mapper < live_auto_combatant &&
          live_auto_combatant < live_auto_member &&
          live_auto_member < live_auto_party_kind &&
          live_auto_party_kind < live_auto_stamina &&
          live_auto_stamina < live_range &&
          live_range < live_range_kind &&
          live_range_kind < live_range_actor &&
          live_range_actor < live_range_mapper &&
          live_range_mapper < live_range_combatant &&
          live_range_combatant < live_range_member &&
          live_range_member < live_range_party_kind &&
          live_range_party_kind < live_range_stamina &&
          live_range_stamina < live_bandage &&
          live_bandage < live_bandage_kind &&
          live_bandage_kind < live_bandage_available &&
          live_bandage_available < live_bandage_actor &&
          live_bandage_actor < live_bandage_mapper &&
          live_bandage_mapper < live_bandage_combatant &&
          live_bandage_combatant < live_bandage_member &&
          live_bandage_member < live_bandage_party_kind &&
          live_bandage_party_kind < live_bandage_stamina &&
          live_bandage_stamina < live_undo &&
          live_undo < live_undo_kind &&
          live_undo_kind < live_undo_available &&
          live_undo_available < live_undo_actor &&
          live_undo_actor < live_undo_mapper &&
          live_undo_mapper < live_undo_combatant &&
          live_undo_combatant < live_undo_member &&
          live_undo_member < live_undo_party_kind &&
          live_undo_party_kind < live_undo_stamina &&
          live_undo_stamina < live_combat_spellbook &&
          live_combat_spellbook < live_combat_spellbook_kind &&
          live_combat_spellbook_kind < live_combat_spellbook_available &&
          live_combat_spellbook_available < live_combat_spellbook_actor &&
          live_combat_spellbook_actor < live_combat_spellbook_mapper &&
          live_combat_spellbook_mapper < live_combat_spellbook_combatant &&
          live_combat_spellbook_combatant < live_combat_spellbook_member &&
          live_combat_spellbook_member < live_combat_spellbook_party_kind &&
          live_combat_spellbook_party_kind < live_combat_spellbook_stamina &&
          live_combat_spellbook_stamina < live_combat_targeting &&
          live_combat_targeting < live_combat_targeting_kind &&
          live_combat_targeting_kind < live_combat_targeting_available &&
          live_combat_targeting_available < live_combat_targeting_actor &&
          live_combat_targeting_actor < live_combat_targeting_mapper &&
          live_combat_targeting_mapper < live_combat_targeting_combatant &&
          live_combat_targeting_combatant < live_combat_targeting_member &&
          live_combat_targeting_member < live_combat_targeting_party_kind &&
          live_combat_targeting_party_kind < live_combat_targeting_stamina &&
          live_combat_targeting_stamina < live_escape &&
          live_escape < live_escape_kind &&
          live_escape_kind < live_escape_actor &&
          live_escape_actor < live_escape_mapper &&
          live_escape_mapper < live_escape_combatant &&
          live_escape_combatant < live_escape_member &&
          live_escape_member < live_escape_party_kind &&
          live_escape_party_kind < live_escape_stamina &&
          live_escape_stamina < live_scroll_case &&
          live_scroll_case < live_scroll_case_kind &&
          live_scroll_case_kind < live_scroll_case_available &&
          live_scroll_case_available < live_scroll_case_actor &&
          live_scroll_case_actor < live_scroll_case_mapper &&
          live_scroll_case_mapper < live_scroll_case_combatant &&
          live_scroll_case_combatant < live_scroll_case_member &&
          live_scroll_case_member < live_scroll_case_party_kind &&
          live_scroll_case_party_kind < live_scroll_case_stamina,
      "combat utility/special controls must validate current page, actor, "
      "capability, and live snapshot before interaction");
  require(count_identifier(composition, "escape_available") == 0,
      "Escape must not invent a snapshot capability bit; Classic owns its "
      "range, condition, and confirmation rules");

  const std::string compact_source = without_whitespace(source);
  const std::size_t eligibility_signature = compact_source.find(
      "boolWindowManager::remastered_shell_keyboard_route_is_eligible()"
      "const{");
  require(eligibility_signature != std::string::npos,
      "WindowManager must define fresh shell route eligibility");
  const std::size_t eligibility_open = compact_source.find(
      '{', eligibility_signature);
  require(eligibility_open != std::string::npos,
      "shell route eligibility is missing its function body");
  const std::size_t eligibility_close = matching_delimiter(
      compact_source, eligibility_open, '{', '}');
  const std::string compact_eligibility = compact_source.substr(
      eligibility_open, eligibility_close - eligibility_open + 1);
  const std::size_t semantic_surface = compact_eligibility.find(
      "constautosemantic_surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t fresh_context = compact_eligibility.find(
      "capture_runtime_legacy_command_context()");
  const std::size_t surface_match = compact_eligibility.find(
      "constboolsurface_matches_context=", fresh_context);
  const std::size_t context_guard = compact_eligibility.find(
      "if(!context.adaptive_eligible||", surface_match);
  require(semantic_surface != std::string::npos &&
          fresh_context != std::string::npos &&
          surface_match != std::string::npos &&
          context_guard != std::string::npos &&
          semantic_surface < fresh_context && fresh_context < surface_match &&
          surface_match < context_guard,
      "fresh shell eligibility must compare the active semantic scope with "
      "the current legacy context before accepting controls");
  const std::string surface_match_expression = compact_eligibility.substr(
      surface_match, context_guard - surface_match);
  require(count_identifier(
              surface_match_expression, "REALMZ_SEMANTIC_INPUT_NONE") == 0 &&
          count_identifier(surface_match_expression,
              "REALMZ_SEMANTIC_INPUT_EXPLORATION") == 1 &&
          count_identifier(surface_match_expression,
              "REALMZ_SEMANTIC_INPUT_DUNGEON") == 1 &&
          count_identifier(surface_match_expression,
              "REALMZ_SEMANTIC_INPUT_COMBAT") == 1,
      "shell route eligibility must fail closed when no semantic gameplay "
      "scope is active");
  const std::size_t switch_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::SwitchWeaponSetAction>"
      "(&control.payload)");
  const std::size_t fresh_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      switch_route);
  const std::size_t acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=switch_weapon->combatant",
      fresh_snapshot);
  const std::size_t party_member = compact_eligibility.find(
      "snapshot->party.member(", acting_actor);
  const std::size_t combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "switch_weapon->combatant,",
      party_member);
  const std::size_t route_accept = compact_eligibility.find(
      "continue;", combatant_view);
  require(switch_route != std::string::npos,
      "fresh shell route eligibility must inspect Weapon controls");
  require(fresh_snapshot != std::string::npos,
      "fresh Weapon route eligibility must re-capture the game snapshot");
  require(acting_actor != std::string::npos,
      "fresh Weapon route eligibility must match the current acting actor");
  require(party_member != std::string::npos &&
          combatant_view != std::string::npos &&
          route_accept != std::string::npos,
      "fresh Weapon route eligibility must validate party and combatant "
      "membership before acceptance");
  require(fresh_context < switch_route && switch_route < fresh_snapshot &&
          fresh_snapshot < acting_actor && acting_actor < party_member &&
          party_member < combatant_view && combatant_view < route_accept,
      "Weapon route eligibility must validate a fresh snapshot before "
      "accepting the current control");

  const std::size_t cycle_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::CycleCombatFocusAction>"
      "(&control.payload)",
      route_accept);
  const std::size_t cycle_mapper = compact_eligibility.find(
      "legacy_key_message_for_cycle_combat_focus(", cycle_route);
  const std::size_t cycle_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      cycle_mapper);
  const std::size_t cycle_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=cycle_focus->combatant",
      cycle_snapshot);
  const std::size_t cycle_party_member = compact_eligibility.find(
      "snapshot->party.member(", cycle_acting_actor);
  const std::size_t cycle_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "cycle_focus->combatant,",
      cycle_party_member);
  const std::size_t cycle_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      cycle_combatant_view);
  const std::size_t cycle_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      cycle_membership_rejection);
  const std::size_t cycle_active = compact_eligibility.find(
      "!combatant->active", cycle_party_kind);
  const std::size_t cycle_targetable = compact_eligibility.find(
      "!combatant->targetable", cycle_active);
  const std::size_t cycle_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", cycle_targetable);
  const std::size_t cycle_route_accept = compact_eligibility.find(
      "continue;", cycle_stamina);
  require(cycle_route != std::string::npos,
      "fresh shell route eligibility must inspect Cycle Focus controls");
  require(cycle_mapper != std::string::npos,
      "fresh Cycle Focus eligibility must use its direction-aware mapper");
  require(cycle_snapshot != std::string::npos &&
          cycle_acting_actor != std::string::npos,
      "fresh Cycle Focus eligibility must recapture the snapshot and match "
      "the acting actor");
  require(cycle_party_member != std::string::npos &&
          cycle_combatant_view != std::string::npos &&
          cycle_membership_rejection != std::string::npos &&
          cycle_party_kind != std::string::npos &&
          cycle_active != std::string::npos &&
          cycle_targetable != std::string::npos &&
          cycle_stamina != std::string::npos &&
          cycle_route_accept != std::string::npos,
      "fresh Cycle Focus eligibility must reject missing PartyView or "
      "ineligible CombatView membership before acceptance");
  require(route_accept < cycle_route && cycle_route < cycle_mapper &&
          cycle_mapper < cycle_snapshot &&
          cycle_snapshot < cycle_acting_actor &&
          cycle_acting_actor < cycle_party_member &&
          cycle_party_member < cycle_combatant_view &&
          cycle_combatant_view < cycle_membership_rejection &&
          cycle_membership_rejection < cycle_party_kind &&
          cycle_party_kind < cycle_active &&
          cycle_active < cycle_targetable &&
          cycle_targetable < cycle_stamina &&
          cycle_stamina < cycle_route_accept,
      "Cycle Focus route eligibility must map and validate its fresh actor, "
      "PartyView, and CombatView before accepting the current control");

  const std::size_t combat_items_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::OpenCombatItemsAction>"
      "(&control.payload)",
      cycle_route_accept);
  const std::size_t combat_items_mapper = compact_eligibility.find(
      "legacy_key_message_for_open_combat_items(", combat_items_route);
  const std::size_t combat_items_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      combat_items_mapper);
  const std::size_t combat_items_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=combat_items->combatant",
      combat_items_snapshot);
  const std::size_t combat_items_selected_identity = compact_eligibility.find(
      "snapshot->party.selected_member!=combat_items->member",
      combat_items_acting_actor);
  const std::size_t combat_items_actor_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(combat_items->combatant))",
      combat_items_selected_identity);
  const std::size_t combat_items_selected_member = compact_eligibility.find(
      "snapshot->party.member(combat_items->member)",
      combat_items_actor_member);
  const std::size_t combat_items_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "combat_items->combatant,",
      combat_items_selected_member);
  const std::size_t combat_items_membership_rejection =
      compact_eligibility.find(
          "if((combatant==snapshot->combat->combatants.end())||"
          "!actor_member||!selected_member||!selected_member->selected||",
          combat_items_combatant_view);
  const std::size_t combat_items_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      combat_items_membership_rejection);
  const std::size_t combat_items_active = compact_eligibility.find(
      "!combatant->active", combat_items_party_kind);
  const std::size_t combat_items_targetable = compact_eligibility.find(
      "!combatant->targetable", combat_items_active);
  const std::size_t combat_items_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", combat_items_targetable);
  const std::size_t combat_items_route_accept = compact_eligibility.find(
      "continue;", combat_items_stamina);
  require(combat_items_route != std::string::npos &&
          combat_items_mapper != std::string::npos &&
          combat_items_snapshot != std::string::npos &&
          combat_items_acting_actor != std::string::npos &&
          combat_items_selected_identity != std::string::npos &&
          combat_items_actor_member != std::string::npos &&
          combat_items_selected_member != std::string::npos &&
          combat_items_combatant_view != std::string::npos &&
          combat_items_membership_rejection != std::string::npos &&
          combat_items_party_kind != std::string::npos &&
          combat_items_active != std::string::npos &&
          combat_items_targetable != std::string::npos &&
          combat_items_stamina != std::string::npos &&
          combat_items_route_accept != std::string::npos,
      "fresh Combat Items eligibility must retain actor, selected-member, "
      "PartyView, and CombatView validation");
  require(cycle_route_accept < combat_items_route &&
          combat_items_route < combat_items_mapper &&
          combat_items_mapper < combat_items_snapshot &&
          combat_items_snapshot < combat_items_acting_actor &&
          combat_items_acting_actor < combat_items_selected_identity &&
          combat_items_selected_identity < combat_items_actor_member &&
          combat_items_actor_member < combat_items_selected_member &&
          combat_items_selected_member < combat_items_combatant_view &&
          combat_items_combatant_view < combat_items_membership_rejection &&
          combat_items_membership_rejection < combat_items_party_kind &&
          combat_items_party_kind < combat_items_active &&
          combat_items_active < combat_items_targetable &&
          combat_items_targetable < combat_items_stamina &&
          combat_items_stamina < combat_items_route_accept,
      "Combat Items route eligibility must revalidate its fresh acting actor "
      "and stable selected member before accepting the current control");

  const std::size_t auto_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::AutoCombatantAction>"
      "(&control.payload)",
      combat_items_route_accept);
  const std::size_t auto_mapper = compact_eligibility.find(
      "legacy_key_message_for_auto_combatant(", auto_route);
  const std::size_t auto_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      auto_mapper);
  const std::size_t auto_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=auto_combatant->combatant",
      auto_snapshot);
  const std::size_t auto_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(auto_combatant->combatant))",
      auto_acting_actor);
  const std::size_t auto_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "auto_combatant->combatant,",
      auto_party_member);
  const std::size_t auto_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      auto_combatant_view);
  const std::size_t auto_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      auto_membership_rejection);
  const std::size_t auto_active = compact_eligibility.find(
      "!combatant->active", auto_party_kind);
  const std::size_t auto_targetable = compact_eligibility.find(
      "!combatant->targetable", auto_active);
  const std::size_t auto_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", auto_targetable);
  const std::size_t auto_route_accept = compact_eligibility.find(
      "continue;", auto_stamina);
  require(auto_route != std::string::npos &&
          auto_mapper != std::string::npos &&
          auto_snapshot != std::string::npos &&
          auto_acting_actor != std::string::npos &&
          auto_party_member != std::string::npos &&
          auto_combatant_view != std::string::npos &&
          auto_membership_rejection != std::string::npos &&
          auto_party_kind != std::string::npos &&
          auto_active != std::string::npos &&
          auto_targetable != std::string::npos &&
          auto_stamina != std::string::npos &&
          auto_route_accept != std::string::npos,
      "fresh Auto eligibility must retain mapper, acting actor, PartyView, "
      "and CombatView validation");
  require(combat_items_route_accept < auto_route &&
          auto_route < auto_mapper && auto_mapper < auto_snapshot &&
          auto_snapshot < auto_acting_actor &&
          auto_acting_actor < auto_party_member &&
          auto_party_member < auto_combatant_view &&
          auto_combatant_view < auto_membership_rejection &&
          auto_membership_rejection < auto_party_kind &&
          auto_party_kind < auto_active && auto_active < auto_targetable &&
          auto_targetable < auto_stamina &&
          auto_stamina < auto_route_accept,
      "Auto route eligibility must revalidate its fresh acting party "
      "combatant before accepting the current utility control");

  const std::size_t range_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::ShowCombatRangeAction>"
      "(&control.payload)",
      auto_route_accept);
  const std::size_t range_mapper = compact_eligibility.find(
      "legacy_key_message_for_show_combat_range(", range_route);
  const std::size_t range_surface_guard = compact_eligibility.find(
      "if(!surface_matches_context||", range_route);
  const std::size_t range_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      range_mapper);
  const std::size_t range_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!="
      "show_combat_range->combatant",
      range_snapshot);
  const std::size_t range_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(show_combat_range->combatant))",
      range_acting_actor);
  const std::size_t range_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "show_combat_range->combatant,",
      range_party_member);
  const std::size_t range_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      range_combatant_view);
  const std::size_t range_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      range_membership_rejection);
  const std::size_t range_active = compact_eligibility.find(
      "!combatant->active", range_party_kind);
  const std::size_t range_targetable = compact_eligibility.find(
      "!combatant->targetable", range_active);
  const std::size_t range_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", range_targetable);
  const std::size_t range_route_accept = compact_eligibility.find(
      "continue;", range_stamina);
  require(range_route != std::string::npos &&
          range_surface_guard != std::string::npos &&
          range_mapper != std::string::npos &&
          range_snapshot != std::string::npos &&
          range_acting_actor != std::string::npos &&
          range_party_member != std::string::npos &&
          range_combatant_view != std::string::npos &&
          range_membership_rejection != std::string::npos &&
          range_party_kind != std::string::npos &&
          range_active != std::string::npos &&
          range_targetable != std::string::npos &&
          range_stamina != std::string::npos &&
          range_route_accept != std::string::npos,
      "fresh Range eligibility must retain mapper, acting actor, PartyView, "
      "and CombatView validation");
  require(auto_route_accept < range_route &&
          range_route < range_surface_guard &&
          range_surface_guard < range_mapper &&
          range_mapper < range_snapshot &&
          range_snapshot < range_acting_actor &&
          range_acting_actor < range_party_member &&
          range_party_member < range_combatant_view &&
          range_combatant_view < range_membership_rejection &&
          range_membership_rejection < range_party_kind &&
          range_party_kind < range_active && range_active < range_targetable &&
          range_targetable < range_stamina &&
          range_stamina < range_route_accept,
      "Range route eligibility must require an active matching combat scope "
      "and revalidate its fresh acting party combatant before accepting the "
      "current utility control");

  const std::size_t bandage_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::BandageCombatantAction>"
      "(&control.payload)",
      range_route_accept);
  const std::size_t bandage_surface_guard = compact_eligibility.find(
      "if(!surface_matches_context||", bandage_route);
  const std::size_t bandage_mapper = compact_eligibility.find(
      "legacy_key_message_for_bandage_combatant(", bandage_surface_guard);
  const std::size_t bandage_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      bandage_mapper);
  const std::size_t bandage_available = compact_eligibility.find(
      "!snapshot->combat->bandage_available", bandage_snapshot);
  const std::size_t bandage_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!="
      "bandage_combatant->combatant",
      bandage_available);
  const std::size_t bandage_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(bandage_combatant->combatant))",
      bandage_acting_actor);
  const std::size_t bandage_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "bandage_combatant->combatant,",
      bandage_party_member);
  const std::size_t bandage_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      bandage_combatant_view);
  const std::size_t bandage_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      bandage_membership_rejection);
  const std::size_t bandage_active = compact_eligibility.find(
      "!combatant->active", bandage_party_kind);
  const std::size_t bandage_targetable = compact_eligibility.find(
      "!combatant->targetable", bandage_active);
  const std::size_t bandage_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", bandage_targetable);
  const std::size_t bandage_route_accept = compact_eligibility.find(
      "continue;", bandage_stamina);
  require(bandage_route != std::string::npos &&
          bandage_surface_guard != std::string::npos &&
          bandage_mapper != std::string::npos &&
          bandage_snapshot != std::string::npos &&
          bandage_available != std::string::npos &&
          bandage_acting_actor != std::string::npos &&
          bandage_party_member != std::string::npos &&
          bandage_combatant_view != std::string::npos &&
          bandage_membership_rejection != std::string::npos &&
          bandage_party_kind != std::string::npos &&
          bandage_active != std::string::npos &&
          bandage_targetable != std::string::npos &&
          bandage_stamina != std::string::npos &&
          bandage_route_accept != std::string::npos,
      "fresh Bandage eligibility must retain its mapper, Classic canundo "
      "copy, acting actor, PartyView, and CombatView validation");
  require(range_route_accept < bandage_route &&
          bandage_route < bandage_surface_guard &&
          bandage_surface_guard < bandage_mapper &&
          bandage_mapper < bandage_snapshot &&
          bandage_snapshot < bandage_available &&
          bandage_available < bandage_acting_actor &&
          bandage_acting_actor < bandage_party_member &&
          bandage_party_member < bandage_combatant_view &&
          bandage_combatant_view < bandage_membership_rejection &&
          bandage_membership_rejection < bandage_party_kind &&
          bandage_party_kind < bandage_active &&
          bandage_active < bandage_targetable &&
          bandage_targetable < bandage_stamina &&
          bandage_stamina < bandage_route_accept,
      "Bandage route eligibility must require an active matching combat scope "
      "and revalidate canundo plus its fresh acting party combatant before "
      "accepting the current utility control");

  const std::size_t undo_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::UndoCombatantAction>"
      "(&control.payload)",
      bandage_route_accept);
  const std::size_t undo_surface_guard = compact_eligibility.find(
      "if(!surface_matches_context||", undo_route);
  const std::size_t undo_mapper = compact_eligibility.find(
      "legacy_key_message_for_undo_combatant(", undo_surface_guard);
  const std::size_t undo_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      undo_mapper);
  const std::size_t undo_available = compact_eligibility.find(
      "!snapshot->combat->undo_available", undo_snapshot);
  const std::size_t undo_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=undo_combatant->combatant",
      undo_available);
  const std::size_t undo_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(undo_combatant->combatant))",
      undo_acting_actor);
  const std::size_t undo_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "undo_combatant->combatant,",
      undo_party_member);
  const std::size_t undo_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      undo_combatant_view);
  const std::size_t undo_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      undo_membership_rejection);
  const std::size_t undo_active = compact_eligibility.find(
      "!combatant->active", undo_party_kind);
  const std::size_t undo_targetable = compact_eligibility.find(
      "!combatant->targetable", undo_active);
  const std::size_t undo_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", undo_targetable);
  const std::size_t undo_route_accept = compact_eligibility.find(
      "continue;", undo_stamina);
  require(undo_route != std::string::npos &&
          undo_surface_guard != std::string::npos &&
          undo_mapper != std::string::npos &&
          undo_snapshot != std::string::npos &&
          undo_available != std::string::npos &&
          undo_acting_actor != std::string::npos &&
          undo_party_member != std::string::npos &&
          undo_combatant_view != std::string::npos &&
          undo_membership_rejection != std::string::npos &&
          undo_party_kind != std::string::npos &&
          undo_active != std::string::npos &&
          undo_targetable != std::string::npos &&
          undo_stamina != std::string::npos &&
          undo_route_accept != std::string::npos,
      "fresh Undo eligibility must retain its mapper, distinct canundo copy, "
      "acting actor, PartyView, and CombatView validation");
  require(bandage_route_accept < undo_route &&
          undo_route < undo_surface_guard &&
          undo_surface_guard < undo_mapper &&
          undo_mapper < undo_snapshot &&
          undo_snapshot < undo_available &&
          undo_available < undo_acting_actor &&
          undo_acting_actor < undo_party_member &&
          undo_party_member < undo_combatant_view &&
          undo_combatant_view < undo_membership_rejection &&
          undo_membership_rejection < undo_party_kind &&
          undo_party_kind < undo_active && undo_active < undo_targetable &&
          undo_targetable < undo_stamina && undo_stamina < undo_route_accept,
      "Undo route eligibility must require an active matching combat scope "
      "and revalidate its distinct gate plus fresh acting party combatant "
      "before accepting the current utility control");

  const std::size_t combat_spellbook_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::OpenCombatSpellbookAction>"
      "(&control.payload)",
      undo_route_accept);
  const std::size_t combat_spellbook_surface_guard =
      compact_eligibility.find(
          "if(!surface_matches_context||", combat_spellbook_route);
  const std::size_t combat_spellbook_mapper = compact_eligibility.find(
      "legacy_key_message_for_open_combat_spellbook(",
      combat_spellbook_surface_guard);
  const std::size_t combat_spellbook_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      combat_spellbook_mapper);
  const std::size_t combat_spellbook_available = compact_eligibility.find(
      "!snapshot->combat->cast_spell_available", combat_spellbook_snapshot);
  const std::size_t combat_spellbook_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!="
      "open_combat_spellbook->combatant",
      combat_spellbook_available);
  const std::size_t combat_spellbook_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_spellbook->combatant))",
      combat_spellbook_acting_actor);
  const std::size_t combat_spellbook_combatant_view =
      compact_eligibility.find(
          "std::ranges::find(snapshot->combat->combatants,"
          "open_combat_spellbook->combatant,",
          combat_spellbook_party_member);
  const std::size_t combat_spellbook_membership_rejection =
      compact_eligibility.find(
          "if((combatant==snapshot->combat->combatants.end())||!member||",
          combat_spellbook_combatant_view);
  const std::size_t combat_spellbook_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      combat_spellbook_membership_rejection);
  const std::size_t combat_spellbook_active = compact_eligibility.find(
      "!combatant->active", combat_spellbook_party_kind);
  const std::size_t combat_spellbook_targetable = compact_eligibility.find(
      "!combatant->targetable", combat_spellbook_active);
  const std::size_t combat_spellbook_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", combat_spellbook_targetable);
  const std::size_t combat_spellbook_route_accept = compact_eligibility.find(
      "continue;", combat_spellbook_stamina);
  require(combat_spellbook_route != std::string::npos &&
          combat_spellbook_surface_guard != std::string::npos &&
          combat_spellbook_mapper != std::string::npos &&
          combat_spellbook_snapshot != std::string::npos &&
          combat_spellbook_available != std::string::npos &&
          combat_spellbook_acting_actor != std::string::npos &&
          combat_spellbook_party_member != std::string::npos &&
          combat_spellbook_combatant_view != std::string::npos &&
          combat_spellbook_membership_rejection != std::string::npos &&
          combat_spellbook_party_kind != std::string::npos &&
          combat_spellbook_active != std::string::npos &&
          combat_spellbook_targetable != std::string::npos &&
          combat_spellbook_stamina != std::string::npos &&
          combat_spellbook_route_accept != std::string::npos,
      "fresh combat-spellbook eligibility must retain mapper, cast "
      "capability, actor, PartyView, and CombatView validation");
  require(undo_route_accept < combat_spellbook_route &&
          combat_spellbook_route < combat_spellbook_surface_guard &&
          combat_spellbook_surface_guard < combat_spellbook_mapper &&
          combat_spellbook_mapper < combat_spellbook_snapshot &&
          combat_spellbook_snapshot < combat_spellbook_available &&
          combat_spellbook_available < combat_spellbook_acting_actor &&
          combat_spellbook_acting_actor < combat_spellbook_party_member &&
          combat_spellbook_party_member < combat_spellbook_combatant_view &&
          combat_spellbook_combatant_view <
              combat_spellbook_membership_rejection &&
          combat_spellbook_membership_rejection <
              combat_spellbook_party_kind &&
          combat_spellbook_party_kind < combat_spellbook_active &&
          combat_spellbook_active < combat_spellbook_targetable &&
          combat_spellbook_targetable < combat_spellbook_stamina &&
          combat_spellbook_stamina < combat_spellbook_route_accept,
      "Combat Spellbook route eligibility must revalidate the special-page "
      "actor and cast capability before accepting the control");

  const std::size_t combat_targeting_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::OpenCombatTargetingAction>"
      "(&control.payload)",
      combat_spellbook_route_accept);
  const std::size_t combat_targeting_surface_guard =
      compact_eligibility.find(
          "if(!surface_matches_context||", combat_targeting_route);
  const std::size_t combat_targeting_mapper = compact_eligibility.find(
      "legacy_key_message_for_open_combat_targeting(",
      combat_targeting_surface_guard);
  const std::size_t combat_targeting_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      combat_targeting_mapper);
  const std::size_t combat_targeting_available = compact_eligibility.find(
      "!snapshot->combat->target_available", combat_targeting_snapshot);
  const std::size_t combat_targeting_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!="
      "open_combat_targeting->combatant",
      combat_targeting_available);
  const std::size_t combat_targeting_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_targeting->combatant))",
      combat_targeting_acting_actor);
  const std::size_t combat_targeting_combatant_view =
      compact_eligibility.find(
          "std::ranges::find(snapshot->combat->combatants,"
          "open_combat_targeting->combatant,",
          combat_targeting_party_member);
  const std::size_t combat_targeting_membership_rejection =
      compact_eligibility.find(
          "if((combatant==snapshot->combat->combatants.end())||!member||",
          combat_targeting_combatant_view);
  const std::size_t combat_targeting_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      combat_targeting_membership_rejection);
  const std::size_t combat_targeting_active = compact_eligibility.find(
      "!combatant->active", combat_targeting_party_kind);
  const std::size_t combat_targeting_targetable = compact_eligibility.find(
      "!combatant->targetable", combat_targeting_active);
  const std::size_t combat_targeting_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", combat_targeting_targetable);
  const std::size_t combat_targeting_route_accept = compact_eligibility.find(
      "continue;", combat_targeting_stamina);
  require(combat_targeting_route != std::string::npos &&
          combat_targeting_surface_guard != std::string::npos &&
          combat_targeting_mapper != std::string::npos &&
          combat_targeting_snapshot != std::string::npos &&
          combat_targeting_available != std::string::npos &&
          combat_targeting_acting_actor != std::string::npos &&
          combat_targeting_party_member != std::string::npos &&
          combat_targeting_combatant_view != std::string::npos &&
          combat_targeting_membership_rejection != std::string::npos &&
          combat_targeting_party_kind != std::string::npos &&
          combat_targeting_active != std::string::npos &&
          combat_targeting_targetable != std::string::npos &&
          combat_targeting_stamina != std::string::npos &&
          combat_targeting_route_accept != std::string::npos,
      "fresh combat-targeting eligibility must retain mapper, target "
      "capability, actor, PartyView, and CombatView validation");
  require(combat_spellbook_route_accept < combat_targeting_route &&
          combat_targeting_route < combat_targeting_surface_guard &&
          combat_targeting_surface_guard < combat_targeting_mapper &&
          combat_targeting_mapper < combat_targeting_snapshot &&
          combat_targeting_snapshot < combat_targeting_available &&
          combat_targeting_available < combat_targeting_acting_actor &&
          combat_targeting_acting_actor < combat_targeting_party_member &&
          combat_targeting_party_member < combat_targeting_combatant_view &&
          combat_targeting_combatant_view <
              combat_targeting_membership_rejection &&
          combat_targeting_membership_rejection <
              combat_targeting_party_kind &&
          combat_targeting_party_kind < combat_targeting_active &&
          combat_targeting_active < combat_targeting_targetable &&
          combat_targeting_targetable < combat_targeting_stamina &&
          combat_targeting_stamina < combat_targeting_route_accept,
      "Combat Target route eligibility must revalidate the special-page "
      "actor and target capability before accepting the control");

  const std::size_t escape_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::EscapeCombatAction>"
      "(&control.payload)",
      combat_targeting_route_accept);
  const std::size_t escape_surface_guard = compact_eligibility.find(
      "if(!surface_matches_context||", escape_route);
  const std::size_t escape_mapper = compact_eligibility.find(
      "legacy_key_message_for_escape_combat(", escape_surface_guard);
  const std::size_t escape_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      escape_mapper);
  const std::size_t escape_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!=escape_combat->combatant",
      escape_snapshot);
  const std::size_t escape_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(escape_combat->combatant))",
      escape_acting_actor);
  const std::size_t escape_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "escape_combat->combatant,",
      escape_party_member);
  const std::size_t escape_membership_rejection = compact_eligibility.find(
      "if((combatant==snapshot->combat->combatants.end())||!member||",
      escape_combatant_view);
  const std::size_t escape_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      escape_membership_rejection);
  const std::size_t escape_active = compact_eligibility.find(
      "!combatant->active", escape_party_kind);
  const std::size_t escape_targetable = compact_eligibility.find(
      "!combatant->targetable", escape_active);
  const std::size_t escape_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", escape_targetable);
  const std::size_t escape_route_accept = compact_eligibility.find(
      "continue;", escape_stamina);
  require(escape_route != std::string::npos &&
          escape_surface_guard != std::string::npos &&
          escape_mapper != std::string::npos &&
          escape_snapshot != std::string::npos &&
          escape_acting_actor != std::string::npos &&
          escape_party_member != std::string::npos &&
          escape_combatant_view != std::string::npos &&
          escape_membership_rejection != std::string::npos &&
          escape_party_kind != std::string::npos &&
          escape_active != std::string::npos &&
          escape_targetable != std::string::npos &&
          escape_stamina != std::string::npos &&
          escape_route_accept != std::string::npos,
      "fresh Escape eligibility must retain mapper, actor, PartyView, and "
      "CombatView validation without a projected capability");
  require(combat_targeting_route_accept < escape_route &&
          escape_route < escape_surface_guard &&
          escape_surface_guard < escape_mapper &&
          escape_mapper < escape_snapshot &&
          escape_snapshot < escape_acting_actor &&
          escape_acting_actor < escape_party_member &&
          escape_party_member < escape_combatant_view &&
          escape_combatant_view < escape_membership_rejection &&
          escape_membership_rejection < escape_party_kind &&
          escape_party_kind < escape_active &&
          escape_active < escape_targetable &&
          escape_targetable < escape_stamina &&
          escape_stamina < escape_route_accept,
      "Escape route eligibility must require the active combat scope and "
      "revalidate its fresh acting party combatant before acceptance");
  const std::size_t scroll_case_route = compact_eligibility.find(
      "std::get_if<realmz::presentation::OpenCombatScrollCaseAction>"
      "(&control.payload)",
      escape_route_accept);
  const std::size_t scroll_case_surface_guard = compact_eligibility.find(
      "if(!surface_matches_context||", scroll_case_route);
  const std::size_t scroll_case_mapper = compact_eligibility.find(
      "legacy_key_message_for_open_combat_scroll_case(",
      scroll_case_surface_guard);
  const std::size_t scroll_case_snapshot = compact_eligibility.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      scroll_case_mapper);
  const std::size_t scroll_case_available = compact_eligibility.find(
      "!snapshot->combat->use_scroll_available", scroll_case_snapshot);
  const std::size_t scroll_case_acting_actor = compact_eligibility.find(
      "snapshot->combat->acting_combatant!="
      "open_combat_scroll_case->combatant",
      scroll_case_available);
  const std::size_t scroll_case_party_member = compact_eligibility.find(
      "snapshot->party.member(static_cast<realmz::presentation::"
      "PartyMemberId>(open_combat_scroll_case->combatant))",
      scroll_case_acting_actor);
  const std::size_t scroll_case_combatant_view = compact_eligibility.find(
      "std::ranges::find(snapshot->combat->combatants,"
      "open_combat_scroll_case->combatant,",
      scroll_case_party_member);
  const std::size_t scroll_case_membership_rejection =
      compact_eligibility.find(
          "if((combatant==snapshot->combat->combatants.end())||!member||",
          scroll_case_combatant_view);
  const std::size_t scroll_case_party_kind = compact_eligibility.find(
      "combatant->kind!="
      "realmz::presentation::CombatantKind::party_member",
      scroll_case_membership_rejection);
  const std::size_t scroll_case_active = compact_eligibility.find(
      "!combatant->active", scroll_case_party_kind);
  const std::size_t scroll_case_targetable = compact_eligibility.find(
      "!combatant->targetable", scroll_case_active);
  const std::size_t scroll_case_stamina = compact_eligibility.find(
      "combatant->stamina.current<=0", scroll_case_targetable);
  const std::size_t scroll_case_route_accept = compact_eligibility.find(
      "continue;", scroll_case_stamina);
  require(scroll_case_route != std::string::npos &&
          scroll_case_surface_guard != std::string::npos &&
          scroll_case_mapper != std::string::npos &&
          scroll_case_snapshot != std::string::npos &&
          scroll_case_available != std::string::npos &&
          scroll_case_acting_actor != std::string::npos &&
          scroll_case_party_member != std::string::npos &&
          scroll_case_combatant_view != std::string::npos &&
          scroll_case_membership_rejection != std::string::npos &&
          scroll_case_party_kind != std::string::npos &&
          scroll_case_active != std::string::npos &&
          scroll_case_targetable != std::string::npos &&
          scroll_case_stamina != std::string::npos &&
          scroll_case_route_accept != std::string::npos,
      "fresh Use Scroll eligibility must retain mapper, scroll capability, "
      "actor, PartyView, and CombatView validation");
  require(escape_route_accept < scroll_case_route &&
          scroll_case_route < scroll_case_surface_guard &&
          scroll_case_surface_guard < scroll_case_mapper &&
          scroll_case_mapper < scroll_case_snapshot &&
          scroll_case_snapshot < scroll_case_available &&
          scroll_case_available < scroll_case_acting_actor &&
          scroll_case_acting_actor < scroll_case_party_member &&
          scroll_case_party_member < scroll_case_combatant_view &&
          scroll_case_combatant_view < scroll_case_membership_rejection &&
          scroll_case_membership_rejection < scroll_case_party_kind &&
          scroll_case_party_kind < scroll_case_active &&
          scroll_case_active < scroll_case_targetable &&
          scroll_case_targetable < scroll_case_stamina &&
          scroll_case_stamina < scroll_case_route_accept,
      "Use Scroll route eligibility must revalidate the special-page actor "
      "and projected scroll capability before accepting the control");
  require(count_identifier(compact_eligibility, "escape_available") == 0,
      "fresh Escape route eligibility must not depend on a synthetic Escape "
      "capability");
}

void verify_center_combat_cursor_contract(
    const fs::path& repository_root) {
  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const auto qualified_member_body = [&window_source](
                                         std::string_view function_name) {
    const std::string qualified =
        "WindowManager::" + std::string(function_name);
    const std::size_t name = window_source.find(qualified);
    require(name != std::string::npos,
        std::string("could not find WindowManager definition: ") +
            std::string(function_name));
    const std::size_t parameters_open = window_source.find(
        '(', name + qualified.size());
    require(parameters_open != std::string::npos,
        std::string("WindowManager definition has no parameters: ") +
            std::string(function_name));
    const std::size_t parameters_close = matching_delimiter(
        window_source, parameters_open, '(', ')');
    const std::size_t body_open = window_source.find(
        '{', parameters_close + 1);
    const std::size_t declaration_end = window_source.find(
        ';', parameters_close + 1);
    require(body_open != std::string::npos &&
            (declaration_end == std::string::npos ||
                body_open < declaration_end),
        std::string("WindowManager definition has no body: ") +
            std::string(function_name));
    const std::size_t body_close = matching_delimiter(
        window_source, body_open, '{', '}');
    return window_source.substr(
        body_open, body_close - body_open + 1);
  };
  const std::string refresh = function_body(
      window_source, "refresh_remastered_combat_cursor_sample");
  const std::string compact_refresh = without_whitespace(refresh);

  require(count_identifier(refresh, "LegacyGameSnapshotSource") == 1 &&
          count_identifier(refresh, "LegacyPointerTarget") == 1 &&
          count_identifier(refresh, "kClassicGameplayCrop") >= 3 &&
          count_identifier(refresh, "kClassicTileExtent") == 2,
      "cursor sampling must use one fresh snapshot and only the mapped "
      "Classic gameplay target with 32-pixel tile arithmetic");
  require(compact_refresh.contains(
              "this->presentation_host.mode()!=realmz::presentation::"
              "PresentationMode::remastered") &&
          compact_refresh.contains(
              "RealmzCurrentSemanticInputSurface()!=REALMZ_SEMANTIC_INPUT_"
              "COMBAT") &&
          compact_refresh.contains(
              "this->adaptive_shell_plan->screen!="
              "realmz::presentation::ScreenContext::combat") &&
          compact_refresh.contains(
              "!context.adaptive_eligible||context.screen!="
              "realmz::presentation::ScreenContext::combat"),
      "cursor sampling must fail closed outside the remastered adaptive "
      "combat surface and runtime context");

  const std::size_t outside_target = compact_refresh.find(
      "std::holds_alternative<realmz::presentation::"
      "OutsideWindowTarget>(target)");
  const std::size_t outside_clear = compact_refresh.find(
      "returnclear_and_report();", outside_target);
  const std::size_t legacy_target = compact_refresh.find(
      "constauto*legacy=std::get_if<realmz::presentation::"
      "LegacyPointerTarget>(&target)", outside_clear);
  const std::size_t gameplay_crop = compact_refresh.find(
      "realmz::presentation::kClassicGameplayCrop.contains("
      "legacy->classic_point)",
      legacy_target);
  const std::size_t retain_chrome = compact_refresh.find(
      "returnfalse;", gameplay_crop);
  const std::size_t snapshot_capture = compact_refresh.find(
      "snapshot=realmz::presentation::LegacyGameSnapshotSource().capture()",
      retain_chrome);
  const std::size_t local_x = compact_refresh.find(
      "constdoublelocal_x=legacy->classic_point.x-"
      "realmz::presentation::kClassicGameplayCrop.x",
      snapshot_capture);
  const std::size_t local_y = compact_refresh.find(
      "constdoublelocal_y=legacy->classic_point.y-"
      "realmz::presentation::kClassicGameplayCrop.y",
      local_x);
  const std::size_t column = compact_refresh.find(
      "std::floor(local_x/realmz::presentation::kClassicTileExtent)",
      local_y);
  const std::size_t row = compact_refresh.find(
      "std::floor(local_y/realmz::presentation::kClassicTileExtent)",
      column);
  const std::size_t visible_bounds = compact_refresh.find(
      "column>=snapshot.combat->visible_columns||"
      "row>=snapshot.combat->visible_rows",
      row);
  const std::size_t absolute_x = compact_refresh.find(
      "static_cast<size_t>(snapshot.combat->field_origin_x)+column",
      visible_bounds);
  const std::size_t absolute_y = compact_refresh.find(
      "static_cast<size_t>(snapshot.combat->field_origin_y)+row",
      absolute_x);
  const std::size_t field_bounds = compact_refresh.find(
      "if(cell_x>89U||cell_y>89U)", absolute_y);
  const std::size_t store_sample = compact_refresh.find(
      "this->remastered_combat_cursor_sample="
      "RemasteredCombatCursorSample{",
      field_bounds);
  require(outside_target != std::string::npos &&
          outside_clear != std::string::npos &&
          legacy_target != std::string::npos &&
          gameplay_crop != std::string::npos &&
          retain_chrome != std::string::npos &&
          snapshot_capture != std::string::npos &&
          local_x != std::string::npos && local_y != std::string::npos &&
          column != std::string::npos && row != std::string::npos &&
          visible_bounds != std::string::npos &&
          absolute_x != std::string::npos &&
          absolute_y != std::string::npos &&
          field_bounds != std::string::npos &&
          store_sample != std::string::npos &&
          outside_target < outside_clear && outside_clear < legacy_target &&
          legacy_target < gameplay_crop && gameplay_crop < retain_chrome &&
          retain_chrome < snapshot_capture && snapshot_capture < local_x &&
          local_x < local_y && local_y < column && column < row &&
          row < visible_bounds && visible_bounds < absolute_x &&
          absolute_x < absolute_y && absolute_y < field_bounds &&
          field_bounds < store_sample,
      "cursor sampling must clear outside-window input, cheaply retain valid "
      "in-window chrome, then subtract the gameplay crop (including its "
      "vertical offset), floor by tile size, add the viewport origin, and "
      "reject cells outside both the viewport and the 90x90 field");
  require(compact_refresh.contains(
              "returnprevious!=this->remastered_combat_cursor_sample;"),
      "gameplay sampling must report whether its actor-bound absolute cell "
      "changed");

  const std::string matches = qualified_member_body(
      "remastered_combat_cursor_sample_matches");
  const std::string compact_matches = without_whitespace(matches);
  for (const auto contract : {
           "snapshot.screen!=realmz::presentation::ScreenContext::combat",
           "!snapshot.combat||!snapshot.combat->active",
           "snapshot.combat->acting_combatant!=sample->combatant",
           "sample->field_origin_x!=snapshot.combat->field_origin_x",
           "sample->field_origin_y!=snapshot.combat->field_origin_y",
           "sample->visible_columns!=snapshot.combat->visible_columns",
           "sample->visible_rows!=snapshot.combat->visible_rows",
           "sample->cell.x<snapshot.combat->field_origin_x",
           "sample->cell.y<snapshot.combat->field_origin_y",
           "combatant->active&&combatant->targetable&&"
           "combatant->stamina.current>0",
       }) {
    require(compact_matches.contains(contract),
        std::string("combat cursor freshness is missing: ") + contract);
  }
  require(count_identifier(matches, "PartyMemberId") == 1 &&
          count_identifier(matches, "CombatantKind") == 1,
      "combat cursor freshness must bind the active actor to live party and "
      "combatant projections");

  const std::string pointer_motion = function_body(
      window_source, "map_remastered_pointer_motion");
  const std::string pointer_begin = function_body(
      window_source, "begin_remastered_pointer");
  const std::string pointer_end = function_body(
      window_source, "end_remastered_pointer");
  require(count_identifier(pointer_motion,
              "refresh_remastered_combat_cursor_sample") == 1 &&
          count_identifier(pointer_begin,
              "refresh_remastered_combat_cursor_sample") == 1 &&
          count_identifier(pointer_end,
              "refresh_remastered_combat_cursor_sample") == 1,
      "pointer motion, press, and release must each refresh the cursor sample "
      "exactly once");
  const std::string compact_end = without_whitespace(pointer_end);
  const std::size_t end_refresh = compact_end.find(
      "this->refresh_remastered_combat_cursor_sample(target)");
  const std::size_t end_dispatch = compact_end.find(
      "this->dispatch_remastered_shell_control(*pressed_control)");
  require(end_refresh != std::string::npos &&
          end_dispatch != std::string::npos && end_refresh < end_dispatch,
      "pointer release must refresh the absolute cursor cell before shell "
      "dispatch");

  const std::string composition = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_composition = without_whitespace(composition);
  require(compact_composition.contains(
              "this->remastered_combat_cursor_sample_matches(snapshot)") &&
          compact_composition.contains(
              "this->remastered_combat_cursor_sample->combatant=="
              "*center_combat_cursor_combatant") &&
          compact_composition.contains(
              ".cell=this->remastered_combat_cursor_sample->cell") &&
          compact_composition.contains(
              "center_combat_cursor_action->can_invoke()") &&
          compact_composition.contains(
              "legacy_key_message_for_center_combat_cursor("),
      "frame composition must bind Center Cursor to a fresh modeled actor, "
      "sampled absolute cell, and valid mapper route");

  const std::string keyboard_eligibility = qualified_member_body(
      "remastered_shell_keyboard_route_is_eligible");
  const std::string compact_keyboard = without_whitespace(
      keyboard_eligibility);
  require(compact_keyboard.contains(
              "std::get_if<realmz::presentation::CenterCombatCursorAction>("
              "&control.payload)") &&
          compact_keyboard.contains(
              "this->remastered_combat_action_page!="
              "realmz::presentation::CombatActionPage::special") &&
          compact_keyboard.contains(
              "!this->remastered_combat_cursor_sample_matches(*snapshot)") &&
          compact_keyboard.contains(
              "this->remastered_combat_cursor_sample->cell!="
              "center_combat_cursor->cell") &&
          count_identifier(keyboard_eligibility,
              "legacy_key_message_for_center_combat_cursor") == 1,
      "keyboard eligibility must revalidate the special-page payload, fresh "
      "sample, actor/cell identity, and mapper");

  const std::string dispatch = function_body(
      window_source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);
  const std::size_t center_dispatch = compact_dispatch.find(
      "if(center_combat_cursor){");
  const std::size_t dispatch_sample = compact_dispatch.find(
      "this->remastered_combat_cursor_sample_matches(snapshot)",
      center_dispatch);
  const std::size_t dispatch_model = compact_dispatch.find(
      "build_presentation_shell_model(", dispatch_sample);
  const std::size_t dispatch_modeled_action = compact_dispatch.find(
      "ActionIntent::center_combat_cursor", dispatch_model);
  const std::size_t dispatch_action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", dispatch_modeled_action);
  require(center_dispatch != std::string::npos &&
          dispatch_sample != std::string::npos &&
          dispatch_model != std::string::npos &&
          dispatch_modeled_action != std::string::npos &&
          dispatch_action != std::string::npos &&
          center_dispatch < dispatch_sample &&
          dispatch_sample < dispatch_model &&
          dispatch_model < dispatch_modeled_action &&
          dispatch_modeled_action < dispatch_action &&
          compact_dispatch.find(
              "this->remastered_combat_action_page!="
              "realmz::presentation::CombatActionPage::special",
              center_dispatch) < dispatch_sample &&
          compact_dispatch.find(
              "legacy_key_message_for_center_combat_cursor(",
              center_dispatch) < dispatch_sample,
      "Center Cursor dispatch must revalidate mapper, page, snapshot, sample, "
      "and rebuilt model before creating a UI action");

  const std::string combat_raw = function_body(read_file(
      repository_root / "src/realmz_orig/combat.c"), "combat");
  const std::string compact_combat = without_whitespace(combat_raw);
  const std::size_t center_case = compact_combat.find("case'm':");
  const std::size_t next_case = compact_combat.find("case'n':", center_case);
  require(center_case != std::string::npos &&
          next_case != std::string::npos && center_case < next_case,
      "Classic combat must retain its physical m Center Cursor branch");
  const std::string center_branch = compact_combat.substr(
      center_case, next_case - center_case);
  const std::size_t raw_center_case = combat_raw.find("case 'm':");
  const std::size_t raw_next_case = combat_raw.find(
      "case 'n':", raw_center_case);
  require(raw_center_case != std::string::npos &&
          raw_next_case != std::string::npos &&
          raw_center_case < raw_next_case,
      "Classic combat source is missing the bounded raw m command branch");
  const std::string raw_center_branch = combat_raw.substr(
      raw_center_case, raw_next_case - raw_center_case);
  const std::size_t take_cell = center_branch.find(
      "TakeSemanticCenterCombatCursorCell(&semantic_center_x,"
      "&semantic_center_y)");
  const std::size_t semantic_center = center_branch.find(
      "centerfield(semantic_center_x-fieldx,semantic_center_y-fieldy)",
      take_cell);
  const std::size_t physical_center = center_branch.find(
      "centerfield((point.h)/32,(point.v)/32)", semantic_center);
  const std::size_t take_count = count_identifier(
      raw_center_branch, "TakeSemanticCenterCombatCursorCell");
  const std::size_t center_count = count_identifier(
      raw_center_branch, "centerfield");
  require(take_count == 1 && center_count == 2,
      "Classic m must consume exactly one staged cursor cell and invoke "
      "centerfield exactly once per semantic/physical branch (found " +
          std::to_string(take_count) + " consumes and " +
          std::to_string(center_count) + " centers)");
  require(take_cell != std::string::npos &&
          semantic_center != std::string::npos &&
          physical_center != std::string::npos,
      "Classic m must retain the exact absolute-cell translation and "
      "physical point/32 fallback expressions");
  require(take_cell < semantic_center && semantic_center < physical_center,
      "Classic m must consume the staged cell before semantic centering and "
      "place the physical point/32 fallback afterward");
  require(!compact_combat.contains("key='m'") &&
          !compact_combat.contains("key=(char)'m'") &&
          count_identifier(combat_raw,
              "RealmzConsumeSemanticCenterCombatCursorEvent") == 0 &&
          count_identifier(combat_raw,
              "RealmzIsSemanticCenterCombatCursorTag") == 0,
      "Classic combat must not synthesize m from the mouse or consume the "
      "semantic tag directly");
}

void verify_top_level_loop(
    std::string_view body,
    std::string_view function_name,
    std::string_view expected_surface) {
  require(count_identifier(body, "GetNextSemanticGameplayEvent") == 1,
      std::string(function_name) +
          " must call exactly one semantic gameplay wrapper");
  require(count_identifier(body, "GetNextEvent") == 0,
      std::string(function_name) +
          " must not bypass the semantic gameplay wrapper");
  require(count_identifier(body, "WaitNextEvent") == 0,
      std::string(function_name) +
          " must not use a nested WaitNextEvent loop");
  require(count_identifier(body, "RealmzBeginSemanticInputSurface") == 0,
      std::string(function_name) +
          " must leave semantic scope ownership to EventManager");
  require(count_identifier(body, "RealmzEndSemanticInputSurface") == 0,
      std::string(function_name) +
          " must leave semantic scope ownership to EventManager");
  require(count_identifier(body, "RealmzConsumeSemanticMovementEvent") == 0,
      std::string(function_name) +
          " must leave tagged-event translation to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticPartySelectionEvent") == 0,
      std::string(function_name) +
          " must leave tagged selection consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenInventoryEvent") == 0,
      std::string(function_name) +
          " must leave tagged inventory consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSpellbookEvent") == 0,
      std::string(function_name) +
          " must leave tagged spellbook consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSaveGameEvent") == 0,
      std::string(function_name) +
          " must leave tagged save consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenLoadGameEvent") == 0,
      std::string(function_name) +
          " must leave tagged load consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticGuardCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged guard consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticFinishCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged finish consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticDelayCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged delay consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticCenterActiveCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged center consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticSwitchWeaponEvent") == 0,
      std::string(function_name) +
          " must leave tagged switch-weapon consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticCycleCombatFocusEvent") == 0,
      std::string(function_name) +
          " must leave tagged cycle-focus consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatItemsEvent") == 0,
      std::string(function_name) +
          " must leave tagged combat-items consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticAutoCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged Auto consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticShowCombatRangeEvent") == 0,
      std::string(function_name) +
          " must leave tagged Range consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticBandageCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged Bandage consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticUndoCombatantEvent") == 0,
      std::string(function_name) +
          " must leave tagged Undo consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatSpellbookEvent") == 0,
      std::string(function_name) +
          " must leave tagged combat-spellbook consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatTargetingEvent") == 0,
      std::string(function_name) +
          " must leave tagged combat-targeting consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticEscapeCombatEvent") == 0,
      std::string(function_name) +
          " must leave tagged Escape consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 0,
      std::string(function_name) +
          " must leave tagged scroll-case consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticCenterCombatCursorEvent") == 0,
      std::string(function_name) +
          " must leave tagged center-cursor consumption to EventManager");
  require(count_identifier(body, "RealmzApplyPartyMemberSelection") == 0,
      std::string(function_name) +
          " must leave selection mutation to EventManager's narrow adapter");
  require(count_identifier(body, "app1Evt") == 1,
      std::string(function_name) +
          " must retain one inert app1Evt case for the legacy switch");

  const std::string compact = without_whitespace(body);
  const std::string wrapper_call =
      "a=GetNextSemanticGameplayEvent(everyEvent,&gTheEvent," +
      std::string(expected_surface) + ");";
  const std::size_t wrapper = compact.find(wrapper_call);
  const std::size_t app_event = compact.find("app1Evt", wrapper);
  const std::size_t keydown_case = compact.find("keyDown", app_event);
  require(wrapper != std::string::npos &&
          app_event != std::string::npos &&
          keydown_case != std::string::npos,
      std::string(function_name) +
          " is missing its exact surface-specific wrapper route");
  require(wrapper < app_event && app_event < keydown_case,
      std::string(function_name) +
          " must poll through the wrapper before its preserved event switch");
}

void verify_legacy_loop_ownership(const fs::path& repository_root) {
  const fs::path legacy_root = repository_root / "src/realmz_orig";
  const std::string misc = code_only(read_file(legacy_root / "misc.c"));
  const std::string threed_source = code_only(
      read_file(legacy_root / "threed.c"));
  const std::string combat_raw_source = read_file(legacy_root / "combat.c");
  const std::string combat_source = code_only(combat_raw_source);
  const std::string combatchoice_source = code_only(read_file(
      legacy_root / "combatinfo-combatchoice.c"));
  const std::string getscroll_source = code_only(read_file(
      legacy_root / "getscroll.c"));
  const std::string items_source = code_only(read_file(
      legacy_root / "items.c"));
  const std::string centerstage_source = code_only(read_file(
      legacy_root / "centerstage.c"));
  const std::string showrange_source = code_only(read_file(
      legacy_root / "showrange.c"));
  const std::string getchoice_source = code_only(
      read_file(legacy_root / "getchoice.c"));
  const std::string question_source = code_only(
      read_file(legacy_root / "question.c"));
  const std::string presentation_context_source = code_only(read_file(
      repository_root / "src/presentation/LegacyPresentationContext.c"));

  const std::string mainscreen = function_body(misc, "mainscreen");
  const std::string threed = function_body(threed_source, "threed");
  const std::string combat = function_body(combat_source, "combat");
  const std::string combat_raw = function_body(combat_raw_source, "combat");
  const std::string combatchoice = function_body(
      combatchoice_source, "combatchoice");
  const std::string getscroll = function_body(getscroll_source, "getscroll");
  const std::string items = function_body(items_source, "items");
  const std::string centerstage = function_body(
      centerstage_source, "centerstage");
  const std::string showrange = function_body(
      showrange_source, "showrange");
  const std::string question3 = function_body(
      question_source, "question3");
  verify_top_level_loop(
      mainscreen,
      "mainscreen",
      "REALMZ_SEMANTIC_INPUT_EXPLORATION");
  verify_top_level_loop(
      threed,
      "threed",
      "REALMZ_SEMANTIC_INPUT_DUNGEON");
  verify_top_level_loop(
      combat,
      "combat",
      "REALMZ_SEMANTIC_INPUT_COMBAT");
  const std::string compact_combat = without_whitespace(combat_raw);
  const std::size_t items_case = compact_combat.find("case'i':");
  const std::size_t items_next_case = compact_combat.find(
      "case'a':", items_case);
  const std::size_t items_control = compact_combat.find(
      "theControl=itemsbut;", items_case);
  const std::size_t items_break = compact_combat.find(
      "break;", items_control);
  require(items_case != std::string::npos &&
          items_next_case != std::string::npos &&
          items_control != std::string::npos &&
          items_break != std::string::npos,
      "combat must retain the exact Classic Items command handoff");
  require(items_case < items_control && items_control < items_break &&
          items_break < items_next_case,
      "combat Items must select itemsbut and break before Auto Character");
  const std::string items_case_branch = compact_combat.substr(
      items_case, items_next_case - items_case);
  require(count_identifier(items_case_branch, "getup") == 0 &&
          count_identifier(items_case_branch, "combatchoice") == 0 &&
          count_identifier(items_case_branch, "GetNextEvent") == 0 &&
          count_identifier(items_case_branch, "WaitNextEvent") == 0,
      "combat Items key branch must only select the shared Classic command "
      "route");
  const std::size_t auto_case = items_next_case;
  const std::size_t auto_next_case = compact_combat.find(
      "case'l':", auto_case);
  const std::size_t auto_control = compact_combat.find(
      "theControl=campbut;", auto_case);
  const std::size_t auto_break = compact_combat.find(
      "break;", auto_control);
  require(auto_case != std::string::npos &&
          auto_next_case != std::string::npos &&
          auto_control != std::string::npos &&
          auto_break != std::string::npos,
      "combat must retain the exact Classic Auto command handoff");
  require(auto_case < auto_control && auto_control < auto_break &&
          auto_break < auto_next_case,
      "combat Auto must select campbut and break before Use Scroll");
  const std::string auto_case_branch = compact_combat.substr(
      auto_case, auto_next_case - auto_case);
  require(count_identifier(auto_case_branch, "getup") == 0 &&
          count_identifier(auto_case_branch, "combatchoice") == 0 &&
          count_identifier(auto_case_branch, "GetNextEvent") == 0 &&
          count_identifier(auto_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(auto_case_branch, "Rand") == 0,
      "combat Auto key branch must only select the shared Classic command "
      "route");

  const std::size_t scroll_case = auto_next_case;
  const std::size_t scroll_next_case = compact_combat.find(
      "case's':", scroll_case);
  const std::size_t scroll_control = compact_combat.find(
      "theControl=viewspellsbut;", scroll_case);
  const std::size_t scroll_jump = compact_combat.find(
      "gotojumpposs;", scroll_control);
  const std::size_t scroll_break = compact_combat.find(
      "break;", scroll_jump);
  require(scroll_case != std::string::npos &&
          scroll_next_case != std::string::npos &&
          scroll_control != std::string::npos &&
          scroll_jump != std::string::npos &&
          scroll_break != std::string::npos,
      "combat must retain the exact Classic Use Scroll control handoff");
  require(scroll_case < scroll_control && scroll_control < scroll_jump &&
          scroll_jump < scroll_break && scroll_break < scroll_next_case,
      "combat Use Scroll must select viewspellsbut and jump to shared "
      "combatchoice before Cast Spell");
  const std::string scroll_case_branch = compact_combat.substr(
      scroll_case, scroll_next_case - scroll_case);
  require(scroll_case_branch.contains(
              "theControl=viewspellsbut;gotojumpposs;break;") &&
          count_identifier(scroll_case_branch, "getscroll") == 0 &&
          count_identifier(scroll_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(scroll_case_branch, "ModalDialog") == 0,
      "combat lowercase l must only select the preserved shared Classic "
      "scroll route");

  const std::string compact_scroll_combatchoice =
      without_whitespace(combatchoice);
  const std::size_t scroll_control_branch =
      compact_scroll_combatchoice.find(
          "if((theControl==viewspellsbut)&&(c[charup].armor[13])){");
  const std::size_t scroll_getscroll = compact_scroll_combatchoice.find(
      "if(getscroll()){", scroll_control_branch);
  const std::size_t scroll_skipload = compact_scroll_combatchoice.find(
      "skipload=TRUE;", scroll_getscroll);
  const std::size_t scroll_goto_wand = compact_scroll_combatchoice.find(
      "gotowand;", scroll_skipload);
  const std::size_t scroll_wand = compact_scroll_combatchoice.find(
      "wand:", scroll_goto_wand);
  require(scroll_control_branch != std::string::npos &&
          scroll_getscroll != std::string::npos &&
          scroll_skipload != std::string::npos &&
          scroll_goto_wand != std::string::npos &&
          scroll_wand != std::string::npos &&
          scroll_control_branch < scroll_getscroll &&
          scroll_getscroll < scroll_skipload &&
          scroll_skipload < scroll_goto_wand &&
          scroll_goto_wand < scroll_wand,
      "Classic combatchoice must call getscroll and route an accepted scroll "
      "into the shared wand targeting path");

  const std::string compact_getscroll = without_whitespace(getscroll);
  const std::size_t scroll_modal_loop = compact_getscroll.find("for(;;){");
  const std::size_t scroll_modal_flush = compact_getscroll.find(
      "FlushEvents(everyEvent,0);", scroll_modal_loop);
  const std::size_t scroll_modal = compact_getscroll.find(
      "ModalDialog(0L,&itemHit);", scroll_modal_flush);
  const std::size_t scroll_browse = compact_getscroll.find(
      "charselectnew++;", scroll_modal);
  const std::size_t scroll_cancel = compact_getscroll.find(
      "if((itemHit==12)||(itemHit==1)){", scroll_modal);
  const std::size_t scroll_cancel_inspell = compact_getscroll.find(
      "inspell=0;", scroll_cancel);
  const std::size_t scroll_cancel_out = compact_getscroll.find(
      "gotoout;", scroll_cancel_inspell);
  const std::size_t scroll_selected = compact_getscroll.find(
      "if(c[charselectnew].scrollcase[itemHit-7].powerlevel){",
      scroll_cancel_out);
  const std::size_t scroll_copy_castnum = compact_getscroll.find(
      "castnum=c[charselectnew].scrollcase[itemHit-7].castnum-1;",
      scroll_selected);
  const std::size_t scroll_copy_power = compact_getscroll.find(
      "powerlevel=c[charselectnew].scrollcase[itemHit-7].powerlevel;",
      scroll_copy_castnum);
  const std::size_t scroll_loadspell = compact_getscroll.find(
      "loadspell(castcaste,castlevel,castnum);", scroll_copy_power);
  const std::size_t scroll_flag = compact_getscroll.find(
      "usescroll=TRUE;", scroll_loadspell);
  const std::size_t scroll_combat_validity = compact_getscroll.find(
      "if((incombat)&&(!spellinfo.incombat)){", scroll_flag);
  const std::size_t scroll_consume = compact_getscroll.find(
      "c[charselectnew].scrollcase[itemHit-7].powerlevel=0;",
      scroll_combat_validity);
  const std::size_t scroll_accept_inspell = compact_getscroll.find(
      "inspell=TRUE;", scroll_consume);
  const std::size_t scroll_accept_out = compact_getscroll.find(
      "gotoout;", scroll_accept_inspell);
  const std::size_t scroll_return = compact_getscroll.find(
      "return(inspell);", scroll_accept_out);
  require(scroll_modal_loop != std::string::npos &&
          scroll_modal_flush != std::string::npos &&
          scroll_modal != std::string::npos &&
          scroll_browse != std::string::npos &&
          scroll_cancel != std::string::npos &&
          scroll_cancel_inspell != std::string::npos &&
          scroll_cancel_out != std::string::npos &&
          scroll_selected != std::string::npos &&
          scroll_copy_castnum != std::string::npos &&
          scroll_copy_power != std::string::npos &&
          scroll_loadspell != std::string::npos &&
          scroll_flag != std::string::npos &&
          scroll_combat_validity != std::string::npos &&
          scroll_consume != std::string::npos &&
          scroll_accept_inspell != std::string::npos &&
          scroll_accept_out != std::string::npos &&
          scroll_return != std::string::npos,
      "Classic getscroll must retain its raw chooser, browsing, cancellation, "
      "selection, validation, and accepted-scroll consumption route");
  require(scroll_modal_loop < scroll_modal_flush &&
          scroll_modal_flush < scroll_modal &&
          scroll_modal < scroll_cancel &&
          scroll_cancel < scroll_cancel_inspell &&
          scroll_cancel_inspell < scroll_cancel_out &&
          scroll_cancel_out < scroll_selected &&
          scroll_selected < scroll_copy_castnum &&
          scroll_copy_castnum < scroll_copy_power &&
          scroll_copy_power < scroll_loadspell &&
          scroll_loadspell < scroll_flag &&
          scroll_flag < scroll_combat_validity &&
          scroll_combat_validity < scroll_consume &&
          scroll_consume < scroll_accept_inspell &&
          scroll_accept_inspell < scroll_accept_out &&
          scroll_accept_out < scroll_return,
      "getscroll must leave cancellation non-consuming and clear an accepted "
      "scroll before returning to shared wand targeting");
  const std::string scroll_cancel_branch = compact_getscroll.substr(
      scroll_cancel, scroll_cancel_out - scroll_cancel);
  require(!scroll_cancel_branch.contains("scrollcase[") &&
          !scroll_cancel_branch.contains("powerlevel=0"),
      "cancelling getscroll before selection must not consume a scroll");
  require(count_identifier(getscroll, "ModalDialog") == 1 &&
          count_identifier(getscroll, "WaitNextEvent") == 0 &&
          count_identifier(getscroll, "GetNextEvent") == 0,
      "getscroll must own exactly one raw ModalDialog loop, not a top-level "
      "semantic gameplay poll");
  require_no_semantic_scope_or_consumer(getscroll, "getscroll");

  const std::size_t cast_case = compact_combat.find("case's':");
  const std::size_t cast_next_case = compact_combat.find(
      "case'u':", cast_case);
  const std::size_t cast_control = compact_combat.find(
      "theControl=castspellsbut;", cast_case);
  const std::size_t cast_jump = compact_combat.find(
      "gotojumpposs;", cast_control);
  const std::size_t cast_break = compact_combat.find(
      "break;", cast_jump);
  const std::size_t jump_label = compact_combat.find(
      "jumpposs:combatchoice();", cast_next_case);
  require(cast_case != std::string::npos &&
          cast_next_case != std::string::npos &&
          cast_control != std::string::npos &&
          cast_jump != std::string::npos &&
          cast_break != std::string::npos &&
          jump_label != std::string::npos,
      "combat must retain the exact Classic Cast Spell control handoff");
  require(cast_case < cast_control && cast_control < cast_jump &&
          cast_jump < cast_break && cast_break < cast_next_case &&
          cast_next_case < jump_label,
      "combat Cast Spell must select castspellsbut and jump to the shared "
      "combatchoice path before the Undo command");
  const std::string cast_case_branch = compact_combat.substr(
      cast_case, cast_next_case - cast_case);
  require(cast_case_branch.contains(
              "theControl=castspellsbut;gotojumpposs;break;"),
      "combat Cast Spell key branch must only select the preserved Classic "
      "command route without opening another semantic path");

  const std::string compact_cast_combatchoice =
      without_whitespace(combatchoice);
  const std::size_t cast_control_branch = compact_cast_combatchoice.find(
      "if(theControl==castspellsbut){");
  const std::size_t cancast_gate = compact_cast_combatchoice.find(
      "if(!cancast(charup,0)){", cast_control_branch);
  const std::size_t castspell_call = compact_cast_combatchoice.find(
      "castspell();", cancast_gate);
  require(cast_control_branch != std::string::npos &&
          cancast_gate != std::string::npos &&
          castspell_call != std::string::npos &&
          cast_control_branch < cancast_gate && cancast_gate < castspell_call,
      "Classic combatchoice must retain cancast enforcement before the "
      "castspell modal/targeting handoff");
  require(count_identifier(combatchoice, "cancast") >= 1 &&
          count_identifier(combatchoice, "castspell") >= 1 &&
          count_identifier(combatchoice,
              "RealmzConsumeSemanticOpenCombatSpellbookEvent") == 0,
      "Classic combatchoice must retain spell validation, targeting, and "
      "mutation ownership without consuming semantic tags");

  const std::size_t target_case = compact_combat.find("case't':");
  const std::size_t target_control = compact_combat.find(
      "theControl=combatitem;", target_case);
  const std::size_t target_break = compact_combat.find(
      "break;", target_control);
  const std::size_t target_switch_end = compact_combat.find(
      "if(whichset)", target_break);
  require(target_case != std::string::npos &&
          target_control != std::string::npos &&
          target_break != std::string::npos &&
          target_switch_end != std::string::npos &&
          target_case < target_control && target_control < target_break &&
          target_break < target_switch_end,
      "combat must retain the exact Classic Target control handoff");
  const std::string target_case_branch = compact_combat.substr(
      target_case, target_switch_end - target_case);
  require(target_case_branch.contains(
              "downbutton(TRUE);theControl=combatitem;break;"),
      "combat Target key branch must only select combatitem before the shared "
      "Classic command route");
  require(count_identifier(target_case_branch, "combatchoice") == 0 &&
          count_identifier(target_case_branch, "Rand") == 0 &&
          count_identifier(target_case_branch, "loaditem") == 0 &&
          count_identifier(target_case_branch, "charge") == 0,
      "combat Target key selection must not absorb Classic targeting rules");

  const std::size_t target_mouse = compact_combat.find(
      "if(PtInRect(point,&buttonrect))key='t';", target_switch_end);
  const std::size_t weapon_mouse = compact_combat.find(
      "if(PtInRect(point,&buttonrect))key='w';", target_mouse);
  const std::size_t mouse_gotkey = compact_combat.find(
      "if(key)gotogotkey;", weapon_mouse);
  const std::size_t shared_jumpposs = compact_combat.find(
      "jumpposs:combatchoice();", mouse_gotkey);
  const std::size_t mouse_find_control = compact_combat.rfind(
      "thePart=FindControl(point,screen,&theControl);", shared_jumpposs);
  const std::size_t shared_empty_queue = compact_combat.find(
      "emptyque();", shared_jumpposs);
  const std::size_t shared_turn_check = compact_combat.find(
      "if(c[charup].attacks<2)getup(FALSE);", shared_empty_queue);
  require(target_mouse != std::string::npos &&
          weapon_mouse != std::string::npos &&
          mouse_gotkey != std::string::npos &&
          shared_jumpposs != std::string::npos &&
          mouse_find_control != std::string::npos &&
          shared_empty_queue != std::string::npos &&
          shared_turn_check != std::string::npos &&
          target_mouse < weapon_mouse && weapon_mouse < mouse_gotkey &&
          mouse_gotkey < shared_jumpposs &&
          mouse_find_control < shared_jumpposs &&
          shared_jumpposs < shared_empty_queue &&
          shared_empty_queue < shared_turn_check,
      "Classic Target mouse hit must converge through gotkey before the same "
      "shared combatchoice route as keyboard t");
  require(compact_combat.find("key='l'", mouse_find_control) ==
          std::string::npos,
      "Classic mouse dispatch must leave Use Scroll on the shared "
      "FindControl/viewspellsbut path rather than inventing a second l map");
  require(count_identifier(combat, "combatchoice") >= 2 &&
          count_identifier(combat, "getup") >= 1,
      "Classic combat must retain the post-combatchoice attack-based turn "
      "decision for keyboard and FindControl routes");

  const std::string compact_target_combatchoice =
      without_whitespace(combatchoice);
  const std::size_t target_control_branch = compact_target_combatchoice.find(
      "if((theControl==combatitem)&&(lastshown==q[up])){");
  const std::size_t target_source_slot = compact_target_combatchoice.find(
      "loaditem(c[charup].armor[2]);", target_control_branch);
  const std::size_t target_quiver = compact_target_combatchoice.find(
      "loaditem(c[charup].armor[10]);", target_source_slot);
  const std::size_t target_toggled_slot = compact_target_combatchoice.find(
      "loaditem(c[charup].armor[15]);", target_quiver);
  const std::size_t target_spell_gate = compact_target_combatchoice.find(
      "if(item.sp2>1100){", target_toggled_slot);
  const std::size_t target_inventory_loop = compact_target_combatchoice.find(
      "for(t=0;t<c[charup].numitems;t++){", target_spell_gate);
  const std::size_t target_matching_item = compact_target_combatchoice.find(
      "if(c[charup].items[t].id==item.itemid)break;",
      target_inventory_loop);
  const std::size_t target_charge_gate = compact_target_combatchoice.find(
      "if(!c[charup].items[t].charge){", target_matching_item);
  const std::size_t target_random_power = compact_target_combatchoice.find(
      "if(powerlevel==8)powerlevel=Rand(7);", target_charge_gate);
  const std::size_t target_animated_range_gate =
      compact_target_combatchoice.find(
      "if(!getrange(charup,c[charup].traiter,FALSE))",
      target_random_power);
  const std::size_t target_animated_random_choice =
      compact_target_combatchoice.find(
          "c[charup].target=randrange(0,maxloopminus);",
          target_animated_range_gate);
  const std::size_t target_charge_mutation = compact_target_combatchoice.find(
      "c[charup].items[itemnum].charge--;",
      target_animated_random_choice);
  const std::size_t target_cast = compact_target_combatchoice.find(
      "cast(targetnum,charup);", target_charge_mutation);
  require(target_control_branch != std::string::npos &&
          target_source_slot != std::string::npos &&
          target_quiver != std::string::npos &&
          target_toggled_slot != std::string::npos &&
          target_spell_gate != std::string::npos &&
          target_inventory_loop != std::string::npos &&
          target_matching_item != std::string::npos &&
          target_charge_gate != std::string::npos &&
          target_random_power != std::string::npos &&
          target_animated_range_gate != std::string::npos &&
          target_animated_random_choice != std::string::npos &&
          target_charge_mutation != std::string::npos &&
          target_cast != std::string::npos,
      "Classic combatchoice must retain Target source, quiver, first item, "
      "charge, animated targeting/RNG, and cast ownership");
  require(target_control_branch < target_source_slot &&
          target_source_slot < target_quiver &&
          target_quiver < target_toggled_slot &&
          target_toggled_slot < target_spell_gate &&
          target_spell_gate < target_inventory_loop &&
          target_inventory_loop < target_matching_item &&
          target_matching_item < target_charge_gate &&
          target_charge_gate < target_random_power &&
          target_random_power < target_animated_range_gate &&
          target_animated_range_gate < target_animated_random_choice &&
          target_animated_random_choice < target_charge_mutation &&
          target_charge_mutation < target_cast,
      "Classic Target command must preserve its ordered rule and mutation "
      "pipeline after the semantic handoff");

  const std::size_t target_usescroll = compact_target_combatchoice.find(
      "usescroll=TRUE;", target_spell_gate);
  const std::size_t target_manual_charge_mutation = target_charge_mutation;
  const std::size_t target_manual_drop = compact_target_combatchoice.find(
      "dropitem(charup,itemused,itemnum,1,FALSE);",
      target_manual_charge_mutation);
  const std::size_t target_items_handoff = compact_target_combatchoice.find(
      "if(theControl==itemsbut){", target_manual_drop);
  const std::size_t target_goto_wand = compact_target_combatchoice.find(
      "gotowand;", target_items_handoff);
  const std::size_t target_wand_label = compact_target_combatchoice.find(
      "wand:", target_goto_wand);
  const std::size_t target_manual_loop = compact_target_combatchoice.find(
      "while(gDone==FALSE){", target_wand_label);
  const std::size_t target_manual_wait = compact_target_combatchoice.find(
      "WaitNextEvent(everyEvent,&gTheEvent,0L,NIL);",
      target_manual_loop);
  const std::size_t target_manual_event_switch =
      compact_target_combatchoice.find(
          "switch(gTheEvent.what){", target_manual_wait);
  require(target_usescroll != std::string::npos &&
          target_manual_charge_mutation != std::string::npos &&
          target_manual_drop != std::string::npos &&
          target_items_handoff != std::string::npos &&
          target_goto_wand != std::string::npos &&
          target_wand_label != std::string::npos &&
          target_manual_loop != std::string::npos &&
          target_manual_wait != std::string::npos &&
          target_manual_event_switch != std::string::npos,
      "Classic Target must retain its non-animated charge/drop handoff into "
      "the shared manual targeting event loop");
  require(target_usescroll < target_manual_charge_mutation &&
          target_manual_charge_mutation < target_manual_drop &&
          target_manual_drop < target_items_handoff &&
          target_items_handoff < target_goto_wand &&
          target_goto_wand < target_wand_label &&
          target_wand_label < target_manual_loop &&
          target_manual_loop < target_manual_wait &&
          target_manual_wait < target_manual_event_switch &&
          target_manual_event_switch < target_cast,
      "Classic must consume/drop the Target item before entering its raw "
      "manual WaitNextEvent targeting loop");
  require(count_identifier(combatchoice, "WaitNextEvent") == 1 &&
          count_identifier(combatchoice, "GetNextEvent") == 0,
      "Classic manual targeting must retain exactly one raw WaitNextEvent "
      "loop without substituting a top-level event poll");

  const std::size_t target_abort_inspell = compact_target_combatchoice.find(
      "inspell=infocombat=FALSE;", target_manual_event_switch);
  const std::size_t target_abort_done = compact_target_combatchoice.find(
      "gDone=TRUE;", target_abort_inspell);
  const std::size_t target_abort_movement = compact_target_combatchoice.find(
      "c[charup].movement-=3;", target_abort_done);
  const std::size_t target_abort_clear = compact_target_combatchoice.find(
      "cleartarget();", target_abort_movement);
  const std::size_t target_abort_refund = compact_target_combatchoice.find(
      "if((!usescroll)&&(memoryspell==TRUE))c[charup].spellpoints+="
      "(.66*(spellinfo.cost*powerlevel));",
      target_abort_clear);
  const std::size_t target_abort_reset = compact_target_combatchoice.find(
      "usescroll=memoryspell=FALSE;", target_abort_refund);
  const std::size_t target_abort_return = compact_target_combatchoice.find(
      "return;", target_abort_reset);
  require(target_abort_inspell != std::string::npos &&
          target_abort_done != std::string::npos &&
          target_abort_movement != std::string::npos &&
          target_abort_clear != std::string::npos &&
          target_abort_refund != std::string::npos &&
          target_abort_reset != std::string::npos &&
          target_abort_return != std::string::npos,
      "Classic manual Target abort must retain its state cleanup, movement "
      "cost, and scroll-aware refund guard");
  require(target_manual_event_switch < target_abort_inspell &&
          target_abort_inspell < target_abort_done &&
          target_abort_done < target_abort_movement &&
          target_abort_movement < target_abort_clear &&
          target_abort_clear < target_abort_refund &&
          target_abort_refund < target_abort_reset &&
          target_abort_reset < target_abort_return,
      "Classic Target abort must charge three movement and apply its refund "
      "guard before clearing the item-spell flags");
  const std::string target_abort_branch = compact_target_combatchoice.substr(
      target_abort_inspell,
      target_abort_return + std::string_view("return;").size() -
          target_abort_inspell);
  require(target_abort_branch.contains("!usescroll") &&
          !target_abort_branch.contains(
              "c[charup].items[itemnum].charge++") &&
          !target_abort_branch.contains("scrollcase[") &&
          count_identifier(target_abort_branch, "attacks") == 0,
      "Target item charges must not be restored by the manual abort branch; "
      "an accepted scroll stays consumed, loses three movement, spends no "
      "attack, and cannot receive the memorized-spell refund");

  const std::size_t target_launch_label = compact_target_combatchoice.find(
      "launch:", target_abort_return);
  const std::size_t target_launch_movement =
      compact_target_combatchoice.find(
          "c[charup].movement-=12;", target_launch_label);
  const std::size_t target_launch_clamp = compact_target_combatchoice.find(
      "if(c[charup].movement<0)c[charup].movement=0;",
      target_launch_movement);
  const std::size_t target_launch_attacks = compact_target_combatchoice.find(
      "c[charup].attacks-=2;", target_launch_clamp);
  const std::size_t target_launch_cast = compact_target_combatchoice.find(
      "cast(targetnum,charup);", target_launch_attacks);
  const std::size_t target_launch_clear = compact_target_combatchoice.find(
      "cleartarget();", target_launch_cast);
  const std::size_t target_launch_return = compact_target_combatchoice.find(
      "return;", target_launch_clear);
  require(target_launch_label != std::string::npos &&
          target_launch_movement != std::string::npos &&
          target_launch_clamp != std::string::npos &&
          target_launch_attacks != std::string::npos &&
          target_launch_cast != std::string::npos &&
          target_launch_clear != std::string::npos &&
          target_launch_return != std::string::npos,
      "Classic manual Target launch must retain its movement/attack costs, "
      "cast, and target cleanup");
  require(target_abort_return < target_launch_label &&
          target_launch_label < target_launch_movement &&
          target_launch_movement < target_launch_clamp &&
          target_launch_clamp < target_launch_attacks &&
          target_launch_attacks < target_launch_cast &&
          target_launch_cast < target_launch_clear &&
          target_launch_clear < target_launch_return,
      "Classic Target launch must apply its costs before spell resolution "
      "and target cleanup");
  const std::string target_launch_branch =
      compact_target_combatchoice.substr(
          target_launch_label,
          target_launch_return + std::string_view("return;").size() -
              target_launch_label);
  require(count_identifier(target_launch_branch, "movement") >= 2 &&
          count_identifier(target_launch_branch, "attacks") >= 2 &&
          count_identifier(target_launch_branch, "spellpoints") == 0 &&
          count_identifier(target_launch_branch, "scrollcase") == 0,
      "accepted scroll launch must spend twelve movement (with clamp) and "
      "two attacks without spending spell points or restoring its charge");
  require(count_identifier(
              combatchoice, "RealmzConsumeSemanticOpenCombatTargetingEvent") ==
          0,
      "Classic combatchoice must not consume semantic Target tags");
  require(count_identifier(combatchoice,
              "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 0 &&
          count_identifier(getscroll,
              "RealmzConsumeSemanticOpenCombatScrollCaseEvent") == 0,
      "Classic combatchoice/getscroll must own scroll choice and effects "
      "without consuming semantic scroll-case tags");

  const std::size_t undo_case = cast_next_case;
  const std::size_t undo_next_case = compact_combat.find(
      "case'e':", undo_case);
  const std::size_t undo_top = compact_combat.find(
      "buttonrect.top=366+downshift;", undo_case);
  const std::size_t undo_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", undo_top);
  const std::size_t undo_left = compact_combat.find(
      "buttonrect.left=422+leftshift;", undo_bottom);
  const std::size_t undo_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+44;", undo_left);
  const std::size_t undo_button_down = compact_combat.find(
      "downbutton(TRUE);", undo_right);
  const std::size_t undo_gate = compact_combat.find(
      "if(canundo){", undo_button_down);
  const std::size_t undo_condition = compact_combat.find(
      "if((!c[charup].condition[COND_HELPLESS])&&(!c[charup].traiter)&&"
      "(!c[charup].condition[COND_CONFUSED])){",
      undo_gate);
  const std::size_t undo_sound = compact_combat.find(
      "sound(664);", undo_condition);
  const std::size_t undo_attacks = compact_combat.find(
      "c[charup].attacks-=(c[charup].normattacks+"
      "c[charup].attackbonus);",
      undo_sound);
  const std::size_t undo_draw = compact_combat.find(
      "drawbody(charup,TRUE,0);", undo_attacks);
  const std::size_t undo_clear_field = compact_combat.find(
      "bodyfield(charup);", undo_draw);
  const std::size_t undo_x = compact_combat.find(
      "pos[charup][0]=undox-fieldx;", undo_clear_field);
  const std::size_t undo_y = compact_combat.find(
      "pos[charup][1]=undoy-fieldy;", undo_x);
  const std::size_t undo_under = compact_combat.find(
      "charunder[charup]=field[undox][undoy];", undo_y);
  const std::size_t undo_restore_field = compact_combat.find(
      "field[undox][undoy]=charup;", undo_under);
  const std::size_t undo_look_port = compact_combat.find(
      "SetPort(GetWindowPort(look));", undo_restore_field);
  const std::size_t undo_queue_rewind = compact_combat.find(
      "up--;", undo_look_port);
  const std::size_t undo_screen_port = compact_combat.find(
      "SetPort(GetWindowPort(screen));", undo_queue_rewind);
  const std::size_t undo_turn = compact_combat.find(
      "getup(FALSE);", undo_screen_port);
  const std::size_t undo_warning = compact_combat.find(
      "warn(82);", undo_turn);
  const std::size_t undo_button_up = compact_combat.find(
      "upbutton(TRUE);", undo_warning);
  const std::size_t undo_break = compact_combat.find(
      "break;", undo_button_up);
  require(undo_case != std::string::npos &&
          undo_next_case != std::string::npos &&
          undo_top != std::string::npos &&
          undo_bottom != std::string::npos &&
          undo_left != std::string::npos &&
          undo_right != std::string::npos &&
          undo_button_down != std::string::npos &&
          undo_gate != std::string::npos &&
          undo_condition != std::string::npos &&
          undo_sound != std::string::npos &&
          undo_attacks != std::string::npos &&
          undo_draw != std::string::npos &&
          undo_clear_field != std::string::npos &&
          undo_x != std::string::npos &&
          undo_y != std::string::npos &&
          undo_under != std::string::npos &&
          undo_restore_field != std::string::npos &&
          undo_look_port != std::string::npos &&
          undo_queue_rewind != std::string::npos &&
          undo_screen_port != std::string::npos &&
          undo_turn != std::string::npos &&
          undo_warning != std::string::npos &&
          undo_button_up != std::string::npos &&
          undo_break != std::string::npos,
      "combat must retain the exact Classic Undo gate, condition checks, "
      "rollback mutations, queue rewind, and turn handoff");
  require(undo_case < undo_top && undo_top < undo_bottom &&
          undo_bottom < undo_left && undo_left < undo_right &&
          undo_right < undo_button_down && undo_button_down < undo_gate &&
          undo_gate < undo_condition && undo_condition < undo_sound &&
          undo_sound < undo_attacks && undo_attacks < undo_draw &&
          undo_draw < undo_clear_field && undo_clear_field < undo_x &&
          undo_x < undo_y && undo_y < undo_under &&
          undo_under < undo_restore_field &&
          undo_restore_field < undo_look_port &&
          undo_look_port < undo_queue_rewind &&
          undo_queue_rewind < undo_screen_port &&
          undo_screen_port < undo_turn && undo_turn < undo_warning &&
          undo_warning < undo_button_up && undo_button_up < undo_break &&
          undo_break < undo_next_case,
      "combat Undo must preserve its complete accepted rollback path before "
      "the rejected canundo warning and following Escape case");
  const std::string undo_case_branch = compact_combat.substr(
      undo_case, undo_next_case - undo_case);
  require(count_identifier(undo_case_branch, "canundo") == 1 &&
          count_identifier(undo_case_branch, "warn") == 1 &&
          count_identifier(undo_case_branch, "downbutton") == 1 &&
          count_identifier(undo_case_branch, "upbutton") == 1 &&
          count_identifier(undo_case_branch, "drawbody") == 1 &&
          count_identifier(undo_case_branch, "bodyfield") == 1 &&
          count_identifier(undo_case_branch, "getup") == 1 &&
          count_identifier(undo_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(undo_case_branch, "GetNextEvent") == 0 &&
          count_identifier(
              undo_case_branch, "GetNextSemanticGameplayEvent") == 0 &&
          count_identifier(undo_case_branch, "Rand") == 0,
      "combat Undo must retain one Classic canundo split without nested "
      "semantic input, modal polling, or random choice");

  const std::size_t escape_case = undo_next_case;
  const std::size_t escape_next_case = compact_combat.find(
      "case'g':", escape_case);
  const std::size_t escape_top = compact_combat.find(
      "buttonrect.top=366+downshift;", escape_case);
  const std::size_t escape_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", escape_top);
  const std::size_t escape_left = compact_combat.find(
      "buttonrect.left=364+leftshift;", escape_bottom);
  const std::size_t escape_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+54;", escape_left);
  const std::size_t escape_button_down = compact_combat.find(
      "downbutton(TRUE);", escape_right);
  const std::size_t escape_range = compact_combat.find(
      "getrange(charup,0,FALSE);", escape_button_down);
  const std::size_t escape_too_close = compact_combat.find(
      "if(range[maxloop+1]<10)", escape_range);
  const std::size_t escape_too_close_warning = compact_combat.find(
      "warn(81);", escape_too_close);
  const std::size_t escape_too_close_button_up = compact_combat.find(
      "upbutton(TRUE);", escape_too_close_warning);
  const std::size_t escape_condition_gate = compact_combat.find(
      "if((c[charup].condition[COND_HELPLESS])||(c[charup].traiter)||"
      "(c[charup].condition[COND_CONFUSED])||"
      "(c[charup].condition[COND_TANGLED])||"
      "(c[charup].condition[COND_SLOW])){",
      escape_too_close_button_up);
  const std::size_t escape_condition_warning = compact_combat.find(
      "warn(83);", escape_condition_gate);
  const std::size_t escape_condition_button_up = compact_combat.find(
      "upbutton(TRUE);", escape_condition_warning);
  const std::size_t escape_confirmation = compact_combat.find(
      "question3((StringPtr)\"EmbraceCowardice\","
      "(StringPtr)\"StayandFight\")==2",
      escape_condition_button_up);
  const std::size_t escape_bodyground = compact_combat.find(
      "bodyground(charup,0);", escape_confirmation);
  const std::size_t escape_bodyfield = compact_combat.find(
      "bodyfield(charup);", escape_bodyground);
  const std::size_t escape_queue_and_position = compact_combat.find(
      "q[up]=c[charup].position=pos[charup][0]=pos[charup][1]=-1;",
      escape_bodyfield);
  const std::size_t escape_inbattle = compact_combat.find(
      "c[charup].inbattle=0;", escape_queue_and_position);
  const std::size_t escape_prestige = compact_combat.find(
      "c[charup].prestigepenelty+=200;", escape_inbattle);
  const std::size_t escape_light = compact_combat.find(
      "updatelight(charup,FALSE);", escape_prestige);
  const std::size_t escape_reply_reset = compact_combat.find(
      "reply=FALSE;", escape_light);
  const std::size_t escape_loyal_scan = compact_combat.find(
      "for(t=0;t<=charnum;t++)", escape_reply_reset);
  const std::size_t escape_loyal_condition = compact_combat.find(
      "if((c[t].inbattle)&&(!c[t].traiter))", escape_loyal_scan);
  const std::size_t escape_reply = compact_combat.find(
      "reply=TRUE;", escape_loyal_condition);
  const std::size_t escape_last_party = compact_combat.find(
      "if(!reply){", escape_reply);
  const std::size_t escape_killmon = compact_combat.find(
      "killmon=numenemy;", escape_last_party);
  const std::size_t escape_coward = compact_combat.find(
      "coward=TRUE;", escape_killmon);
  const std::size_t escape_turn = compact_combat.find(
      "getup(FALSE);", escape_coward);
  const std::size_t escape_break = compact_combat.find(
      "break;", escape_turn);
  require(escape_case != std::string::npos &&
          escape_next_case != std::string::npos,
      "combat must retain the Classic Escape and following Guard cases");
  require(
          escape_top != std::string::npos &&
          escape_bottom != std::string::npos &&
          escape_left != std::string::npos &&
          escape_right != std::string::npos &&
          escape_button_down != std::string::npos &&
          escape_range != std::string::npos &&
          escape_too_close != std::string::npos,
      "combat must retain the exact Classic Escape button geometry and "
      "range gate");
  require(
          escape_too_close_warning != std::string::npos &&
          escape_too_close_button_up != std::string::npos &&
          escape_condition_gate != std::string::npos &&
          escape_condition_warning != std::string::npos &&
          escape_condition_button_up != std::string::npos &&
          escape_confirmation != std::string::npos,
      "combat must retain Escape warning precedence before question3");
  require(
          escape_bodyground != std::string::npos &&
          escape_bodyfield != std::string::npos &&
          escape_queue_and_position != std::string::npos &&
          escape_inbattle != std::string::npos &&
          escape_prestige != std::string::npos &&
          escape_light != std::string::npos &&
          escape_reply_reset != std::string::npos &&
          escape_loyal_scan != std::string::npos &&
          escape_loyal_condition != std::string::npos &&
          escape_reply != std::string::npos &&
          escape_last_party != std::string::npos &&
          escape_killmon != std::string::npos &&
          escape_coward != std::string::npos &&
          escape_turn != std::string::npos &&
          escape_break != std::string::npos,
      "combat must retain the exact Classic Escape confirmed mutation route");
  require(escape_case < escape_top && escape_top < escape_bottom &&
          escape_bottom < escape_left && escape_left < escape_right &&
          escape_right < escape_button_down &&
          escape_button_down < escape_range &&
          escape_range < escape_too_close &&
          escape_too_close < escape_too_close_warning &&
          escape_too_close_warning < escape_too_close_button_up &&
          escape_too_close_button_up < escape_condition_gate &&
          escape_condition_gate < escape_condition_warning &&
          escape_condition_warning < escape_condition_button_up &&
          escape_condition_button_up < escape_confirmation &&
          escape_confirmation < escape_bodyground &&
          escape_bodyground < escape_bodyfield &&
          escape_bodyfield < escape_queue_and_position &&
          escape_queue_and_position < escape_inbattle &&
          escape_inbattle < escape_prestige &&
          escape_prestige < escape_light &&
          escape_light < escape_reply_reset &&
          escape_reply_reset < escape_loyal_scan &&
          escape_loyal_scan < escape_loyal_condition &&
          escape_loyal_condition < escape_reply &&
          escape_reply < escape_last_party &&
          escape_last_party < escape_killmon &&
          escape_killmon < escape_coward &&
          escape_coward < escape_turn && escape_turn < escape_break &&
          escape_break < escape_next_case,
      "Classic Escape must preserve getrange, warning precedence, fresh "
      "confirmation, and confirm-only mutation ordering");
  const std::string escape_preconfirmation = compact_combat.substr(
      escape_case, escape_confirmation - escape_case);
  require(count_identifier(escape_preconfirmation, "bodyground") == 0 &&
          count_identifier(escape_preconfirmation, "bodyfield") == 0 &&
          count_identifier(escape_preconfirmation, "prestigepenelty") == 0 &&
          count_identifier(escape_preconfirmation, "updatelight") == 0 &&
          count_identifier(escape_preconfirmation, "killmon") == 0 &&
          count_identifier(escape_preconfirmation, "coward") == 0 &&
          count_identifier(escape_preconfirmation, "getup") == 0,
      "Escape warnings and a declined confirmation must not perform retreat "
      "state, prestige, light, loyalty-scan, cowardice, or turn mutations");
  const std::string escape_case_branch = compact_combat.substr(
      escape_case, escape_next_case - escape_case);
  require(count_identifier(escape_case_branch, "getrange") == 1 &&
          count_identifier(escape_case_branch, "warn") == 2 &&
          count_identifier(escape_case_branch, "question3") == 1 &&
          count_identifier(escape_case_branch, "bodyground") == 1 &&
          count_identifier(escape_case_branch, "bodyfield") == 1 &&
          count_identifier(escape_case_branch, "updatelight") == 1 &&
          count_identifier(escape_case_branch, "getup") == 1 &&
          count_identifier(escape_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(escape_case_branch, "GetNextEvent") == 0 &&
          count_identifier(
              escape_case_branch, "GetNextSemanticGameplayEvent") == 0,
      "combat Escape must leave its fresh modal input to raw question3 and "
      "must not open another semantic gameplay scope");

  const std::size_t escape_mouse = compact_combat.find(
      "if(PtInRect(point,&buttonrect))key='e';", target_switch_end);
  const std::size_t undo_mouse = compact_combat.find(
      "if(PtInRect(point,&buttonrect))key='u';", escape_mouse);
  const std::size_t target_mouse_after_escape = compact_combat.find(
      "if(PtInRect(point,&buttonrect))key='t';", undo_mouse);
  const std::size_t escape_mouse_gotkey = compact_combat.find(
      "if(key)gotogotkey;", target_mouse_after_escape);
  require(escape_mouse != std::string::npos &&
          undo_mouse != std::string::npos &&
          target_mouse_after_escape != std::string::npos &&
          escape_mouse_gotkey != std::string::npos &&
          escape_mouse < undo_mouse &&
          undo_mouse < target_mouse_after_escape &&
          target_mouse_after_escape < escape_mouse_gotkey,
      "Classic Escape mouse hit must synthesize the same lowercase e and "
      "converge through gotkey before the keyboard-owned case");

  const std::string compact_question3 = without_whitespace(question3);
  const std::size_t question_flush = compact_question3.find(
      "FlushEvents(everyEvent,0);");
  const std::size_t question_loop = compact_question3.find(
      "for(;;){", question_flush);
  const std::size_t question_wait = compact_question3.find(
      "WaitNextEvent(everyEvent,&gTheEvent,0L,0L);", question_loop);
  const std::size_t question_key = compact_question3.find(
      "if(gTheEvent.what==keyDown){", question_wait);
  const std::size_t question_dialog = compact_question3.find(
      "if(IsDialogEvent(&gTheEvent)){", question_key);
  const std::size_t question_select = compact_question3.find(
      "DialogSelect(&gTheEvent,&dummy,&itemHit)", question_dialog);
  require(count_identifier(question3, "FlushEvents") == 1 &&
          count_identifier(question3, "WaitNextEvent") == 1 &&
          count_identifier(question3, "GetNextEvent") == 0 &&
          question_flush != std::string::npos &&
          question_loop != std::string::npos &&
          question_wait != std::string::npos &&
          question_key != std::string::npos &&
          question_dialog != std::string::npos &&
          question_select != std::string::npos &&
          question_flush < question_loop && question_loop < question_wait &&
          question_wait < question_key && question_key < question_dialog &&
          question_dialog < question_select,
      "raw question3 must flush preexisting input, then own one fresh "
      "WaitNextEvent modal accepting keyboard or dialog input");
  require_no_semantic_scope_or_consumer(question3, "question3");

  const std::size_t bandage_case = compact_combat.find("case'b':");
  const std::size_t bandage_next_case = compact_combat.find(
      "case'r':", bandage_case);
  const std::size_t bandage_top = compact_combat.find(
      "buttonrect.top=386+downshift;", bandage_case);
  const std::size_t bandage_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", bandage_top);
  const std::size_t bandage_left = compact_combat.find(
      "buttonrect.left=364+leftshift;", bandage_bottom);
  const std::size_t bandage_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+60;", bandage_left);
  const std::size_t bandage_button_down = compact_combat.find(
      "downbutton(TRUE);", bandage_right);
  const std::size_t bandage_canundo_rejection = compact_combat.find(
      "if(!canundo)", bandage_button_down);
  const std::size_t bandage_warning = compact_combat.find(
      "warn(84);", bandage_canundo_rejection);
  const std::size_t bandage_rejected_button_up = compact_combat.find(
      "upbutton(TRUE);", bandage_warning);
  const std::size_t bandage_accepted_path = compact_combat.find(
      "}else{", bandage_rejected_button_up);
  const std::size_t bandage_prompt = compact_combat.find(
      "flashmessage((StringPtr)\"Selectcharactertobandage.\","
      "30,100,-1,10105);",
      bandage_accepted_path);
  const std::size_t bandage_screen_port = compact_combat.find(
      "SetPort(GetWindowPort(screen));", bandage_prompt);
  const std::size_t bandage_choice = compact_combat.find(
      "getchoice(0,0,TRUE);", bandage_screen_port);
  const std::size_t bandage_look_port = compact_combat.find(
      "SetPort(GetWindowPort(look));", bandage_choice);
  const std::size_t bandage_clear_prompt = compact_combat.find(
      "flashmessage((StringPtr)\"\",30,100,-1,0);",
      bandage_look_port);
  const std::size_t bandage_restore_screen_port = compact_combat.find(
      "SetPort(GetWindowPort(screen));", bandage_clear_prompt);
  const std::size_t bandage_target_loop = compact_combat.find(
      "for(t=0;t<=charnum;t++){", bandage_restore_screen_port);
  const std::size_t bandage_selected_target = compact_combat.find(
      "if(track[t]){", bandage_target_loop);
  const std::size_t bandage_mutation = compact_combat.find(
      "c[t].bleeding=FALSE;", bandage_selected_target);
  const std::size_t bandage_target_redraw = compact_combat.find(
      "updatepictbox(t,TRUE,0);", bandage_mutation);
  const std::size_t bandage_turn_advance = compact_combat.find(
      "getup(FALSE);", bandage_target_redraw);
  const std::size_t bandage_break = compact_combat.find(
      "break;", bandage_turn_advance);
  require(bandage_case != std::string::npos &&
          bandage_next_case != std::string::npos &&
          bandage_top != std::string::npos &&
          bandage_bottom != std::string::npos &&
          bandage_left != std::string::npos &&
          bandage_right != std::string::npos &&
          bandage_button_down != std::string::npos &&
          bandage_canundo_rejection != std::string::npos &&
          bandage_warning != std::string::npos &&
          bandage_rejected_button_up != std::string::npos &&
          bandage_accepted_path != std::string::npos &&
          bandage_prompt != std::string::npos &&
          bandage_screen_port != std::string::npos &&
          bandage_choice != std::string::npos &&
          bandage_look_port != std::string::npos &&
          bandage_clear_prompt != std::string::npos &&
          bandage_restore_screen_port != std::string::npos &&
          bandage_target_loop != std::string::npos &&
          bandage_selected_target != std::string::npos &&
          bandage_mutation != std::string::npos &&
          bandage_target_redraw != std::string::npos &&
          bandage_turn_advance != std::string::npos &&
          bandage_break != std::string::npos,
      "combat must retain the exact bounded Classic Bandage button, canundo "
      "rejection, target selection, mutation, and turn handoff");
  require(bandage_case < bandage_top && bandage_top < bandage_bottom &&
          bandage_bottom < bandage_left && bandage_left < bandage_right &&
          bandage_right < bandage_button_down &&
          bandage_button_down < bandage_canundo_rejection &&
          bandage_canundo_rejection < bandage_warning &&
          bandage_warning < bandage_rejected_button_up &&
          bandage_rejected_button_up < bandage_accepted_path &&
          bandage_accepted_path < bandage_prompt &&
          bandage_prompt < bandage_screen_port &&
          bandage_screen_port < bandage_choice &&
          bandage_choice < bandage_look_port &&
          bandage_look_port < bandage_clear_prompt &&
          bandage_clear_prompt < bandage_restore_screen_port &&
          bandage_restore_screen_port < bandage_target_loop &&
          bandage_target_loop < bandage_selected_target &&
          bandage_selected_target < bandage_mutation &&
          bandage_mutation < bandage_target_redraw &&
          bandage_target_redraw < bandage_turn_advance &&
          bandage_turn_advance < bandage_break &&
          bandage_break < bandage_next_case,
      "combat Bandage must preserve both canundo outcomes and complete its "
      "accepted raw target/mutation path before the Range case");
  const std::string bandage_case_branch = compact_combat.substr(
      bandage_case, bandage_next_case - bandage_case);
  require(count_identifier(bandage_case_branch, "canundo") == 1 &&
          count_identifier(bandage_case_branch, "warn") == 1 &&
          count_identifier(bandage_case_branch, "upbutton") == 1 &&
          count_identifier(bandage_case_branch, "flashmessage") == 2 &&
          count_identifier(bandage_case_branch, "SetPort") == 3 &&
          count_identifier(bandage_case_branch, "getchoice") == 1 &&
          count_identifier(bandage_case_branch, "bleeding") == 1 &&
          count_identifier(bandage_case_branch, "updatepictbox") == 1 &&
          count_identifier(bandage_case_branch, "getup") == 1 &&
          count_identifier(bandage_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(bandage_case_branch, "GetNextEvent") == 0 &&
          count_identifier(
              bandage_case_branch, "GetNextSemanticGameplayEvent") == 0 &&
          count_identifier(bandage_case_branch, "Rand") == 0,
      "combat Bandage must preserve one Classic canundo split and delegate "
      "selection to getchoice without opening another semantic input route");
  const std::size_t range_case = compact_combat.find("case'r':");
  const std::size_t range_next_case = compact_combat.find(
      "case'd':", range_case);
  const std::size_t range_top = compact_combat.find(
      "buttonrect.top=346+downshift;", range_case);
  const std::size_t range_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", range_top);
  const std::size_t range_left = compact_combat.find(
      "buttonrect.left=364+leftshift;", range_bottom);
  const std::size_t range_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+102;", range_left);
  const std::size_t range_button_down = compact_combat.find(
      "downbutton(TRUE);", range_right);
  const std::size_t range_modal = compact_combat.find(
      "showrange(0);", range_button_down);
  const std::size_t range_recenter = compact_combat.find(
      "centerfield(5+(2*screensize),5+screensize);", range_modal);
  const std::size_t range_flush = compact_combat.find(
      "FlushEvents(everyEvent,0);", range_recenter);
  const std::size_t range_button_up = compact_combat.find(
      "upbutton(TRUE);", range_flush);
  const std::size_t range_return = compact_combat.find(
      "gotobackfromrange;", range_button_up);
  const std::size_t range_break = compact_combat.find(
      "break;", range_return);
  require(range_case != std::string::npos &&
          range_next_case != std::string::npos &&
          range_top != std::string::npos &&
          range_bottom != std::string::npos &&
          range_left != std::string::npos &&
          range_right != std::string::npos &&
          range_button_down != std::string::npos &&
          range_modal != std::string::npos &&
          range_recenter != std::string::npos &&
          range_flush != std::string::npos &&
          range_button_up != std::string::npos &&
          range_return != std::string::npos &&
          range_break != std::string::npos,
      "combat must retain the exact bounded Classic Range handoff, raw modal, "
      "recenter, flush, and return sequence");
  require(range_case < range_top && range_top < range_bottom &&
          range_bottom < range_left && range_left < range_right &&
          range_right < range_button_down &&
          range_button_down < range_modal && range_modal < range_recenter &&
          range_recenter < range_flush && range_flush < range_button_up &&
          range_button_up < range_return && range_return < range_break &&
          range_break < range_next_case,
      "combat Range must preserve its exact Classic sequence before Delay");
  const std::string range_case_branch = compact_combat.substr(
      range_case, range_next_case - range_case);
  require(count_identifier(range_case_branch, "showrange") == 1 &&
          count_identifier(range_case_branch, "GetNextEvent") == 0 &&
          count_identifier(range_case_branch, "WaitNextEvent") == 0 &&
          count_identifier(range_case_branch, "GetNextSemanticGameplayEvent") ==
              0 &&
          count_identifier(range_case_branch, "Rand") == 0,
      "combat Range key branch must delegate its raw dismissal loop to "
      "showrange without adding another semantic route or random choice");

  const std::string compact_showrange = without_whitespace(showrange);
  const std::size_t range_modal_flush = compact_showrange.find(
      "FlushEvents(everyEvent,0);");
  const std::size_t range_raw_wait = compact_showrange.find(
      "WaitNextEvent(everyEvent,&gTheEvent,0L,NIL);", range_modal_flush);
  const std::size_t range_dismissal = compact_showrange.find(
      "if((gTheEvent.what!=mouseDown)&&(gTheEvent.what!=keyDown))"
      "gotobackup;",
      range_raw_wait);
  require(range_modal_flush != std::string::npos &&
          range_raw_wait != std::string::npos &&
          range_dismissal != std::string::npos &&
          range_modal_flush < range_raw_wait &&
          range_raw_wait < range_dismissal,
      "showrange must flush before its one raw WaitNextEvent dismissal loop "
      "and accept only mouseDown or keyDown");
  require(count_identifier(showrange, "FlushEvents") == 1 &&
          count_identifier(showrange, "WaitNextEvent") == 1 &&
          count_identifier(showrange, "GetNextEvent") == 0 &&
          count_identifier(showrange, "GetNextSemanticGameplayEvent") == 0 &&
          count_identifier(showrange, "Rand") == 0,
      "showrange must remain a deterministic Classic-owned raw modal without "
      "a semantic gameplay poll or random choice");
  require_no_semantic_scope_or_consumer(showrange, "showrange");
  const std::size_t guard_case = compact_combat.find("case'g':");
  const std::size_t guard_mutation = compact_combat.find(
      "c[charup].guarding=TRUE;", guard_case);
  const std::size_t guard_turn_advance = compact_combat.find(
      "getup(FALSE);", guard_mutation);
  require(guard_case != std::string::npos &&
          guard_mutation != std::string::npos &&
          guard_turn_advance != std::string::npos,
      "combat must retain the preserved Guard branch, mutation, and turn "
      "advance");
  require(guard_case < guard_mutation && guard_mutation < guard_turn_advance,
      "combat Guard must mutate the active party member before advancing the "
      "turn");
  const std::size_t next_focus_case = compact_combat.find("case'n':");
  const std::size_t next_focus_boundary = compact_combat.find(
      "case'p':", next_focus_case);
  const std::size_t next_focus_top = compact_combat.find(
      "buttonrect.top=386+downshift;", next_focus_case);
  const std::size_t next_focus_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", next_focus_top);
  const std::size_t next_focus_left = compact_combat.find(
      "buttonrect.left=428+leftshift;", next_focus_bottom);
  const std::size_t next_focus_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+38;", next_focus_left);
  const std::size_t next_focus_button_down = compact_combat.find(
      "downbutton(TRUE);", next_focus_right);
  const std::size_t next_focus_target_save = compact_combat.find(
      "targetrect=buttonrect;", next_focus_button_down);
  const std::size_t next_focus_stage = compact_combat.find(
      "centerstage(1);", next_focus_target_save);
  const std::size_t next_focus_target_restore = compact_combat.find(
      "buttonrect=targetrect;", next_focus_stage);
  const std::size_t next_focus_button_up = compact_combat.find(
      "upbutton(TRUE);", next_focus_target_restore);
  const std::size_t next_focus_break = compact_combat.find(
      "break;", next_focus_button_up);
  require(next_focus_case != std::string::npos &&
          next_focus_boundary != std::string::npos &&
          next_focus_top != std::string::npos &&
          next_focus_bottom != std::string::npos &&
          next_focus_left != std::string::npos &&
          next_focus_right != std::string::npos &&
          next_focus_button_down != std::string::npos &&
          next_focus_target_save != std::string::npos &&
          next_focus_stage != std::string::npos &&
          next_focus_target_restore != std::string::npos &&
          next_focus_button_up != std::string::npos &&
          next_focus_break != std::string::npos,
      "combat must retain the exact bounded Center Next button and relative "
      "centerstage sequence");
  require(next_focus_case < next_focus_top &&
          next_focus_top < next_focus_bottom &&
          next_focus_bottom < next_focus_left &&
          next_focus_left < next_focus_right &&
          next_focus_right < next_focus_button_down &&
          next_focus_button_down < next_focus_target_save &&
          next_focus_target_save < next_focus_stage &&
          next_focus_stage < next_focus_target_restore &&
          next_focus_target_restore < next_focus_button_up &&
          next_focus_button_up < next_focus_break &&
          next_focus_break < next_focus_boundary,
      "combat Center Next must preserve its exact relative-focus sequence "
      "before Center Previous");
  const std::string next_focus_branch = compact_combat.substr(
      next_focus_case, next_focus_boundary - next_focus_case);
  require(count_identifier(next_focus_branch, "getup") == 0 &&
          count_identifier(next_focus_branch, "combatchoice") == 0 &&
          count_identifier(next_focus_branch, "WaitNextEvent") == 0 &&
          count_identifier(next_focus_branch, "GetNextEvent") == 0 &&
          count_identifier(next_focus_branch, "Rand") == 0,
      "combat Center Next must not advance the turn, enter another input "
      "flow, or choose a destination randomly");

  const std::size_t previous_focus_case = next_focus_boundary;
  const std::size_t previous_focus_boundary = compact_combat.find(
      "case'c':", previous_focus_case);
  const std::size_t previous_focus_top = compact_combat.find(
      "buttonrect.top=386+downshift;", previous_focus_case);
  const std::size_t previous_focus_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", previous_focus_top);
  const std::size_t previous_focus_left = compact_combat.find(
      "buttonrect.left=470+leftshift;", previous_focus_bottom);
  const std::size_t previous_focus_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+50;", previous_focus_left);
  const std::size_t previous_focus_target_save = compact_combat.find(
      "targetrect=buttonrect;", previous_focus_right);
  const std::size_t previous_focus_button_down = compact_combat.find(
      "downbutton(TRUE);", previous_focus_target_save);
  const std::size_t previous_focus_stage = compact_combat.find(
      "centerstage(-1);", previous_focus_button_down);
  const std::size_t previous_focus_target_restore = compact_combat.find(
      "buttonrect=targetrect;", previous_focus_stage);
  const std::size_t previous_focus_button_up = compact_combat.find(
      "upbutton(TRUE);", previous_focus_target_restore);
  const std::size_t previous_focus_break = compact_combat.find(
      "break;", previous_focus_button_up);
  require(previous_focus_case != std::string::npos &&
          previous_focus_boundary != std::string::npos &&
          previous_focus_top != std::string::npos &&
          previous_focus_bottom != std::string::npos &&
          previous_focus_left != std::string::npos &&
          previous_focus_right != std::string::npos &&
          previous_focus_target_save != std::string::npos &&
          previous_focus_button_down != std::string::npos &&
          previous_focus_stage != std::string::npos &&
          previous_focus_target_restore != std::string::npos &&
          previous_focus_button_up != std::string::npos &&
          previous_focus_break != std::string::npos,
      "combat must retain the exact bounded Center Previous button and "
      "relative centerstage sequence");
  require(previous_focus_case < previous_focus_top &&
          previous_focus_top < previous_focus_bottom &&
          previous_focus_bottom < previous_focus_left &&
          previous_focus_left < previous_focus_right &&
          previous_focus_right < previous_focus_target_save &&
          previous_focus_target_save < previous_focus_button_down &&
          previous_focus_button_down < previous_focus_stage &&
          previous_focus_stage < previous_focus_target_restore &&
          previous_focus_target_restore < previous_focus_button_up &&
          previous_focus_button_up < previous_focus_break &&
          previous_focus_break < previous_focus_boundary,
      "combat Center Previous must preserve its exact relative-focus "
      "sequence before Center Active");
  const std::string previous_focus_branch = compact_combat.substr(
      previous_focus_case, previous_focus_boundary - previous_focus_case);
  require(count_identifier(previous_focus_branch, "getup") == 0 &&
          count_identifier(previous_focus_branch, "combatchoice") == 0 &&
          count_identifier(previous_focus_branch, "WaitNextEvent") == 0 &&
          count_identifier(previous_focus_branch, "GetNextEvent") == 0 &&
          count_identifier(previous_focus_branch, "Rand") == 0,
      "combat Center Previous must not advance the turn, enter another input "
      "flow, or choose a destination randomly");

  const std::string compact_centerstage = without_whitespace(centerstage);
  const std::size_t relative_step = compact_centerstage.find(
      "aimindex+=way;");
  const std::size_t upper_wrap = compact_centerstage.find(
      "if(aimindex>maxloopminus)aimindex=1;", relative_step);
  const std::size_t lower_wrap = compact_centerstage.find(
      "if(aimindex<1)aimindex=maxloopminus;", upper_wrap);
  const std::size_t bounded_return = compact_centerstage.find(
      "if(++count>maxloopminus)return;", lower_wrap);
  const std::size_t skip_empty = compact_centerstage.find(
      "while(q[aimindex]==-1);", bounded_return);
  const std::size_t resolve_relative = compact_centerstage.find(
      "who=q[aimindex];", skip_empty);
  const std::size_t relative_sound = compact_centerstage.find(
      "if(way)sound(147);", resolve_relative);
  require(relative_step != std::string::npos &&
          upper_wrap != std::string::npos &&
          lower_wrap != std::string::npos &&
          bounded_return != std::string::npos &&
          skip_empty != std::string::npos &&
          resolve_relative != std::string::npos &&
          relative_sound != std::string::npos,
      "centerstage must retain bounded queue-relative focus resolution and "
      "relative-command feedback");
  require(relative_step < upper_wrap && upper_wrap < lower_wrap &&
          lower_wrap < bounded_return && bounded_return < skip_empty &&
          skip_empty < resolve_relative && resolve_relative < relative_sound,
      "centerstage must step, wrap, bound, skip empty queue slots, resolve, "
      "and then provide relative-command feedback");
  require(count_identifier(centerstage, "Rand") == 0 &&
          count_identifier(centerstage, "WaitNextEvent") == 0 &&
          count_identifier(centerstage, "GetNextEvent") == 0 &&
          count_identifier(centerstage, "getup") == 0 &&
          count_identifier(centerstage, "combatchoice") == 0,
      "centerstage must remain a bounded non-random view operation without "
      "nested input or turn ownership");

  const std::size_t center_case = previous_focus_boundary;
  const std::size_t center_next_case = compact_combat.find(
      "case'f':", center_case);
  const std::size_t center_target_save = compact_combat.find(
      "targetrect=buttonrect;", center_case);
  const std::size_t center_sound = compact_combat.find(
      "sound(147);", center_target_save);
  const std::size_t center_button_down = compact_combat.find(
      "downbutton(TRUE);", center_sound);
  const std::size_t center_aim = compact_combat.find(
      "aimindex=up;", center_button_down);
  const std::size_t center_stage = compact_combat.find(
      "centerstage(0);", center_aim);
  const std::size_t center_target_restore = compact_combat.find(
      "buttonrect=targetrect;", center_stage);
  const std::size_t center_button_up = compact_combat.find(
      "upbutton(TRUE);", center_target_restore);
  require(center_case != std::string::npos &&
          center_next_case != std::string::npos &&
          center_target_save != std::string::npos &&
          center_sound != std::string::npos &&
          center_button_down != std::string::npos &&
          center_aim != std::string::npos &&
          center_stage != std::string::npos &&
          center_target_restore != std::string::npos &&
          center_button_up != std::string::npos,
      "combat must retain the preserved Center button, sound, active-actor, "
      "and camera sequence");
  require(center_case < center_target_save &&
          center_target_save < center_sound &&
          center_sound < center_button_down &&
          center_button_down < center_aim &&
          center_aim < center_stage &&
          center_stage < center_target_restore &&
          center_target_restore < center_button_up &&
          center_button_up < center_next_case,
      "combat Center must preserve its exact camera sequence before Finish");
  const std::string center_branch = compact_combat.substr(
      center_case, center_next_case - center_case);
  require(count_identifier(center_branch, "getup") == 0 &&
          count_identifier(center_branch, "combatchoice") == 0 &&
          count_identifier(center_branch, "WaitNextEvent") == 0,
      "combat Center must not advance the turn or enter a nested command or "
      "event loop");
  const std::size_t finish_case = compact_combat.find("case'f':");
  const std::size_t finish_mutation = compact_combat.find(
      "c[charup].movement=c[charup].guarding=0;", finish_case);
  const std::size_t finish_turn_advance = compact_combat.find(
      "getup(FALSE);", finish_mutation);
  require(finish_case != std::string::npos &&
          finish_mutation != std::string::npos &&
          finish_turn_advance != std::string::npos,
      "combat must retain the preserved Finish branch, mutation, and turn "
      "advance");
  require(finish_case < finish_mutation &&
          finish_mutation < finish_turn_advance,
      "combat Finish must clear movement and guarding before advancing the "
      "turn");
  const std::size_t weapon_case = compact_combat.find("case'w':");
  const std::size_t weapon_next_case = compact_combat.find(
      "case't':", weapon_case);
  const std::size_t weapon_top = compact_combat.find(
      "buttonrect.top=326+downshift;", weapon_case);
  const std::size_t weapon_bottom = compact_combat.find(
      "buttonrect.bottom=buttonrect.top+18;", weapon_top);
  const std::size_t weapon_left = compact_combat.find(
      "buttonrect.left=522+leftshift;", weapon_bottom);
  const std::size_t weapon_right = compact_combat.find(
      "buttonrect.right=buttonrect.left+44;", weapon_left);
  const std::size_t weapon_button_down = compact_combat.find(
      "downbutton(TRUE);", weapon_right);
  const std::size_t weapon_control = compact_combat.find(
      "theControl=melee;", weapon_button_down);
  const std::size_t weapon_break = compact_combat.find(
      "break;", weapon_control);
  require(weapon_case != std::string::npos &&
          weapon_next_case != std::string::npos &&
          weapon_top != std::string::npos &&
          weapon_bottom != std::string::npos &&
          weapon_left != std::string::npos &&
          weapon_right != std::string::npos &&
          weapon_button_down != std::string::npos &&
          weapon_control != std::string::npos &&
          weapon_break != std::string::npos,
      "combat must retain the preserved Weapon button and melee-control "
      "handoff");
  require(weapon_case < weapon_top && weapon_top < weapon_bottom &&
          weapon_bottom < weapon_left && weapon_left < weapon_right &&
          weapon_right < weapon_button_down &&
          weapon_button_down < weapon_control &&
          weapon_control < weapon_break && weapon_break < weapon_next_case,
      "combat Weapon must preserve its exact handoff sequence before Target");
  const std::string weapon_branch = compact_combat.substr(
      weapon_case, weapon_next_case - weapon_case);
  require(weapon_branch.find("c[charup]") == std::string::npos &&
          count_identifier(weapon_branch, "getup") == 0 &&
          count_identifier(weapon_branch, "combatchoice") == 0 &&
          count_identifier(weapon_branch, "WaitNextEvent") == 0 &&
          count_identifier(weapon_branch, "upbutton") == 0,
      "combat Weapon branch must only select the preserved shared command "
      "route");

  const std::size_t shared_choice_guard = compact_combat.find(
      "if((theControl)&&(q[up]<9)){", weapon_next_case);
  const std::size_t shared_party_loss_guard = compact_combat.find(
      "if(killparty>charnum)", shared_choice_guard);
  const std::size_t shared_party_loss = compact_combat.find(
      "partyloss(0);", shared_party_loss_guard);
  const std::size_t shared_choice = compact_combat.find(
      "combatchoice();", shared_party_loss);
  const std::size_t shared_attack_guard = compact_combat.find(
      "if(c[charup].attacks<2)", shared_choice);
  const std::size_t shared_getup = compact_combat.find(
      "getup(FALSE);", shared_attack_guard);
  const std::size_t shared_tail_break = compact_combat.find(
      "break;", shared_getup);
  require(shared_choice_guard != std::string::npos &&
          shared_party_loss_guard != std::string::npos &&
          shared_party_loss != std::string::npos &&
          shared_choice != std::string::npos &&
          shared_attack_guard != std::string::npos &&
          shared_getup != std::string::npos &&
          shared_tail_break != std::string::npos,
      "combat must retain the shared Classic command and post-command turn "
      "tail");
  require(items_break < auto_case && auto_break < shared_choice_guard &&
          weapon_next_case < shared_choice_guard &&
          shared_choice_guard < shared_party_loss_guard &&
          shared_party_loss_guard < shared_party_loss &&
          shared_party_loss < shared_choice &&
          shared_choice < shared_attack_guard &&
          shared_attack_guard < shared_getup &&
          shared_getup < shared_tail_break,
      "combat Items, Auto, and Weapon must hand off through party-loss "
      "handling and combatchoice before the preserved attacks/getup tail");

  const std::string compact_combatchoice = without_whitespace(combatchoice);
  const std::size_t items_block_begin = compact_combatchoice.find(
      "if(theControl==itemsbut){");
  const std::size_t items_block_end = compact_combatchoice.find(
      "if(itemused<-99)", items_block_begin);
  const std::size_t items_skipload = compact_combatchoice.find(
      "if(!skipload){", items_block_begin);
  const std::size_t items_sound = compact_combatchoice.find(
      "sound(141);", items_skipload);
  const std::size_t items_bounds = compact_combatchoice.find(
      "GetControlBounds(itemsbut,&r);", items_sound);
  const std::size_t items_feedback = compact_combatchoice.find(
      "ploticon3(129,r);", items_bounds);
  const std::size_t items_in = compact_combatchoice.find(
      "in();", items_feedback);
  const std::size_t items_modal = compact_combatchoice.find(
      "items();", items_in);
  const std::size_t items_out = compact_combatchoice.find(
      "out();", items_modal);
  const std::size_t items_combat_update = compact_combatchoice.find(
      "combatupdate2(charup);", items_out);
  const std::size_t items_center = compact_combatchoice.find(
      "centerfield(pos[charup][0],pos[charup][1]);", items_combat_update);
  const std::size_t items_controls = compact_combatchoice.find(
      "updatecontrols();", items_center);
  const std::size_t items_menu = compact_combatchoice.find(
      "SetMenuBar(myMenuBar);", items_controls);
  const std::size_t items_sound_menu = compact_combatchoice.find(
      "InsertMenu(gSound,-1);", items_menu);
  const std::size_t items_speed_menu = compact_combatchoice.find(
      "InsertMenu(gSpeed,-1);", items_sound_menu);
  const std::size_t items_draw_menu = compact_combatchoice.find(
      "DrawMenuBar();", items_speed_menu);
  const std::size_t items_flush = compact_combatchoice.find(
      "FlushEvents(everyEvent,0);", items_draw_menu);
  require(items_block_begin != std::string::npos &&
          items_block_end != std::string::npos &&
          items_skipload != std::string::npos &&
          items_sound != std::string::npos &&
          items_bounds != std::string::npos &&
          items_feedback != std::string::npos &&
          items_in != std::string::npos &&
          items_modal != std::string::npos &&
          items_out != std::string::npos &&
          items_combat_update != std::string::npos &&
          items_center != std::string::npos &&
          items_controls != std::string::npos &&
          items_menu != std::string::npos &&
          items_sound_menu != std::string::npos &&
          items_speed_menu != std::string::npos &&
          items_draw_menu != std::string::npos &&
          items_flush != std::string::npos,
      "combatchoice must retain the bounded Classic Items modal handoff, "
      "combat redraw, menu restoration, and event flush");
  require(items_block_begin < items_skipload &&
          items_skipload < items_sound && items_sound < items_bounds &&
          items_bounds < items_feedback && items_feedback < items_in &&
          items_in < items_modal && items_modal < items_out &&
          items_out < items_combat_update &&
          items_combat_update < items_center &&
          items_center < items_controls && items_controls < items_menu &&
          items_menu < items_sound_menu &&
          items_sound_menu < items_speed_menu &&
          items_speed_menu < items_draw_menu &&
          items_draw_menu < items_flush && items_flush < items_block_end,
      "combatchoice Items must enter and leave the modal before redraw, menu "
      "restoration, and queue flush");
  const std::string items_command_block = compact_combatchoice.substr(
      items_block_begin, items_block_end - items_block_begin);
  require(count_identifier(items_command_block, "GetNextEvent") == 0 &&
          count_identifier(items_command_block, "WaitNextEvent") == 0,
      "bounded combatchoice Items handling must leave modal input to items()");

  const std::size_t auto_block_begin = compact_combatchoice.find(
      "if(theControl==campbut){");
  const std::size_t auto_block_end = compact_combatchoice.find(
      "if((theControl==viewspellsbut)&&", auto_block_begin);
  const std::size_t auto_bounds = compact_combatchoice.find(
      "GetControlBounds(campbut,&r);", auto_block_begin);
  const std::size_t auto_feedback = compact_combatchoice.find(
      "ploticon3(129,r);", auto_bounds);
  const std::size_t auto_sound = compact_combatchoice.find(
      "sound(141);", auto_feedback);
  const std::size_t auto_window_guard = compact_combatchoice.find(
      "if(!inwindow(charup)){", auto_sound);
  const std::size_t auto_spell_guard = compact_combatchoice.find(
      "if(inspell)", auto_window_guard);
  const std::size_t auto_spell_center = compact_combatchoice.find(
      "gotocentercharupspell;", auto_spell_guard);
  const std::size_t auto_field_center = compact_combatchoice.find(
      "centerfield(pos[charup][0],pos[charup][1]);", auto_spell_center);
  const std::size_t auto_condition_guard = compact_combatchoice.find(
      "if(!c[charup].condition[COND_ANIMATED])", auto_field_center);
  const std::size_t auto_condition_mutation = compact_combatchoice.find(
      "c[charup].condition[COND_ANIMATED]=TRUE;", auto_condition_guard);
  const std::size_t auto_flush = compact_combatchoice.find(
      "FlushEvents(everyEvent,0);", auto_condition_mutation);
  require(auto_block_begin != std::string::npos &&
          auto_block_end != std::string::npos &&
          auto_bounds != std::string::npos &&
          auto_feedback != std::string::npos &&
          auto_sound != std::string::npos &&
          auto_window_guard != std::string::npos &&
          auto_spell_guard != std::string::npos &&
          auto_spell_center != std::string::npos &&
          auto_field_center != std::string::npos &&
          auto_condition_guard != std::string::npos &&
          auto_condition_mutation != std::string::npos &&
          auto_flush != std::string::npos,
      "combatchoice must retain the bounded Classic Auto feedback, centering, "
      "animated-state handoff, and event flush");
  require(auto_block_begin < auto_bounds &&
          auto_bounds < auto_feedback && auto_feedback < auto_sound &&
          auto_sound < auto_window_guard &&
          auto_window_guard < auto_spell_guard &&
          auto_spell_guard < auto_spell_center &&
          auto_spell_center < auto_field_center &&
          auto_field_center < auto_condition_guard &&
          auto_condition_guard < auto_condition_mutation &&
          auto_condition_mutation < auto_flush &&
          auto_flush < auto_block_end,
      "combatchoice Auto must preserve its Classic-owned ordered handoff");
  const std::string auto_command_block = compact_combatchoice.substr(
      auto_block_begin, auto_block_end - auto_block_begin);
  require(count_identifier(auto_command_block, "GetNextEvent") == 0 &&
          count_identifier(auto_command_block, "WaitNextEvent") == 0 &&
          count_identifier(auto_command_block, "Rand") == 0,
      "bounded combatchoice Auto handoff must not own a nested event loop or "
      "make a random decision in this branch");

  const std::string compact_items = without_whitespace(items);
  const std::size_t modal_flag = compact_items.find("initems=TRUE;");
  const std::size_t raw_modal_poll = compact_items.find(
      "GetNextEvent(everyEvent,&gTheEvent);", modal_flag);
  const std::size_t modal_flag_clear = compact_items.find(
      "initems=FALSE;", raw_modal_poll);
  require(modal_flag != std::string::npos &&
          raw_modal_poll != std::string::npos &&
          modal_flag_clear != std::string::npos,
      "items must retain its Classic modal flag, raw event poll, and exit "
      "flag clear");
  require(modal_flag < raw_modal_poll && raw_modal_poll < modal_flag_clear,
      "items must enter full-frame modal state before its raw event loop and "
      "clear that state only on an exit path");
  require(count_identifier(items, "GetNextEvent") == 1 &&
          count_identifier(items, "GetNextSemanticGameplayEvent") == 0 &&
          count_identifier(items, "WaitNextEvent") == 0,
      "items must remain a Classic-owned raw modal without a semantic input "
      "surface or nested WaitNextEvent route");
  require_no_semantic_scope_or_consumer(items, "items");

  const std::string classify_context = function_body(
      presentation_context_source,
      "RealmzClassifyLegacyPresentationContext");
  const std::string compact_classify = without_whitespace(classify_context);
  const std::size_t nested_inventory = compact_classify.find(
      "if(signals.in_items||signals.in_swap||signals.in_booty){");
  const std::size_t inventory_full_frame = compact_classify.find(
      "returnmake_context(REALMZ_LEGACY_SCREEN_INVENTORY,1,0);",
      nested_inventory);
  const std::size_t combat_context = compact_classify.find(
      "if(signals.in_combat)", inventory_full_frame);
  require(nested_inventory != std::string::npos &&
          inventory_full_frame != std::string::npos &&
          combat_context != std::string::npos &&
          nested_inventory < inventory_full_frame &&
          inventory_full_frame < combat_context,
      "legacy presentation classification must make nested Items a "
      "non-adaptive inventory context before considering combat beneath it");
  const std::string make_context_body = function_body(
      presentation_context_source, "make_context");
  require(without_whitespace(make_context_body).contains(
              "result.requires_full_frame=result.adaptive_eligible?0:1;"),
      "legacy presentation context must derive full-frame ownership from "
      "failed adaptive eligibility");

  const std::size_t melee_block_begin = compact_combatchoice.find(
      "if(theControl==melee){");
  const std::size_t monster_block_begin = compact_combatchoice.find(
      "if(theControl==monsterbut){", melee_block_begin);
  const std::size_t melee_bounds = compact_combatchoice.find(
      "GetControlBounds(melee,&buttonrect);", melee_block_begin);
  const std::size_t melee_button_down = compact_combatchoice.find(
      "downbutton(TRUE);", melee_bounds);
  const std::size_t melee_sound = compact_combatchoice.find(
      "sound(141);", melee_button_down);
  const std::size_t toggle_snapshot = compact_combatchoice.find(
      "temp=c[charup].toggle;", melee_sound);
  const std::size_t primary_slot = compact_combatchoice.find(
      "if(!c[charup].armor[2])", toggle_snapshot);
  const std::size_t default_weapon_sound = compact_combatchoice.find(
      "c[charup].weaponsound=(30+c[charup].gender*8);", primary_slot);
  const std::size_t primary_load = compact_combatchoice.find(
      "loaditem(c[charup].armor[2]);", default_weapon_sound);
  const std::size_t loaded_weapon_sound = compact_combatchoice.find(
      "c[charup].weaponsound=item.sound;", primary_load);
  const std::size_t primary_toggle = compact_combatchoice.find(
      "c[charup].toggle=0;", loaded_weapon_sound);
  const std::size_t secondary_slot = compact_combatchoice.find(
      "if(c[charup].armor[15])", primary_toggle);
  const std::size_t secondary_toggle = compact_combatchoice.find(
      "c[charup].toggle=1;", secondary_slot);
  const std::size_t toggle_changed = compact_combatchoice.find(
      "if(temp!=c[charup].toggle)", secondary_toggle);
  const std::size_t toggle_refresh = compact_combatchoice.find(
      "combatupdate2(charup);", toggle_changed);
  const std::size_t toggle_failure = compact_combatchoice.find(
      "sound(6000);", toggle_refresh);
  require(melee_block_begin != std::string::npos &&
          monster_block_begin != std::string::npos &&
          melee_bounds != std::string::npos &&
          melee_button_down != std::string::npos &&
          melee_sound != std::string::npos &&
          toggle_snapshot != std::string::npos &&
          primary_slot != std::string::npos &&
          default_weapon_sound != std::string::npos &&
          primary_load != std::string::npos &&
          loaded_weapon_sound != std::string::npos &&
          primary_toggle != std::string::npos &&
          secondary_slot != std::string::npos &&
          secondary_toggle != std::string::npos &&
          toggle_changed != std::string::npos &&
          toggle_refresh != std::string::npos &&
          toggle_failure != std::string::npos,
      "combatchoice must retain its bounded live Weapon toggle and failure "
      "block");
  require(melee_block_begin < melee_bounds &&
          melee_bounds < melee_button_down &&
          melee_button_down < melee_sound &&
          melee_sound < toggle_snapshot &&
          toggle_snapshot < primary_slot &&
          primary_slot < default_weapon_sound &&
          default_weapon_sound < primary_load &&
          primary_load < loaded_weapon_sound &&
          loaded_weapon_sound < primary_toggle &&
          primary_toggle < secondary_slot &&
          secondary_slot < secondary_toggle &&
          secondary_toggle < toggle_changed &&
          toggle_changed < toggle_refresh &&
          toggle_refresh < toggle_failure &&
          toggle_failure < monster_block_begin,
      "combatchoice Weapon source must keep the relative toggle, refresh, and "
      "fallback order inside its melee block");
  const std::string melee_block = compact_combatchoice.substr(
      melee_block_begin, monster_block_begin - melee_block_begin);
  require(count_identifier(melee_block, "WaitNextEvent") == 0 &&
          count_identifier(melee_block, "getup") == 0,
      "bounded combatchoice Weapon handling must not own a nested event loop "
      "or direct turn advance");
  const std::size_t delay_case = compact_combat.find("case'd':");
  const std::size_t delay_next_case = compact_combat.find(
      "case'm':", delay_case);
  const std::size_t delay_predicate = compact_combat.find(
      "if(c[charup].movement==c[charup].movementmax){", delay_case);
  const std::size_t delay_attack_deduction = compact_combat.find(
      "c[charup].attacks-=(c[charup].normattacks+"
      "c[charup].attackbonus);",
      delay_predicate);
  const std::size_t delay_queue_rotation = compact_combat.find(
      "q[ttt]=q[ttt+1];", delay_attack_deduction);
  const std::size_t delay_rewind = compact_combat.find(
      "up--;", delay_queue_rotation);
  const std::size_t delay_turn_advance = compact_combat.find(
      "getup(TRUE);", delay_rewind);
  const std::size_t delay_warning = compact_combat.find(
      "warn(59);", delay_turn_advance);
  require(delay_case != std::string::npos &&
          delay_next_case != std::string::npos &&
          delay_predicate != std::string::npos &&
          delay_attack_deduction != std::string::npos &&
          delay_queue_rotation != std::string::npos &&
          delay_rewind != std::string::npos &&
          delay_turn_advance != std::string::npos &&
          delay_warning != std::string::npos,
      "combat must retain the preserved Delay predicate, queue rotation, "
      "turn advance, and moved-actor warning");
  require(delay_case < delay_predicate &&
          delay_predicate < delay_attack_deduction &&
          delay_attack_deduction < delay_queue_rotation &&
          delay_queue_rotation < delay_rewind &&
          delay_rewind < delay_turn_advance &&
          delay_turn_advance < delay_warning &&
          delay_warning < delay_next_case,
      "combat Delay must remain a full-movement-only queue rotation before "
      "the next Classic command branch");

  std::size_t global_wrapper_count = 0;
  std::size_t global_begin_count = 0;
  std::size_t global_end_count = 0;
  std::size_t global_consumer_count = 0;
  std::size_t global_selection_consumer_count = 0;
  std::size_t global_inventory_consumer_count = 0;
  std::size_t global_spellbook_consumer_count = 0;
  std::size_t global_save_consumer_count = 0;
  std::size_t global_load_consumer_count = 0;
  std::size_t global_guard_consumer_count = 0;
  std::size_t global_finish_consumer_count = 0;
  std::size_t global_delay_consumer_count = 0;
  std::size_t global_center_consumer_count = 0;
  std::size_t global_switch_consumer_count = 0;
  std::size_t global_cycle_focus_consumer_count = 0;
  std::size_t global_combat_items_consumer_count = 0;
  std::size_t global_auto_consumer_count = 0;
  std::size_t global_range_consumer_count = 0;
  std::size_t global_bandage_consumer_count = 0;
  std::size_t global_undo_consumer_count = 0;
  std::size_t global_combat_spellbook_consumer_count = 0;
  std::size_t global_combat_targeting_consumer_count = 0;
  std::size_t global_escape_consumer_count = 0;
  std::size_t global_scroll_case_consumer_count = 0;
  std::size_t global_center_cursor_consumer_count = 0;
  std::size_t global_selection_apply_count = 0;
  std::vector<fs::path> c_sources;
  for (const auto& entry : fs::recursive_directory_iterator(legacy_root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".c") {
      c_sources.emplace_back(entry.path());
    }
  }
  std::ranges::sort(c_sources);
  for (const auto& path : c_sources) {
    const std::string source = code_only(read_file(path));
    global_wrapper_count += count_identifier(
        source, "GetNextSemanticGameplayEvent");
    global_begin_count += count_identifier(
        source, "RealmzBeginSemanticInputSurface");
    global_end_count += count_identifier(
        source, "RealmzEndSemanticInputSurface");
    global_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticMovementEvent");
    global_selection_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticPartySelectionEvent");
    global_inventory_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenInventoryEvent");
    global_spellbook_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenSpellbookEvent");
    global_save_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenSaveGameEvent");
    global_load_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenLoadGameEvent");
    global_guard_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticGuardCombatantEvent");
    global_finish_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticFinishCombatantEvent");
    global_delay_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticDelayCombatantEvent");
    global_center_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticCenterActiveCombatantEvent");
    global_switch_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticSwitchWeaponEvent");
    global_cycle_focus_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticCycleCombatFocusEvent");
    global_combat_items_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatItemsEvent");
    global_auto_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticAutoCombatantEvent");
    global_range_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticShowCombatRangeEvent");
    global_bandage_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticBandageCombatantEvent");
    global_undo_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticUndoCombatantEvent");
    global_combat_spellbook_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatSpellbookEvent");
    global_combat_targeting_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatTargetingEvent");
    global_escape_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticEscapeCombatEvent");
    global_scroll_case_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatScrollCaseEvent");
    global_center_cursor_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticCenterCombatCursorEvent");
    global_selection_apply_count += count_identifier(
        source, "RealmzApplyPartyMemberSelection");
  }
  require(global_wrapper_count == 3,
      "only mainscreen, threed, and combat may call the semantic gameplay "
      "wrapper");
  require(global_begin_count == 0,
      "legacy loops must not begin semantic input scopes directly");
  require(global_end_count == 0,
      "legacy loops must not end semantic input scopes directly");
  require(global_consumer_count == 0,
      "legacy loops must not consume tagged semantic movement directly");
  require(global_selection_consumer_count == 0,
      "legacy loops must not consume tagged semantic selection directly");
  require(global_inventory_consumer_count == 0,
      "legacy loops must not consume tagged semantic inventory directly");
  require(global_spellbook_consumer_count == 0,
      "legacy loops must not consume tagged semantic spellbook input directly");
  require(global_save_consumer_count == 0,
      "legacy loops must not consume tagged semantic save input directly");
  require(global_load_consumer_count == 0,
      "legacy loops must not consume tagged semantic load input directly");
  require(global_guard_consumer_count == 0,
      "legacy loops must not consume tagged semantic guard input directly");
  require(global_finish_consumer_count == 0,
      "legacy loops must not consume tagged semantic finish input directly");
  require(global_delay_consumer_count == 0,
      "legacy loops must not consume tagged semantic delay input directly");
  require(global_center_consumer_count == 0,
      "legacy loops must not consume tagged semantic center input directly");
  require(global_switch_consumer_count == 0,
      "legacy loops must not consume tagged semantic switch-weapon input "
      "directly");
  require(global_cycle_focus_consumer_count == 0,
      "legacy loops must not consume tagged semantic cycle-focus input "
      "directly");
  require(global_combat_items_consumer_count == 0,
      "legacy loops must not consume tagged semantic combat-items input "
      "directly");
  require(global_auto_consumer_count == 0,
      "legacy loops must not consume tagged semantic Auto input directly");
  require(global_range_consumer_count == 0,
      "legacy loops must not consume tagged semantic Range input directly");
  require(global_bandage_consumer_count == 0,
      "legacy loops must not consume tagged semantic Bandage input directly");
  require(global_undo_consumer_count == 0,
      "legacy loops must not consume tagged semantic Undo input directly");
  require(global_combat_spellbook_consumer_count == 0,
      "legacy loops must not consume tagged semantic combat-spellbook input "
      "directly");
  require(global_combat_targeting_consumer_count == 0,
      "legacy loops must not consume tagged semantic combat-targeting input "
      "directly");
  require(global_escape_consumer_count == 0,
      "legacy loops must not consume tagged semantic Escape input directly");
  require(global_scroll_case_consumer_count == 0,
      "legacy loops must not consume tagged semantic scroll-case input "
      "directly");
  require(global_center_cursor_consumer_count == 0,
      "legacy loops must not consume tagged semantic center-cursor input "
      "directly");
  require(global_selection_apply_count == 0,
      "legacy loops must not apply semantic selection directly");

  const std::string getchoice = function_body(getchoice_source, "getchoice");
  const std::string compact_getchoice = without_whitespace(getchoice);
  require(count_identifier(getchoice, "WaitNextEvent") == 1,
      "getchoice must retain its nested WaitNextEvent loop");
  require(count_identifier(getchoice, "GetNextEvent") == 0,
      "getchoice must not substitute a top-level GetNextEvent");
  require(compact_getchoice.contains(
              "WaitNextEvent(everyEvent,&gTheEvent,0L,0L);"),
      "getchoice must retain the exact raw Classic WaitNextEvent target poll");
  require(count_identifier(getchoice, "app1Evt") == 0 &&
          count_identifier(getchoice, "RealmzIsSemanticGameplayTag") == 0,
      "getchoice must remain shell-inert and leave inactive-surface tagged "
      "event filtering to EventManager's raw dequeue path");
  require_no_semantic_scope_or_consumer(getchoice, "getchoice");

  const std::string updatemain = function_body(misc, "updatemain");
  require(count_identifier(updatemain, "GetNextEvent") == 1,
      "updatemain must retain exactly one unguarded GetNextEvent");
  require(count_identifier(updatemain, "WaitNextEvent") == 0,
      "updatemain must not add a guarded WaitNextEvent route");
  require_no_semantic_scope_or_consumer(updatemain, "updatemain");
}

void verify_production_call_ownership(const fs::path& repository_root) {
  const fs::path source_root = repository_root / "src";
  std::size_t wrapper_calls = 0;
  std::size_t begin_calls = 0;
  std::size_t end_calls = 0;
  std::size_t consume_calls = 0;
  std::size_t selection_consume_calls = 0;
  std::size_t inventory_consume_calls = 0;
  std::size_t spellbook_consume_calls = 0;
  std::size_t save_consume_calls = 0;
  std::size_t load_consume_calls = 0;
  std::size_t guard_consume_calls = 0;
  std::size_t finish_consume_calls = 0;
  std::size_t delay_consume_calls = 0;
  std::size_t center_consume_calls = 0;
  std::size_t switch_consume_calls = 0;
  std::size_t cycle_focus_consume_calls = 0;
  std::size_t combat_items_consume_calls = 0;
  std::size_t auto_consume_calls = 0;
  std::size_t range_consume_calls = 0;
  std::size_t bandage_consume_calls = 0;
  std::size_t undo_consume_calls = 0;
  std::size_t combat_spellbook_consume_calls = 0;
  std::size_t combat_targeting_consume_calls = 0;
  std::size_t escape_consume_calls = 0;
  std::size_t scroll_case_consume_calls = 0;
  std::size_t center_cursor_consume_calls = 0;
  std::vector<fs::path> wrapper_callers;

  for (const auto& entry : fs::recursive_directory_iterator(source_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension();
    if (extension != ".c" && extension != ".cpp" && extension != ".mm") {
      continue;
    }
    const fs::path relative = fs::relative(entry.path(), source_root);
    if (!relative.empty() && *relative.begin() == "tests") {
      continue;
    }
    if (relative == fs::path("EventManager.cpp") ||
        relative == fs::path("presentation/SemanticInputBoundary.cpp")) {
      continue;
    }

    const std::string source = code_only(read_file(entry.path()));
    const std::size_t file_wrapper_calls = count_identifier(
        source, "GetNextSemanticGameplayEvent");
    wrapper_calls += file_wrapper_calls;
    begin_calls += count_identifier(
        source, "RealmzBeginSemanticInputSurface");
    end_calls += count_identifier(
        source, "RealmzEndSemanticInputSurface");
    consume_calls += count_identifier(
        source, "RealmzConsumeSemanticMovementEvent");
    selection_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticPartySelectionEvent");
    inventory_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenInventoryEvent");
    spellbook_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenSpellbookEvent");
    save_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenSaveGameEvent");
    load_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenLoadGameEvent");
    guard_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticGuardCombatantEvent");
    finish_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticFinishCombatantEvent");
    delay_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticDelayCombatantEvent");
    center_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticCenterActiveCombatantEvent");
    switch_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticSwitchWeaponEvent");
    cycle_focus_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticCycleCombatFocusEvent");
    combat_items_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatItemsEvent");
    auto_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticAutoCombatantEvent");
    range_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticShowCombatRangeEvent");
    bandage_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticBandageCombatantEvent");
    undo_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticUndoCombatantEvent");
    combat_spellbook_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatSpellbookEvent");
    combat_targeting_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatTargetingEvent");
    escape_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticEscapeCombatEvent");
    scroll_case_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenCombatScrollCaseEvent");
    center_cursor_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticCenterCombatCursorEvent");
    if (file_wrapper_calls != 0) {
      wrapper_callers.emplace_back(relative);
    }
  }

  std::ranges::sort(wrapper_callers);
  require(wrapper_calls == 3,
      "exactly three production call sites may use the semantic gameplay "
      "wrapper");
  require(wrapper_callers == std::vector<fs::path>{
              fs::path("realmz_orig/combat.c"),
              fs::path("realmz_orig/misc.c"),
              fs::path("realmz_orig/threed.c"),
          },
      "only combat.c, misc.c mainscreen, and threed.c may call the semantic "
      "wrapper");
  require(begin_calls == 0,
      "only EventManager may call RealmzBeginSemanticInputSurface");
  require(end_calls == 0,
      "only EventManager may call RealmzEndSemanticInputSurface");
  require(consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticMovementEvent");
  require(selection_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticPartySelectionEvent");
  require(inventory_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenInventoryEvent");
  require(spellbook_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenSpellbookEvent");
  require(save_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenSaveGameEvent");
  require(load_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenLoadGameEvent");
  require(guard_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticGuardCombatantEvent");
  require(finish_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticFinishCombatantEvent");
  require(delay_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticDelayCombatantEvent");
  require(center_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticCenterActiveCombatantEvent");
  require(switch_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticSwitchWeaponEvent");
  require(cycle_focus_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticCycleCombatFocusEvent");
  require(combat_items_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticOpenCombatItemsEvent");
  require(auto_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticAutoCombatantEvent");
  require(range_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticShowCombatRangeEvent");
  require(bandage_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticBandageCombatantEvent");
  require(undo_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticUndoCombatantEvent");
  require(combat_spellbook_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticOpenCombatSpellbookEvent");
  require(combat_targeting_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticOpenCombatTargetingEvent");
  require(escape_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticEscapeCombatEvent");
  require(scroll_case_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticOpenCombatScrollCaseEvent");
  require(center_cursor_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticCenterCombatCursorEvent");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: SemanticTopLevelLoopContractTest <repository-root>");
    }
    const fs::path repository_root = fs::weakly_canonical(fs::path(argv[1]));
    require(fs::is_directory(repository_root),
        "repository root argument is not a directory");
    verify_event_manager(repository_root);
    verify_party_selection_adapter(repository_root);
    verify_center_combat_cursor_contract(repository_root);
    verify_legacy_loop_ownership(repository_root);
    verify_production_call_ownership(repository_root);
    verify_window_manager_named_combat_sinks(repository_root);
    verify_window_manager_shell_dispatch_freshness(repository_root);
    verify_mode_switch_cancellation(repository_root);
    std::cout << "SemanticTopLevelLoopContractTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticTopLevelLoopContractTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
