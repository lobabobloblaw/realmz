#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "presentation/UIAction.hpp"

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

[[nodiscard]] std::size_t count_text(
    std::string_view source,
    std::string_view needle) noexcept {
  if (needle.empty()) {
    return 0;
  }
  std::size_t count = 0;
  for (std::size_t position = source.find(needle);
       position != std::string_view::npos;
       position = source.find(needle, position + needle.size())) {
    ++count;
  }
  return count;
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
    bool consumed_qualifier = true;
    while (consumed_qualifier) {
      consumed_qualifier = false;
      for (const auto qualifier : {
               std::string_view("const"), std::string_view("volatile"),
               std::string_view("override"), std::string_view("final")}) {
        if (find_identifier(stripped_source, qualifier, position) == position) {
          position = skip_whitespace(
              stripped_source, position + qualifier.size());
          consumed_qualifier = true;
          break;
        }
      }
      if (consumed_qualifier) {
        continue;
      }
      if (find_identifier(stripped_source, "noexcept", position) == position) {
        position = skip_whitespace(stripped_source,
            position + std::string_view("noexcept").size());
        if (position < stripped_source.size() &&
            stripped_source[position] == '(') {
          position = skip_whitespace(stripped_source,
              matching_delimiter(stripped_source, position, '(', ')') + 1);
        }
        consumed_qualifier = true;
      } else if (position < stripped_source.size() &&
          stripped_source[position] == '&') {
        ++position;
        if (position < stripped_source.size() &&
            stripped_source[position] == '&') {
          ++position;
        }
        position = skip_whitespace(stripped_source, position);
        consumed_qualifier = true;
      }
    }
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
              body, "RealmzConsumeSemanticOpenCharacterSheetEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Character Sheet input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenInventoryEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic inventory");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSpellbookEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic spellbook input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenScrollCaseEvent") == 0,
      std::string(function_name) +
          " must not consume tagged non-combat scroll-case input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenSaveGameEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic save input");
  require(count_identifier(
              body, "RealmzConsumeSemanticOpenLoadGameEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic load input");
  require(count_identifier(
              body, "RealmzConsumeSemanticRestPartyEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Rest input");
  require(count_identifier(
              body, "RealmzConsumeSemanticSetCampStateEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Camp input");
  require(count_identifier(
              body, "RealmzConsumeSemanticSetSearchStateEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Search input");
  require(count_identifier(
              body, "RealmzConsumeSemanticUseTorchEvent") == 0,
      std::string(function_name) +
          " must not consume tagged semantic Torch input");
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
  const std::string cached_mouse_query = function_body(
      source, "is_mouse_button_down_without_event_pump");
  const std::string compact_cached_mouse_query =
      without_whitespace(cached_mouse_query);
  require(compact_cached_mouse_query.contains(
              "constSDL_MouseButtonFlagssdl_mouse_buttons="
              "SDL_GetMouseState(nullptr,nullptr);") &&
          compact_cached_mouse_query.contains(
              "return!(this->modifier_flags&EVMOD_MOUSE_BUTTON_UP)||"
              "((sdl_mouse_buttons&SDL_BUTTON_LMASK)!=0);") &&
          count_identifier(cached_mouse_query, "SDL_GetMouseState") == 1 &&
          count_identifier(cached_mouse_query, "enqueue_pending_events") == 0 &&
          count_identifier(cached_mouse_query, "SDL_PollEvent") == 0 &&
          count_identifier(cached_mouse_query, "WindowManager") == 0,
      "semantic delivery mouse-state query must combine cached SDL and "
      "Classic state without pumping or recursively dispatching input");
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

  const std::string push_character_sheet = function_body(
      source, "push_semantic_open_character_sheet_event");
  const std::string compact_push_character_sheet =
      without_whitespace(push_character_sheet);
  require(count_identifier(push_character_sheet,
              "RealmzIsSemanticOpenCharacterSheetTag") == 1,
      "semantic Character Sheet enqueue must validate exactly one tag");
  require(count_identifier(push_character_sheet, "app1Evt") == 1,
      "semantic Character Sheet enqueue must use app1Evt exactly once");
  require(count_identifier(push_character_sheet, "keyDown") == 0 &&
          count_identifier(push_character_sheet, "mouseDown") == 0 &&
          count_identifier(push_character_sheet, "FindControl") == 0 &&
          count_identifier(push_character_sheet, "viewcharacter") == 0,
      "semantic Character Sheet enqueue must not synthesize Classic input or "
      "enter the character modal");
  require(compact_push_character_sheet.contains("ev.what=app1Evt;") &&
          compact_push_character_sheet.contains(
              "ev.message=tagged_message;"),
      "semantic Character Sheet must retain its tagged app1Evt payload");

  const std::string character_sheet_wrapper = function_body(
      source, "PushSemanticOpenCharacterSheetEvent");
  require(without_whitespace(character_sheet_wrapper).contains(
              "returnem.push_semantic_open_character_sheet_event("
              "tagged_message);"),
      "public Character Sheet enqueue must delegate to its tagged queue");
  require(count_identifier(character_sheet_wrapper, "keyDown") == 0 &&
          count_identifier(character_sheet_wrapper, "mouseDown") == 0,
      "public Character Sheet enqueue must not synthesize Classic input");

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

  const std::string push_world_scroll_case = function_body(
      source, "push_semantic_open_scroll_case_event");
  const std::string compact_push_world_scroll_case =
      without_whitespace(push_world_scroll_case);
  require(count_identifier(push_world_scroll_case,
              "RealmzIsSemanticOpenScrollCaseTag") == 1,
      "semantic non-combat scroll-case enqueue must validate exactly one tag");
  require(count_identifier(push_world_scroll_case, "app1Evt") == 1,
      "semantic non-combat scroll-case enqueue must use app1Evt exactly once");
  require(count_identifier(push_world_scroll_case, "keyDown") == 0 &&
          count_identifier(push_world_scroll_case, "mouseDown") == 0,
      "semantic non-combat scroll-case enqueue must not synthesize Classic input");
  require(compact_push_world_scroll_case.contains("ev.what=app1Evt;") &&
          compact_push_world_scroll_case.contains(
              "ev.message=tagged_message;"),
      "semantic non-combat scroll-case must retain its tagged app1Evt payload");

  const std::string world_scroll_case_wrapper = function_body(
      source, "PushSemanticOpenScrollCaseEvent");
  require(without_whitespace(world_scroll_case_wrapper).contains(
              "returnem.push_semantic_open_scroll_case_event("
              "tagged_message);"),
      "public non-combat scroll-case enqueue must delegate to its tagged queue");
  require(count_identifier(world_scroll_case_wrapper, "keyDown") == 0 &&
          count_identifier(world_scroll_case_wrapper, "mouseDown") == 0,
      "public non-combat scroll-case enqueue must not synthesize Classic input");

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

  const std::string push_rest = function_body(
      source, "push_semantic_rest_party_event");
  const std::string compact_push_rest = without_whitespace(push_rest);
  require(count_identifier(
              push_rest, "RealmzIsSemanticRestPartyTag") == 1,
      "semantic Rest enqueue must validate exactly one Rest tag");
  require(count_identifier(push_rest, "app1Evt") == 1,
      "semantic Rest enqueue must use app1Evt exactly once");
  require(count_identifier(push_rest, "keyDown") == 0 &&
          count_identifier(push_rest, "mouseDown") == 0 &&
          count_identifier(push_rest, "ShowCombatRangeAction") == 0,
      "semantic Rest enqueue must retain its world tag without synthesizing "
      "Classic input or sharing the combat Range route");
  require(compact_push_rest.contains("ev.what=app1Evt;") &&
          compact_push_rest.contains("ev.message=tagged_message;"),
      "semantic Rest must retain its tagged app1Evt payload");

  const std::string rest_wrapper = function_body(
      source, "PushSemanticRestPartyEvent");
  require(without_whitespace(rest_wrapper).contains(
              "returnem.push_semantic_rest_party_event(tagged_message);"),
      "public semantic Rest enqueue must delegate to its tagged queue");
  require(count_identifier(rest_wrapper, "keyDown") == 0 &&
          count_identifier(rest_wrapper, "mouseDown") == 0,
      "public semantic Rest enqueue must not synthesize Classic input");

  const std::string push_camp = function_body(
      source, "push_semantic_set_camp_state_event");
  const std::string compact_push_camp = without_whitespace(push_camp);
  require(count_identifier(
              push_camp, "RealmzIsSemanticSetCampStateTag") == 1,
      "semantic Camp enqueue must validate exactly one Camp tag");
  require(count_identifier(push_camp, "app1Evt") == 1,
      "semantic Camp enqueue must use app1Evt exactly once");
  require(count_identifier(push_camp, "keyDown") == 0 &&
          count_identifier(push_camp, "mouseDown") == 0 &&
          count_identifier(push_camp, "CenterActiveCombatantAction") == 0,
      "semantic Camp enqueue must retain its absolute world tag without "
      "synthesizing Classic input or sharing combat Center");
  require(compact_push_camp.contains("ev.what=app1Evt;") &&
          compact_push_camp.contains("ev.message=tagged_message;"),
      "semantic Camp must retain its tagged app1Evt payload");

  const std::string camp_wrapper = function_body(
      source, "PushSemanticSetCampStateEvent");
  require(without_whitespace(camp_wrapper).contains(
              "returnem.push_semantic_set_camp_state_event(tagged_message);"),
      "public semantic Camp enqueue must delegate to its tagged queue");
  require(count_identifier(camp_wrapper, "keyDown") == 0 &&
          count_identifier(camp_wrapper, "mouseDown") == 0,
      "public semantic Camp enqueue must not synthesize Classic input");

  const std::string push_search = function_body(
      source, "push_semantic_set_search_state_event");
  const std::string compact_push_search = without_whitespace(push_search);
  require(count_identifier(
              push_search, "RealmzIsSemanticSetSearchStateTag") == 1 &&
          count_identifier(push_search, "app1Evt") == 1,
      "semantic Search enqueue must validate one Search tag and retain it as "
      "one app1Evt");
  require(count_identifier(push_search, "keyDown") == 0 &&
          count_identifier(push_search, "mouseDown") == 0 &&
          compact_push_search.contains("ev.what=app1Evt;") &&
          compact_push_search.contains("ev.message=tagged_message;"),
      "semantic Search enqueue must preserve its absolute tagged payload "
      "without synthesizing Classic input");
  const std::string search_wrapper = function_body(
      source, "PushSemanticSetSearchStateEvent");
  require(without_whitespace(search_wrapper).contains(
              "returnem.push_semantic_set_search_state_event("
              "tagged_message);") &&
          count_identifier(search_wrapper, "keyDown") == 0 &&
          count_identifier(search_wrapper, "mouseDown") == 0,
      "public semantic Search enqueue must delegate only to its tagged queue");

  const std::string push_torch = function_body(
      source, "push_semantic_use_torch_event");
  const std::string compact_push_torch = without_whitespace(push_torch);
  require(count_identifier(
              push_torch, "RealmzIsSemanticUseTorchTag") == 1 &&
          count_identifier(push_torch, "app1Evt") == 1,
      "semantic Torch enqueue must validate one Torch tag and retain it as "
      "one app1Evt");
  require(count_identifier(push_torch, "keyDown") == 0 &&
          count_identifier(push_torch, "mouseDown") == 0 &&
          compact_push_torch.contains("ev.what=app1Evt;") &&
          compact_push_torch.contains("ev.message=tagged_message;"),
      "semantic Torch enqueue must preserve its source-tagged payload without "
      "synthesizing Classic input");
  const std::string torch_wrapper = function_body(
      source, "PushSemanticUseTorchEvent");
  require(without_whitespace(torch_wrapper).contains(
              "returnem.push_semantic_use_torch_event(tagged_message);") &&
          count_identifier(torch_wrapper, "keyDown") == 0 &&
          count_identifier(torch_wrapper, "mouseDown") == 0,
      "public semantic Torch enqueue must delegate only to its tagged queue");

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
              "RealmzConsumeSemanticOpenCharacterSheetEvent") == 1,
      "semantic gameplay wrapper must have one late Character Sheet consumer");
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
              "RealmzConsumeSemanticOpenScrollCaseEvent") == 1,
      "semantic gameplay wrapper must have one late non-combat scroll-case "
      "consumer");
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
              "RealmzConsumeSemanticRestPartyEvent") == 1,
      "semantic gameplay wrapper must have one late Rest consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticSetCampStateEvent") == 1,
      "semantic gameplay wrapper must have one late Camp consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticSetSearchStateEvent") == 1,
      "semantic gameplay wrapper must have one late Search consumer");
  require(count_identifier(
              semantic_wrapper,
              "RealmzConsumeSemanticUseTorchEvent") == 1,
      "semantic gameplay wrapper must have one late Torch consumer");
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
              semantic_wrapper, "RealmzApplyPartyMemberSelection") == 2,
      "semantic gameplay wrapper must use one narrow selection adapter in "
      "each replay route");
  require(count_identifier(semantic_wrapper, "get_next_event") == 1 &&
          count_identifier(semantic_wrapper, "get_next_semantic_event") == 1,
      "semantic gameplay wrapper must separate its Classic and scoped polls");
  require(count_identifier(semantic_wrapper, "app1Evt") == 28,
      "semantic gameplay wrapper must recognize all twenty-eight tagged paths");
  require(count_identifier(semantic_wrapper, "keyDown") == 25 &&
          count_text(compact_semantic, "ret->what=keyDown;") == 23 &&
          count_text(
              compact_semantic, ".kind=(ret->what==keyDown)") == 2,
      "only guarded Classic replay injection or late movement, inventory, "
      "spellbook, non-combat scroll-case, Rest, Camp, guard, finish, delay, "
      "center, "
      "switch-weapon, cycle-focus, "
      "combat-items, Auto, Range, Bandage, Undo, combat-spellbook, "
      "combat-targeting, Escape, scroll-case, or center-cursor validation may "
      "produce keyDown, followed by typed replay delivery observations");
  require(count_identifier(semantic_wrapper, "mouseDown") == 2,
      "only late save/load validation may produce menu mouseDown events");
  require(count_identifier(semantic_wrapper, "MenuSelect") == 0 &&
          count_identifier(semantic_wrapper, "HandleMenuChoice") == 0 &&
          count_identifier(semantic_wrapper, "FindControl") == 0 &&
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
              source, "RealmzConsumeSemanticOpenCharacterSheetEvent") == 1,
      "EventManager may consume Character Sheet input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenInventoryEvent") == 1,
      "EventManager may consume semantic inventory only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenSpellbookEvent") == 1,
      "EventManager may consume semantic spellbook input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenScrollCaseEvent") == 1,
      "EventManager may consume semantic non-combat scroll-case input only "
      "inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenSaveGameEvent") == 1,
      "EventManager may consume semantic save input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticOpenLoadGameEvent") == 1,
      "EventManager may consume semantic load input only inside its gameplay wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticRestPartyEvent") == 1,
      "EventManager may consume semantic Rest input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticSetCampStateEvent") == 1,
      "EventManager may consume semantic Camp input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticSetSearchStateEvent") == 1,
      "EventManager may consume semantic Search input only inside its gameplay "
      "wrapper");
  require(count_identifier(
              source, "RealmzConsumeSemanticUseTorchEvent") == 1,
      "EventManager may consume semantic Torch input only inside its gameplay "
      "wrapper");
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
  require(count_identifier(source, "RealmzApplyPartyMemberSelection") == 2,
      "EventManager may apply replay selection only inside its gameplay wrapper");

  const std::size_t replay_classic_route = compact_semantic.find(
      "replay->replay_route()==realmz::replay::ReplayRoute::classic");
  const std::size_t classic_selection_apply = compact_semantic.find(
      "RealmzApplyPartyMemberSelection(*replay_expected_party_member)",
      replay_classic_route);
  const std::size_t classic_selection_ack = compact_semantic.find(
      "replay->acknowledge_party_selection_delivery(",
      classic_selection_apply);
  const std::size_t classic_selection_null = compact_semantic.find(
      "ret->what=nullEvent", classic_selection_ack);
  const std::size_t classic_selection_return = compact_semantic.find(
      "returnfalse;", classic_selection_null);
  require(replay_classic_route != std::string::npos &&
          classic_selection_apply != std::string::npos &&
          classic_selection_ack != std::string::npos &&
          classic_selection_null != std::string::npos &&
          classic_selection_return != std::string::npos,
      "Classic replay selection is missing adapter, acknowledgement, or "
      "nullEvent delivery");
  require(replay_classic_route < classic_selection_apply &&
          classic_selection_apply < classic_selection_ack &&
          classic_selection_ack < classic_selection_null &&
          classic_selection_null < classic_selection_return,
      "Classic replay selection must apply before acknowledgement and return "
      "only nullEvent");

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
  const std::size_t semantic_selection_ack = compact_semantic.find(
      "replay->acknowledge_party_selection_delivery(",
      classic_selection_ack + 1U);
  require(semantic_selection_ack != std::string::npos &&
          selection_apply < semantic_selection_ack,
      "semantic replay selection must acknowledge only after late consume "
      "and adapter application");
  const std::size_t character_sheet_branch = compact_semantic.find(
      "RealmzIsSemanticOpenCharacterSheetTag(ret->message)",
      selection_message);
  const std::size_t character_sheet_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenCharacterSheetEvent(",
      character_sheet_branch);
  const std::size_t character_sheet_stage = compact_semantic.find(
      "stage_semantic_open_character_sheet_member(party_member);",
      character_sheet_consume);
  const std::size_t character_sheet_message = compact_semantic.find(
      "ret->message=0", character_sheet_stage);
  const std::size_t character_sheet_where = compact_semantic.find(
      "ret->where={};", character_sheet_message);
  const std::size_t character_sheet_modifiers = compact_semantic.find(
      "ret->modifiers=0", character_sheet_where);
  const std::size_t character_sheet_window = compact_semantic.find(
      "ret->window_port=nullptr", character_sheet_modifiers);
  const std::size_t character_sheet_rejected_null = compact_semantic.find(
      "ret->what=nullEvent", character_sheet_window);
  const std::size_t character_sheet_rejected_message = compact_semantic.find(
      "ret->message=0", character_sheet_rejected_null);
  const std::size_t inventory_branch = compact_semantic.find(
      "RealmzIsSemanticOpenInventoryTag(ret->message)",
      character_sheet_rejected_message);
  const std::size_t inventory_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenInventoryEvent(", inventory_branch);
  const std::size_t inventory_keydown = compact_semantic.find(
      "ret->what=keyDown", inventory_consume);
  const std::size_t inventory_null = compact_semantic.find(
      "ret->what=nullEvent", inventory_keydown);
  const std::size_t inventory_message = compact_semantic.find(
      "ret->message=0", inventory_null);
  require(character_sheet_branch != std::string::npos &&
          inventory_branch != std::string::npos &&
          character_sheet_branch < inventory_branch,
      "Character Sheet must remain a distinct late route before inventory");
  const std::size_t character_sheet_route_start = compact_semantic.rfind(
      "ret->what==app1Evt", character_sheet_branch);
  const std::size_t character_sheet_route_end = compact_semantic.rfind(
      "ret->what==app1Evt", inventory_branch);
  require(character_sheet_route_start != std::string::npos &&
          character_sheet_route_end != std::string::npos &&
          character_sheet_route_start < character_sheet_branch &&
          character_sheet_branch < character_sheet_route_end,
      "Character Sheet structural route must include its own app1Evt guard "
      "and exclude the following inventory guard");
  const std::string character_sheet_route = compact_semantic.substr(
      character_sheet_route_start,
      character_sheet_route_end - character_sheet_route_start);
  require(count_identifier(character_sheet_route, "app1Evt") == 1 &&
          count_identifier(character_sheet_route, "keyDown") == 0 &&
          count_identifier(character_sheet_route, "mouseDown") == 0 &&
          count_identifier(character_sheet_route, "FindControl") == 0 &&
          count_identifier(character_sheet_route, "viewcharacter") == 0 &&
          count_identifier(character_sheet_route, "buttonchoice") == 0 &&
          count_identifier(character_sheet_route, "charmainbut") == 0,
      "Character Sheet late validation must not forge a click, key, control, "
      "or direct modal call");
  require(count_text(character_sheet_route, "ret->what=nullEvent;") == 1 &&
          count_text(character_sheet_route, "ret->what=app1Evt;") == 0 &&
          character_sheet_route.contains(
              "stage_semantic_open_character_sheet_member(party_member);"
              "ret->message=0;ret->where={};ret->modifiers=0;"
              "ret->window_port=nullptr;"),
      "successful Character Sheet delivery must retain a neutral app1Evt and "
      "clear every transport field; only rejection may assign nullEvent");
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
  const std::size_t world_scroll_case_branch = compact_semantic.find(
      "RealmzIsSemanticOpenScrollCaseTag(ret->message)", spellbook_message);
  const std::size_t world_scroll_case_consume = compact_semantic.find(
      "RealmzConsumeSemanticOpenScrollCaseEvent(",
      world_scroll_case_branch);
  const std::size_t world_scroll_case_keydown = compact_semantic.find(
      "ret->what=keyDown", world_scroll_case_consume);
  const std::size_t world_scroll_case_null = compact_semantic.find(
      "ret->what=nullEvent", world_scroll_case_keydown);
  const std::size_t world_scroll_case_rejected_message = compact_semantic.find(
      "ret->message=0", world_scroll_case_null);
  const std::size_t save_branch = compact_semantic.find(
      "RealmzIsSemanticOpenSaveGameTag(ret->message)",
      world_scroll_case_rejected_message);
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
  const std::size_t rest_branch = compact_semantic.find(
      "RealmzIsSemanticRestPartyTag(ret->message)",
      load_rejected_message);
  const std::size_t rest_mouse_check = compact_semantic.find(
      "em.is_mouse_button_down_without_event_pump()", rest_branch);
  const std::size_t rest_mouse_gate = compact_semantic.find(
      "!mouse_button_held", rest_mouse_check);
  const std::size_t rest_consume = compact_semantic.find(
      "RealmzConsumeSemanticRestPartyEvent(", rest_mouse_gate);
  const std::size_t rest_keydown = compact_semantic.find(
      "ret->what=keyDown", rest_consume);
  const std::size_t rest_invalidate = compact_semantic.find(
      "RealmzInvalidateSemanticInputBoundary()", rest_keydown);
  const std::size_t rest_null = compact_semantic.find(
      "ret->what=nullEvent", rest_invalidate);
  const std::size_t rest_rejected_message = compact_semantic.find(
      "ret->message=0", rest_null);
  const std::size_t camp_branch = compact_semantic.find(
      "RealmzIsSemanticSetCampStateTag(ret->message)",
      rest_rejected_message);
  const std::size_t camp_consume = compact_semantic.find(
      "RealmzConsumeSemanticSetCampStateEvent(", camp_branch);
  const std::size_t camp_keydown = compact_semantic.find(
      "ret->what=keyDown", camp_consume);
  const std::size_t camp_null = compact_semantic.find(
      "ret->what=nullEvent", camp_keydown);
  const std::size_t camp_rejected_message = compact_semantic.find(
      "ret->message=0", camp_null);
  const std::size_t search_branch = compact_semantic.find(
      "RealmzIsSemanticSetSearchStateTag(ret->message)",
      camp_rejected_message);
  const std::size_t search_consume = compact_semantic.find(
      "RealmzConsumeSemanticSetSearchStateEvent(", search_branch);
  const std::size_t search_stage = compact_semantic.find(
      "stage_semantic_set_search_state_desired(desired_searching!=0);",
      search_consume);
  const std::size_t search_message = compact_semantic.find(
      "ret->message=0", search_stage);
  const std::size_t search_where = compact_semantic.find(
      "ret->where={};", search_message);
  const std::size_t search_modifiers = compact_semantic.find(
      "ret->modifiers=0", search_where);
  const std::size_t search_window = compact_semantic.find(
      "ret->window_port=nullptr", search_modifiers);
  const std::size_t search_null = compact_semantic.find(
      "ret->what=nullEvent", search_window);
  const std::size_t search_rejected_message = compact_semantic.find(
      "ret->message=0", search_null);
  const std::size_t torch_branch = compact_semantic.find(
      "RealmzIsSemanticUseTorchTag(ret->message)",
      search_rejected_message);
  const std::size_t torch_consume = compact_semantic.find(
      "RealmzConsumeSemanticUseTorchEvent(", torch_branch);
  const std::size_t torch_stage = compact_semantic.find(
      "stage_semantic_use_torch_source(member,slot);", torch_consume);
  const std::size_t torch_message = compact_semantic.find(
      "ret->message=0", torch_stage);
  const std::size_t torch_where = compact_semantic.find(
      "ret->where={};", torch_message);
  const std::size_t torch_modifiers = compact_semantic.find(
      "ret->modifiers=0", torch_where);
  const std::size_t torch_window = compact_semantic.find(
      "ret->window_port=nullptr", torch_modifiers);
  const std::size_t torch_null = compact_semantic.find(
      "ret->what=nullEvent", torch_window);
  const std::size_t torch_rejected_message = compact_semantic.find(
      "ret->message=0", torch_null);
  const std::size_t guard_branch = compact_semantic.find(
      "RealmzIsSemanticGuardCombatantTag(ret->message)",
      torch_rejected_message);
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
          character_sheet_branch != std::string::npos &&
          character_sheet_consume != std::string::npos &&
          character_sheet_stage != std::string::npos &&
          character_sheet_message != std::string::npos &&
          character_sheet_where != std::string::npos &&
          character_sheet_modifiers != std::string::npos &&
          character_sheet_window != std::string::npos &&
          character_sheet_rejected_null != std::string::npos &&
          character_sheet_rejected_message != std::string::npos &&
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
          world_scroll_case_branch != std::string::npos &&
          world_scroll_case_consume != std::string::npos &&
          world_scroll_case_keydown != std::string::npos &&
          world_scroll_case_null != std::string::npos &&
          world_scroll_case_rejected_message != std::string::npos &&
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
          rest_branch != std::string::npos &&
          rest_mouse_check != std::string::npos &&
          rest_mouse_gate != std::string::npos &&
          rest_consume != std::string::npos &&
          rest_keydown != std::string::npos &&
          rest_invalidate != std::string::npos &&
          rest_null != std::string::npos &&
          rest_rejected_message != std::string::npos &&
          camp_branch != std::string::npos &&
          camp_consume != std::string::npos &&
          camp_keydown != std::string::npos &&
          camp_null != std::string::npos &&
          camp_rejected_message != std::string::npos &&
          search_branch != std::string::npos &&
          search_consume != std::string::npos &&
          search_stage != std::string::npos &&
          search_message != std::string::npos &&
          search_where != std::string::npos &&
          search_modifiers != std::string::npos &&
          search_window != std::string::npos &&
          search_null != std::string::npos &&
          search_rejected_message != std::string::npos &&
          torch_branch != std::string::npos &&
          torch_consume != std::string::npos &&
          torch_stage != std::string::npos &&
          torch_message != std::string::npos &&
          torch_where != std::string::npos &&
          torch_modifiers != std::string::npos &&
          torch_window != std::string::npos &&
          torch_null != std::string::npos &&
          torch_rejected_message != std::string::npos &&
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
          selection_message < character_sheet_branch &&
          character_sheet_branch < character_sheet_consume &&
          character_sheet_consume < character_sheet_stage &&
          character_sheet_stage < character_sheet_message &&
          character_sheet_message < character_sheet_where &&
          character_sheet_where < character_sheet_modifiers &&
          character_sheet_modifiers < character_sheet_window &&
          character_sheet_window < character_sheet_rejected_null &&
          character_sheet_rejected_null <
              character_sheet_rejected_message &&
          character_sheet_rejected_message < inventory_branch &&
          inventory_branch < inventory_consume &&
          inventory_consume < inventory_keydown &&
          inventory_keydown < inventory_null &&
          inventory_null < inventory_message &&
          inventory_message < spellbook_branch &&
          spellbook_branch < spellbook_consume &&
          spellbook_consume < spellbook_keydown &&
          spellbook_keydown < spellbook_null &&
          spellbook_null < spellbook_message &&
          spellbook_message < world_scroll_case_branch &&
          world_scroll_case_branch < world_scroll_case_consume &&
          world_scroll_case_consume < world_scroll_case_keydown &&
          world_scroll_case_keydown < world_scroll_case_null &&
          world_scroll_case_null < world_scroll_case_rejected_message &&
          world_scroll_case_rejected_message < save_branch &&
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
          load_rejected_message < rest_branch &&
          rest_branch < rest_mouse_check &&
          rest_mouse_check < rest_mouse_gate &&
          rest_mouse_gate < rest_consume &&
          rest_consume < rest_keydown &&
          rest_keydown < rest_invalidate &&
          rest_invalidate < rest_null &&
          rest_null < rest_rejected_message &&
          rest_rejected_message < camp_branch &&
          camp_branch < camp_consume &&
          camp_consume < camp_keydown &&
          camp_keydown < camp_null &&
          camp_null < camp_rejected_message &&
          camp_rejected_message < search_branch &&
          search_branch < search_consume &&
          search_consume < search_stage &&
          search_stage < search_message &&
          search_message < search_where &&
          search_where < search_modifiers &&
          search_modifiers < search_window &&
          search_window < search_null &&
          search_null < search_rejected_message &&
          search_rejected_message < torch_branch &&
          torch_branch < torch_consume &&
          torch_consume < torch_stage &&
          torch_stage < torch_message &&
          torch_message < torch_where &&
          torch_where < torch_modifiers &&
          torch_modifiers < torch_window &&
          torch_window < torch_null &&
          torch_null < torch_rejected_message &&
          torch_rejected_message < guard_branch &&
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
  require(scope_block_close < rest_branch &&
          rest_branch < rest_mouse_check &&
          rest_mouse_check < rest_mouse_gate &&
          rest_mouse_gate < rest_consume &&
          rest_consume < rest_keydown && rest_keydown < rest_invalidate &&
          rest_invalidate < camp_branch,
      "Rest must leave semantic gameplay scope, reject a cached held mouse "
      "without pumping, then consume once before its lowercase-r handoff");
  require(scope_block_close < camp_branch &&
          camp_branch < camp_consume &&
          camp_consume < camp_keydown && camp_keydown < camp_null &&
          camp_null < search_branch,
      "Camp must leave semantic gameplay scope, revalidate its absolute "
      "desired state once, then produce only its lowercase-c handoff or an "
      "inert rejection before Search");
  require(scope_block_close < search_branch &&
          search_branch < search_consume && search_consume < search_stage &&
          search_stage < search_null && search_null < torch_branch,
      "Search must leave semantic gameplay scope, revalidate its absolute "
      "desired state, and stage only a neutral app1Evt one-shot before combat "
      "routes");
  require(scope_block_close < torch_branch &&
          torch_branch < torch_consume && torch_consume < torch_stage &&
          torch_stage < torch_null && torch_null < guard_branch,
      "Torch must leave semantic gameplay scope, revalidate its exact source, "
      "and stage only a neutral app1Evt one-shot before combat routes");
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
  require(scope_block_close < world_scroll_case_branch &&
          world_scroll_case_branch < world_scroll_case_consume &&
          world_scroll_case_consume < world_scroll_case_keydown,
      "Non-combat Use Scroll must leave semantic gameplay scope before its "
      "surface-specific lowercase l/p handoff, leaving the five-slot chooser "
      "and all scroll behavior in Classic");
  require(scope_block_close < character_sheet_branch &&
          character_sheet_branch < character_sheet_consume &&
          character_sheet_consume < character_sheet_stage,
      "Character Sheet must leave semantic gameplay scope before staging its "
      "one-shot selected-member handoff to the preserved world loop");
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
  require(count_identifier(get_next,
              "clear_pending_semantic_open_character_sheet_member") == 1 &&
          count_identifier(wait_next,
              "clear_pending_semantic_open_character_sheet_member") == 1 &&
          count_identifier(semantic_wrapper,
              "clear_pending_semantic_open_character_sheet_member") == 1,
      "every ordinary, raw, or semantic event poll must clear a stale staged "
      "Character Sheet member before dequeuing another event");
  require(without_whitespace(get_next).find(
              "clear_pending_semantic_open_character_sheet_member();") <
          without_whitespace(get_next).find("*ret=em.get_next_event(0);") &&
          compact_wait_next.find(
              "clear_pending_semantic_open_character_sheet_member();") <
          compact_wait_next.find("*ret=em.get_next_event(sleep);") &&
          compact_semantic.find(
              "clear_pending_semantic_open_character_sheet_member();") <
          compact_semantic.find("*ret=em.get_next_semantic_event(0);"),
      "Character Sheet one-shot state must clear before every event poll");
  require(count_identifier(
              get_next, "clear_pending_semantic_center_combat_cursor_cell") ==
          1 &&
          count_identifier(wait_next,
              "clear_pending_semantic_center_combat_cursor_cell") == 1 &&
          count_identifier(semantic_wrapper,
              "clear_pending_semantic_center_combat_cursor_cell") == 1,
      "every ordinary, raw, or semantic event poll must clear a stale staged "
      "center-cursor cell before dequeuing another event");
  require(count_identifier(
              get_next, "clear_pending_semantic_set_search_state_desired") ==
          1 &&
          count_identifier(wait_next,
              "clear_pending_semantic_set_search_state_desired") == 1 &&
          count_identifier(semantic_wrapper,
              "clear_pending_semantic_set_search_state_desired") == 1,
      "every ordinary, raw, or semantic event poll must clear a stale staged "
      "Search state before dequeuing another event");
  require(without_whitespace(get_next).find(
              "clear_pending_semantic_set_search_state_desired();") <
          without_whitespace(get_next).find("*ret=em.get_next_event(0);") &&
          compact_wait_next.find(
              "clear_pending_semantic_set_search_state_desired();") <
          compact_wait_next.find("*ret=em.get_next_event(sleep);") &&
          compact_semantic.find(
              "clear_pending_semantic_set_search_state_desired();") <
          compact_semantic.find("*ret=em.get_next_semantic_event(0);"),
      "Search one-shot state must clear before every event poll");
  require(count_identifier(
              get_next, "clear_pending_semantic_use_torch_source") == 1 &&
          count_identifier(
              wait_next, "clear_pending_semantic_use_torch_source") == 1 &&
          count_identifier(
              semantic_wrapper, "clear_pending_semantic_use_torch_source") == 1,
      "every ordinary, raw, or semantic event poll must clear a stale staged "
      "Torch source before dequeuing another event");
  require(without_whitespace(get_next).find(
              "clear_pending_semantic_use_torch_source();") <
          without_whitespace(get_next).find("*ret=em.get_next_event(0);") &&
          compact_wait_next.find(
              "clear_pending_semantic_use_torch_source();") <
          compact_wait_next.find("*ret=em.get_next_event(sleep);") &&
          compact_semantic.find(
              "clear_pending_semantic_use_torch_source();") <
          compact_semantic.find("*ret=em.get_next_semantic_event(0);"),
      "Torch one-shot source must clear before every event poll");
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
  require(count_identifier(cancel,
              "clear_pending_semantic_open_character_sheet_member") == 1,
      "semantic cancellation must clear a staged Character Sheet member once");
  require(count_identifier(cancel,
              "clear_pending_semantic_set_search_state_desired") == 1,
      "semantic cancellation must clear a staged Search state once");
  require(count_identifier(cancel,
              "clear_pending_semantic_use_torch_source") == 1,
      "semantic cancellation must clear a staged Torch source once");
  const std::string take_character_sheet = function_body(
      source, "TakeSemanticOpenCharacterSheetMember");
  const std::string compact_take_character_sheet =
      without_whitespace(take_character_sheet);
  require(count_identifier(take_character_sheet,
              "clear_pending_semantic_open_character_sheet_member") == 1 &&
          compact_take_character_sheet.contains(
              "constautopending="
              "pending_semantic_open_character_sheet_member;") &&
          compact_take_character_sheet.contains("if(!pending||!party_member)") &&
          compact_take_character_sheet.contains("*party_member=*pending;") &&
          compact_take_character_sheet.find(
              "clear_pending_semantic_open_character_sheet_member();") <
              compact_take_character_sheet.find(
                  "if(!pending||!party_member)"),
      "the Classic Character Sheet handoff must clear before null-output "
      "validation and return its staged member at most once");
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
  const std::string take_search = function_body(
      source, "TakeSemanticSetSearchStateDesired");
  const std::string compact_take_search = without_whitespace(take_search);
  require(count_identifier(take_search,
              "clear_pending_semantic_set_search_state_desired") == 1 &&
          compact_take_search.contains(
              "constautopending=pending_semantic_set_search_state_desired;") &&
          compact_take_search.contains(
              "if(!pending||!desired_searching)") &&
          compact_take_search.contains(
              "*desired_searching=*pending?1:0;") &&
          compact_take_search.find(
              "clear_pending_semantic_set_search_state_desired();") <
              compact_take_search.find("if(!pending||!desired_searching)"),
      "the Classic Search handoff must clear before null-output validation and "
      "return a strict absolute boolean at most once");
  const std::string take_torch = function_body(
      source, "TakeSemanticUseTorchSource");
  const std::string compact_take_torch = without_whitespace(take_torch);
  require(count_identifier(take_torch,
              "clear_pending_semantic_use_torch_source") == 1 &&
          compact_take_torch.contains(
              "constautopending=pending_semantic_use_torch_source;") &&
          compact_take_torch.contains("if(!pending||!member||!slot)") &&
          compact_take_torch.contains("*member=pending->member;") &&
          compact_take_torch.contains("*slot=pending->slot;") &&
          compact_take_torch.find(
              "clear_pending_semantic_use_torch_source();") <
              compact_take_torch.find("if(!pending||!member||!slot)"),
      "the Classic Torch handoff must clear before null-output validation and "
      "return its staged locator at most once");
  const std::string flush = function_body(source, "FlushEvents");
  require(count_identifier(flush,
              "clear_pending_semantic_open_character_sheet_member") == 1,
      "Classic event flushing must also make a staged Character Sheet member "
      "inert");
  require(count_identifier(flush,
              "clear_pending_semantic_center_combat_cursor_cell") == 1,
      "Classic event flushing must also make any staged semantic cursor cell "
      "inert");
  require(count_identifier(flush,
              "clear_pending_semantic_set_search_state_desired") == 1,
      "Classic event flushing must also make a staged Search state inert");
  require(count_identifier(flush,
              "clear_pending_semantic_use_torch_source") == 1,
      "Classic event flushing must also make a staged Torch source inert");
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

  const std::string boundary_source = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.cpp"));
  const std::string character_consumer = function_body(
      boundary_source, "RealmzConsumeSemanticOpenCharacterSheetEvent");
  const std::string compact_character_consumer =
      without_whitespace(character_consumer);
  const std::size_t character_snapshot = compact_character_consumer.find(
      "LegacyGameSnapshotSource().capture()");
  const std::size_t character_world_context =
      compact_character_consumer.find(
          ".world_presentation=snapshot.world.presentation",
          character_snapshot);
  const std::size_t character_world_gate = compact_character_consumer.find(
      "runtime_legacy_context_supports_open_character_sheet(context)",
      character_world_context);
  const std::size_t character_output = compact_character_consumer.find(
      "*party_member=character_sheet->member", character_world_gate);
  require(character_snapshot != std::string::npos &&
          character_world_context != std::string::npos &&
          character_world_gate != std::string::npos &&
          character_output != std::string::npos &&
          character_snapshot < character_world_context &&
          character_world_context < character_world_gate &&
          character_world_gate < character_output,
      "late Character Sheet consumption must recapture the exact world "
      "presentation and pass the shared runtime predicate before releasing "
      "its selected member");
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

void verify_noncombat_scroll_case_contract(
    const fs::path& repository_root) {
  const auto type_body = [](const std::string& source,
                             std::string_view type_name) {
    const std::size_t name = find_identifier(source, type_name);
    require(name != std::string::npos,
        std::string("missing type definition for ") + std::string(type_name));
    const std::size_t opening = source.find('{', name + type_name.size());
    require(opening != std::string::npos,
        std::string("missing type body for ") + std::string(type_name));
    const std::size_t closing = matching_delimiter(source, opening, '{', '}');
    return source.substr(opening, closing - opening + 1U);
  };

  const std::string raw_ui_action = read_file(
      repository_root / "src/presentation/UIAction.hpp");
  const std::string ui_action = code_only(raw_ui_action);
  const std::string open_scroll_action = type_body(
      ui_action, "OpenScrollCaseAction");
  require(count_identifier(open_scroll_action, "PartyMemberId") == 1 &&
          count_identifier(open_scroll_action, "member") == 1,
      "OpenScrollCaseAction must carry exactly one typed party-member ID");
  const std::size_t payload_alias = find_identifier(
      ui_action, "UIActionPayload");
  const std::size_t payload_end = ui_action.find(';', payload_alias);
  require(payload_alias != std::string::npos &&
          payload_end != std::string::npos,
      "UIActionPayload definition is missing");
  const std::string payload_types = ui_action.substr(
      payload_alias, payload_end - payload_alias + 1U);
  require(count_identifier(payload_types, "OpenScrollCaseAction") == 1 &&
          count_identifier(payload_types, "OpenCombatScrollCaseAction") == 1,
      "non-combat and combat scroll cases must remain distinct UIAction payloads");
  const std::string action_name_body = function_body(ui_action, "action_name");
  require(count_identifier(action_name_body, "OpenScrollCaseAction") == 1 &&
          count_text(raw_ui_action, "\"open_scroll_case\"") == 1,
      "OpenScrollCaseAction must retain its distinct stable action name");

  const std::string game_snapshot = code_only(read_file(
      repository_root / "src/presentation/GameSnapshot.hpp"));
  const std::string party_member_view = type_body(
      game_snapshot, "PartyMemberView");
  require(count_identifier(
              party_member_view, "use_scroll_available") == 1,
      "PartyMemberView must expose one non-combat scroll-case capability");

  const std::string snapshot_source = code_only(read_file(
      repository_root / "src/presentation/LegacyGameSnapshotSource.cpp"));
  const std::string capture = function_body(snapshot_source, "capture");
  const std::string compact_capture = without_whitespace(capture);
  require(compact_capture.contains(
              ".use_scroll_available=(inspell==0)&&"
              "(legacy.stamina>0)&&(legacy.armor[13]!=0),"),
      "snapshot capture must project only Classic's live non-combat case, "
      "spell-flow, and stamina gate");

  const std::string party_model_header = code_only(read_file(
      repository_root / "src/presentation/PartyRailModel.hpp"));
  const std::string action_intent = type_body(
      party_model_header, "ActionIntent");
  require(count_identifier(action_intent, "open_scroll_case") == 1 &&
          count_identifier(action_intent, "open_combat_scroll_case") == 1,
      "party model must keep non-combat and combat scroll intents distinct");
  const std::string party_model_source = code_only(read_file(
      repository_root / "src/presentation/PartyRailModel.cpp"));
  const std::string build_actions = function_body(
      party_model_source, "build_actions");
  const std::string compact_actions = without_whitespace(build_actions);
  require(compact_actions.contains(
              "elseif(!navigation_context||"
              "!selected->use_scroll_available){") &&
          compact_actions.contains("ActionIntent::open_scroll_case,") &&
          compact_actions.contains(
              "result.back().party_member=selected_member;"),
      "party model must bind non-combat scroll availability and payload to the "
      "selected member on a live world surface");

  const std::string shell_header = code_only(read_file(
      repository_root / "src/presentation/ShellControlLayout.hpp"));
  const std::string shell_kinds = type_body(shell_header, "ShellControlKind");
  require(count_identifier(shell_kinds, "open_scroll_case") == 1 &&
          count_identifier(shell_kinds, "open_combat_scroll_case") == 1,
      "shell control kinds must distinguish world and combat scroll cases");
  const std::string shell_request = type_body(
      shell_header, "ShellControlLayoutRequest");
  require(count_identifier(shell_request, "scroll_case_member") == 1 &&
          count_identifier(shell_request, "scroll_case_available") == 1,
      "shell layout request must carry the non-combat member and capability "
      "independently");
  const std::string shell_source = code_only(read_file(
      repository_root / "src/presentation/ShellControlLayout.cpp"));
  const std::string shell_layout = function_body(
      shell_source, "compute_shell_control_layout");
  const std::string compact_shell_layout = without_whitespace(shell_layout);
  require(compact_shell_layout.contains("if(request.scroll_case_member){") &&
          compact_shell_layout.contains(
              ".kind=ShellControlKind::open_scroll_case,") &&
          compact_shell_layout.contains(
              ".enabled=request.scroll_case_available,") &&
          compact_shell_layout.contains(
              ".payload=OpenScrollCaseAction{"
              "*request.scroll_case_member},"),
      "shell composition must emit one typed, member-bound non-combat scroll "
      "control using the modeled capability");

  const std::string legacy_bridge_header = code_only(read_file(
      repository_root / "src/presentation/LegacyCommandBridge.hpp"));
  const std::string legacy_handlers = type_body(
      legacy_bridge_header, "LegacyActionHandlers");
  const std::string compact_legacy_handlers =
      without_whitespace(legacy_handlers);
  require(compact_legacy_handlers.contains(
              "LegacyActionHandler<OpenScrollCaseAction>open_scroll_case;") &&
          compact_legacy_handlers.contains(
              "LegacyActionHandler<OpenCombatScrollCaseAction>"
              "open_combat_scroll_case;"),
      "legacy dispatch must register distinct typed world and combat scroll "
      "handlers");
  const std::string legacy_bridge_source = code_only(read_file(
      repository_root / "src/presentation/LegacyCommandBridge.cpp"));
  const std::string injected_dispatch = function_body(
      legacy_bridge_source, "dispatch");
  const std::string compact_injected_dispatch =
      without_whitespace(injected_dispatch);
  require(compact_injected_dispatch.contains(
              "std::is_same_v<Action,OpenScrollCaseAction>") &&
          compact_injected_dispatch.contains(
              "this->handlers_.open_scroll_case,payload"),
      "InjectedLegacyCommandBridge must dispatch OpenScrollCaseAction only to "
      "its dedicated handler");

  const std::string runtime_header = code_only(read_file(
      repository_root /
          "src/presentation/RuntimeLegacyCommandBridge.hpp"));
  const std::string world_sinks_type = type_body(
      runtime_header, "RuntimeLegacyWorldActionSinks");
  const std::string named_sinks_tag = type_body(
      runtime_header, "RuntimeLegacyNamedActionSinksTag");
  const std::string compact_runtime_header =
      without_whitespace(runtime_header);
  const std::string compact_named_sinks_tag =
      without_whitespace(named_sinks_tag);
  require(count_identifier(
              runtime_header, "RuntimeLegacyOpenScrollCaseSink") == 2 &&
          count_identifier(world_sinks_type, "open_scroll_case") == 1,
      "runtime bridge must expose one named non-combat scroll-case sink");
  require(compact_named_sinks_tag.contains(
              "RuntimeLegacyNamedActionSinksTag()=delete;") &&
          compact_named_sinks_tag.contains(
              "explicitconstexprRuntimeLegacyNamedActionSinksTag(int)"
              "noexcept{}") &&
          count_identifier(
              runtime_header, "kRuntimeLegacyNamedActionSinks") == 1 &&
          compact_runtime_header.contains(
              "inlineRuntimeLegacyNamedActionSinksTag"
              "kRuntimeLegacyNamedActionSinks{0};") &&
          compact_runtime_header.contains(
              "RuntimeLegacyCommandBridge("
              "RuntimeLegacyNamedActionSinksTag&,"
              "RuntimeLegacyContextProvidercontext_provider,"),
      "named world-action construction must retain its non-default-constructible "
      "tag-first overload and unique named tag constant");
  const std::string runtime_source = code_only(read_file(
      repository_root /
          "src/presentation/RuntimeLegacyCommandBridge.cpp"));
  const std::string compact_runtime_source = without_whitespace(runtime_source);
  require(count_identifier(
              runtime_source, "RuntimeLegacyNamedActionSinksTag") == 1 &&
          compact_runtime_source.contains(
              "RuntimeLegacyCommandBridge::RuntimeLegacyCommandBridge("
              "RuntimeLegacyNamedActionSinksTag&,"
              "RuntimeLegacyContextProvidercontext_provider,"),
      "runtime implementation must define the unique tag-first named-sinks "
      "constructor");
  require(compact_runtime_source.contains(
              "kOpenOutdoorScrollCaseMessage=0x0000256CU;") &&
          compact_runtime_source.contains(
              "kOpenDungeonScrollCaseMessage=0x00002370U;"),
      "runtime bridge must preserve the exact lowercase outdoor l and dungeon "
      "p key records");
  const std::string key_mapper = function_body(
      runtime_source, "legacy_key_message_for_open_scroll_case");
  const std::string compact_key_mapper = without_whitespace(key_mapper);
  require(compact_key_mapper.contains(
              "if((context.screen==ScreenContext::exploration)&&"
              "(context.world_presentation==WorldPresentation::outdoor)){"
              "returnkOpenOutdoorScrollCaseMessage;}") &&
          compact_key_mapper.contains(
              "if((context.screen==ScreenContext::dungeon)&&"
              "dungeon_presentation){returnkOpenDungeonScrollCaseMessage;}"),
      "scroll-case key mapping must separate outdoor l from both supported "
      "dungeon p presentations");

  const std::size_t runtime_world_sinks = find_identifier(
      runtime_source, "RuntimeLegacyWorldActionSinks");
  require(runtime_world_sinks != std::string::npos,
      "runtime world-action handler constructor is missing");
  const std::size_t runtime_world_body_open = runtime_source.find(
      '{', runtime_world_sinks +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(runtime_world_body_open != std::string::npos,
      "runtime world-action handler body is missing");
  const std::size_t runtime_world_body_close = matching_delimiter(
      runtime_source, runtime_world_body_open, '{', '}');
  const std::string runtime_world_handlers = runtime_source.substr(
      runtime_world_body_open,
      runtime_world_body_close - runtime_world_body_open + 1U);
  const std::string compact_runtime_world_handlers =
      without_whitespace(runtime_world_handlers);
  require(compact_runtime_world_handlers.contains(
              "handlers.open_scroll_case=[") &&
          compact_runtime_world_handlers.contains(
              "legacy_key_message_for_open_scroll_case(context)") &&
          compact_runtime_world_handlers.contains(
              "open_scroll_case_sink(action.member,*message,context)"),
      "runtime world handler must map and enqueue the member-bound scroll case "
      "through its named sink");

  const std::string boundary_header = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.h"));
  for (const auto identifier : {
           "RealmzIsSemanticOpenScrollCaseTag",
           "RealmzSemanticOpenScrollCaseTagSurface",
           "RealmzConsumeSemanticOpenScrollCaseEvent",
           "semantic_open_scroll_case_tag",
       }) {
    require(count_identifier(boundary_header, identifier) == 1,
        std::string("semantic boundary must expose exactly one ") + identifier);
  }
  const std::string boundary_source = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.cpp"));
  const std::string compact_boundary_source =
      without_whitespace(boundary_source);
  require(compact_boundary_source.contains(
              "kSemanticOpenScrollCaseSignature=0x53550000U;") &&
          compact_boundary_source.contains(
              "kSemanticOpenCombatScrollCaseSignature=0x55530000U;"),
      "world and combat scroll cases must retain distinct semantic signatures");
  const std::string decode_tag = function_body(
      boundary_source, "decode_open_scroll_case");
  const std::string compact_decode_tag = without_whitespace(decode_tag);
  require(compact_decode_tag.contains(
              "surface_value!=REALMZ_SEMANTIC_INPUT_EXPLORATION") &&
          compact_decode_tag.contains(
              "surface_value!=REALMZ_SEMANTIC_INPUT_DUNGEON") &&
          compact_decode_tag.contains(
              "tagged_message&kSemanticOpenScrollCaseMemberMask") &&
          compact_decode_tag.contains(".surface=surface_value,"),
      "non-combat scroll-case decoder must retain both member and originating "
      "world surface while rejecting every other surface");
  const std::string make_tag = function_body(
      boundary_source, "semantic_open_scroll_case_tag");
  const std::string compact_make_tag = without_whitespace(make_tag);
  require(compact_make_tag.contains(
              "kSemanticOpenScrollCaseSignature|"
              "(static_cast<uint32_t>(surface)<<8U)|"
              "static_cast<uint32_t>(member)") &&
          count_identifier(make_tag, "is_world_gameplay_surface") == 1,
      "non-combat scroll-case tag must encode its member and validated world "
      "surface");
  const std::string consume_tag = function_body(
      boundary_source, "RealmzConsumeSemanticOpenScrollCaseEvent");
  const std::string compact_consume_tag = without_whitespace(consume_tag);
  for (const auto identifier : {
           "authorize_completed_scope", "decode_open_scroll_case",
           "RealmzCaptureLegacyPresentationContext", "LegacyGameSnapshotSource",
           "use_scroll_available", "selected_member",
           "legacy_key_message_for_open_scroll_case",
       }) {
    require(count_identifier(consume_tag, identifier) >= 1,
        std::string("late scroll-case consumer must revalidate ") + identifier);
  }
  const std::size_t consume_authorize = compact_consume_tag.find(
      "authorize_completed_scope(expected_surface)");
  const std::size_t consume_decode = compact_consume_tag.find(
      "decode_open_scroll_case(tagged_message)");
  const std::size_t consume_surface = compact_consume_tag.find(
      "scroll_case->surface!=expected_surface", consume_decode);
  const std::size_t consume_context = compact_consume_tag.find(
      "RealmzCaptureLegacyPresentationContext()", consume_surface);
  const std::size_t consume_adaptive = compact_consume_tag.find(
      "!legacy.adaptive_eligible", consume_context);
  const std::size_t consume_context_surface = compact_consume_tag.find(
      "screen!=screen_for_surface(expected_surface)", consume_adaptive);
  const std::size_t consume_snapshot = compact_consume_tag.find(
      "LegacyGameSnapshotSource().capture()", consume_context_surface);
  const std::size_t consume_member = compact_consume_tag.find(
      "snapshot.party.member(scroll_case->member)", consume_snapshot);
  const std::size_t consume_snapshot_context = compact_consume_tag.find(
      "snapshot.screen!=screen", consume_member);
  const std::size_t consume_capability = compact_consume_tag.find(
      "!member->use_scroll_available", consume_snapshot_context);
  const std::size_t consume_selection = compact_consume_tag.find(
      "snapshot.party.selected_member!=scroll_case->member",
      consume_capability);
  const std::size_t consume_mapper = compact_consume_tag.find(
      "legacy_key_message_for_open_scroll_case", consume_selection);
  const std::size_t consume_output = compact_consume_tag.find(
      "*classic_key_message=*message", consume_mapper);
  require(consume_authorize != std::string::npos &&
          consume_decode != std::string::npos &&
          consume_surface != std::string::npos &&
          consume_context != std::string::npos &&
          consume_adaptive != std::string::npos &&
          consume_context_surface != std::string::npos &&
          consume_snapshot != std::string::npos &&
          consume_member != std::string::npos &&
          consume_snapshot_context != std::string::npos &&
          consume_capability != std::string::npos &&
          consume_selection != std::string::npos &&
          consume_mapper != std::string::npos &&
          consume_output != std::string::npos &&
          consume_authorize < consume_decode &&
          consume_decode < consume_surface &&
          consume_surface < consume_context &&
          consume_context < consume_adaptive &&
          consume_adaptive < consume_context_surface &&
          consume_context_surface < consume_snapshot &&
          consume_snapshot < consume_member &&
          consume_member < consume_snapshot_context &&
          consume_snapshot_context < consume_capability &&
          consume_capability < consume_selection &&
          consume_selection < consume_mapper && consume_mapper < consume_output,
      "late scroll-case consumption must validate tag, surface, fresh member "
      "capability, selection, and exact context before returning a Classic key");
  require(count_identifier(consume_tag, "keyDown") == 0 &&
          count_identifier(consume_tag, "getscroll") == 0,
      "semantic boundary must return only a validated key record and never "
      "invoke Classic scroll UI directly");

  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string create_window = function_body(
      window_source, "create_sdl_window");
  const std::size_t bridge = find_identifier(
      create_window, "RuntimeLegacyCommandBridge");
  const std::size_t invocation_open = create_window.find(
      '(', bridge + std::string_view("RuntimeLegacyCommandBridge").size());
  require(bridge != std::string::npos && invocation_open != std::string::npos,
      "WindowManager runtime bridge construction is missing");
  const std::size_t invocation_close = matching_delimiter(
      create_window, invocation_open, '(', ')');
  const std::string invocation = create_window.substr(
      invocation_open, invocation_close - invocation_open + 1U);
  const std::string compact_invocation = without_whitespace(invocation);
  require(count_identifier(
              invocation, "RuntimeLegacyWorldActionSinks") == 1,
      "WindowManager must construct exactly one named world-action sink bundle");
  const std::size_t named_world_sinks = find_identifier(
      invocation, "RuntimeLegacyWorldActionSinks");
  const std::size_t named_sinks_tag_argument = find_identifier(
      invocation, "kRuntimeLegacyNamedActionSinks");
  require(count_identifier(
              invocation, "kRuntimeLegacyNamedActionSinks") == 1 &&
          count_identifier(
              invocation, "RuntimeLegacyNamedActionSinksTag") == 0 &&
          named_sinks_tag_argument < named_world_sinks &&
          compact_invocation.starts_with(
              "(realmz::presentation::kRuntimeLegacyNamedActionSinks,"),
      "WindowManager must use the named tag constant to select the tag-first "
      "constructor before passing its world-action bundle");
  const std::size_t named_world_open = skip_whitespace(
      invocation,
      named_world_sinks +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(named_world_open < invocation.size() &&
          invocation[named_world_open] == '{',
      "WindowManager named world-action sink aggregate is missing");
  const std::size_t named_world_close = matching_delimiter(
      invocation, named_world_open, '{', '}');
  const std::string named_world_aggregate = invocation.substr(
      named_world_open, named_world_close - named_world_open + 1U);
  const std::string world_scroll_sink = designated_lambda_body(
      named_world_aggregate, "open_scroll_case");
  const std::string compact_world_scroll_sink =
      without_whitespace(world_scroll_sink);
  for (const auto identifier : {
           "legacy_key_message_for_open_scroll_case",
           "semantic_open_scroll_case_tag",
           "PushSemanticOpenScrollCaseEvent",
       }) {
    require(count_identifier(world_scroll_sink, identifier) == 1,
        std::string("named WindowManager scroll-case sink must own one ") +
            identifier);
  }
  require(compact_world_scroll_sink.contains(
              "if(!matching_surface||!expected||(message!=*expected)){") &&
          compact_world_scroll_sink.contains(
              "semantic_open_scroll_case_tag(member,surface)") &&
          compact_world_scroll_sink.contains(
              "returntag&&PushSemanticOpenScrollCaseEvent(tag);"),
      "WindowManager world sink must reject a mismatched key record before "
      "enqueuing the member-and-surface tag");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  const std::size_t modeled_scroll = compact_present.find(
      "ActionIntent::open_scroll_case");
  const std::size_t modeled_member = compact_present.find(
      "scroll_case_action->party_member", modeled_scroll);
  const std::size_t modeled_available = compact_present.find(
      "constboolscroll_case_available=", modeled_member);
  const std::size_t modeled_context = compact_present.find(
      "snapshot_context_matches", modeled_available);
  const std::size_t modeled_mapper = compact_present.find(
      "legacy_key_message_for_open_scroll_case", modeled_context);
  const std::size_t composed_member = compact_present.find(
      ".scroll_case_member=scroll_case_member", modeled_mapper);
  const std::size_t composed_available = compact_present.find(
      ".scroll_case_available=scroll_case_available", composed_member);
  require(modeled_scroll != std::string::npos &&
          modeled_member != std::string::npos &&
          modeled_available != std::string::npos &&
          modeled_context != std::string::npos &&
          modeled_mapper != std::string::npos &&
          composed_member != std::string::npos &&
          composed_available != std::string::npos &&
          modeled_scroll < modeled_member && modeled_member < modeled_available &&
          modeled_available < modeled_context && modeled_context < modeled_mapper &&
          modeled_mapper < composed_member && composed_member < composed_available,
      "WindowManager shell composition must derive and pass the live modeled "
      "scroll member and surface-specific availability");

  const auto scroll_liveness_branch = [](std::string_view body,
                                          std::string_view gate_name) {
    const std::size_t action = find_identifier(
        body, "OpenScrollCaseAction");
    require(action != std::string::npos,
        std::string(gate_name) +
            " must inspect the typed non-combat scroll payload");
    const std::size_t opening = body.find('{', action);
    require(opening != std::string::npos,
        std::string(gate_name) + " scroll-case branch is missing");
    const std::size_t closing = matching_delimiter(body, opening, '{', '}');
    return without_whitespace(
        body.substr(opening, closing - opening + 1U));
  };
  const std::string composition_liveness = scroll_liveness_branch(
      present, "composition-time liveness gate");
  require(composition_liveness.contains(
              "snapshot.party.member(scroll_case->member)") &&
          composition_liveness.contains(
              "ShellControlKind::open_scroll_case") &&
          composition_liveness.contains("member&&member->selected&&") &&
          composition_liveness.contains(
              "member->use_scroll_available&&") &&
          composition_liveness.contains(
              "snapshot.party.selected_member==scroll_case->member") &&
          composition_liveness.contains(
              "legacy_key_message_for_open_scroll_case(context)"),
      "composition-time liveness gate must bind the typed control to its "
      "selected capable member and current world context");
  const std::string keyboard_eligibility = function_body(
      window_source, "remastered_shell_keyboard_route_is_eligible");
  const std::string dispatch_liveness = scroll_liveness_branch(
      keyboard_eligibility, "dispatch-time keyboard liveness gate");
  require(dispatch_liveness.contains("!surface_matches_context||") &&
          dispatch_liveness.contains(
              "ShellControlKind::open_scroll_case||") &&
          dispatch_liveness.contains(
              "!realmz::presentation::"
              "legacy_key_message_for_open_scroll_case(context)") &&
          dispatch_liveness.contains(
              "LegacyGameSnapshotSource().capture()") &&
          dispatch_liveness.contains(
              "snapshot->party.member(scroll_case->member)") &&
          dispatch_liveness.contains("snapshot->screen!=context.screen") &&
          dispatch_liveness.contains("!member->selected") &&
          dispatch_liveness.contains("!member->use_scroll_available") &&
          dispatch_liveness.contains(
              "snapshot->party.selected_member!=scroll_case->member"),
      "dispatch-time keyboard liveness gate must recapture and revalidate the "
      "typed selected member, scroll capability, and exact world context");
  const std::string shell_dispatch = function_body(
      window_source, "dispatch_remastered_shell_control");
  require(count_identifier(
              shell_dispatch, "remastered_shell_keyboard_route_is_eligible") >=
          1 &&
          count_identifier(shell_dispatch, "runtime_legacy_command_bridge") >= 1,
      "shell dispatch must pass the fresh keyboard liveness gate before the "
      "runtime bridge can enqueue any world action");

  const std::string draw_shell = function_body(
      window_source, "draw_shell_panel_contents");
  require(count_identifier(draw_shell, "open_scroll_case") == 2 &&
          count_identifier(draw_shell, "has_semantic_scroll_case") == 2,
      "action-bar renderer must discover the non-combat scroll control, "
      "summarize it, and admit it to generic control drawing");

  const std::string raw_checkkeypad = read_file(
      repository_root / "src/realmz_orig/checkkeypad.c");
  const std::string checkkeypad = code_only(raw_checkkeypad);
  const std::string checkkeypad_body = function_body(
      checkkeypad, "checkkeypad");
  const std::size_t outdoor_l = raw_checkkeypad.find("case 'l':");
  const std::size_t outdoor_next = raw_checkkeypad.find(
      "case 'k':", outdoor_l);
  require(outdoor_l != std::string::npos && outdoor_next != std::string::npos &&
          outdoor_l < outdoor_next,
      "Classic outdoor checkkeypad must retain its lowercase l branch");
  const std::string outdoor_l_branch = code_only(raw_checkkeypad.substr(
      outdoor_l, outdoor_next - outdoor_l));
  require(without_whitespace(outdoor_l_branch).contains(
              "if((!inspell)&&(checkfortype(charselectnew,13,TRUE)))"
              "theControl=viewspellsbut;"),
      "Classic outdoor l must gate only on active spell flow, living selected "
      "member, and equipped scroll case before choosing viewspellsbut");
  require(count_identifier(checkkeypad_body, "getscroll") == 0,
      "outdoor key handling must select the Classic control rather than opening "
      "the chooser directly");

  const std::string raw_threed = read_file(
      repository_root / "src/realmz_orig/threed.c");
  const std::string threed = code_only(raw_threed);
  const std::string threed_body = function_body(threed, "threed");
  const std::size_t dungeon_p = raw_threed.find("case 'p':");
  const std::size_t dungeon_next = raw_threed.find("case 'k':", dungeon_p);
  require(dungeon_p != std::string::npos && dungeon_next != std::string::npos &&
          dungeon_p < dungeon_next,
      "Classic dungeon threed must retain its lowercase p branch");
  const std::string dungeon_p_branch = code_only(raw_threed.substr(
      dungeon_p, dungeon_next - dungeon_p));
  require(without_whitespace(dungeon_p_branch).contains(
              "if((!inspell)&&(checkfortype(charselectnew,13,TRUE)))"
              "theControl=viewspellsbut;"),
      "Classic dungeon p must gate only on active spell flow, living selected "
      "member, and equipped scroll case before choosing viewspellsbut");
  require(count_identifier(threed_body, "getscroll") == 0,
      "dungeon key handling must select the Classic control rather than opening "
      "the chooser directly");

  const std::string button_choice_source = code_only(read_file(
      repository_root / "src/realmz_orig/buttonchoice.c"));
  const std::string button_choice = function_body(
      button_choice_source, "buttonchoice");
  const std::string compact_button_choice = without_whitespace(button_choice);
  require(compact_button_choice.contains(
              "if(theControl==viewspellsbut){") &&
          count_identifier(button_choice, "getscroll") == 1,
      "Classic buttonchoice must remain the single world control-to-getscroll "
      "handoff");

  const std::string checkfortype_source = code_only(read_file(
      repository_root / "src/realmz_orig/checkfortype.c"));
  const std::string checkfortype_body = function_body(
      checkfortype_source, "checkfortype");
  const std::string compact_checkfortype =
      without_whitespace(checkfortype_body);
  require(compact_checkfortype.contains("if(c[t].armor[type]){") &&
          compact_checkfortype.contains("if(!aliveonly)reply=TRUE;") &&
          compact_checkfortype.contains(
              "elseif(c[t].stamina>0)reply=TRUE;") &&
          count_identifier(checkfortype_body, "scrollcase") == 0,
      "Classic checkfortype gate must inspect only equipped-case presence and "
      "optional liveness, never any scroll slot");

  const std::string getscroll_source = code_only(read_file(
      repository_root / "src/realmz_orig/getscroll.c"));
  const std::string getscroll = function_body(getscroll_source, "getscroll");
  const std::string compact_getscroll = without_whitespace(getscroll);
  const std::size_t chooser_open = compact_getscroll.find(
      "scroll=GetNewDialog(154,0L,(WindowPtr)-1L)");
  const std::size_t equipped_case_gate = compact_getscroll.find(
      "checkfortype(charselectnew,13,FALSE)", chooser_open);
  const std::size_t five_slots = compact_getscroll.find(
      "for(t=0;t<5;t++)", equipped_case_gate);
  const std::size_t first_slot_value = compact_getscroll.find(
      "c[charselectnew].scrollcase[t].powerlevel", five_slots);
  const std::size_t modal = compact_getscroll.find("ModalDialog(0L,&itemHit)");
  require(chooser_open != std::string::npos &&
          equipped_case_gate != std::string::npos &&
          five_slots != std::string::npos &&
          first_slot_value != std::string::npos && modal != std::string::npos &&
          chooser_open < equipped_case_gate && equipped_case_gate < five_slots &&
          five_slots < first_slot_value &&
          first_slot_value < modal,
      "Classic getscroll must open the chooser and render all five slots before "
      "waiting for a selection");
  require(compact_getscroll.contains("iconhand=GetCIcon(176);") &&
          compact_getscroll.contains(
              "elseMyrCDiStr(t+20,(StringPtr));"),
      "Classic getscroll must render empty slots rather than reject an equipped "
      "but empty scroll case");

  for (const auto& [name, source] : std::array{
           std::pair{"GameSnapshot", game_snapshot},
           std::pair{"snapshot capture", snapshot_source},
           std::pair{"party model", party_model_source},
           std::pair{"shell layout", shell_source},
           std::pair{"runtime bridge", runtime_source},
           std::pair{"semantic boundary", boundary_source},
           std::pair{"WindowManager", window_source},
       }) {
    require(count_identifier(source, "scrollcase") == 0,
        std::string(name) +
            " must not inspect Classic scroll-case slot contents");
  }
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

  using realmz::presentation::CombatActionPage;
  using realmz::presentation::is_valid_combat_action_page_transition;
  constexpr std::array combat_pages{
      CombatActionPage::primary,
      CombatActionPage::secondary,
      CombatActionPage::utility,
      CombatActionPage::special,
  };
  for (const auto from : combat_pages) {
    for (const auto to : combat_pages) {
      require(is_valid_combat_action_page_transition(from, to),
          "combat deck must accept direct and idempotent selection among "
          "all four valid pages");
    }
  }
  const auto invalid_combat_page = static_cast<CombatActionPage>(255);
  for (const auto page : combat_pages) {
    require(!is_valid_combat_action_page_transition(invalid_combat_page, page) &&
            !is_valid_combat_action_page_transition(page, invalid_combat_page),
        "combat deck must reject malformed source and destination pages");
  }

  const std::string panel_draw = function_body(
      source, "draw_shell_panel_contents");
  const std::string compact_panel_draw = without_whitespace(panel_draw);
  const std::size_t selected_render_state = compact_panel_draw.find(
      "constboolselected_tab=control.selected&&(control.kind=="
      "realmz::presentation::ShellControlKind::world_action_page||"
      "control.kind==realmz::presentation::ShellControlKind::"
      "combat_action_page);");
  const std::size_t selected_material_state = compact_panel_draw.find(
      "constboolselected_material=material_drawn&&"
      "(surface_state==ShellSurfaceState::selected);",
      selected_render_state);
  const std::size_t selected_inner_border = compact_panel_draw.find(
      "if(pressed||selected_tab)", selected_material_state);
  const std::size_t selected_indicator = compact_panel_draw.find(
      "if(selected_tab)", selected_inner_border);
  const std::size_t selected_label = compact_panel_draw.find(
      "selected_material?kSelectedMaterialInk:"
      "(selected_tab?kSelected:(control.enabled?kBody:kMuted))",
      selected_indicator);
  require(selected_render_state != std::string::npos &&
          selected_material_state != std::string::npos &&
          selected_inner_border != std::string::npos &&
          selected_indicator != std::string::npos &&
          selected_label != std::string::npos &&
          selected_render_state < selected_material_state &&
          selected_material_state < selected_inner_border &&
          selected_inner_border < selected_indicator &&
          selected_indicator < selected_label,
      "WindowManager must render selected world and combat tabs through the "
      "same persistent active border, indicator, and label state");
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
      "WindowManager page dispatch must reject through the shared direct-page "
      "predicate before selecting combat page state");

  const std::string composition = function_body(
      source, "present_remastered_frame");
  const std::string compact_composition = without_whitespace(composition);
  const std::size_t combat_deck_actor = compact_composition.find(
      "constautocombat_deck_combatant=live_combat_party_actor("
      "snapshot.combat?snapshot.combat->acting_combatant:std::nullopt);");
  const std::size_t combat_deck_live_gate = compact_composition.find(
      "!valid_transition||!combat_deck_combatant||",
      combat_deck_actor);
  require(combat_deck_actor != std::string::npos &&
          combat_deck_live_gate != std::string::npos &&
          combat_deck_actor < combat_deck_live_gate &&
          count_identifier(composition, "invalid_paged_combat_actions") == 0 &&
          count_identifier(composition, "unavailable_utility_page") == 0 &&
          count_identifier(composition, "unavailable_special_page") == 0,
      "combat deck tabs must bind directly to the fresh active party actor "
      "and must not snap a valid sparse or empty page back to Turn");
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

void verify_character_sheet_window_manager_contract(
    const fs::path& repository_root) {
  const std::string header = code_only(read_file(
      repository_root / "src/WindowManager.hpp"));
  const std::string source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string compact_header = without_whitespace(header);
  require(compact_header.contains(
              "realmz::presentation::WorldActionPage"
              "remastered_world_action_page="
              "realmz::presentation::WorldActionPage::travel;"),
      "WindowManager must persist world action-page state with a Travel "
      "default");

  const std::string composition = function_body(
      source, "present_remastered_frame");
  const std::string compact_composition = without_whitespace(composition);
  const std::size_t legacy_context = compact_composition.find(
      "legacy_context=RealmzCaptureLegacyPresentationContext()");
  const std::size_t stable_world_screen = compact_composition.find(
      "constboolstable_world_screen=", legacy_context);
  const std::size_t stable_world_screen_end = compact_composition.find(
      ';', stable_world_screen);
  require(stable_world_screen != std::string::npos &&
          stable_world_screen_end != std::string::npos,
      "world-page reset must derive a stable legacy-screen predicate");
  const std::string stable_world_clause = compact_composition.substr(
      stable_world_screen,
      stable_world_screen_end - stable_world_screen + 1U);
  require(count_identifier(stable_world_clause, "legacy_context") == 1 &&
          count_identifier(stable_world_clause, "adaptive_eligible") == 1 &&
          count_identifier(stable_world_clause, "screen") == 2 &&
          count_identifier(stable_world_clause, "exploration") == 1 &&
          count_identifier(stable_world_clause, "dungeon") == 1 &&
          count_identifier(
              stable_world_clause, "RealmzCurrentSemanticInputSurface") == 0 &&
          count_identifier(stable_world_clause, "snapshot") == 0,
      "stable world-screen eligibility must use only captured legacy adaptive "
      "and screen state, never a transient semantic scope");
  const std::size_t stable_world_reset = compact_composition.find(
      "if(!stable_world_screen){this->remastered_world_action_page="
      "realmz::presentation::WorldActionPage::travel;}",
      stable_world_screen_end);
  const std::size_t snapshot_capture = compact_composition.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      stable_world_reset);
  const std::size_t snapshot_context = compact_composition.find(
      "constboolsnapshot_context_matches=snapshot.screen==screen;",
      snapshot_capture);
  const std::size_t world_action_surface = compact_composition.find(
      "constboolworld_action_surface=", snapshot_context);
  const std::size_t world_action_surface_end = compact_composition.find(
      ';', world_action_surface);
  require(world_action_surface != std::string::npos &&
          world_action_surface_end != std::string::npos,
      "world action composition must derive a fresh snapshot predicate");
  const std::string world_action_clause = compact_composition.substr(
      world_action_surface,
      world_action_surface_end - world_action_surface + 1U);
  require(count_identifier(
              world_action_clause, "snapshot_context_matches") == 1 &&
          count_identifier(world_action_clause, "legacy_context") == 1 &&
          count_identifier(world_action_clause, "adaptive_eligible") == 1 &&
          count_identifier(world_action_clause, "snapshot") == 3 &&
          count_identifier(world_action_clause, "outdoor") == 1 &&
          count_identifier(world_action_clause, "dungeon_map") == 1 &&
          count_identifier(
              world_action_clause, "dungeon_first_person") == 1 &&
          count_identifier(
              world_action_clause, "RealmzCurrentSemanticInputSurface") == 0,
      "world actions must require a context-matching fresh snapshot and exact "
      "outdoor/map/first-person presentation without semantic-scope coupling");
  const std::size_t snapshot_world_reset = compact_composition.find(
      "if(!world_action_surface){this->remastered_world_action_page="
      "realmz::presentation::WorldActionPage::travel;}",
      world_action_surface_end);
  const std::size_t snapshot_screen_overwrite = compact_composition.find(
      "snapshot.screen=screen;", snapshot_world_reset);
  require(legacy_context != std::string::npos &&
          stable_world_reset != std::string::npos &&
          snapshot_capture != std::string::npos &&
          snapshot_context != std::string::npos &&
          snapshot_world_reset != std::string::npos &&
          snapshot_screen_overwrite != std::string::npos &&
          legacy_context < stable_world_screen &&
          stable_world_screen < stable_world_reset &&
          stable_world_reset < snapshot_capture &&
          snapshot_capture < snapshot_context &&
          snapshot_context < world_action_surface &&
          world_action_surface < snapshot_world_reset &&
          snapshot_world_reset < snapshot_screen_overwrite,
      "world page state must reset only after stable legacy or fresh snapshot "
      "world eligibility fails");
  require(count_text(compact_composition,
              "this->remastered_world_action_page="
              "realmz::presentation::WorldActionPage::travel;") == 3 &&
          compact_composition.contains(
              "catch(conststd::exception&e){"
              "this->remastered_shell_controls.clear();"
              "this->remastered_combat_cursor_sample.reset();"
              "this->remastered_world_action_page="
              "realmz::presentation::WorldActionPage::travel;"),
      "world pages may reset only for unstable legacy context, mismatched "
      "snapshot presentation, or failed composition; valid frames must "
      "preserve the selected page");

  const std::size_t stable_combat_reset = compact_composition.find(
      "if(screen!=realmz::presentation::ScreenContext::combat||"
      "legacy_context.adaptive_eligible==0){"
      "this->remastered_combat_action_page="
      "realmz::presentation::CombatActionPage::primary;",
      stable_world_reset);
  const std::size_t combat_action_surface = compact_composition.find(
      "constboolcombat_action_surface=", snapshot_world_reset);
  const std::size_t combat_action_surface_end = compact_composition.find(
      ';', combat_action_surface);
  const std::string combat_action_clause = compact_composition.substr(
      combat_action_surface,
      combat_action_surface_end - combat_action_surface + 1U);
  const std::size_t snapshot_combat_reset = compact_composition.find(
      "if(!combat_action_surface){this->remastered_combat_action_page="
      "realmz::presentation::CombatActionPage::primary;}",
      combat_action_surface_end);
  const std::size_t cursor_scope_guard = compact_composition.find(
      "if(!snapshot_context_matches||RealmzCurrentSemanticInputSurface()!="
      "REALMZ_SEMANTIC_INPUT_COMBAT||",
      snapshot_combat_reset);
  require(stable_combat_reset != std::string::npos &&
          combat_action_surface != std::string::npos &&
          combat_action_surface_end != std::string::npos &&
          snapshot_combat_reset != std::string::npos &&
          cursor_scope_guard != std::string::npos &&
          stable_world_reset < stable_combat_reset &&
          stable_combat_reset < snapshot_capture &&
          snapshot_world_reset < combat_action_surface &&
          combat_action_surface < snapshot_combat_reset &&
          snapshot_combat_reset < cursor_scope_guard &&
          count_identifier(
              combat_action_clause, "snapshot_context_matches") == 1 &&
          count_identifier(combat_action_clause, "legacy_context") == 1 &&
          count_identifier(combat_action_clause, "adaptive_eligible") == 1 &&
          count_identifier(combat_action_clause, "screen") == 1 &&
          count_identifier(combat_action_clause, "snapshot") == 2 &&
          count_identifier(combat_action_clause, "combat") == 3 &&
          count_identifier(
              combat_action_clause, "RealmzCurrentSemanticInputSurface") == 0,
      "combat page persistence must use stable legacy and fresh active-combat "
      "snapshot state; only the cursor-sample guard may depend on semantic "
      "scope");

  const std::size_t modeled_world_page = compact_composition.find(
      ".world_action_page=this->remastered_world_action_page",
      snapshot_screen_overwrite);
  const std::size_t character_action = compact_composition.find(
      "constautocharacter_sheet_action=std::ranges::find_if(",
      modeled_world_page);
  const std::size_t character_intent = compact_composition.find(
      "ActionIntent::open_character_sheet", character_action);
  const std::size_t character_member = compact_composition.find(
      "character_sheet_member=(world_action_surface&&"
      "(character_sheet_action!=shell_model->actions.end()))?"
      "character_sheet_action->party_member:std::nullopt;",
      character_intent);
  const std::size_t character_member_view = compact_composition.find(
      "snapshot.party.member(*character_sheet_member)", character_member);
  const std::size_t character_available = compact_composition.find(
      "constboolcharacter_sheet_available=character_sheet_member&&",
      character_member_view);
  const std::size_t character_member_bound = compact_composition.find(
      "*character_sheet_member<=5U", character_available);
  const std::size_t character_selected = compact_composition.find(
      "character_sheet_member_view->selected", character_member_bound);
  const std::size_t character_exact_selection = compact_composition.find(
      "snapshot.party.selected_member==*character_sheet_member",
      character_selected);
  const std::size_t character_can_invoke = compact_composition.find(
      "character_sheet_action->can_invoke()", character_exact_selection);
  const std::size_t character_context = compact_composition.find(
      "runtime_legacy_context_supports_open_character_sheet({",
      character_can_invoke);
  const std::size_t layout_world_page = compact_composition.find(
      ".world_action_page=shell_model->world_action_page", character_context);
  const std::size_t requested_character_member = compact_composition.find(
      ".character_sheet_member=character_sheet_member", layout_world_page);
  const std::size_t requested_character_available = compact_composition.find(
      ".character_sheet_available=character_sheet_available",
      requested_character_member);
  require(modeled_world_page != std::string::npos &&
          character_action != std::string::npos &&
          character_intent != std::string::npos &&
          character_member != std::string::npos &&
          character_member_view != std::string::npos &&
          character_available != std::string::npos &&
          character_member_bound != std::string::npos &&
          character_selected != std::string::npos &&
          character_exact_selection != std::string::npos &&
          character_can_invoke != std::string::npos &&
          character_context != std::string::npos &&
          layout_world_page != std::string::npos &&
          requested_character_member != std::string::npos &&
          requested_character_available != std::string::npos &&
          modeled_world_page < character_action &&
          character_action < character_intent &&
          character_intent < character_member &&
          character_member < character_member_view &&
          character_member_view < character_available &&
          character_available < character_member_bound &&
          character_member_bound < character_selected &&
          character_selected < character_exact_selection &&
          character_exact_selection < character_can_invoke &&
          character_can_invoke < character_context &&
          character_context < layout_world_page &&
          layout_world_page < requested_character_member &&
          requested_character_member < requested_character_available,
      "Character Sheet composition must bind its modeled action to a stable "
      "selected member and carry the exact member/availability into layout");

  const std::size_t live_controls = compact_composition.find(
      "constboolevery_enabled_control_is_live=std::ranges::all_of(",
      requested_character_available);
  const std::size_t live_character = compact_composition.find(
      "std::get_if<realmz::presentation::OpenCharacterSheetAction>"
      "(&control.payload)",
      live_controls);
  const std::size_t live_character_member = compact_composition.find(
      "snapshot.party.member(character_sheet->member)", live_character);
  const std::size_t live_character_modeled = compact_composition.find(
      "constautomodeled_action=std::ranges::find_if(",
      live_character_member);
  const std::size_t live_character_kind = compact_composition.find(
      "control.kind==realmz::presentation::ShellControlKind::"
      "open_character_sheet",
      live_character_modeled);
  const std::size_t live_character_page = compact_composition.find(
      "current_world_action_page=="
      "realmz::presentation::WorldActionPage::party",
      live_character_kind);
  const std::size_t live_character_panel = compact_composition.find(
      "action_panel.contains(control.bounds)", live_character_page);
  const std::size_t live_character_bound = compact_composition.find(
      "character_sheet->member<=5U", live_character_panel);
  const std::size_t live_character_selected = compact_composition.find(
      "member->selected", live_character_bound);
  const std::size_t live_character_exact = compact_composition.find(
      "snapshot.party.selected_member==character_sheet->member",
      live_character_selected);
  const std::size_t live_character_screen = compact_composition.find(
      "snapshot.screen==context.screen", live_character_exact);
  const std::size_t live_character_modeled_member = compact_composition.find(
      "modeled_action->party_member==character_sheet->member",
      live_character_screen);
  const std::size_t live_character_invocable = compact_composition.find(
      "modeled_action->can_invoke()", live_character_modeled_member);
  const std::size_t live_character_context = compact_composition.find(
      "runtime_legacy_context_supports_open_character_sheet(context)",
      live_character_invocable);
  require(live_controls != std::string::npos &&
          live_character != std::string::npos &&
          live_character_member != std::string::npos &&
          live_character_modeled != std::string::npos &&
          live_character_kind != std::string::npos &&
          live_character_page != std::string::npos &&
          live_character_panel != std::string::npos &&
          live_character_bound != std::string::npos &&
          live_character_selected != std::string::npos &&
          live_character_exact != std::string::npos &&
          live_character_screen != std::string::npos &&
          live_character_modeled_member != std::string::npos &&
          live_character_invocable != std::string::npos &&
          live_character_context != std::string::npos &&
          live_controls < live_character &&
          live_character < live_character_member &&
          live_character_member < live_character_modeled &&
          live_character_modeled < live_character_kind &&
          live_character_kind < live_character_page &&
          live_character_page < live_character_panel &&
          live_character_panel < live_character_bound &&
          live_character_bound < live_character_selected &&
          live_character_selected < live_character_exact &&
          live_character_exact < live_character_screen &&
          live_character_screen < live_character_modeled_member &&
          live_character_modeled_member < live_character_invocable &&
          live_character_invocable < live_character_context,
      "recomposition must retain only a PARTY-page Character Sheet control "
      "whose bounded member, exact selection, model, context, and action-bar "
      "placement are still live");

  const std::string keyboard = function_body(
      source, "remastered_shell_keyboard_route_is_eligible");
  const std::string compact_keyboard = without_whitespace(keyboard);
  const std::size_t keyboard_character = compact_keyboard.find(
      "std::get_if<realmz::presentation::OpenCharacterSheetAction>"
      "(&control.payload)");
  const std::size_t keyboard_surface = compact_keyboard.find(
      "if(!surface_matches_context||", keyboard_character);
  const std::size_t keyboard_kind = compact_keyboard.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_character_sheet",
      keyboard_surface);
  const std::size_t keyboard_page = compact_keyboard.find(
      "this->remastered_world_action_page!="
      "realmz::presentation::WorldActionPage::party",
      keyboard_kind);
  const std::size_t keyboard_panel = compact_keyboard.find(
      "action_bar.contains(control.bounds)", keyboard_page);
  const std::size_t keyboard_bound = compact_keyboard.find(
      "character_sheet->member>5U", keyboard_panel);
  const std::size_t keyboard_context = compact_keyboard.find(
      "runtime_legacy_context_supports_open_character_sheet(context)",
      keyboard_bound);
  const std::size_t keyboard_snapshot = compact_keyboard.find(
      "realmz::presentation::LegacyGameSnapshotSource().capture()",
      keyboard_context);
  const std::size_t keyboard_member = compact_keyboard.find(
      "snapshot->party.member(character_sheet->member)", keyboard_snapshot);
  const std::size_t keyboard_screen = compact_keyboard.find(
      "snapshot->screen!=context.screen", keyboard_member);
  const std::size_t keyboard_selected = compact_keyboard.find(
      "!member->selected", keyboard_screen);
  const std::size_t keyboard_exact = compact_keyboard.find(
      "snapshot->party.selected_member!=character_sheet->member",
      keyboard_selected);
  const std::size_t keyboard_accept = compact_keyboard.find(
      "continue;", keyboard_exact);
  require(keyboard_character != std::string::npos &&
          keyboard_surface != std::string::npos &&
          keyboard_kind != std::string::npos &&
          keyboard_page != std::string::npos &&
          keyboard_panel != std::string::npos &&
          keyboard_bound != std::string::npos &&
          keyboard_context != std::string::npos &&
          keyboard_snapshot != std::string::npos &&
          keyboard_member != std::string::npos &&
          keyboard_screen != std::string::npos &&
          keyboard_selected != std::string::npos &&
          keyboard_exact != std::string::npos &&
          keyboard_accept != std::string::npos &&
          keyboard_character < keyboard_surface &&
          keyboard_surface < keyboard_kind && keyboard_kind < keyboard_page &&
          keyboard_page < keyboard_panel && keyboard_panel < keyboard_bound &&
          keyboard_bound < keyboard_context &&
          keyboard_context < keyboard_snapshot &&
          keyboard_snapshot < keyboard_member &&
          keyboard_member < keyboard_screen &&
          keyboard_screen < keyboard_selected &&
          keyboard_selected < keyboard_exact &&
          keyboard_exact < keyboard_accept,
      "keyboard liveness must revalidate Character Sheet surface, PARTY page, "
      "action-bar placement, bounded member, fresh snapshot, and exact "
      "selection before accepting the control");

  const std::string dispatch = function_body(
      source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);
  const std::size_t dispatch_character = compact_dispatch.find(
      "std::get_if<realmz::presentation::OpenCharacterSheetAction>"
      "(&control.payload)");
  const std::size_t dispatch_live = compact_dispatch.find(
      "returncandidate.enabled&&candidate==control;", dispatch_character);
  const std::size_t dispatch_fresh = compact_dispatch.find(
      "!this->remastered_shell_keyboard_route_is_eligible()", dispatch_live);
  const std::size_t dispatch_character_guard = compact_dispatch.find(
      "(open_character_sheet&&", dispatch_fresh);
  const std::size_t dispatch_character_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "open_character_sheet",
      dispatch_character_guard);
  const std::size_t dispatch_character_page = compact_dispatch.find(
      "this->remastered_world_action_page!="
      "realmz::presentation::WorldActionPage::party",
      dispatch_character_kind);
  const std::size_t dispatch_character_panel = compact_dispatch.find(
      "action_bar.contains(control.bounds)", dispatch_character_page);
  const std::size_t dispatch_action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", dispatch_character_panel);
  const std::size_t dispatch_bridge = compact_dispatch.find(
      "this->runtime_legacy_command_bridge->dispatch(action)",
      dispatch_action);
  require(dispatch_character != std::string::npos &&
          dispatch_live != std::string::npos &&
          dispatch_fresh != std::string::npos &&
          dispatch_character_guard != std::string::npos &&
          dispatch_character_kind != std::string::npos &&
          dispatch_character_page != std::string::npos &&
          dispatch_character_panel != std::string::npos &&
          dispatch_action != std::string::npos &&
          dispatch_bridge != std::string::npos &&
          dispatch_character < dispatch_live && dispatch_live < dispatch_fresh &&
          dispatch_fresh < dispatch_character_guard &&
          dispatch_character_guard < dispatch_character_kind &&
          dispatch_character_kind < dispatch_character_page &&
          dispatch_character_page < dispatch_character_panel &&
          dispatch_character_panel < dispatch_action &&
          dispatch_action < dispatch_bridge,
      "Character Sheet dispatch must validate the exact enabled descriptor, "
      "fresh route, control kind, PARTY page, and action-bar containment "
      "before constructing or bridging an action");
  require(count_identifier(dispatch, "semantic_open_character_sheet_tag") == 0 &&
          count_identifier(
              dispatch, "PushSemanticOpenCharacterSheetEvent") == 0 &&
          count_identifier(dispatch, "viewcharacter") == 0 &&
          count_identifier(dispatch, "FindControl") == 0,
      "WindowManager dispatch must leave Character Sheet semantic tagging and "
      "Classic modal ownership to the production sink and preserved loop");

  const std::size_t world_page_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::SetWorldActionPageAction>"
      "(&control.payload)");
  const std::size_t world_page_transition = compact_dispatch.find(
      "is_valid_world_action_page_transition("
      "this->remastered_world_action_page,world_page->page)",
      world_page_payload);
  const std::size_t world_page_kind = compact_dispatch.find(
      "control.kind!=realmz::presentation::ShellControlKind::"
      "world_action_page",
      world_page_transition);
  const std::size_t world_page_panel = compact_dispatch.find(
      "action_bar.contains(control.bounds)", world_page_kind);
  const std::size_t world_page_assignment = compact_dispatch.find(
      "this->remastered_world_action_page=world_page->page",
      dispatch_action);
  const std::size_t world_page_return = compact_dispatch.find(
      "return;", world_page_assignment);
  require(world_page_payload != std::string::npos &&
          world_page_transition != std::string::npos &&
          world_page_kind != std::string::npos &&
          world_page_panel != std::string::npos &&
          world_page_assignment != std::string::npos &&
          world_page_return != std::string::npos &&
          world_page_payload < world_page_transition &&
          world_page_transition < world_page_kind &&
          world_page_kind < world_page_panel &&
          world_page_panel < dispatch_action &&
          dispatch_action < world_page_assignment &&
          world_page_assignment < world_page_return &&
          world_page_return < dispatch_bridge,
      "world-page dispatch must validate transition, kind, and action-bar "
      "placement, persist the selected page, and return before the legacy "
      "bridge");

  const std::string create_window = function_body(source, "create_sdl_window");
  const std::size_t world_sinks_name = find_identifier(
      create_window, "RuntimeLegacyWorldActionSinks");
  const std::size_t world_sinks_open = skip_whitespace(
      create_window,
      world_sinks_name +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(world_sinks_name != std::string::npos &&
          world_sinks_open < create_window.size() &&
          create_window[world_sinks_open] == '{',
      "WindowManager must construct a named world-action sink bundle");
  const std::size_t world_sinks_close = matching_delimiter(
      create_window, world_sinks_open, '{', '}');
  const std::string world_sinks = create_window.substr(
      world_sinks_open, world_sinks_close - world_sinks_open + 1U);
  const std::string character_sink = designated_lambda_body(
      world_sinks, "open_character_sheet");
  const std::string compact_character_sink =
      without_whitespace(character_sink);
  const std::size_t sink_surface = compact_character_sink.find(
      "surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t sink_matching_surface = compact_character_sink.find(
      "constboolmatching_surface=", sink_surface);
  const std::size_t sink_context = compact_character_sink.find(
      "runtime_legacy_context_supports_open_character_sheet(context)",
      sink_matching_surface);
  const std::size_t sink_tag = compact_character_sink.find(
      "semantic_open_character_sheet_tag(member,surface)", sink_context);
  const std::size_t sink_push = compact_character_sink.find(
      "returntag&&PushSemanticOpenCharacterSheetEvent(tag);", sink_tag);
  require(sink_surface != std::string::npos &&
          sink_matching_surface != std::string::npos &&
          sink_context != std::string::npos &&
          sink_tag != std::string::npos && sink_push != std::string::npos &&
          sink_surface < sink_matching_surface &&
          sink_matching_surface < sink_context && sink_context < sink_tag &&
          sink_tag < sink_push,
      "the production Character Sheet sink must bind the active matching "
      "world surface, validate its runtime context, encode member/surface, "
      "and enqueue exactly one semantic event");
  require(count_identifier(
              character_sink, "RealmzCurrentSemanticInputSurface") == 1 &&
          count_identifier(character_sink,
              "runtime_legacy_context_supports_open_character_sheet") == 1 &&
          count_identifier(
              character_sink, "semantic_open_character_sheet_tag") == 1 &&
          count_identifier(
              character_sink, "PushSemanticOpenCharacterSheetEvent") == 1 &&
          count_identifier(character_sink, "mouseDown") == 0 &&
          count_identifier(character_sink, "keyDown") == 0 &&
          count_identifier(character_sink, "FindControl") == 0 &&
          count_identifier(character_sink, "viewcharacter") == 0,
      "the Character Sheet sink must have one typed tag/push path and no "
      "synthetic Classic input or modal shortcut");
}

void verify_rest_party_window_manager_contract(
    const fs::path& repository_root) {
  const auto type_body = [](const std::string& source,
                             std::string_view type_name) {
    const std::size_t name = find_identifier(source, type_name);
    require(name != std::string::npos,
        std::string("missing type definition for ") + std::string(type_name));
    const std::size_t opening = source.find('{', name + type_name.size());
    require(opening != std::string::npos,
        std::string("missing type body for ") + std::string(type_name));
    const std::size_t closing = matching_delimiter(source, opening, '{', '}');
    return source.substr(opening, closing - opening + 1U);
  };

  const std::string runtime_header = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.hpp"));
  const std::string runtime_context = type_body(
      runtime_header, "RuntimeLegacyCommandContext");
  const std::string world_sinks_type = type_body(
      runtime_header, "RuntimeLegacyWorldActionSinks");
  require(count_identifier(runtime_context, "in_camp") == 1 &&
          count_identifier(runtime_context, "bool") >= 2,
      "runtime legacy context must carry one value-only camp-state flag");
  require(count_identifier(
              runtime_header, "RuntimeLegacyRestPartySink") == 2 &&
          count_identifier(world_sinks_type, "rest_party") == 1,
      "runtime bridge must append exactly one named Rest sink");

  const std::string legacy_bridge_header = code_only(read_file(
      repository_root / "src/presentation/LegacyCommandBridge.hpp"));
  const std::string legacy_handlers = type_body(
      legacy_bridge_header, "LegacyActionHandlers");
  require(without_whitespace(legacy_handlers).ends_with(
              "LegacyActionHandler<RestPartyAction>rest_party;"
              "LegacyActionHandler<SetCampStateAction>set_camp_state;"
              "LegacyActionHandler<SetSearchStateAction>set_search_state;"
              "LegacyActionHandler<UseTorchAction>use_torch;}"),
      "LegacyActionHandlers must retain Rest and Camp followed by append-only "
      "Search and Torch so positional aggregate clients keep their prior member "
      "order");

  const std::string runtime_source = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.cpp"));
  const std::string compact_runtime_source =
      without_whitespace(runtime_source);
  require(compact_runtime_source.contains(
              "kRestPartyMessage=0x00000F72U;") &&
          compact_runtime_source.contains(
              "kShowCombatRangeMessage=0x00000F72U;"),
      "world Rest and combat Range must preserve the same exact Classic r "
      "record behind distinct typed routes");
  const std::string rest_mapper = function_body(
      runtime_source, "legacy_key_message_for_rest_party");
  const std::string compact_rest_mapper = without_whitespace(rest_mapper);
  require(compact_rest_mapper.contains(
              "if(!context.adaptive_eligible||!context.in_camp){") &&
          compact_rest_mapper.contains(
              "context.screen==ScreenContext::exploration") &&
          compact_rest_mapper.contains(
              "context.world_presentation==WorldPresentation::outdoor") &&
          compact_rest_mapper.contains(
              "context.screen==ScreenContext::dungeon") &&
          compact_rest_mapper.contains(
              "WorldPresentation::dungeon_map") &&
          compact_rest_mapper.contains(
              "WorldPresentation::dungeon_first_person") &&
          count_identifier(rest_mapper, "kRestPartyMessage") == 2 &&
          count_identifier(rest_mapper, "ShowCombatRangeAction") == 0,
      "Rest key mapping must require camp plus exact outdoor or dungeon "
      "presentation and remain independent from combat Range");

  const std::size_t runtime_world_sinks = find_identifier(
      runtime_source, "RuntimeLegacyWorldActionSinks");
  const std::size_t runtime_world_body_open = runtime_source.find(
      '{', runtime_world_sinks +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(runtime_world_sinks != std::string::npos &&
          runtime_world_body_open != std::string::npos,
      "runtime world-action handler body is missing");
  const std::size_t runtime_world_body_close = matching_delimiter(
      runtime_source, runtime_world_body_open, '{', '}');
  const std::string runtime_world_handlers = runtime_source.substr(
      runtime_world_body_open,
      runtime_world_body_close - runtime_world_body_open + 1U);
  const std::string compact_runtime_world_handlers =
      without_whitespace(runtime_world_handlers);
  require(compact_runtime_world_handlers.contains(
              "handlers.rest_party=[") &&
          compact_runtime_world_handlers.contains(
              "constRestPartyAction&") &&
          compact_runtime_world_handlers.contains(
              "legacy_key_message_for_rest_party(context)") &&
          compact_runtime_world_handlers.contains(
              "rest_party_sink(*message,context)"),
      "runtime world handler must map an empty typed Rest activation through "
      "only its named sink");

  const std::string snapshot_source = code_only(read_file(
      repository_root / "src/presentation/LegacyGameSnapshotSource.cpp"));
  const std::string snapshot_capture = function_body(
      snapshot_source, "capture");
  require(without_whitespace(snapshot_capture).contains(
              "snapshot.world.in_camp=incamp!=0;"),
      "snapshot capture must project Classic's authoritative camp flag "
      "without mutating it");

  const std::string boundary_source = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.cpp"));
  const std::string compact_boundary_source =
      without_whitespace(boundary_source);
  require(compact_boundary_source.contains(
              "kSemanticRestPartySignature=0x57520000U;") &&
          compact_boundary_source.contains(
              "kSemanticShowCombatRangeSignature=0x52520000U;"),
      "world Rest and combat Range must retain distinct wire signatures");
  const std::string decode_rest = function_body(
      boundary_source, "decode_rest_party");
  require(count_identifier(decode_rest, "is_world_gameplay_surface") == 1 &&
          count_identifier(decode_rest, "kSemanticRestPartyReservedMask") ==
              1,
      "Rest tag decoding must reject non-world surfaces and nonzero reserved "
      "payload bytes");
  const std::string consume_rest = function_body(
      boundary_source, "RealmzConsumeSemanticRestPartyEvent");
  const std::string compact_consume_rest = without_whitespace(consume_rest);
  const std::size_t consume_authorize = compact_consume_rest.find(
      "authorize_completed_scope(expected_surface)");
  const std::size_t consume_decode = compact_consume_rest.find(
      "decode_rest_party(tagged_message)", consume_authorize);
  const std::size_t consume_origin = compact_consume_rest.find(
      "rest_party->surface!=expected_surface", consume_decode);
  const std::size_t consume_legacy = compact_consume_rest.find(
      "RealmzCaptureLegacyPresentationContext()", consume_origin);
  const std::size_t consume_adaptive = compact_consume_rest.find(
      "!legacy.adaptive_eligible", consume_legacy);
  const std::size_t consume_screen = compact_consume_rest.find(
      "screen!=screen_for_surface(expected_surface)", consume_adaptive);
  const std::size_t consume_snapshot = compact_consume_rest.find(
      "LegacyGameSnapshotSource().capture()", consume_screen);
  const std::size_t consume_presentation = compact_consume_rest.find(
      ".world_presentation=snapshot.world.presentation", consume_snapshot);
  const std::size_t consume_camp = compact_consume_rest.find(
      ".in_camp=snapshot.world.in_camp", consume_presentation);
  const std::size_t consume_snapshot_screen = compact_consume_rest.find(
      "snapshot.screen!=screen", consume_camp);
  const std::size_t consume_mapper = compact_consume_rest.find(
      "legacy_key_message_for_rest_party(context)", consume_snapshot_screen);
  const std::size_t consume_output = compact_consume_rest.find(
      "*classic_key_message=*message", consume_mapper);
  require(consume_authorize != std::string::npos &&
          consume_decode != std::string::npos &&
          consume_origin != std::string::npos &&
          consume_legacy != std::string::npos &&
          consume_adaptive != std::string::npos &&
          consume_screen != std::string::npos &&
          consume_snapshot != std::string::npos &&
          consume_presentation != std::string::npos &&
          consume_camp != std::string::npos &&
          consume_snapshot_screen != std::string::npos &&
          consume_mapper != std::string::npos &&
          consume_output != std::string::npos &&
          consume_authorize < consume_decode && consume_decode < consume_origin &&
          consume_origin < consume_legacy && consume_legacy < consume_adaptive &&
          consume_adaptive < consume_screen && consume_screen < consume_snapshot &&
          consume_snapshot < consume_presentation &&
          consume_presentation < consume_camp &&
          consume_camp < consume_snapshot_screen &&
          consume_snapshot_screen < consume_mapper &&
          consume_mapper < consume_output,
      "late Rest consumption must recheck single-use origin, fresh adaptive "
      "screen, exact presentation, and camp state before returning r");
  require(count_identifier(consume_rest, "keyDown") == 0 &&
          count_identifier(consume_rest, "buttonchoice") == 0 &&
          count_identifier(consume_rest, "updatefat") == 0 &&
          count_identifier(consume_rest, "timeclick") == 0,
      "semantic Rest boundary must return only a validated key record and "
      "never execute Classic rest behavior");

  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string capture_context = function_body(
      window_source, "capture_runtime_legacy_command_context");
  const std::string compact_capture_context =
      without_whitespace(capture_context);
  require(compact_capture_context.contains(
              "context.in_camp=snapshot.world.in_camp;") &&
          count_identifier(capture_context, "incamp") == 0,
      "WindowManager runtime context must copy only snapshot camp state and "
      "never read the Classic global directly");

  const std::string create_window = function_body(
      window_source, "create_sdl_window");
  const std::size_t world_sinks_name = find_identifier(
      create_window, "RuntimeLegacyWorldActionSinks");
  const std::size_t world_sinks_open = skip_whitespace(
      create_window,
      world_sinks_name +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(world_sinks_name != std::string::npos &&
          world_sinks_open < create_window.size() &&
          create_window[world_sinks_open] == '{',
      "WindowManager named world-action sink bundle is missing");
  const std::size_t world_sinks_close = matching_delimiter(
      create_window, world_sinks_open, '{', '}');
  const std::string world_sinks = create_window.substr(
      world_sinks_open, world_sinks_close - world_sinks_open + 1U);
  const std::string rest_sink = designated_lambda_body(
      world_sinks, "rest_party");
  const std::string compact_rest_sink = without_whitespace(rest_sink);
  const std::size_t sink_surface = compact_rest_sink.find(
      "surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t sink_match = compact_rest_sink.find(
      "constboolmatching_surface=", sink_surface);
  const std::size_t sink_mapper = compact_rest_sink.find(
      "legacy_key_message_for_rest_party(context)", sink_match);
  const std::size_t sink_message = compact_rest_sink.find(
      "message!=*expected", sink_mapper);
  const std::size_t sink_tag = compact_rest_sink.find(
      "semantic_rest_party_tag(surface)", sink_message);
  const std::size_t sink_push = compact_rest_sink.find(
      "returntag&&PushSemanticRestPartyEvent(tag);", sink_tag);
  require(sink_surface != std::string::npos &&
          sink_match != std::string::npos && sink_mapper != std::string::npos &&
          sink_message != std::string::npos && sink_tag != std::string::npos &&
          sink_push != std::string::npos && sink_surface < sink_match &&
          sink_match < sink_mapper && sink_mapper < sink_message &&
          sink_message < sink_tag && sink_tag < sink_push,
      "production Rest sink must bind the active world surface, validate the "
      "exact mapped r record, then tag and enqueue once");
  require(count_identifier(rest_sink, "ShowCombatRangeAction") == 0 &&
          count_identifier(rest_sink, "semantic_show_combat_range_tag") == 0 &&
          count_identifier(rest_sink, "mouseDown") == 0 &&
          count_identifier(rest_sink, "keyDown") == 0,
      "production Rest sink must not share combat Range or synthesize input");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  const std::size_t rest_action = compact_present.find(
      "ActionIntent::rest");
  const std::size_t rest_visible = compact_present.find(
      "constboolrest_control_visible=", rest_action);
  const std::size_t rest_available = compact_present.find(
      "constboolrest_available=", rest_visible);
  const std::size_t rest_snapshot_camp = compact_present.find(
      "snapshot.world.in_camp", rest_available);
  const std::size_t rest_can_invoke = compact_present.find(
      "rest_action->can_invoke()", rest_snapshot_camp);
  const std::size_t rest_context_match = compact_present.find(
      "snapshot_context_matches", rest_can_invoke);
  const std::size_t rest_mapper_position = compact_present.find(
      "legacy_key_message_for_rest_party({", rest_context_match);
  const std::size_t request_visible = compact_present.find(
      ".rest_control_visible=rest_control_visible", rest_mapper_position);
  const std::size_t request_available = compact_present.find(
      ".rest_available=rest_available", request_visible);
  require(rest_action != std::string::npos &&
          rest_visible != std::string::npos &&
          rest_available != std::string::npos &&
          rest_snapshot_camp != std::string::npos &&
          rest_can_invoke != std::string::npos &&
          rest_context_match != std::string::npos &&
          rest_mapper_position != std::string::npos &&
          request_visible != std::string::npos &&
          request_available != std::string::npos &&
          rest_action < rest_visible && rest_visible < rest_available &&
          rest_available < rest_snapshot_camp &&
          rest_snapshot_camp < rest_can_invoke &&
          rest_can_invoke < rest_context_match &&
          rest_context_match < rest_mapper_position &&
          rest_mapper_position < request_visible &&
          request_visible < request_available,
      "Rest composition must derive visibility and availability from the "
      "modeled action plus fresh world/camp context before layout");

  const std::size_t live_rest = compact_present.find(
      "std::holds_alternative<realmz::presentation::RestPartyAction>",
      request_available);
  const std::size_t live_rest_end = compact_present.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>", live_rest);
  require(live_rest != std::string::npos &&
          live_rest_end != std::string::npos && live_rest < live_rest_end,
      "composition-time Rest liveness branch is missing");
  const std::string live_rest_branch = compact_present.substr(
      live_rest, live_rest_end - live_rest);
  for (const auto needle : {
           "ShellControlKind::rest_party",
           "WorldActionPage::game",
           "action_panel.contains(control.bounds)",
           "snapshot.screen==context.screen",
           "snapshot.world.in_camp&&context.in_camp",
           "ActionIntent::rest",
           "modeled_action->can_invoke()",
           "legacy_key_message_for_rest_party(context).has_value()",
       }) {
    require(live_rest_branch.contains(needle),
        std::string("composition-time Rest liveness must retain ") + needle);
  }

  const std::string keyboard = function_body(
      window_source, "remastered_shell_keyboard_route_is_eligible");
  const std::string compact_keyboard = without_whitespace(keyboard);
  const std::size_t keyboard_rest = compact_keyboard.find(
      "std::holds_alternative<realmz::presentation::RestPartyAction>");
  const std::size_t keyboard_rest_end = compact_keyboard.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>",
      keyboard_rest);
  require(keyboard_rest != std::string::npos &&
          keyboard_rest_end != std::string::npos &&
          keyboard_rest < keyboard_rest_end,
      "keyboard Rest liveness branch is missing");
  const std::string keyboard_rest_branch = compact_keyboard.substr(
      keyboard_rest, keyboard_rest_end - keyboard_rest);
  for (const auto needle : {
           "!surface_matches_context",
           "ShellControlKind::rest_party",
           "WorldActionPage::game",
           "action_bar.contains(control.bounds)",
           "legacy_key_message_for_rest_party(context)",
           "LegacyGameSnapshotSource().capture()",
           "snapshot->screen!=context.screen",
           "snapshot->world.presentation!=context.world_presentation",
           "!snapshot->world.in_camp||!context.in_camp",
       }) {
    require(keyboard_rest_branch.contains(needle),
        std::string("keyboard Rest liveness must retain ") + needle);
  }
  require(count_identifier(keyboard_rest_branch, "Button") == 0 &&
          count_identifier(keyboard_rest_branch, "StillDown") == 0 &&
          count_identifier(keyboard_rest_branch, "SDL_PollEvent") == 0,
      "WindowManager Rest liveness must not pump EventManager or recursively "
      "dispatch SDL input");

  const std::string event_source = code_only(read_file(
      repository_root / "src/EventManager.cpp"));
  const std::string cached_mouse_query = function_body(
      event_source, "is_mouse_button_down_without_event_pump");
  const std::string compact_cached_mouse_query =
      without_whitespace(cached_mouse_query);
  require(compact_cached_mouse_query.contains(
              "constSDL_MouseButtonFlagssdl_mouse_buttons="
              "SDL_GetMouseState(nullptr,nullptr);") &&
          compact_cached_mouse_query.contains(
              "return!(this->modifier_flags&EVMOD_MOUSE_BUTTON_UP)||"
              "((sdl_mouse_buttons&SDL_BUTTON_LMASK)!=0);") &&
          count_identifier(cached_mouse_query, "SDL_GetMouseState") == 1 &&
          count_identifier(cached_mouse_query, "enqueue_pending_events") == 0 &&
          count_identifier(cached_mouse_query, "SDL_PollEvent") == 0 &&
          count_identifier(cached_mouse_query, "WindowManager") == 0,
      "Rest delivery's held-button query must combine cached SDL and Classic "
      "state without a reentrant event pump");
  const std::string semantic_delivery = function_body(
      event_source, "GetNextSemanticGameplayEvent");
  const std::string compact_semantic_delivery =
      without_whitespace(semantic_delivery);
  const std::size_t delivery_rest = compact_semantic_delivery.find(
      "RealmzIsSemanticRestPartyTag(ret->message)");
  const std::size_t delivery_mouse = compact_semantic_delivery.find(
      "em.is_mouse_button_down_without_event_pump()", delivery_rest);
  const std::size_t delivery_gate = compact_semantic_delivery.find(
      "still_remastered&&!mouse_button_held", delivery_mouse);
  const std::size_t delivery_consume = compact_semantic_delivery.find(
      "RealmzConsumeSemanticRestPartyEvent(", delivery_gate);
  const std::size_t delivery_key = compact_semantic_delivery.find(
      "ret->what=keyDown", delivery_consume);
  const std::size_t delivery_message = compact_semantic_delivery.find(
      "ret->message=classic_key_message", delivery_key);
  const std::size_t delivery_held_rejection = compact_semantic_delivery.find(
      "if(mouse_button_held)", delivery_message);
  const std::size_t delivery_burn = compact_semantic_delivery.find(
      "RealmzInvalidateSemanticInputBoundary()", delivery_held_rejection);
  const std::size_t delivery_null = compact_semantic_delivery.find(
      "ret->what=nullEvent", delivery_burn);
  const std::size_t delivery_guard = compact_semantic_delivery.find(
      "RealmzIsSemanticGuardCombatantTag(ret->message)", delivery_null);
  require(delivery_rest != std::string::npos &&
          delivery_mouse != std::string::npos &&
          delivery_gate != std::string::npos &&
          delivery_consume != std::string::npos &&
          delivery_key != std::string::npos &&
          delivery_message != std::string::npos &&
          delivery_held_rejection != std::string::npos &&
          delivery_burn != std::string::npos &&
          delivery_null != std::string::npos &&
          delivery_guard != std::string::npos,
      "EventManager semantic Rest delivery route is incomplete");
  require(delivery_rest < delivery_mouse && delivery_mouse < delivery_gate &&
          delivery_gate < delivery_consume && delivery_consume < delivery_key &&
          delivery_key < delivery_message &&
          delivery_message < delivery_held_rejection &&
          delivery_held_rejection < delivery_burn &&
          delivery_burn < delivery_null && delivery_null < delivery_guard,
      "EventManager must reject and burn a held Rest at delivery, while an "
      "accepted activation consumes once and yields one Classic keyDown");
  require(count_identifier(
              compact_semantic_delivery.substr(
                  delivery_rest, delivery_guard - delivery_rest),
              "is_mouse_button_down_without_event_pump") == 1,
      "EventManager Rest delivery must consult cached held state exactly once");

  const std::string dispatch = function_body(
      window_source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);
  const std::size_t dispatch_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::RestPartyAction>(&control.payload)");
  const std::size_t dispatch_rest_guard = compact_dispatch.find(
      "rest_party&&", dispatch_payload);
  const std::size_t dispatch_kind = compact_dispatch.find(
      "ShellControlKind::rest_party", dispatch_rest_guard);
  const std::size_t dispatch_page = compact_dispatch.find(
      "WorldActionPage::game", dispatch_kind);
  const std::size_t dispatch_panel = compact_dispatch.find(
      "action_bar.contains(control.bounds)", dispatch_page);
  const std::size_t dispatch_action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", dispatch_panel);
  const std::size_t dispatch_bridge = compact_dispatch.find(
      "runtime_legacy_command_bridge->dispatch(action)", dispatch_action);
  require(dispatch_payload != std::string::npos &&
          dispatch_rest_guard != std::string::npos &&
          dispatch_kind != std::string::npos &&
          dispatch_page != std::string::npos &&
          dispatch_panel != std::string::npos &&
          dispatch_action != std::string::npos &&
          dispatch_bridge != std::string::npos &&
          dispatch_payload < dispatch_rest_guard &&
          dispatch_rest_guard < dispatch_kind && dispatch_kind < dispatch_page &&
          dispatch_page < dispatch_panel && dispatch_panel < dispatch_action &&
          dispatch_action < dispatch_bridge,
      "Rest dispatch must validate the live descriptor, GAME page, and action "
      "bar before constructing and bridging the typed action");

  const std::string release = function_body(
      window_source, "end_remastered_pointer");
  const std::string compact_release = without_whitespace(release);
  const std::size_t release_cancel = compact_release.find(
      "this->cancel_remastered_pointer_capture()");
  const std::size_t release_dispatch = compact_release.find(
      "this->dispatch_remastered_shell_control(*pressed_control)",
      release_cancel);
  require(release_cancel != std::string::npos &&
          release_dispatch != std::string::npos &&
          release_cancel < release_dispatch,
      "pointer Rest activation must release/cancel capture before EventManager "
      "applies its cached held-button delivery gate");

  const std::string raw_outdoor = read_file(
      repository_root / "src/realmz_orig/checkkeypad.c");
  const std::string raw_dungeon = read_file(
      repository_root / "src/realmz_orig/threed.c");
  for (const auto& [name, raw_source] : std::array{
           std::pair{"outdoor", raw_outdoor},
           std::pair{"dungeon", raw_dungeon},
       }) {
    const std::size_t rest_case = raw_source.find("case 'r':");
    const std::size_t next_case = raw_source.find("case 'e':", rest_case);
    require(rest_case != std::string::npos &&
            next_case != std::string::npos && rest_case < next_case,
        std::string("Classic ") + name + " lowercase-r branch is missing");
    const std::string rest_case_body = without_whitespace(code_only(
        raw_source.substr(rest_case, next_case - rest_case)));
    require(rest_case_body.contains(
                "if(incamp==TRUE)theControl=rest;break;"),
        std::string("Classic ") + name +
            " r must still gate only on camp and select the Rest control");
  }

  const std::string buttonchoice_source = code_only(read_file(
      repository_root / "src/realmz_orig/buttonchoice.c"));
  const std::string buttonchoice = function_body(
      buttonchoice_source, "buttonchoice");
  const std::string compact_buttonchoice = without_whitespace(buttonchoice);
  const std::size_t classic_rest = compact_buttonchoice.find(
      "if(theControl==rest){");
  const std::size_t classic_camp = compact_buttonchoice.find(
      "if(theControl==campbut){", classic_rest);
  require(classic_rest != std::string::npos &&
          classic_camp != std::string::npos && classic_rest < classic_camp,
      "Classic buttonchoice Rest branch is missing");
  const std::string classic_rest_branch = compact_buttonchoice.substr(
      classic_rest, classic_camp - classic_rest);
  const std::size_t classic_sound = classic_rest_branch.find(
      "sound(6001)");
  const std::size_t classic_tick = classic_rest_branch.find(
      "tickcheck()", classic_sound);
  const std::size_t classic_delay = classic_rest_branch.find(
      "delay(1)", classic_tick);
  const std::size_t classic_update = classic_rest_branch.find(
      "updatefat(FALSE,-2,FALSE)", classic_delay);
  const std::size_t classic_time_three = classic_rest_branch.find(
      "timeclick(3,FALSE)", classic_update);
  const std::size_t classic_time_two = classic_rest_branch.find(
      "timeclick(2,TRUE)", classic_time_three);
  const std::size_t classic_revert = classic_rest_branch.find(
      "if(revertgame)return(0)", classic_time_two);
  const std::size_t classic_still_down = classic_rest_branch.find(
      "while(StillDown())", classic_revert);
  require(classic_sound != std::string::npos &&
          classic_tick != std::string::npos &&
          classic_delay != std::string::npos &&
          classic_update != std::string::npos &&
          classic_time_three != std::string::npos &&
          classic_time_two != std::string::npos &&
          classic_revert != std::string::npos &&
          classic_still_down != std::string::npos &&
          classic_sound < classic_tick && classic_tick < classic_delay &&
          classic_delay < classic_update && classic_update < classic_time_three &&
          classic_time_three < classic_time_two &&
          classic_time_two < classic_revert &&
          classic_revert < classic_still_down,
      "Classic must remain authoritative for fatigue, time, revert handling, "
      "and its existing hold-to-repeat Rest loop");

  for (const auto& entry : fs::recursive_directory_iterator(
           repository_root / "src/replay")) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string replay_source = code_only(read_file(entry.path()));
    require(count_identifier(replay_source, "RestPartyAction") == 0 &&
            count_identifier(replay_source, "semantic_rest_party_tag") == 0 &&
            count_identifier(replay_source, "PushSemanticRestPartyEvent") == 0,
        "Rest slice must not add replay actions, tags, or enqueue vocabulary");
  }
}

void verify_set_camp_state_window_manager_contract(
    const fs::path& repository_root) {
  const auto type_body = [](const std::string& source,
                             std::string_view type_name) {
    const std::size_t name = find_identifier(source, type_name);
    require(name != std::string::npos,
        std::string("missing type definition for ") + std::string(type_name));
    const std::size_t opening = source.find('{', name + type_name.size());
    require(opening != std::string::npos,
        std::string("missing type body for ") + std::string(type_name));
    const std::size_t closing = matching_delimiter(source, opening, '{', '}');
    return source.substr(opening, closing - opening + 1U);
  };

  const std::string runtime_header = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.hpp"));
  const std::string world_sinks_type = type_body(
      runtime_header, "RuntimeLegacyWorldActionSinks");
  require(count_identifier(
              runtime_header, "RuntimeLegacySetCampStateSink") == 2 &&
          count_identifier(world_sinks_type, "set_camp_state") == 1,
      "runtime bridge must append exactly one named absolute Camp-state sink");

  const std::string runtime_source = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.cpp"));
  const std::string compact_runtime_source =
      without_whitespace(runtime_source);
  require(compact_runtime_source.contains(
              "kSetCampStateMessage=0x00000863U;") &&
          compact_runtime_source.contains(
              "kCenterActiveCombatantMessage=0x00000863U;"),
      "world Camp and combat Center must preserve the same exact Classic c "
      "record behind distinct typed routes");
  const std::string camp_mapper = function_body(
      runtime_source, "legacy_key_message_for_set_camp_state");
  const std::string compact_camp_mapper = without_whitespace(camp_mapper);
  require(compact_camp_mapper.contains(
              "if(!context.adaptive_eligible||"
              "(context.in_camp==desired_in_camp)){") &&
          compact_camp_mapper.contains(
              "context.screen==ScreenContext::exploration") &&
          compact_camp_mapper.contains(
              "context.world_presentation==WorldPresentation::outdoor") &&
          compact_camp_mapper.contains(
              "context.screen==ScreenContext::dungeon") &&
          compact_camp_mapper.contains(
              "WorldPresentation::dungeon_map") &&
          compact_camp_mapper.contains(
              "WorldPresentation::dungeon_first_person") &&
          count_identifier(camp_mapper, "kSetCampStateMessage") == 2 &&
          count_identifier(camp_mapper, "cancamp") == 0 &&
          count_identifier(camp_mapper, "CenterActiveCombatantAction") == 0,
      "Camp key mapping must require an absolute state mismatch plus an exact "
      "adaptive world presentation, without projecting Classic cancamp");

  const std::size_t runtime_world_sinks = find_identifier(
      runtime_source, "RuntimeLegacyWorldActionSinks");
  const std::size_t runtime_world_body_open = runtime_source.find(
      '{', runtime_world_sinks +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(runtime_world_sinks != std::string::npos &&
          runtime_world_body_open != std::string::npos,
      "runtime named world-action handler body is missing");
  const std::size_t runtime_world_body_close = matching_delimiter(
      runtime_source, runtime_world_body_open, '{', '}');
  const std::string runtime_world_handlers = runtime_source.substr(
      runtime_world_body_open,
      runtime_world_body_close - runtime_world_body_open + 1U);
  const std::string compact_runtime_world_handlers =
      without_whitespace(runtime_world_handlers);
  require(compact_runtime_world_handlers.contains(
              "handlers.set_camp_state=[") &&
          compact_runtime_world_handlers.contains(
              "constSetCampStateAction&action") &&
          compact_runtime_world_handlers.contains(
              "legacy_key_message_for_set_camp_state("
              "action.desired_in_camp,context)") &&
          compact_runtime_world_handlers.contains(
              "set_camp_state_sink("
              "action.desired_in_camp,*message,context)"),
      "runtime world handler must preserve the typed desired Camp state "
      "through only its named sink");

  const std::string boundary_source = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.cpp"));
  const std::string compact_boundary_source =
      without_whitespace(boundary_source);
  require(compact_boundary_source.contains(
              "kSemanticSetCampStateSignature=0x57430000U;") &&
          compact_boundary_source.contains(
              "kSemanticCenterActiveCombatantSignature=0x52430000U;"),
      "world Camp and combat Center must retain distinct wire signatures");
  const std::string decode_camp = function_body(
      boundary_source, "decode_set_camp_state");
  const std::string compact_decode_camp = without_whitespace(decode_camp);
  require(count_identifier(decode_camp, "is_world_gameplay_surface") == 1 &&
          count_identifier(
              decode_camp, "kSemanticSetCampStateDesiredMask") == 1 &&
          compact_decode_camp.contains("desired_value>1U"),
      "Camp tag decoding must reject non-world surfaces and every low-byte "
      "payload other than strict false or true");
  const std::string camp_tag = function_body(
      boundary_source, "semantic_set_camp_state_tag");
  const std::string compact_camp_tag = without_whitespace(camp_tag);
  require(compact_camp_tag.contains(
              "if(!is_world_gameplay_surface(surface)){return0;}") &&
          compact_camp_tag.contains("kSemanticSetCampStateSignature|") &&
          compact_camp_tag.contains(
              "static_cast<uint32_t>(desired_in_camp)"),
      "Camp tag production must encode only a world surface and strict typed "
      "desired-state bit");

  const std::string consume_camp = function_body(
      boundary_source, "RealmzConsumeSemanticSetCampStateEvent");
  const std::string compact_consume_camp =
      without_whitespace(consume_camp);
  const std::size_t consume_authorize = compact_consume_camp.find(
      "authorize_completed_scope(expected_surface)");
  const std::size_t consume_decode = compact_consume_camp.find(
      "decode_set_camp_state(tagged_message)", consume_authorize);
  const std::size_t consume_origin = compact_consume_camp.find(
      "camp_state->surface!=expected_surface", consume_decode);
  const std::size_t consume_legacy = compact_consume_camp.find(
      "RealmzCaptureLegacyPresentationContext()", consume_origin);
  const std::size_t consume_adaptive = compact_consume_camp.find(
      "!legacy.adaptive_eligible", consume_legacy);
  const std::size_t consume_screen = compact_consume_camp.find(
      "screen!=screen_for_surface(expected_surface)", consume_adaptive);
  const std::size_t consume_snapshot = compact_consume_camp.find(
      "LegacyGameSnapshotSource().capture()", consume_screen);
  const std::size_t consume_camp_snapshot = compact_consume_camp.find(
      ".in_camp=snapshot.world.in_camp", consume_snapshot);
  const std::size_t consume_snapshot_screen = compact_consume_camp.find(
      "snapshot.screen!=screen", consume_camp_snapshot);
  const std::size_t consume_desired_mismatch = compact_consume_camp.find(
      "snapshot.world.in_camp==camp_state->desired_in_camp",
      consume_snapshot_screen);
  const std::size_t consume_mapper = compact_consume_camp.find(
      "legacy_key_message_for_set_camp_state("
      "camp_state->desired_in_camp,context)",
      consume_desired_mismatch);
  const std::size_t consume_output = compact_consume_camp.find(
      "*classic_key_message=*message", consume_mapper);
  require(consume_authorize != std::string::npos &&
          consume_decode != std::string::npos &&
          consume_origin != std::string::npos &&
          consume_legacy != std::string::npos &&
          consume_adaptive != std::string::npos &&
          consume_screen != std::string::npos &&
          consume_snapshot != std::string::npos &&
          consume_camp_snapshot != std::string::npos &&
          consume_snapshot_screen != std::string::npos &&
          consume_desired_mismatch != std::string::npos &&
          consume_mapper != std::string::npos &&
          consume_output != std::string::npos &&
          consume_authorize < consume_decode &&
          consume_decode < consume_origin && consume_origin < consume_legacy &&
          consume_legacy < consume_adaptive && consume_adaptive < consume_screen &&
          consume_screen < consume_snapshot &&
          consume_snapshot < consume_camp_snapshot &&
          consume_camp_snapshot < consume_snapshot_screen &&
          consume_snapshot_screen < consume_desired_mismatch &&
          consume_desired_mismatch < consume_mapper &&
          consume_mapper < consume_output,
      "late Camp consumption must recheck one-shot origin, fresh adaptive "
      "screen/presentation, and desired-state mismatch before returning c");
  require(count_identifier(consume_camp, "cancamp") == 0 &&
          count_identifier(consume_camp, "keyDown") == 0 &&
          count_identifier(consume_camp, "buttonchoice") == 0 &&
          count_identifier(consume_camp, "music") == 0 &&
          count_identifier(consume_camp, "timeclick") == 0,
      "semantic Camp boundary must validate and return only a key record, "
      "never decide permission or execute Classic mutations");

  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string create_window = function_body(
      window_source, "create_sdl_window");
  const std::size_t world_sinks_name = find_identifier(
      create_window, "RuntimeLegacyWorldActionSinks");
  const std::size_t world_sinks_open = skip_whitespace(
      create_window,
      world_sinks_name +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(world_sinks_name != std::string::npos &&
          world_sinks_open < create_window.size() &&
          create_window[world_sinks_open] == '{',
      "WindowManager named world-action sink bundle is missing");
  const std::size_t world_sinks_close = matching_delimiter(
      create_window, world_sinks_open, '{', '}');
  const std::string world_sinks = create_window.substr(
      world_sinks_open, world_sinks_close - world_sinks_open + 1U);
  const std::string production_camp_sink = designated_lambda_body(
      world_sinks, "set_camp_state");
  const std::string compact_production_camp_sink =
      without_whitespace(production_camp_sink);
  const std::size_t sink_surface = compact_production_camp_sink.find(
      "surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t sink_match = compact_production_camp_sink.find(
      "constboolmatching_surface=", sink_surface);
  const std::size_t sink_mapper = compact_production_camp_sink.find(
      "legacy_key_message_for_set_camp_state(desired_in_camp,context)",
      sink_match);
  const std::size_t sink_message = compact_production_camp_sink.find(
      "message!=*expected", sink_mapper);
  const std::size_t sink_tag = compact_production_camp_sink.find(
      "semantic_set_camp_state_tag(desired_in_camp,surface)", sink_message);
  const std::size_t sink_push = compact_production_camp_sink.find(
      "returntag&&PushSemanticSetCampStateEvent(tag);", sink_tag);
  require(sink_surface != std::string::npos &&
          sink_match != std::string::npos && sink_mapper != std::string::npos &&
          sink_message != std::string::npos && sink_tag != std::string::npos &&
          sink_push != std::string::npos && sink_surface < sink_match &&
          sink_match < sink_mapper && sink_mapper < sink_message &&
          sink_message < sink_tag && sink_tag < sink_push,
      "production Camp sink must bind the active world surface, preserve the "
      "desired state, validate c, and enqueue exactly once");
  require(count_identifier(production_camp_sink, "cancamp") == 0 &&
          count_identifier(
              production_camp_sink, "CenterActiveCombatantAction") == 0 &&
          count_identifier(production_camp_sink, "mouseDown") == 0 &&
          count_identifier(production_camp_sink, "keyDown") == 0,
      "production Camp sink must not preempt Classic permission, share combat "
      "Center, or synthesize input");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  const std::size_t camp_action = compact_present.find(
      "ActionIntent::set_camp_state");
  const std::size_t camp_desired = compact_present.find(
      "conststd::optional<bool>camp_desired_in_camp=", camp_action);
  const std::size_t camp_visible = compact_present.find(
      "constboolcamp_control_visible=", camp_desired);
  const std::size_t camp_available = compact_present.find(
      "constboolcamp_available=", camp_visible);
  const std::size_t camp_can_invoke = compact_present.find(
      "camp_action->can_invoke()", camp_available);
  const std::size_t camp_context_match = compact_present.find(
      "snapshot_context_matches", camp_can_invoke);
  const std::size_t camp_mapper_position = compact_present.find(
      "legacy_key_message_for_set_camp_state(", camp_context_match);
  const std::size_t request_visible = compact_present.find(
      ".camp_control_visible=camp_control_visible", camp_mapper_position);
  const std::size_t request_available = compact_present.find(
      ".camp_available=camp_available", request_visible);
  const std::size_t request_desired = compact_present.find(
      ".camp_desired_in_camp=camp_desired_in_camp.value_or(false)",
      request_available);
  require(camp_action != std::string::npos &&
          camp_desired != std::string::npos &&
          camp_visible != std::string::npos &&
          camp_available != std::string::npos &&
          camp_can_invoke != std::string::npos &&
          camp_context_match != std::string::npos &&
          camp_mapper_position != std::string::npos &&
          request_visible != std::string::npos &&
          request_available != std::string::npos &&
          request_desired != std::string::npos &&
          camp_action < camp_desired && camp_desired < camp_visible &&
          camp_visible < camp_available && camp_available < camp_can_invoke &&
          camp_can_invoke < camp_context_match &&
          camp_context_match < camp_mapper_position &&
          camp_mapper_position < request_visible &&
          request_visible < request_available &&
          request_available < request_desired,
      "Camp composition must carry its modeled absolute desired state through "
      "fresh availability and into the GAME layout request");

  const std::size_t live_camp = compact_present.find(
      "std::get_if<realmz::presentation::SetCampStateAction>",
      request_desired);
  const std::size_t live_camp_end = compact_present.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>", live_camp);
  require(live_camp != std::string::npos &&
          live_camp_end != std::string::npos && live_camp < live_camp_end,
      "composition-time Camp liveness branch is missing");
  const std::string live_camp_branch = compact_present.substr(
      live_camp, live_camp_end - live_camp);
  for (const auto needle : {
           "ShellControlKind::set_camp_state",
           "WorldActionPage::game",
           "action_panel.contains(control.bounds)",
           "snapshot.screen==context.screen",
           "snapshot.world.in_camp==context.in_camp",
           "snapshot.world.in_camp!=camp->desired_in_camp",
           "ActionIntent::set_camp_state",
           "modeled_action->can_invoke()",
           "modeled_action->desired_in_camp=="
               "std::optional<bool>{camp->desired_in_camp}",
           "legacy_key_message_for_set_camp_state("
               "camp->desired_in_camp,context)",
       }) {
    require(live_camp_branch.contains(needle),
        std::string("composition-time Camp liveness must retain ") + needle);
  }

  const std::string keyboard = function_body(
      window_source, "remastered_shell_keyboard_route_is_eligible");
  const std::string compact_keyboard = without_whitespace(keyboard);
  const std::size_t keyboard_camp = compact_keyboard.find(
      "std::get_if<realmz::presentation::SetCampStateAction>");
  const std::size_t keyboard_camp_end = compact_keyboard.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>",
      keyboard_camp);
  require(keyboard_camp != std::string::npos &&
          keyboard_camp_end != std::string::npos &&
          keyboard_camp < keyboard_camp_end,
      "keyboard Camp liveness branch is missing");
  const std::string keyboard_camp_branch = compact_keyboard.substr(
      keyboard_camp, keyboard_camp_end - keyboard_camp);
  for (const auto needle : {
           "!surface_matches_context",
           "ShellControlKind::set_camp_state",
           "WorldActionPage::game",
           "action_bar.contains(control.bounds)",
           "legacy_key_message_for_set_camp_state("
               "camp->desired_in_camp,context)",
           "LegacyGameSnapshotSource().capture()",
           "snapshot->screen!=context.screen",
           "snapshot->world.presentation!=context.world_presentation",
           "snapshot->world.in_camp!=context.in_camp",
           "snapshot->world.in_camp==camp->desired_in_camp",
       }) {
    require(keyboard_camp_branch.contains(needle),
        std::string("keyboard Camp liveness must retain ") + needle);
  }
  require(count_identifier(keyboard_camp_branch, "cancamp") == 0 &&
          count_identifier(keyboard_camp_branch, "Button") == 0 &&
          count_identifier(keyboard_camp_branch, "StillDown") == 0 &&
          count_identifier(keyboard_camp_branch, "SDL_PollEvent") == 0,
      "WindowManager Camp liveness must neither decide Classic permission nor "
      "pump or recursively dispatch input");

  const std::string dispatch = function_body(
      window_source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);
  const std::size_t dispatch_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::SetCampStateAction>("
      "&control.payload)");
  const std::size_t dispatch_guard = compact_dispatch.find(
      "set_camp_state&&", dispatch_payload);
  const std::size_t dispatch_kind = compact_dispatch.find(
      "ShellControlKind::set_camp_state", dispatch_guard);
  const std::size_t dispatch_page = compact_dispatch.find(
      "WorldActionPage::game", dispatch_kind);
  const std::size_t dispatch_panel = compact_dispatch.find(
      "action_bar.contains(control.bounds)", dispatch_page);
  const std::size_t dispatch_action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", dispatch_panel);
  const std::size_t dispatch_bridge = compact_dispatch.find(
      "runtime_legacy_command_bridge->dispatch(action)", dispatch_action);
  require(dispatch_payload != std::string::npos &&
          dispatch_guard != std::string::npos &&
          dispatch_kind != std::string::npos &&
          dispatch_page != std::string::npos &&
          dispatch_panel != std::string::npos &&
          dispatch_action != std::string::npos &&
          dispatch_bridge != std::string::npos &&
          dispatch_payload < dispatch_guard && dispatch_guard < dispatch_kind &&
          dispatch_kind < dispatch_page && dispatch_page < dispatch_panel &&
          dispatch_panel < dispatch_action && dispatch_action < dispatch_bridge,
      "Camp dispatch must validate the live typed descriptor, GAME page, and "
      "world action bar before bridging it");

  const std::string event_source = code_only(read_file(
      repository_root / "src/EventManager.cpp"));
  const std::string semantic_delivery = function_body(
      event_source, "GetNextSemanticGameplayEvent");
  const std::string compact_semantic_delivery =
      without_whitespace(semantic_delivery);
  const std::size_t delivery_camp = compact_semantic_delivery.find(
      "RealmzIsSemanticSetCampStateTag(ret->message)");
  const std::size_t delivery_consume = compact_semantic_delivery.find(
      "RealmzConsumeSemanticSetCampStateEvent(", delivery_camp);
  const std::size_t delivery_key = compact_semantic_delivery.find(
      "ret->what=keyDown", delivery_consume);
  const std::size_t delivery_null = compact_semantic_delivery.find(
      "ret->what=nullEvent", delivery_key);
  const std::size_t delivery_search = compact_semantic_delivery.find(
      "RealmzIsSemanticSetSearchStateTag(ret->message)", delivery_null);
  require(delivery_camp != std::string::npos &&
          delivery_consume != std::string::npos &&
          delivery_key != std::string::npos &&
          delivery_null != std::string::npos &&
          delivery_search != std::string::npos &&
          delivery_camp < delivery_consume &&
          delivery_consume < delivery_key && delivery_key < delivery_null &&
          delivery_null < delivery_search,
      "EventManager Camp delivery must produce one guarded keyDown or an "
      "inert rejection before the Search path");
  const std::string delivery_route = compact_semantic_delivery.substr(
      delivery_camp, delivery_search - delivery_camp);
  require(count_identifier(delivery_route, "keyDown") == 1 &&
          count_identifier(delivery_route, "nullEvent") == 1 &&
          count_identifier(delivery_route, "mouseDown") == 0 &&
          count_identifier(delivery_route, "buttonchoice") == 0 &&
          count_identifier(delivery_route, "cancamp") == 0,
      "Camp delivery must only translate the validated tag and never run or "
      "prejudge Classic camp behavior");

  for (const auto& [name, source_path] : std::array{
           std::pair{"outdoor", repository_root /
               "src/realmz_orig/checkkeypad.c"},
           std::pair{"dungeon", repository_root /
               "src/realmz_orig/threed.c"},
       }) {
    const std::string classic_source = read_file(source_path);
    const std::size_t camp_case = classic_source.find("case 'c':");
    const std::size_t next_case = classic_source.find("case 'i':", camp_case);
    require(camp_case != std::string::npos &&
            next_case != std::string::npos && camp_case < next_case,
        std::string("Classic ") + name + " lowercase-c branch is missing");
    const std::string camp_case_body = without_whitespace(code_only(
        classic_source.substr(camp_case, next_case - camp_case)));
    require(camp_case_body.contains("theControl=campbut;break;"),
        std::string("Classic ") + name +
            " c must still select only the Camp control");
  }

  const std::string buttonchoice_source = code_only(read_file(
      repository_root / "src/realmz_orig/buttonchoice.c"));
  const std::string buttonchoice = function_body(
      buttonchoice_source, "buttonchoice");
  const std::string compact_buttonchoice = without_whitespace(buttonchoice);
  const std::size_t classic_camp = compact_buttonchoice.find(
      "if(theControl==campbut){");
  const std::size_t classic_next = compact_buttonchoice.find(
      "if(theControl==overviewbut)", classic_camp);
  require(classic_camp != std::string::npos &&
          classic_next != std::string::npos && classic_camp < classic_next,
      "Classic buttonchoice Camp branch is missing");
  const std::string classic_camp_branch = compact_buttonchoice.substr(
      classic_camp, classic_next - classic_camp);
  for (const auto needle : {
           "if(!incamp)",
           "if(!cancamp)",
           "sound(10001)",
           "music(9)",
           "incamp=TRUE",
           "moveparty(0)",
           "timeclick(3,FALSE)",
           "timeclick(2,TRUE)",
           "flashmessage(",
           "sound(141)",
           "incamp=FALSE",
           "timeclick(2,FALSE)",
           "updatecontrols()",
       }) {
    require(classic_camp_branch.contains(needle),
        std::string("Classic Camp branch must retain ") + needle);
  }

  for (const auto& entry : fs::recursive_directory_iterator(
           repository_root / "src/replay")) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string replay_source = code_only(read_file(entry.path()));
    require(count_identifier(replay_source, "SetCampStateAction") == 0 &&
            count_identifier(
                replay_source, "semantic_set_camp_state_tag") == 0 &&
            count_identifier(
                replay_source, "PushSemanticSetCampStateEvent") == 0,
        "Camp slice must not add replay actions, tags, or enqueue vocabulary");
  }
}

void verify_set_search_state_window_manager_contract(
    const fs::path& repository_root) {
  const auto type_body = [](const std::string& source,
                             std::string_view type_name) {
    const std::size_t name = find_identifier(source, type_name);
    require(name != std::string::npos,
        std::string("missing type definition for ") + std::string(type_name));
    const std::size_t opening = source.find('{', name + type_name.size());
    require(opening != std::string::npos,
        std::string("missing type body for ") + std::string(type_name));
    const std::size_t closing = matching_delimiter(source, opening, '{', '}');
    return source.substr(opening, closing - opening + 1U);
  };

  const std::string runtime_header = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.hpp"));
  const std::string runtime_context = type_body(
      runtime_header, "RuntimeLegacyCommandContext");
  const std::string world_sinks_type = type_body(
      runtime_header, "RuntimeLegacyWorldActionSinks");
  require(count_identifier(runtime_context, "searching") == 1 &&
          count_identifier(
              runtime_header, "RuntimeLegacySetSearchStateSink") == 2 &&
          count_identifier(world_sinks_type, "set_search_state") == 1,
      "runtime bridge must append one value-only Search flag and one named "
      "absolute Search-state sink");

  const std::string runtime_source = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.cpp"));
  const std::string compact_runtime = without_whitespace(runtime_source);
  const std::string predicate = function_body(
      runtime_source, "runtime_legacy_context_supports_set_search_state");
  const std::string compact_predicate = without_whitespace(predicate);
  require(compact_predicate.contains(
              "if(!context.adaptive_eligible||"
              "(context.searching==desired_searching)){returnfalse;}") &&
          compact_predicate.contains(
              "context.screen==ScreenContext::exploration") &&
          compact_predicate.contains(
              "context.world_presentation==WorldPresentation::outdoor") &&
          compact_predicate.contains(
              "context.screen==ScreenContext::dungeon") &&
          compact_predicate.contains("WorldPresentation::dungeon_map") &&
          compact_predicate.contains(
              "WorldPresentation::dungeon_first_person") &&
          count_identifier(predicate, "keyDown") == 0 &&
          count_identifier(predicate, "mouseDown") == 0,
      "Search availability must require an absolute mismatch and exact "
      "adaptive world presentation without inventing Classic input");
  require(compact_runtime.contains(
              "handlers.set_camp_state=[context_provider,") &&
          compact_runtime.contains(
              "handlers.set_search_state=["
              "context_provider,") &&
          compact_runtime.contains(
              "handlers.use_torch=["
              "context_provider=std::move(context_provider),") &&
          compact_runtime.contains(
              "runtime_legacy_context_supports_set_search_state("
              "action.desired_searching,context)") &&
          compact_runtime.contains(
              "set_search_state_sink(action.desired_searching,context)"),
      "Camp and Search must copy the shared provider and only final Torch may "
      "move it, while Search preserves the typed desired state through its "
      "sink");

  const std::string boundary_source = code_only(read_file(
      repository_root / "src/presentation/SemanticInputBoundary.cpp"));
  const std::string compact_boundary = without_whitespace(boundary_source);
  require(compact_boundary.contains(
              "kSemanticSetSearchStateSignature=0x57530000U;") &&
          compact_boundary.contains(
              "kSemanticSetSearchStateMask=0xFFFF0000U;") &&
          compact_boundary.contains(
              "kSemanticSetSearchStateSurfaceMask=0x0000FF00U;") &&
          compact_boundary.contains(
              "kSemanticSetSearchStateDesiredMask=0x000000FFU;"),
      "Search wire format must remain 0x5753SSDD with surface and strict "
      "desired-state bytes");
  const std::string decode = function_body(
      boundary_source, "decode_set_search_state");
  const std::string tag = function_body(
      boundary_source, "semantic_set_search_state_tag");
  require(count_identifier(decode, "is_world_gameplay_surface") == 1 &&
          without_whitespace(decode).contains("desired_value>1U") &&
          without_whitespace(tag).contains(
              "if(!is_world_gameplay_surface(surface)){return0;}") &&
          without_whitespace(tag).contains(
              "kSemanticSetSearchStateSignature|"),
      "Search tag decode/encode must reject non-world surfaces and every "
      "payload other than strict false or true");
  const std::string consumer = function_body(
      boundary_source, "RealmzConsumeSemanticSetSearchStateEvent");
  const std::string compact_consumer = without_whitespace(consumer);
  const std::size_t authorize = compact_consumer.find(
      "authorize_completed_scope(expected_surface)");
  const std::size_t decode_position = compact_consumer.find(
      "decode_set_search_state(tagged_message)", authorize);
  const std::size_t origin = compact_consumer.find(
      "search_state->surface!=expected_surface", decode_position);
  const std::size_t legacy = compact_consumer.find(
      "RealmzCaptureLegacyPresentationContext()", origin);
  const std::size_t snapshot = compact_consumer.find(
      "LegacyGameSnapshotSource().capture()", legacy);
  const std::size_t snapshot_search = compact_consumer.find(
      ".searching=snapshot.world.searching", snapshot);
  const std::size_t late_predicate = compact_consumer.find(
      "runtime_legacy_context_supports_set_search_state("
      "search_state->desired_searching,context)", snapshot_search);
  const std::size_t output = compact_consumer.find(
      "*desired_searching=search_state->desired_searching?1:0",
      late_predicate);
  require(authorize != std::string::npos &&
          decode_position != std::string::npos && origin != std::string::npos &&
          legacy != std::string::npos && snapshot != std::string::npos &&
          snapshot_search != std::string::npos &&
          late_predicate != std::string::npos && output != std::string::npos &&
          authorize < decode_position && decode_position < origin &&
          origin < legacy && legacy < snapshot && snapshot < snapshot_search &&
          snapshot_search < late_predicate && late_predicate < output,
      "late Search consumption must burn scope authorization first, then "
      "revalidate origin, fresh snapshot/presentation, mismatch, and emit only "
      "a strict desired boolean");
  require(count_identifier(consumer, "keyDown") == 0 &&
          count_identifier(consumer, "mouseDown") == 0 &&
          count_identifier(consumer, "partycondition") == 0 &&
          count_identifier(consumer, "buttonchoice") == 0 &&
          count_identifier(consumer, "PushSemanticSetSearchStateEvent") == 0,
      "Search boundary consumer must neither forge input nor mutate or enter "
      "Classic directly");

  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string context_capture = function_body(
      window_source, "capture_runtime_legacy_command_context");
  require(without_whitespace(context_capture).contains(
              "context.searching=snapshot.world.searching;") &&
          count_identifier(context_capture, "searching") == 2,
      "production runtime context must capture Search from the fresh snapshot");
  const std::string create_window = function_body(
      window_source, "create_sdl_window");
  const std::size_t world_sinks_name = find_identifier(
      create_window, "RuntimeLegacyWorldActionSinks");
  const std::size_t world_sinks_open = skip_whitespace(
      create_window,
      world_sinks_name +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(world_sinks_name != std::string::npos &&
          world_sinks_open < create_window.size() &&
          create_window[world_sinks_open] == '{',
      "WindowManager named world-action sink bundle is missing");
  const std::size_t world_sinks_close = matching_delimiter(
      create_window, world_sinks_open, '{', '}');
  const std::string world_sinks = create_window.substr(
      world_sinks_open, world_sinks_close - world_sinks_open + 1U);
  const std::string sink = designated_lambda_body(
      world_sinks, "set_search_state");
  const std::string compact_sink = without_whitespace(sink);
  const std::size_t sink_surface = compact_sink.find(
      "surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t sink_match = compact_sink.find(
      "constboolmatching_surface=", sink_surface);
  const std::size_t sink_predicate = compact_sink.find(
      "runtime_legacy_context_supports_set_search_state("
      "desired_searching,context)", sink_match);
  const std::size_t sink_tag = compact_sink.find(
      "semantic_set_search_state_tag(desired_searching,surface)",
      sink_predicate);
  const std::size_t sink_push = compact_sink.find(
      "returntag&&PushSemanticSetSearchStateEvent(tag);", sink_tag);
  require(sink_surface != std::string::npos && sink_match != std::string::npos &&
          sink_predicate != std::string::npos && sink_tag != std::string::npos &&
          sink_push != std::string::npos && sink_surface < sink_match &&
          sink_match < sink_predicate && sink_predicate < sink_tag &&
          sink_tag < sink_push && count_identifier(sink, "keyDown") == 0 &&
          count_identifier(sink, "mouseDown") == 0 &&
          count_identifier(sink, "partycondition") == 0,
      "production Search sink must bind the active matching world surface, "
      "validate absolute state, tag/push once, and never synthesize input");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  for (const auto needle : {
           "ActionIntent::set_search_state",
           "conststd::optional<bool>search_desired_searching=",
           "constboolsearch_control_visible=",
           "constboolsearch_available=",
           "runtime_legacy_context_supports_set_search_state(",
           ".search_control_visible=search_control_visible",
           ".search_available=search_available",
           ".search_desired_searching=search_desired_searching.value_or(false)",
           "std::get_if<realmz::presentation::SetSearchStateAction>",
           "ShellControlKind::set_search_state",
           "snapshot.world.searching==context.searching",
           "snapshot.world.searching!=search->desired_searching",
       }) {
    require(compact_present.contains(needle),
        std::string("Search composition/freshness must retain ") + needle);
  }

  for (const auto& entry : fs::recursive_directory_iterator(
           repository_root / "src/replay")) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string replay_source = code_only(read_file(entry.path()));
    require(count_identifier(replay_source, "SetSearchStateAction") == 0 &&
            count_identifier(
                replay_source, "semantic_set_search_state_tag") == 0 &&
            count_identifier(
                replay_source, "PushSemanticSetSearchStateEvent") == 0 &&
            count_identifier(
                replay_source, "TakeSemanticSetSearchStateDesired") == 0 &&
            count_identifier(replay_source,
                "RealmzConsumeSemanticSetSearchStateEvent") == 0,
        "Search slice must not add replay actions, tags, enqueue, consume, or "
        "sideband vocabulary");
  }
}

void verify_use_torch_window_manager_contract(
    const fs::path& repository_root) {
  const auto type_body = [](const std::string& source,
                             std::string_view type_name) {
    const std::size_t name = find_identifier(source, type_name);
    require(name != std::string::npos,
        std::string("missing type definition for ") + std::string(type_name));
    const std::size_t opening = source.find('{', name + type_name.size());
    require(opening != std::string::npos,
        std::string("missing type body for ") + std::string(type_name));
    const std::size_t closing = matching_delimiter(source, opening, '{', '}');
    return source.substr(opening, closing - opening + 1U);
  };

  const std::string runtime_header = code_only(read_file(
      repository_root / "src/presentation/RuntimeLegacyCommandBridge.hpp"));
  const std::string runtime_context = type_body(
      runtime_header, "RuntimeLegacyCommandContext");
  const std::string world_sinks_type = type_body(
      runtime_header, "RuntimeLegacyWorldActionSinks");
  require(count_identifier(runtime_context, "usable_torch_source") == 1 &&
          count_identifier(runtime_header, "RuntimeLegacyUseTorchSink") == 2 &&
          count_identifier(world_sinks_type, "use_torch") == 1,
      "runtime bridge must carry one optional Torch locator and one named "
      "typed Torch sink");

  const std::string window_raw = read_file(
      repository_root / "src/WindowManager.cpp");
  const std::string window_source = code_only(window_raw);
  const std::string context_capture = function_body(
      window_source, "capture_runtime_legacy_command_context");
  const std::string compact_capture = without_whitespace(context_capture);
  require(compact_capture.contains(
              "context.usable_torch_source="
              "snapshot.world.usable_torch_source;") &&
          count_identifier(context_capture, "usable_torch_source") == 2,
      "production runtime context must capture the exact usable Torch "
      "locator from its fresh snapshot");

  const std::string create_window = function_body(
      window_source, "create_sdl_window");
  const std::size_t world_sinks_name = find_identifier(
      create_window, "RuntimeLegacyWorldActionSinks");
  const std::size_t world_sinks_open = skip_whitespace(
      create_window,
      world_sinks_name +
          std::string_view("RuntimeLegacyWorldActionSinks").size());
  require(world_sinks_name != std::string::npos &&
          world_sinks_open < create_window.size() &&
          create_window[world_sinks_open] == '{',
      "WindowManager named world-action sink bundle is missing");
  const std::size_t world_sinks_close = matching_delimiter(
      create_window, world_sinks_open, '{', '}');
  const std::string world_sinks = create_window.substr(
      world_sinks_open, world_sinks_close - world_sinks_open + 1U);
  const std::string sink = designated_lambda_body(world_sinks, "use_torch");
  const std::string compact_sink = without_whitespace(sink);
  const std::size_t sink_surface = compact_sink.find(
      "surface=RealmzCurrentSemanticInputSurface()");
  const std::size_t sink_match = compact_sink.find(
      "constboolmatching_surface=", sink_surface);
  const std::size_t sink_predicate = compact_sink.find(
      "runtime_legacy_context_supports_use_torch(source,context)",
      sink_match);
  const std::size_t sink_tag = compact_sink.find(
      "semantic_use_torch_tag(source,surface)", sink_predicate);
  const std::size_t sink_push = compact_sink.find(
      "returntag&&PushSemanticUseTorchEvent(tag);", sink_tag);
  require(sink_surface != std::string::npos && sink_match != std::string::npos &&
          sink_predicate != std::string::npos && sink_tag != std::string::npos &&
          sink_push != std::string::npos && sink_surface < sink_match &&
          sink_match < sink_predicate && sink_predicate < sink_tag &&
          sink_tag < sink_push,
      "production Torch sink must bind the active matching world surface, "
      "validate the typed locator, encode it, and enqueue exactly once");
  require(count_identifier(sink, "keyDown") == 0 &&
          count_identifier(sink, "mouseDown") == 0 &&
          count_identifier(sink, "checkforitem") == 0 &&
          count_identifier(sink, "buttonchoice") == 0,
      "production Torch sink must not synthesize input, inspect inventory "
      "again, or enter Classic behavior directly");

  const std::string model_source = code_only(read_file(
      repository_root / "src/presentation/PartyRailModel.cpp"));
  const std::string build_actions = function_body(model_source, "build_actions");
  const std::string compact_actions = without_whitespace(build_actions);
  for (const auto needle : {
           "conststd::optional<TorchSource>torch_source=",
           "snapshot.world.usable_torch_source->member<6U",
           "snapshot.world.usable_torch_source->slot<30U",
           "constboolcan_use_torch=navigation_context&&"
               "torch_source.has_value()",
           "ActionIntent::use_torch",
           "can_use_torch?ActionAvailability::deferred_to_engine:"
               "ActionAvailability::unavailable",
           "result.back().torch_source=torch_source",
       }) {
    require(compact_actions.contains(needle),
        std::string("Torch action modeling must retain ") + needle);
  }
  require(count_identifier(build_actions, "use_torch") == 1,
      "Torch must have exactly one always-present modeled action shell");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  const std::size_t action = compact_present.find("ActionIntent::use_torch");
  const std::size_t visible = compact_present.find(
      "constbooltorch_control_visible=", action);
  const std::size_t source = compact_present.find(
      "conststd::optional<realmz::presentation::TorchSource>torch_source=",
      visible);
  const std::size_t available = compact_present.find(
      "constbooltorch_available=", source);
  const std::size_t engaged = compact_present.find(
      "torch_source.has_value()", available);
  const std::size_t can_invoke = compact_present.find(
      "torch_action->can_invoke()", engaged);
  const std::size_t context_match = compact_present.find(
      "snapshot_context_matches", can_invoke);
  const std::size_t predicate = compact_present.find(
      "runtime_legacy_context_supports_use_torch(", context_match);
  const std::size_t predicate_source = compact_present.find(
      "*torch_source", predicate);
  const std::size_t context_source = compact_present.find(
      ".usable_torch_source=snapshot.world.usable_torch_source",
      predicate_source);
  const std::size_t request_visible = compact_present.find(
      ".torch_control_visible=torch_control_visible", context_source);
  const std::size_t request_available = compact_present.find(
      ".torch_available=torch_available", request_visible);
  const std::size_t request_source = compact_present.find(
      ".torch_source=torch_source", request_available);
  require(action != std::string::npos && visible != std::string::npos &&
          source != std::string::npos && available != std::string::npos &&
          engaged != std::string::npos && can_invoke != std::string::npos &&
          context_match != std::string::npos && predicate != std::string::npos &&
          predicate_source != std::string::npos &&
          context_source != std::string::npos &&
          request_visible != std::string::npos &&
          request_available != std::string::npos &&
          request_source != std::string::npos && action < visible &&
          visible < source && source < available && available < engaged &&
          engaged < can_invoke && can_invoke < context_match &&
          context_match < predicate && predicate < predicate_source &&
          predicate_source < context_source && context_source < request_visible &&
          request_visible < request_available &&
          request_available < request_source,
      "Torch composition must carry an engaged modeled locator through fresh "
      "availability and into the GAME layout request");

  const std::string layout_header = code_only(read_file(
      repository_root / "src/presentation/ShellControlLayout.hpp"));
  const std::string request_type = type_body(
      layout_header, "ShellControlLayoutRequest");
  require(count_identifier(request_type, "torch_control_visible") == 1 &&
          count_identifier(request_type, "torch_available") == 1 &&
          count_identifier(request_type, "torch_source") == 1,
      "shell layout request must carry one Torch visibility, availability, "
      "and optional locator field");
  const std::string layout_source = code_only(read_file(
      repository_root / "src/presentation/ShellControlLayout.cpp"));
  const std::string layout = function_body(
      layout_source, "compute_shell_control_layout");
  const std::string compact_layout = without_whitespace(layout);
  const std::size_t layout_torch = compact_layout.find(
      "if(request.torch_control_visible){");
  const std::size_t layout_end = compact_layout.find(
      "}else{return{};}", layout_torch);
  require(layout_torch != std::string::npos && layout_end != std::string::npos &&
          layout_torch < layout_end,
      "GAME shell layout must contain a bounded Torch control branch");
  const std::string torch_layout = compact_layout.substr(
      layout_torch, layout_end - layout_torch);
  for (const auto needle : {
           "ShellControlKind::use_torch",
           "request.torch_source.has_value()",
           ".enabled=request.torch_available&&request.navigation_available&&"
               "request.torch_source.has_value()",
           ".payload=UseTorchAction{request.torch_source}",
       }) {
    require(torch_layout.contains(needle),
        std::string("Torch layout must retain ") + needle);
  }

  const std::size_t live_torch = compact_present.find(
      "std::get_if<realmz::presentation::UseTorchAction>", request_source);
  const std::size_t live_torch_end = compact_present.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>", live_torch);
  require(live_torch != std::string::npos &&
          live_torch_end != std::string::npos && live_torch < live_torch_end,
      "composition-time Torch liveness branch is missing");
  const std::string live_torch_branch = compact_present.substr(
      live_torch, live_torch_end - live_torch);
  for (const auto needle : {
           "ShellControlKind::use_torch",
           "WorldActionPage::game",
           "action_panel.contains(control.bounds)",
           "snapshot.screen==context.screen",
           "snapshot.world.presentation==context.world_presentation",
           "snapshot.world.usable_torch_source==context.usable_torch_source",
           "torch->source.has_value()",
           "snapshot.world.usable_torch_source==torch->source",
           "ActionIntent::use_torch",
           "modeled_action->can_invoke()",
           "modeled_action->torch_source==torch->source",
           "runtime_legacy_context_supports_use_torch(*torch->source,context)",
       }) {
    require(live_torch_branch.contains(needle),
        std::string("composition-time Torch liveness must retain ") + needle);
  }

  const std::string keyboard = function_body(
      window_source, "remastered_shell_keyboard_route_is_eligible");
  const std::string compact_keyboard = without_whitespace(keyboard);
  const std::size_t keyboard_torch = compact_keyboard.find(
      "std::get_if<realmz::presentation::UseTorchAction>");
  const std::size_t keyboard_torch_end = compact_keyboard.find(
      "std::get_if<realmz::presentation::GuardCombatantAction>",
      keyboard_torch);
  require(keyboard_torch != std::string::npos &&
          keyboard_torch_end != std::string::npos &&
          keyboard_torch < keyboard_torch_end,
      "keyboard Torch liveness branch is missing");
  const std::string keyboard_torch_branch = compact_keyboard.substr(
      keyboard_torch, keyboard_torch_end - keyboard_torch);
  for (const auto needle : {
           "!surface_matches_context",
           "!torch->source",
           "ShellControlKind::use_torch",
           "WorldActionPage::game",
           "action_bar.contains(control.bounds)",
           "runtime_legacy_context_supports_use_torch(*torch->source,context)",
           "LegacyGameSnapshotSource().capture()",
           "snapshot->screen!=context.screen",
           "snapshot->world.presentation!=context.world_presentation",
           "snapshot->world.usable_torch_source!=context.usable_torch_source",
           "snapshot->world.usable_torch_source!=torch->source",
       }) {
    require(keyboard_torch_branch.contains(needle),
        std::string("keyboard Torch liveness must retain ") + needle);
  }
  require(count_identifier(keyboard_torch_branch, "checkforitem") == 0 &&
          count_identifier(keyboard_torch_branch, "buttonchoice") == 0 &&
          count_identifier(keyboard_torch_branch, "SDL_PollEvent") == 0,
      "WindowManager Torch liveness must be nonmutating and must not pump or "
      "recursively dispatch input");

  const std::string dispatch = function_body(
      window_source, "dispatch_remastered_shell_control");
  const std::string compact_dispatch = without_whitespace(dispatch);
  const std::size_t dispatch_payload = compact_dispatch.find(
      "std::get_if<realmz::presentation::UseTorchAction>(&control.payload)");
  const std::size_t dispatch_live = compact_dispatch.find(
      "this->remastered_shell_keyboard_route_is_eligible()", dispatch_payload);
  const std::size_t dispatch_guard = compact_dispatch.find(
      "(use_torch&&", dispatch_live);
  const std::size_t dispatch_source = compact_dispatch.find(
      "!use_torch->source", dispatch_guard);
  const std::size_t dispatch_kind = compact_dispatch.find(
      "ShellControlKind::use_torch", dispatch_source);
  const std::size_t dispatch_page = compact_dispatch.find(
      "WorldActionPage::game", dispatch_kind);
  const std::size_t dispatch_exploration = compact_dispatch.find(
      "ScreenContext::exploration", dispatch_page);
  const std::size_t dispatch_dungeon = compact_dispatch.find(
      "ScreenContext::dungeon", dispatch_exploration);
  const std::size_t dispatch_panel = compact_dispatch.find(
      "action_bar.contains(control.bounds)", dispatch_dungeon);
  const std::size_t dispatch_action = compact_dispatch.find(
      "constrealmz::presentation::UIActionaction{", dispatch_panel);
  const std::size_t dispatch_bridge = compact_dispatch.find(
      "runtime_legacy_command_bridge->dispatch(action)", dispatch_action);
  require(dispatch_payload != std::string::npos &&
          dispatch_live != std::string::npos &&
          dispatch_guard != std::string::npos &&
          dispatch_source != std::string::npos &&
          dispatch_kind != std::string::npos &&
          dispatch_page != std::string::npos &&
          dispatch_exploration != std::string::npos &&
          dispatch_dungeon != std::string::npos &&
          dispatch_panel != std::string::npos &&
          dispatch_action != std::string::npos &&
          dispatch_bridge != std::string::npos &&
          dispatch_payload < dispatch_live && dispatch_live < dispatch_guard &&
          dispatch_guard < dispatch_source && dispatch_source < dispatch_kind &&
          dispatch_kind < dispatch_page && dispatch_page < dispatch_exploration &&
          dispatch_exploration < dispatch_dungeon &&
          dispatch_dungeon < dispatch_panel && dispatch_panel < dispatch_action &&
          dispatch_action < dispatch_bridge,
      "Torch dispatch must require a live source-bearing GAME control on an "
      "exploration/dungeon world action bar before typed bridge dispatch");

  const std::string renderer = function_body(
      window_source, "draw_shell_panel_contents");
  const std::string compact_renderer = without_whitespace(renderer);
  const std::size_t renderer_presence = compact_renderer.find(
      "constboolhas_semantic_torch=std::ranges::any_of(");
  const std::size_t renderer_summary = compact_renderer.find(
      "if(has_semantic_torch){action_summary+=;}", renderer_presence);
  const std::size_t renderer_compact_summary = compact_renderer.find(
      "if(has_semantic_torch){append_summary();}", renderer_summary);
  const std::size_t renderer_gate = compact_renderer.find(
      "has_semantic_search||has_semantic_torch||has_semantic_guard",
      renderer_compact_summary);
  const std::size_t renderer_allowlist = compact_renderer.find(
      "control.kind!=realmz::presentation::ShellControlKind::use_torch",
      renderer_gate);
  require(renderer_presence != std::string::npos &&
          renderer_summary != std::string::npos &&
          renderer_compact_summary != std::string::npos &&
          renderer_gate != std::string::npos &&
          renderer_allowlist != std::string::npos &&
          renderer_presence < renderer_summary &&
          renderer_summary < renderer_compact_summary &&
          renderer_compact_summary < renderer_gate &&
          renderer_gate < renderer_allowlist &&
          count_identifier(renderer, "has_semantic_torch") == 5 &&
          count_identifier(renderer, "use_torch") == 2 &&
          count_text(window_raw, "action_summary += \" · TORCH\"") == 1 &&
          count_text(window_raw, "append_summary(\"TORCH\")") == 1,
      "action-bar renderer must summarize Torch in paged and compact world "
      "chrome and admit only its explicit semantic control kind");
}

void verify_selected_party_details_renderer_contract(
    const fs::path& repository_root) {
  const std::string model_header = code_only(read_file(
      repository_root / "src/presentation/PartyRailModel.hpp"));
  const std::string model_source = code_only(read_file(
      repository_root / "src/presentation/PartyRailModel.cpp"));
  const std::string layout_header = code_only(read_file(
      repository_root /
          "src/presentation/SelectedPartyDetailsLayout.hpp"));
  const std::string layout_source = code_only(read_file(
      repository_root /
          "src/presentation/SelectedPartyDetailsLayout.cpp"));
  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));

  const std::size_t details_name = find_identifier(
      model_header, "SelectedPartyDetailsModel");
  require(details_name != std::string::npos,
      "selected-member Details must retain a detached presentation model");
  const std::size_t details_open = model_header.find('{', details_name);
  require(details_open != std::string::npos,
      "selected-member Details model is missing its definition");
  const std::size_t details_close = matching_delimiter(
      model_header, details_open, '{', '}');
  const std::string details_model = model_header.substr(
      details_open, details_close - details_open + 1);
  for (const auto field : {
           "member", "name", "level", "armor_class", "movement",
           "movement_maximum", "stamina", "spell_points", "states",
           "conscious"}) {
    require(count_identifier(details_model, field) != 0,
        std::string("selected-member Details model is missing read-only field ") +
            field);
  }

  const std::string selected_details = function_body(
      model_source, "selected_details");
  const std::string compact_selected_details =
      without_whitespace(selected_details);
  const std::size_t stamina_copy = compact_selected_details.find(
      "result.stamina=rail_member->stamina;");
  const std::size_t spell_points_copy = compact_selected_details.find(
      "result.spell_points=rail_member->spell_points;", stamina_copy);
  const std::size_t states_copy = compact_selected_details.find(
      "result.states=selected_detail_states(*rail_member);",
      spell_points_copy);
  const std::size_t consciousness_copy = compact_selected_details.find(
      "result.conscious=rail_member->conscious;", states_copy);
  require(stamina_copy != std::string::npos &&
          spell_points_copy != std::string::npos &&
          states_copy != std::string::npos &&
          consciousness_copy != std::string::npos &&
          stamina_copy < spell_points_copy &&
          spell_points_copy < states_copy &&
          states_copy < consciousness_copy,
      "selected-member Details must copy both meters, the complete normalized "
      "state sequence, and consciousness from its detached party-rail model");
  require(count_identifier(selected_details, "build_actions") == 0 &&
          count_identifier(selected_details, "UIAction") == 0 &&
          count_identifier(selected_details, "dispatch") == 0,
      "selected-member Details projection must not create or dispatch an "
      "action");

  const std::string normalized_details_states = function_body(
      model_source, "selected_detail_states");
  const std::string compact_normalized_details_states =
      without_whitespace(normalized_details_states);
  require(compact_normalized_details_states.contains(
              "for(constauto&state:member.states)") &&
          compact_normalized_details_states.contains(
              "state.identifier==") &&
          compact_normalized_details_states.contains(
              "std::ranges::find(result,state.identifier,") &&
          compact_normalized_details_states.contains(
              "&StateTokenModel::identifier)") &&
          count_identifier(normalized_details_states, "emplace_back") >= 2,
      "selected-member Details must visit every normalized party state, "
      "deduplicate by stable identifier, and retain each distinct state");
  require(count_identifier(normalized_details_states, "UIAction") == 0 &&
          count_identifier(normalized_details_states, "dispatch") == 0,
      "selected-member Details state normalization must remain read-only");

  const std::string member_states = function_body(
      model_source, "member_states");
  const std::string compact_member_states = without_whitespace(member_states);
  const std::size_t copy_conditions = compact_member_states.find(
      "autocondition_codes=member.conditions;");
  const std::size_t sort_conditions = compact_member_states.find(
      "std::ranges::sort(condition_codes);", copy_conditions);
  const std::size_t unique_conditions = compact_member_states.find(
      "std::ranges::unique(condition_codes)", sort_conditions);
  const std::size_t erase_duplicates = compact_member_states.find(
      "condition_codes.erase(", unique_conditions);
  require(copy_conditions != std::string::npos &&
          sort_conditions != std::string::npos &&
          unique_conditions != std::string::npos &&
          erase_duplicates != std::string::npos &&
          copy_conditions < sort_conditions &&
          sort_conditions < unique_conditions &&
          unique_conditions < erase_duplicates,
      "the state sequence copied into Details must derive from an immutable "
      "sorted and deduplicated condition-code snapshot");

  require(count_identifier(
              layout_header, "compute_selected_party_details_layout") == 1 &&
          count_identifier(
              layout_source, "compute_selected_party_details_layout") == 1,
      "selected-member Details must expose one pure layout declaration and "
      "one definition");
  const std::size_t layout_declaration = find_identifier(
      layout_header, "compute_selected_party_details_layout");
  const std::size_t layout_parameters_open = layout_header.find(
      '(', layout_declaration);
  require(layout_parameters_open != std::string::npos,
      "selected-member Details layout declaration has no request parameters");
  const std::size_t layout_parameters_close = matching_delimiter(
      layout_header, layout_parameters_open, '(', ')');
  const std::string compact_layout_parameters = without_whitespace(
      layout_header.substr(
          layout_parameters_open,
          layout_parameters_close - layout_parameters_open + 1));
  require(compact_layout_parameters.contains(
              "constSelectedPartyDetailsLayoutRequest&"),
      "selected-member Details layout must accept its input by const "
      "reference");
  const std::size_t density_name = find_identifier(
      layout_header, "SelectedPartyDetailsLayoutDensity");
  const std::size_t density_open = layout_header.find('{', density_name);
  require(density_name != std::string::npos &&
          density_open != std::string::npos,
      "selected-member Details layout density is missing its definition");
  const std::size_t density_close = matching_delimiter(
      layout_header, density_open, '{', '}');
  const std::string density_values = layout_header.substr(
      density_open, density_close - density_open + 1);
  require(count_identifier(density_values, "wide") == 1 &&
          count_identifier(density_values, "compact") == 1,
      "selected-member Details layout must distinguish only wide and compact "
      "rendering density");

  for (const auto forbidden : {
           "SDL_Renderer", "SDL_Texture", "UIAction",
           "LegacyCommandBridge", "RuntimeLegacyCommandBridge",
           "ResourceManager", "ResourceDASM", "GameSnapshot",
           "dispatch_remastered_shell_control", "semantic_controls_ready"}) {
    require(count_identifier(layout_header, forbidden) == 0 &&
            count_identifier(layout_source, forbidden) == 0,
        std::string("pure selected-member Details layout must not depend on ") +
            forbidden);
  }
  require(layout_header.find("SDL_") == std::string::npos &&
          layout_source.find("SDL_") == std::string::npos,
      "pure selected-member Details layout must not contain an SDL API path");

  const std::string details_layout = function_body(
      layout_source, "compute_selected_party_details_layout");
  require(count_identifier(details_layout, "stamina") != 0 &&
          count_identifier(details_layout, "spell_points") != 0 &&
          count_identifier(details_layout, "conscious") != 0 &&
          count_identifier(details_layout, "armor_class") != 0 &&
          count_identifier(details_layout, "movement") != 0 &&
          count_identifier(details_layout, "states") != 0,
      "shared selected-member Details layout must consume both meters, "
      "consciousness, armor, movement, and the complete state sequence");
  require(count_identifier(details_layout, "state_tokens") != 0 &&
          count_identifier(details_layout, "visible_state_count") != 0 &&
          count_identifier(details_layout, "hidden_state_count") != 0,
      "shared selected-member Details layout must retain complete renderable "
      "state tokens and an explicit compact elision count");

  const std::string draw_details = function_body(
      window_source, "draw_selected_party_details");
  require(count_identifier(
              draw_details, "compute_selected_party_details_layout") == 1,
      "the shared Details renderer must own the sole WindowManager layout "
      "computation");
  require(count_identifier(draw_details, "draw_shell_text") != 0 &&
          count_identifier(draw_details, "draw_shell_meter") == 1 &&
          count_identifier(draw_details, "draw_meter") == 3 &&
          count_identifier(draw_details, "state_tokens") != 0 &&
          count_identifier(draw_details, "state_text") != 0 &&
          count_identifier(draw_details, "hidden_state_count") != 0,
      "the shared Details renderer must draw both meters, complete marker-"
      "bearing state data, and explicit compact elision");
  require(count_identifier(draw_details, "format") == 0,
      "the shared Details renderer must consume the pure layout instead of "
      "rebuilding ad-hoc text");
  for (const auto forbidden : {
           "dispatch_remastered_shell_control", "LegacyCommandBridge",
           "RuntimeLegacyCommandBridge", "UIAction", "GetResource",
           "ResourceDASM", "PushEvent", "semantic_controls_ready"}) {
    require(count_identifier(draw_details, forbidden) == 0,
        std::string("read-only selected-member Details renderer must not use ") +
            forbidden);
  }

  const std::size_t draw_details_name = find_identifier(
      window_source, "draw_selected_party_details");
  const std::size_t draw_details_parameters_open = window_source.find(
      '(', draw_details_name);
  require(draw_details_parameters_open != std::string::npos,
      "shared selected-member Details renderer has no parameters");
  const std::size_t draw_details_parameters_close = matching_delimiter(
      window_source, draw_details_parameters_open, '(', ')');
  const std::string compact_draw_details_parameters = without_whitespace(
      window_source.substr(
          draw_details_parameters_open,
          draw_details_parameters_close - draw_details_parameters_open + 1));
  require(compact_draw_details_parameters.contains(
              "constrealmz::presentation::SelectedPartyDetailsModel&"),
      "shared selected-member Details renderer must receive its model by "
      "const reference");

  const std::string panel_draw = function_body(
      window_source, "draw_shell_panel_contents");
  const std::string compact_panel_draw = without_whitespace(panel_draw);
  const auto branch_after = [&compact_panel_draw](std::string_view marker) {
    const std::size_t marker_position = compact_panel_draw.find(marker);
    require(marker_position != std::string::npos,
        std::string("could not find selected-member Details branch: ") +
            std::string(marker));
    const std::size_t opening = compact_panel_draw.find('{', marker_position);
    require(opening != std::string::npos,
        "selected-member Details branch is missing its body");
    const std::size_t closing = matching_delimiter(
        compact_panel_draw, opening, '{', '}');
    return compact_panel_draw.substr(opening, closing - opening + 1);
  };
  const std::string wide_branch = branch_after(
      "if(kind==ShellPanelKind::details)");
  const std::string compact_branch = branch_after(
      "*model.drawers.active_panel=="
      "realmz::presentation::DrawerPanel::details");
  require(count_identifier(panel_draw, "draw_selected_party_details") == 2 &&
          count_identifier(wide_branch, "draw_selected_party_details") == 1 &&
          count_identifier(compact_branch,
              "draw_selected_party_details") == 1,
      "wide Details and compact Details drawer must each delegate exactly "
      "once to the same renderer");
  require(wide_branch.contains(
              "SelectedPartyDetailsLayoutDensity::wide") &&
          compact_branch.contains(
              "SelectedPartyDetailsLayoutDensity::compact"),
      "wide and compact Details branches must declare their density through "
      "the shared layout contract");
  for (const auto direct_render : {
           "compute_selected_party_details_layout", "draw_shell_text",
           "draw_shell_meter", "format", "stamina", "spell_points",
           "conscious", "armor_class", "movement", "states"}) {
    require(count_identifier(wide_branch, direct_render) == 0 &&
            count_identifier(compact_branch, direct_render) == 0,
        std::string("wide/compact Details branches must not duplicate ") +
            direct_render);
  }

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  require(count_identifier(present, "semantic_controls_ready") == 1 &&
          compact_present.contains(".semantic_controls_ready=false,"),
      "the bounded Details renderer milestone must keep the complete Classic "
      "frame and must not enable the cropped semantic-controls route");
}

void verify_gameplay_chrome_coverage_contract(
    const fs::path& repository_root) {
  const std::string coverage_header = code_only(read_file(
      repository_root /
          "src/presentation/GameplayChromeCoverage.hpp"));
  const std::string raw_coverage_source = read_file(
      repository_root / "src/presentation/GameplayChromeCoverage.cpp");
  const std::string coverage_source = code_only(raw_coverage_source);
  const std::string coverage_test = code_only(read_file(
      repository_root / "src/tests/GameplayChromeCoverageTest.cpp"));
  const std::string window_source = code_only(read_file(
      repository_root / "src/WindowManager.cpp"));
  const std::string legacy_misc = code_only(read_file(
      repository_root / "src/realmz_orig/misc.c"));
  const std::string legacy_buttons = code_only(read_file(
      repository_root / "src/realmz_orig/buttonchoice.c"));
  const std::string legacy_controls = code_only(read_file(
      repository_root / "src/realmz_orig/updatecontrols.c"));
  const std::string legacy_combat_choice = code_only(read_file(
      repository_root /
          "src/realmz_orig/combatinfo-combatchoice.c"));
  const std::string legacy_combat_update = code_only(read_file(
      repository_root / "src/realmz_orig/combatupdate-2.c"));
  const std::string legacy_text = code_only(read_file(
      repository_root / "src/realmz_orig/textbox-time.c"));
  const std::string legacy_party_conditions = code_only(read_file(
      repository_root /
          "src/realmz_orig/tickcheck.c-updatetorch.c"));

  for (const auto value : {
           "exploration", "dungeon", "combat", "interaction",
           "essential_information", "retained_in_crop",
           "semantic_complete", "missing"}) {
    require(count_identifier(coverage_header, value) != 0,
        std::string("gameplay-chrome coverage type is missing ") + value);
  }
  require(count_identifier(
              coverage_header, "kGameplayChromeInventoryRevision") != 0 &&
          count_identifier(
              coverage_header, "canonical_role_set_matches") != 0,
      "gameplay-chrome coverage must expose a versioned exact-role-set gate");
  for (const auto function : {
           "gameplay_chrome_coverage_manifest",
           "gameplay_chrome_inventory_revision",
           "validate_gameplay_chrome_coverage",
           "assess_gameplay_chrome_coverage",
           "evaluate_gameplay_crop_readiness",
           "evaluate_current_gameplay_crop_readiness"}) {
    require(count_identifier(coverage_header, function) == 1 &&
            count_identifier(coverage_source, function) >= 1,
        std::string("gameplay-chrome coverage must declare and implement ") +
            function);
  }
  for (const auto forbidden : {
           "SDL_Renderer", "SDL_Texture", "UIAction",
           "LegacyCommandBridge", "dispatch_remastered_shell_control",
           "GetResource"}) {
    require(count_identifier(coverage_header, forbidden) == 0 &&
            count_identifier(coverage_source, forbidden) == 0,
        std::string("pure gameplay-chrome coverage must not depend on ") +
            forbidden);
  }
  require(coverage_header.find("SDL_") == std::string::npos &&
          coverage_source.find("SDL_") == std::string::npos,
      "pure gameplay-chrome coverage must not contain an SDL API path");

  for (const auto role : {
           "exploration.action.eight_direction_movement",
           "exploration.action.selected_item_drilldown",
           "exploration.action.contextual_shop_temple_encounter",
           "exploration.action.use_scroll",
           "exploration.action.use_torch",
           "exploration.info.narrative_messages",
           "exploration.info.calendar_clock",
           "exploration.info.fatigue",
           "exploration.info.pooled_money",
           "exploration.info.party_vitals",
           "exploration.info.party_condition_indicators",
           "dungeon.action.relative_movement",
           "dungeon.action.selected_item_drilldown",
           "dungeon.action.contextual_shop_temple_encounter",
           "dungeon.action.use_scroll",
           "dungeon.action.use_torch",
           "dungeon.info.narrative_messages",
           "dungeon.info.calendar_clock",
           "dungeon.info.fatigue",
           "dungeon.info.pooled_money",
           "dungeon.info.party_vitals",
           "dungeon.info.party_condition_indicators",
           "combat.action.guard",
           "combat.action.center_cursor",
           "combat.action.inspect_focused_combatant",
           "combat.action.inspect_party_member",
           "combat.action.inspect_items",
           "combat.action.inspect_conditions",
           "combat.action.inspect_attacks",
           "combat.action.turn_undead",
           "combat.action.party_auto_toggles",
           "combat.info.narrative_messages",
           "combat.info.inspected_combatant",
           "combat.info.conditions_and_attacks",
           "combat.info.round",
           "combat.info.enemies_remaining",
           "combat.info.party_vitals",
           "combat.info.party_condition_indicators"}) {
    require(raw_coverage_source.find(std::string("\"") + role + "\"") !=
            std::string::npos,
        std::string("canonical gameplay-chrome inventory is missing role ") +
            role);
  }

  const std::string assessment = function_body(
      coverage_source, "assess_gameplay_chrome_coverage");
  require(count_identifier(assessment, "canonical_role_set_matches") >= 2 &&
          count_identifier(assessment, "inventory_missing_count") >= 2 &&
          count_identifier(assessment, "complete") != 0,
      "coverage assessment must require the canonical role set and reject "
      "inventory-wide missing roles");
  require(count_identifier(coverage_source, "compute_inventory_revision") >= 3 &&
          count_identifier(coverage_source, "static_assert") != 0 &&
          coverage_header.find("0x97D7228BC94A5358ULL") !=
              std::string::npos,
      "gameplay-chrome inventory revision must be content-addressed and "
      "compile-time pinned");
  const std::string readiness = function_body(
      coverage_source, "evaluate_gameplay_crop_readiness");
  const std::string compact_readiness = without_whitespace(readiness);
  for (const auto prerequisite : {
           "expected_context_variant_known",
           "live_context_variant_known", "context_matches_surface",
           "context_variants_exact_match",
           "standard_context_variant_supported",
           "snapshot_matches_context",
           "shell_model_matches_snapshot_context",
           "gameplay_window_active", "front_is_gameplay_surface",
           "legacy_requires_full_frame", "legacy_full_frame_clear",
           "inventory_revision_matches", "snapshot_revision_nonzero",
           "snapshot_revision_matches_shell_model", "shell_model_valid",
           "shell_layout_valid", "shell_font_valid",
           "expected_controls_present", "live_handlers_present",
           "informational_surfaces_complete",
           "runtime_prerequisites_complete"}) {
    require(count_identifier(readiness, prerequisite) >= 2,
        std::string("crop readiness must fail closed on ") + prerequisite);
  }
  require(compact_readiness.contains(
              "result.ready=result.coverage.complete&&"
              "result.runtime_prerequisites_complete;"),
      "crop readiness must conjoin static coverage and runtime prerequisites");

  const std::string button_choice = function_body(
      legacy_buttons, "buttonchoice");
  for (const auto control : {
           "rest", "search", "torch", "swapbut", "campbut", "itemsbut",
           "tradebut", "barbut", "shopbut", "viewspellsbut",
           "castspellsbut", "overviewbut", "charmainbut", "showitembut",
           "showconditionbut"}) {
    require(count_identifier(button_choice, control) != 0,
        std::string("Classic world control census lost ") + control);
  }
  require(count_identifier(legacy_misc, "autoone") >= 3 &&
          count_identifier(legacy_misc, "GetNewControl") != 0,
      "Classic per-member Auto control creation/handling must remain in the "
      "coverage census");

  const std::string combat_choice = function_body(
      legacy_combat_choice, "combatchoice");
  for (const auto control : {
           "monsterbut", "showitems", "condition", "attacks", "turn",
           "combatitem", "melee", "viewspellsbut", "castspellsbut"}) {
    require(count_identifier(combat_choice, control) != 0,
        std::string("Classic combat control census lost ") + control);
  }
  const std::string update_controls = function_body(
      legacy_controls, "updatecontrols");
  require(count_identifier(update_controls, "undead") != 0 &&
          count_identifier(update_controls, "canpriestturn") != 0 &&
          count_identifier(update_controls, "hasturned") != 0,
      "Classic conditional Turn Undead visibility must remain in the census");
  const std::string combat_update = function_body(
      legacy_combat_update, "combatupdate2");
  require(count_identifier(combat_update, "lastshown") != 0,
      "Classic focused-combatant information must remain in the census");
  const std::string textbox = function_body(legacy_text, "textbox");
  require(count_identifier(textbox, "textrect") != 0,
      "Classic narrative message surface must remain in the census");
  const std::string update_torch = function_body(
      legacy_party_conditions, "updatetorch");
  require(count_identifier(update_torch, "partycondition") != 0 &&
          count_identifier(legacy_party_conditions, "partycondition") >= 3,
      "Classic torch and party-wide condition indicators must remain in the "
      "coverage census");

  for (const auto negative_gate : {
           "expected_variant", "live_variant", "gameplay_window_active",
           "front_is_gameplay_surface", "legacy_requires_full_frame",
           "legacy_full_frame_clear", "expected_inventory_revision",
           "snapshot_revision_nonzero", "shell_model_revision",
           "shell_model_context", "shell_model_valid",
           "shell_layout_valid", "shell_font_valid",
           "expected_controls_present", "live_handlers_present",
           "informational_surfaces_complete"}) {
    require(count_identifier(coverage_test, negative_gate) >= 2,
        std::string("coverage tests must isolate the negative gate ") +
            negative_gate);
  }
  require(count_identifier(coverage_test, "kExpectedManifestRows") >= 3 &&
          coverage_test.find("kExpectedManifestRows.size() == 95U") !=
              std::string::npos &&
          coverage_test.find("first.size() == 95U") != std::string::npos &&
          coverage_test.find("0x97D7228BC94A5358ULL") !=
              std::string::npos &&
          count_identifier(coverage_test,
              "test_inventory_revision_covers_every_ordered_manifest_field") >=
              2 &&
          count_identifier(coverage_test,
              "test_manifest_source_anchors_resolve") >= 2,
      "coverage tests must freeze the complete reviewed manifest, its "
      "content revision, and source anchors");

  const std::string present = function_body(
      window_source, "present_remastered_frame");
  const std::string compact_present = without_whitespace(present);
  require(count_identifier(present, "semantic_controls_ready") == 1 &&
          compact_present.contains(".semantic_controls_ready=false,"),
      "the coverage-contract milestone must keep the complete Classic frame "
      "and must not enable cropping");
}

void verify_remastered_runtime_asset_integration(
    const fs::path& repository_root) {
  const std::string raw_source = read_file(
      repository_root / "src/WindowManager.cpp");
  const std::string source = code_only(raw_source);
  const std::string raw_window_header = read_file(
      repository_root / "src/WindowManager.hpp");
  const std::string window_header = code_only(raw_window_header);
  const std::string raw_cache_source = read_file(repository_root /
      "src/remaster/assets/ShellMaterialTextureCache.cpp");
  const std::string cache_source = code_only(raw_cache_source);
  const std::string compact_cache_source = without_whitespace(cache_source);
  const std::string catalog_source = code_only(read_file(
      repository_root / "src/remaster/assets/ShellMaterialCatalog.cpp"));
  const std::string raw_portrait_catalog_source = read_file(repository_root /
      "src/remaster/assets/PartyPortraitCatalog.cpp");
  const std::string portrait_catalog_source =
      code_only(raw_portrait_catalog_source);
  const std::string raw_portrait_catalog_header = read_file(repository_root /
      "src/remaster/assets/PartyPortraitCatalog.hpp");
  const std::string portrait_catalog_header =
      code_only(raw_portrait_catalog_header);
  const std::string raw_portrait_cache_source = read_file(repository_root /
      "src/remaster/assets/PartyPortraitTextureCache.cpp");
  const std::string portrait_cache_source =
      code_only(raw_portrait_cache_source);
  const std::string portrait_cache_header = code_only(read_file(
      repository_root / "src/remaster/assets/PartyPortraitTextureCache.hpp"));
  const std::string compact_portrait_cache_source =
      without_whitespace(portrait_cache_source);
  const std::string raw_verified_source = read_file(repository_root /
      "src/remaster/assets/VerifiedRasterSurface.cpp");
  const std::string verified_source = code_only(raw_verified_source);
  const std::string raw_quickdraw_source = read_file(
      repository_root / "src/QuickDraw.cpp");
  const std::string quickdraw_source = code_only(raw_quickdraw_source);
  const std::string raw_tutorial_source = read_file(repository_root /
      "src/remaster/assets/TutorialTitleCompositor.cpp");
  const std::string tutorial_source = code_only(raw_tutorial_source);
  const std::string raw_tutorial_header = read_file(repository_root /
      "src/remaster/assets/TutorialTitleCompositor.hpp");
  const std::string raw_resource_manager_source = read_file(
      repository_root / "src/ResourceManager.cpp");
  const std::string resource_manager_source =
      code_only(raw_resource_manager_source);
  const std::string shell_ensure = function_body(
      source, "ensure_remastered_shell_materials");
  const std::string compact_shell_ensure =
      without_whitespace(shell_ensure);
  const std::string portrait_ensure = function_body(
      source, "ensure_remastered_party_portraits");
  const std::string compact_portrait_ensure =
      without_whitespace(portrait_ensure);
  const std::string classic = function_body(source, "present_classic_frame");
  const std::string remastered = function_body(
      source, "present_remastered_frame");
  const std::string compact_remastered = without_whitespace(remastered);

  const std::size_t attempted = compact_shell_ensure.find(
      "this->remastered_shell_materials_attempted=true;");
  const std::size_t catalog = compact_shell_ensure.find(
      "realmz::remaster::assets::ShellMaterialCatalog::load(", attempted);
  const std::size_t publish = compact_shell_ensure.find(
      "this->remastered_shell_materials="
      "std::make_unique<ShellMaterialTextureCache>(renderer,catalog);",
      catalog);
  const std::size_t catch_reset = compact_shell_ensure.find(
      "this->remastered_shell_materials.reset();", publish);
  require(attempted != std::string::npos && catalog != std::string::npos &&
          publish != std::string::npos && catch_reset != std::string::npos &&
          attempted < catalog && catalog < publish && publish < catch_reset,
      "native shell materials must be loaded and published atomically, with "
      "the whole cache cleared on failure");

  const std::size_t portrait_attempted = compact_portrait_ensure.find(
      "this->remastered_party_portraits_attempted=true;");
  const std::size_t portrait_catalog = compact_portrait_ensure.find(
      "realmz::remaster::assets::PartyPortraitCatalog::load(",
      portrait_attempted);
  const std::size_t portrait_publish = compact_portrait_ensure.find(
      "this->remastered_party_portraits="
      "std::make_unique<PartyPortraitTextureCache>("
      "renderer,std::move(catalog));",
      portrait_catalog);
  const std::size_t portrait_catch_reset = compact_portrait_ensure.find(
      "this->remastered_party_portraits.reset();", portrait_publish);
  require(portrait_attempted != std::string::npos &&
          portrait_catalog != std::string::npos &&
          portrait_publish != std::string::npos &&
          portrait_catch_reset != std::string::npos &&
          portrait_attempted < portrait_catalog &&
          portrait_catalog < portrait_publish &&
          portrait_publish < portrait_catch_reset,
      "native party portraits must load and publish atomically, with the "
      "whole proof-bound cache cleared on failure");
  require(count_identifier(shell_ensure,
              "ensure_remastered_party_portraits") == 0 &&
          count_identifier(portrait_ensure,
              "ensure_remastered_shell_materials") == 0,
      "shell-material and party-portrait realization must remain two "
      "separate public runtime asset ensures");
  require(count_text(raw_source,
              "root / \"phase1.runtime-manifest.json\"") == 2 &&
          count_text(raw_source, "root / \"phase1.census.json\"") == 2 &&
          count_text(raw_source,
              "host_path_for_mac_filename(\":Remastered\", false)") == 2 &&
          count_identifier(shell_ensure,
              "host_filename_for_mac_filename") == 0 &&
          count_identifier(portrait_ensure,
              "host_filename_for_mac_filename") == 0,
      "both native runtime asset ensures must independently use only the "
      "bundled public Remastered manifest, census, and native asset root");
  for (const auto forbidden : {
           "ResourceSelectionHook",
           "resourceAssetSelectionForHandle",
           "PayloadDigest",
           "classicPayloadSha256",
           "GetResource",
       }) {
    require(count_identifier(shell_ensure, forbidden) == 0,
        std::string("native shell material loader must not use private/") +
            "Classic resource API " + forbidden);
    require(count_identifier(cache_source, forbidden) == 0,
        std::string("native shell material texture cache must not use private/") +
            "Classic resource API " + forbidden);
    require(count_identifier(catalog_source, forbidden) == 0,
        std::string("native shell material catalog must not use private/") +
            "Classic resource API " + forbidden);
  }

  for (const auto forbidden : {
           "ResourceSelectionHook",
           "resourceAssetSelectionForHandle",
           "resourceAssetSelectionForCurrentWinner",
           "PayloadDigest",
           "GetResource",
       }) {
    require(count_identifier(portrait_ensure, forbidden) == 0,
        std::string("native portrait loader must not use private/Classic ") +
            "resource API " + forbidden);
    require(count_identifier(portrait_cache_source, forbidden) == 0,
        std::string("portrait texture cache must not bypass its proof via ") +
            forbidden);
    require(count_identifier(portrait_catalog_source, forbidden) == 0,
        std::string("portrait catalog must not select a Classic winner via ") +
            forbidden);
  }

  require(count_identifier(classic, "ensure_remastered_shell_materials") == 0 &&
          count_identifier(classic,
              "ensure_remastered_party_portraits") == 0 &&
          count_identifier(remastered,
              "ensure_remastered_shell_materials") == 1 &&
          count_identifier(remastered,
              "ensure_remastered_party_portraits") == 1,
      "Classic presentation must never initiate either public native asset "
      "loader; Remastered presentation must request each cache once per frame");
  const std::size_t ensure_shell_in_frame = compact_remastered.find(
      "this->ensure_remastered_shell_materials(renderer);");
  const std::size_t ensure_portraits_in_frame = compact_remastered.find(
      "this->ensure_remastered_party_portraits(renderer);",
      ensure_shell_in_frame);
  require(ensure_shell_in_frame != std::string::npos &&
          ensure_portraits_in_frame != std::string::npos &&
          ensure_shell_in_frame < ensure_portraits_in_frame,
      "the Remastered frame must retain separate shell and portrait cache "
      "handles before interpreting draw commands");
  const std::size_t captured_pointer = compact_remastered.find(
      "if(constauto&captured=this->remastered_pressed_shell_control)");
  const std::size_t live_pointer = compact_remastered.find(
      "returncandidate.enabled&&", captured_pointer);
  const std::size_t published_pointer_visual = compact_remastered.find(
      "pressed_control=control->region;", live_pointer);
  require(captured_pointer != std::string::npos &&
          live_pointer != std::string::npos &&
          published_pointer_visual != std::string::npos &&
          captured_pointer < live_pointer &&
          live_pointer < published_pointer_visual,
      "a captured pointer press must match a currently enabled live control "
      "before it can render pressed state");
  const std::size_t local_records = compact_cache_source.find(
      "Recordsloaded;");
  const std::size_t tiled_texture = compact_cache_source.find(
      "SDL_RenderTextureTiled(", local_records);
  const std::size_t publish_records = compact_cache_source.find(
      "this->records_=std::move(loaded);", local_records);
  require(local_records != std::string::npos &&
          tiled_texture != std::string::npos &&
          publish_records != std::string::npos &&
          local_records < publish_records &&
          count_identifier(cache_source, "SDL_RenderTextureTiled") == 1,
      "shell textures must build as a complete local set and render through "
      "SDL's tiled-texture path");

  const std::string verified_load = function_body(
      verified_source, "loadVerifiedRasterSurface");
  const std::string compact_verified_load =
      without_whitespace(verified_load);
  const std::string bounded_read = function_body(
      verified_source, "loadBoundedBytes");
  const std::string png_preflight = function_body(
      verified_source, "preflightPng");
  const std::size_t bounded_bytes = compact_verified_load.find(
      "constautoencoded=loadBoundedBytes(path,key);");
  const std::size_t digest_check = compact_verified_load.find(
      "assetContentSha256Hex(encoded)!=expectedSha256", bounded_bytes);
  const std::size_t preflight = compact_verified_load.find(
      "constautodimensions=preflightPng(encoded,key);", digest_check);
  const std::size_t exact_stream = compact_verified_load.find(
      "SDL_IOFromConstMem(encoded.data(),encoded.size())", preflight);
  const std::size_t typed_decode = compact_verified_load.find(
      "IMG_LoadTyped_IO(stream,true", exact_stream);
  const std::size_t decoded_dimensions = compact_verified_load.find(
      "static_cast<std::uint32_t>(surface->w)!=dimensions.width",
      typed_decode);
  require(bounded_bytes != std::string::npos &&
          digest_check != std::string::npos &&
          preflight != std::string::npos &&
          exact_stream != std::string::npos &&
          typed_decode != std::string::npos &&
          decoded_dimensions != std::string::npos &&
          bounded_bytes < digest_check && digest_check < preflight &&
          preflight < exact_stream && exact_stream < typed_decode &&
          typed_decode < decoded_dimensions,
      "the shared raster loader must bound and snapshot native bytes, verify "
      "their digest and PNG header, decode that exact buffer as PNG, then "
      "cross-check decoded dimensions");
  require(count_identifier(bounded_read, "ifstream") == 1 &&
          count_identifier(bounded_read,
              "kMaximumVerifiedRasterEncodedBytes") == 1 &&
          count_identifier(bounded_read, "peek") == 1 &&
          count_identifier(png_preflight, "kPngSignature") == 2 &&
          count_identifier(png_preflight, "kMaximumVerifiedRasterDimension") ==
              2 &&
          count_identifier(png_preflight, "kMaximumVerifiedRasterPixels") == 1 &&
          count_identifier(verified_load, "isLowercaseSha256") == 1 &&
          count_identifier(verified_load, "assetContentSha256Hex") == 1 &&
          count_identifier(verified_load, "SDL_IOFromConstMem") == 1 &&
          count_identifier(verified_load, "IMG_LoadTyped_IO") == 1 &&
          count_text(raw_verified_source, "\"PNG\"") == 1,
      "shared raster verification must retain its size, digest-shape, PNG, "
      "pixel-count, exact-memory, and typed-decoder defenses");
  require(count_identifier(verified_source, "SDL_GetError") == 0 &&
          count_text(raw_verified_source, ".string()") == 0 &&
          count_identifier(verified_source, "IMG_Load") == 0 &&
          count_identifier(verified_source, "IMG_Load_IO") == 0 &&
          count_identifier(verified_source, "GetResource") == 0 &&
          count_identifier(verified_source, "ResourceSelectionHook") == 0 &&
          count_identifier(verified_source, "resourceAssetSelectionForHandle") ==
              0,
      "shared raster verification must not leak decoder/path details or "
      "acquire private Classic resources");

  for (const auto consumer : std::array{
           std::string_view(cache_source),
           std::string_view(portrait_cache_source),
       }) {
    require(count_identifier(consumer, "loadVerifiedRasterSurface") == 1 &&
            count_identifier(consumer, "assetContentSha256Hex") == 0 &&
            count_identifier(consumer, "SDL_IOFromConstMem") == 0 &&
            count_identifier(consumer, "IMG_LoadTyped_IO") == 0 &&
            count_identifier(consumer, "IMG_Load_IO") == 0 &&
            count_identifier(consumer, "IMG_Load") == 0,
        "every native texture cache must delegate exactly once to the shared "
        "verified raster surface boundary without a decoder bypass");
  }
  require(count_text(raw_cache_source, "material.path.string()") == 0 &&
          count_text(raw_portrait_cache_source, "portrait.path.string()") == 0 &&
          count_identifier(cache_source, "SDL_GetError") == 0 &&
          count_identifier(portrait_cache_source, "SDL_GetError") == 0,
      "native texture caches must report stable resource identities without "
      "absolute paths or backend diagnostics");
  require(count_identifier(shell_ensure, "what") == 0 &&
          count_identifier(portrait_ensure, "what") == 0 &&
          count_text(raw_source,
              "Could not validate and realize Remastered shell materials; ") ==
              1 &&
          count_text(raw_source, "using flat colors") == 1 &&
          count_text(raw_source,
              "Could not validate and realize Remastered party portraits; ") ==
              1 &&
          count_text(raw_source, "using code-native monograms") == 1,
      "public runtime asset fallback diagnostics must not expose absolute "
      "asset paths, user paths, or exception details");

  const std::string panel_draw = function_body(
      source, "draw_shell_panel_contents");
  require(count_identifier(source, "draw_shell_material_or_color") == 5 &&
          count_identifier(panel_draw, "draw_shell_material_or_color") == 3 &&
          count_identifier(remastered, "draw_shell_material_or_color") == 1 &&
          count_identifier(remastered, "draw_shell_panel_contents") == 1,
      "one shared material-or-flat path must cover panels, party cards, "
      "action controls, and drawer tabs");
  for (const auto state : {
           "panel", "normal", "selected", "pressed", "inactive"}) {
    require(count_text(source,
                "ShellSurfaceState::" + std::string(state)) != 0,
        std::string("native shell material integration is missing state ") +
            state);
  }
  require(count_text(raw_source, "SDL_Color{240, 227, 190, 144}") == 1 &&
          count_text(raw_source, "SDL_Color{12, 13, 17, 186}") == 1 &&
          count_text(raw_source,
              "kSelectedMaterialInk{22, 24, 35, 255}") == 1,
      "hash-pinned material contrast scrims and selected ink changed without "
      "updating their exhaustive pixel contract");

  const std::string catalog_load = function_body(catalog_source, "load");
  const std::string compact_catalog_load = without_whitespace(catalog_load);
  require(count_identifier(catalog_load, "resolve") == 1 &&
          compact_catalog_load.find(
              "resolver.resolve(presentation::PresentationMode::remastered,key)") !=
              std::string::npos,
      "native shell catalog must use only key-based two-argument Remastered "
      "asset resolution");

  const std::string approved_image = function_body(
      quickdraw_source, "approved_image_for_resource");
  const std::string compact_approved_image =
      without_whitespace(approved_image);
  const std::size_t override_gate = compact_approved_image.find(
      "selection->resolution.kind!=AssetResolutionKind::Override");
  const std::size_t approved_digest = compact_approved_image.find(
      "!resolution.approvedContentSha256", override_gate);
  const std::size_t verified_decode = compact_approved_image.find(
      "loadVerifiedRasterSurface(", approved_digest);
  const std::size_t logical_scale = compact_approved_image.find(
      "SDL_ScaleSurface(", verified_decode);
  const std::size_t argb_conversion = compact_approved_image.find(
      "SDL_ConvertSurface(", logical_scale);
  const std::size_t tutorial_gate = compact_approved_image.find(
      "tutorialTitleEligible(", argb_conversion);
  const std::size_t tutorial_compose = compact_approved_image.find(
      "composeTutorialTitle(", tutorial_gate);
  const std::size_t final_image = compact_approved_image.find(
      "image_for_sdl_surface(converted.get())", tutorial_compose);
  require(override_gate != std::string::npos &&
          approved_digest != std::string::npos &&
          verified_decode != std::string::npos &&
          logical_scale != std::string::npos &&
          argb_conversion != std::string::npos &&
          tutorial_gate != std::string::npos &&
          tutorial_compose != std::string::npos &&
          final_image != std::string::npos &&
          override_gate < approved_digest &&
          approved_digest < verified_decode &&
          verified_decode < logical_scale && logical_scale < argb_conversion &&
          argb_conversion < tutorial_gate &&
          tutorial_gate < tutorial_compose && tutorial_compose < final_image,
      "QuickDraw must require a selected approved override and verified "
      "digest, scale and convert its exact bytes, then gate and compose only "
      "the Tutorial title before publishing pixels");
  require(count_identifier(approved_image, "loadVerifiedRasterSurface") == 1 &&
          count_identifier(approved_image, "assetContentSha256Hex") == 0 &&
          count_identifier(approved_image, "SDL_IOFromConstMem") == 0 &&
          count_identifier(approved_image, "IMG_LoadTyped_IO") == 0 &&
          count_identifier(approved_image, "IMG_Load_IO") == 0 &&
          count_identifier(approved_image, "IMG_Load") == 0 &&
          count_identifier(approved_image, "SDL_GetError") == 0 &&
          count_text(raw_quickdraw_source,
              "resolution.overridePath->string()") == 0,
      "QuickDraw must use the shared verified raster boundary without a "
      "direct decoder, digest, backend-error, or host-path bypass");

  const std::size_t font_ready = compact_approved_image.find(
      "TTF_WasInit()", tutorial_gate);
  const std::size_t chancery_font = compact_approved_image.find(
      "load_font(BLACK_CHANCERY_FONT_ID)", font_ready);
  const std::size_t remember_font_size = compact_approved_image.find(
      "TTF_GetFontSize(*titleFont)", chancery_font);
  const std::size_t configure_font = compact_approved_image.find(
      "TTF_SetFontSize(*titleFont,kTutorialTitlePointSize)",
      remember_font_size);
  const std::size_t bold_font = compact_approved_image.find(
      "previousStyle|TTF_STYLE_BOLD", configure_font);
  const std::size_t restore_font_size = compact_approved_image.find(
      "TTF_SetFontSize(*titleFont,previousPointSize)", tutorial_compose);
  const std::size_t restore_font_style = compact_approved_image.find(
      "TTF_SetFontStyle(*titleFont,previousStyle)", restore_font_size);
  const std::size_t reject_failed_title = compact_approved_image.find(
      "if(!titled||!restored)", restore_font_style);
  require(font_ready != std::string::npos &&
          chancery_font != std::string::npos &&
          remember_font_size != std::string::npos &&
          configure_font != std::string::npos &&
          bold_font != std::string::npos &&
          restore_font_size != std::string::npos &&
          restore_font_style != std::string::npos &&
          reject_failed_title != std::string::npos &&
          tutorial_gate < font_ready && font_ready < chancery_font &&
          chancery_font < remember_font_size &&
          remember_font_size < configure_font && configure_font < bold_font &&
          bold_font < tutorial_compose && tutorial_compose < restore_font_size &&
          restore_font_size < restore_font_style &&
          restore_font_style < reject_failed_title,
      "Tutorial typography must use initialized bundled Black Chancery, "
      "temporarily configure the reviewed title face, restore shared font "
      "state, and fail back to Classic if composition or restoration fails");
  require(count_identifier(approved_image,
              "VerifiedRasterSurfaceError") == 1 &&
          count_identifier(approved_image,
              "verifiedRasterSurfacePhaseName") == 1 &&
          count_identifier(approved_image, "exception") == 1,
      "QuickDraw fallback must distinguish a stable verified-raster phase "
      "from a path-free generic failure");

  const std::string tutorial_eligibility = function_body(
      tutorial_source, "tutorialTitleEligible");
  const std::string tutorial_compositor = function_body(
      tutorial_source, "composeTutorialTitle");
  const std::string compact_tutorial_compositor =
      without_whitespace(tutorial_compositor);
  require(count_text(raw_tutorial_source,
              "\"Scenarios/Tutorial/Scenario\"") == 1 &&
          count_text(raw_tutorial_source, "\"PICT\"") == 1 &&
          count_text(raw_tutorial_source, "32128") == 1 &&
          count_text(raw_tutorial_header,
              "b3ad37374ae9b92a0bf745627c4db0d5022b53e16fa9d106bc4b5b4ecc032bba") ==
              1 &&
          count_identifier(tutorial_eligibility,
              "kTutorialTitleApprovedOutputSha256") == 1 &&
          count_identifier(tutorial_eligibility,
              "kTutorialTitleLogicalDimensions") == 1,
      "Tutorial title authorization must remain bound to only the reviewed "
      "Tutorial PICT key, approved output digest, and logical dimensions");
  const std::size_t compositor_gate = compact_tutorial_compositor.find(
      "tutorialTitleEligible(");
  const std::size_t duplicate_source = compact_tutorial_compositor.find(
      "SDL_DuplicateSurface(source)", compositor_gate);
  const std::size_t render_title = compact_tutorial_compositor.find(
      "TTF_RenderText_Blended(", duplicate_source);
  const std::size_t blit_title = compact_tutorial_compositor.find(
      "SDL_BlitSurface(title.get(),nullptr,result.get(),&destination)",
      render_title);
  require(compositor_gate != std::string::npos &&
          duplicate_source != std::string::npos &&
          render_title != std::string::npos &&
          blit_title != std::string::npos &&
          compositor_gate < duplicate_source &&
          duplicate_source < render_title && render_title < blit_title &&
          count_identifier(tutorial_compositor, "SDL_DuplicateSurface") == 1 &&
          count_identifier(tutorial_compositor, "SDL_BlitSurface") == 1,
      "Tutorial composition must authorize first and render only onto one "
      "independent copy, discarding every partial failure");

  const std::string portrait_catalog_load = function_body(
      portrait_catalog_source, "load");
  const std::string portrait_authorization = function_body(
      portrait_catalog_source, "authorizeSelectedPortrait");
  const std::string compact_portrait_authorization =
      without_whitespace(portrait_authorization);
  require(count_text(raw_portrait_catalog_source,
              "\"Data Files/Portraits\"") == 1 &&
          count_text(raw_portrait_catalog_source, "\"cicn\"") == 1 &&
          count_text(raw_portrait_catalog_source,
              "{257, 267, 297, 337}") == 1 &&
          count_text(raw_portrait_catalog_source,
              "kPortraitDimensions{44U, 44U}") == 1 &&
          count_text(raw_portrait_catalog_source, "\"portrait\"") == 1 &&
          count_text(raw_portrait_catalog_source, "\"original_mask\"") == 1 &&
          count_identifier(portrait_catalog_load, "classicPayloadSha256") >= 2 &&
          count_identifier(portrait_catalog_load,
              "approvedContentSha256") >= 3 &&
          count_identifier(portrait_catalog_load, "overridePath") >= 2 &&
          count_identifier(portrait_catalog_load, "masterKey") >= 2 &&
          count_identifier(portrait_catalog_load, "logicalDimensions") >= 3,
      "the public portrait catalog must pin its four exact keys, dimensions, "
      "semantic/alpha policy, and distinct Classic/output/path proofs");
  require(count_identifier(portrait_catalog_header,
              "authorizeSelectedPortrait") == 1 &&
          count_identifier(portrait_catalog_header,
              "PostSelectionResult") == 2 &&
          count_identifier(portrait_catalog_header, "find") == 0 &&
          count_identifier(portrait_catalog_header, "int16_t") == 0 &&
          count_identifier(portrait_cache_header, "draw") == 1 &&
          count_identifier(portrait_cache_header,
              "PostSelectionResult") == 1 &&
          count_identifier(portrait_cache_header, "int16_t") == 0 &&
          compact_portrait_authorization.contains(
              "selection.resolution.kind!=AssetResolutionKind::Override") &&
          count_identifier(portrait_authorization, "selection") >= 10 &&
          count_identifier(portrait_authorization, "immutablePayloadSha256") ==
              1 &&
          count_identifier(portrait_authorization, "overridePath") == 2 &&
          count_identifier(portrait_authorization, "masterKey") == 2 &&
          count_identifier(portrait_authorization, "logicalDimensions") == 3 &&
          count_identifier(portrait_authorization,
              "approvedContentSha256") == 3,
      "portrait catalog/cache APIs must expose only the complete live "
      "PostSelectionResult proof, no ID lookup, and compare every selected "
      "winner field");

  const std::string portrait_draw = function_body(
      portrait_cache_source, "draw");
  const std::string compact_portrait_draw =
      without_whitespace(portrait_draw);
  const std::size_t portrait_local_records =
      compact_portrait_cache_source.find("Recordsloaded;");
  const std::size_t portrait_verified_surface =
      compact_portrait_cache_source.find(
          "loadVerifiedRasterSurface(", portrait_local_records);
  const std::size_t portrait_publish_records =
      compact_portrait_cache_source.find(
          "this->records_=std::move(loaded);", portrait_verified_surface);
  require(portrait_local_records != std::string::npos &&
          portrait_verified_surface != std::string::npos &&
          portrait_publish_records != std::string::npos &&
          portrait_local_records < portrait_verified_surface &&
          portrait_verified_surface < portrait_publish_records &&
          count_identifier(portrait_cache_source,
              "loadVerifiedRasterSurface") == 1 &&
          count_identifier(portrait_cache_source,
              "SDL_CreateTextureFromSurface") == 1 &&
          count_identifier(portrait_cache_source,
              "SDL_SetTextureBlendMode") == 1,
      "portrait textures must verify and configure a complete local set "
      "before atomically publishing renderer-owned records");
  require(compact_portrait_draw.contains(
              "this->catalog_.authorizeSelectedPortrait(selection)") &&
          compact_portrait_draw.contains(
              "record.key==portrait->key") &&
          count_identifier(portrait_draw, "SDL_RenderTexture") == 1 &&
          count_identifier(portrait_draw, "resourceAssetSelectionForCurrentWinner") ==
              0 &&
          count_identifier(portrait_draw, "GetResource") == 0,
      "portrait drawing must accept a caller's live proof, authorize it in "
      "the catalog, then select a texture by the authorized exact key only");

  const std::string party_panel_draw = function_body(
      source, "draw_shell_panel_contents");
  const std::string compact_party_panel_draw =
      without_whitespace(party_panel_draw);
  const std::size_t current_winner = compact_party_panel_draw.find(
      "resourceAssetSelectionForCurrentWinner(");
  const std::size_t proof_draw = compact_party_panel_draw.find(
      "portraits->draw(renderer,*selection,portrait_rect)", current_winner);
  const std::size_t monogram_fallback = compact_party_panel_draw.find(
      "if(!portrait_drawn){draw_shell_portrait_monogram(", proof_draw);
  require(current_winner != std::string::npos &&
          proof_draw != std::string::npos &&
          monogram_fallback != std::string::npos &&
          current_winner < proof_draw && proof_draw < monogram_fallback &&
          count_identifier(party_panel_draw,
              "resourceAssetSelectionForCurrentWinner") == 1 &&
          count_identifier(party_panel_draw, "GetResource") == 0 &&
          count_identifier(party_panel_draw,
              "resourceAssetSelectionForHandle") == 0,
      "each party card must re-run Resource Manager winner selection "
      "immediately before proof-bound drawing and otherwise use its native "
      "monogram fallback");

  const std::string current_winner_selection = function_body(
      resource_manager_source, "resourceAssetSelectionForCurrentWinner");
  const std::string compact_current_winner_selection =
      without_whitespace(current_winner_selection);
  const std::string quiet_resource_lookup = function_body(
      resource_manager_source, "find_resource");
  const std::string compact_quiet_resource_lookup =
      without_whitespace(quiet_resource_lookup);
  const std::string inspect_selected = function_body(
      resource_manager_source, "inspect_selected_resource");
  const std::string compact_inspect_selected =
      without_whitespace(inspect_selected);
  const std::size_t quiet_winner = compact_current_winner_selection.find(
      "constautoselected=rm.find_resource(type,id);");
  const std::size_t absent_winner = compact_current_winner_selection.find(
      "if(!selected){returnstd::nullopt;}", quiet_winner);
  const std::size_t inspect_winner = compact_current_winner_selection.find(
      "returnrm.inspect_selected_resource(selected);", absent_winner);
  require(quiet_winner != std::string::npos &&
          absent_winner != std::string::npos &&
          inspect_winner != std::string::npos &&
          quiet_winner < absent_winner && absent_winner < inspect_winner &&
          count_identifier(current_winner_selection, "Handle") == 0 &&
          count_identifier(current_winner_selection, "get_resource") == 0 &&
          count_identifier(current_winner_selection, "print_chain") == 0 &&
          count_identifier(current_winner_selection, "rm_log") == 0 &&
          count_identifier(current_winner_selection, "host_filename") == 0 &&
          count_identifier(current_winner_selection, "throw") == 0 &&
          compact_quiet_resource_lookup.contains(
              "for(size_tz=this->search_start_index;z<this->files.size();z++)"
              "{autores=this->files[z]->get_resource(type,id);"
              "if(res!=nullptr){returnres;}}") &&
          count_identifier(quiet_resource_lookup, "print_chain") == 0 &&
          count_identifier(quiet_resource_lookup, "rm_log") == 0 &&
          count_identifier(quiet_resource_lookup, "host_filename") == 0 &&
          count_identifier(quiet_resource_lookup, "throw") == 0,
      "native portrait proof must quietly re-run unchanged legacy resource "
      "search precedence, return no proof for a missing custom ID, and never "
      "leak the open resource-file chain or host paths");
  const std::size_t classic_bypass = compact_inspect_selected.find(
      "this->presentation_mode=="
      "realmz::presentation::PresentationMode::classic");
  const std::size_t writable_rejection = compact_inspect_selected.find(
      "if(res->data_modified)", classic_bypass);
  const std::size_t cached_source_proof = compact_inspect_selected.find(
      "res->asset_selection_source==res->source_res.get()",
      writable_rejection);
  const std::size_t cached_failure = compact_inspect_selected.find(
      "!res->asset_selection->resolution.covered()", cached_source_proof);
  const std::size_t cached_diagnostic = compact_inspect_selected.find(
      "this->last_asset_diagnostic="
      "res->asset_selection->resolution.diagnostic;",
      cached_failure);
  const std::size_t immutable_inspection = compact_inspect_selected.find(
      "hook.inspect(*file->logical_pack,res->source_res->type,"
      "res->source_res->id,res->source_res->data)", cached_diagnostic);
  require(classic_bypass != std::string::npos &&
          writable_rejection != std::string::npos &&
          cached_source_proof != std::string::npos &&
          cached_failure != std::string::npos &&
          cached_diagnostic != std::string::npos &&
          immutable_inspection != std::string::npos &&
          classic_bypass < writable_rejection &&
          writable_rejection < cached_source_proof &&
          cached_source_proof < cached_failure &&
          cached_failure < cached_diagnostic &&
          cached_diagnostic < immutable_inspection &&
          count_identifier(inspect_selected, "data_handle") == 0,
      "winner proof must preserve zero-cost Classic bypass, reject pending "
      "writable changes, bind cached proof to its immutable source object, "
      "restore cached failure diagnostics after a mode cycle, and inspect "
      "immutable fork bytes only");
  const std::string mark_modified = function_body(
      resource_manager_source, "mark_modified");
  const std::string write_resources = function_body(
      resource_manager_source, "write");
  require(count_identifier(mark_modified, "asset_selection") == 1 &&
          count_identifier(mark_modified, "asset_selection_source") == 1 &&
          count_identifier(write_resources, "asset_selection") == 1 &&
          count_identifier(write_resources, "asset_selection_source") == 1,
      "writable resource mutation and publication must invalidate both the "
      "cached selection proof and its source-identity guard");

  const std::string file_manager_source = code_only(read_file(
      repository_root / "src/FileManager.cpp"));
  const std::string native_host_path = function_body(
      file_manager_source, "host_path_for_mac_filename");
  const std::string legacy_host_string = function_body(
      file_manager_source, "host_filename_for_mac_filename");
  require(count_identifier(native_host_path, "SDL_GetBasePath") == 1 &&
          count_identifier(native_host_path, "path_from_utf8") == 1 &&
          count_identifier(native_host_path,
              "safe_relative_path_for_classic_path") == 1 &&
          count_identifier(legacy_host_string,
              "host_path_for_mac_filename") == 1,
      "bundled paths must convert SDL UTF-8 directly to native filesystem "
      "paths before joining validated Classic-relative components");
  require(count_text(raw_resource_manager_source,
              "host_path_for_mac_filename(\":Remastered\", false)") == 1 &&
          count_text(raw_resource_manager_source,
              "host_filename_for_mac_filename(\":Remastered\", false)") == 0,
      "the proof-producing Resource Manager must preserve the SDL UTF-8 base "
      "path as a native filesystem path");

  const std::string cmake_source = read_file(
      repository_root / "CMakeLists.txt");
  const std::size_t windows_install_begin = cmake_source.find(
      "elseif(WIN32)\n    install(TARGETS Realmz");
  const std::size_t windows_dependencies = cmake_source.find(
      "# CMake's RUNTIME_DEPENDENCIES", windows_install_begin);
  require(windows_install_begin != std::string::npos &&
          windows_dependencies != std::string::npos &&
          windows_install_begin < windows_dependencies,
      "could not isolate the Windows package resource-install contract");
  const std::string windows_install = cmake_source.substr(
      windows_install_begin, windows_dependencies - windows_install_begin);
  for (const auto required : {
           "phase1.placeholder-manifest.json",
           "phase1.runtime-manifest.json",
           "phase1.census.json",
           "phase1.scope.json",
           "assets/remastered/style-proof/generation/outputs/",
           "${CMAKE_INSTALL_BINDIR}/Remastered/style-proof/generation/outputs",
       }) {
    require(count_text(windows_install, required) != 0,
        std::string("Windows package omits native Remastered runtime input ") +
            required);
  }

  const std::string create_window = function_body(source, "create_sdl_window");
  const std::string compact_create_window = without_whitespace(create_window);
  const std::size_t invalidate_shell_before_window = compact_create_window.find(
      "this->invalidate_remastered_shell_materials();");
  const std::size_t invalidate_portraits_before_window =
      compact_create_window.find(
          "this->invalidate_remastered_party_portraits();",
          invalidate_shell_before_window);
  const std::size_t replace_window = compact_create_window.find(
      "this->sdl_window=sdl_make_shared(SDL_CreateWindow(",
      invalidate_portraits_before_window);
  require(invalidate_shell_before_window != std::string::npos &&
          invalidate_portraits_before_window != std::string::npos &&
          replace_window != std::string::npos &&
          invalidate_shell_before_window < invalidate_portraits_before_window &&
          invalidate_portraits_before_window < replace_window,
      "all renderer-owned public textures must be destroyed before replacing "
      "the SDL window and its associated renderer");

  const std::size_t window_member = find_identifier(
      window_header, "sdl_window");
  const std::size_t shell_cache_member = find_identifier(
      window_header, "remastered_shell_materials", window_member);
  const std::size_t portrait_cache_member = find_identifier(
      window_header, "remastered_party_portraits", shell_cache_member);
  require(window_member != std::string::npos &&
          shell_cache_member != std::string::npos &&
          portrait_cache_member != std::string::npos &&
          window_member < shell_cache_member &&
          window_member < portrait_cache_member,
      "renderer-owned caches must remain declared after the SDL window so "
      "C++ destruction order releases their textures before the renderer");

  const std::string shell_invalidation = function_body(
      source, "invalidate_remastered_shell_materials");
  const std::string portrait_invalidation = function_body(
      source, "invalidate_remastered_party_portraits");
  require(count_identifier(shell_invalidation, "reset") == 1 &&
          count_identifier(shell_invalidation,
              "remastered_shell_material_renderer") == 1 &&
          count_identifier(shell_invalidation,
              "remastered_shell_materials_attempted") == 1,
      "shell material invalidation must clear textures, renderer identity, "
      "and the attempt latch");
  require(count_identifier(portrait_invalidation, "reset") == 1 &&
          count_identifier(portrait_invalidation,
              "remastered_party_portrait_renderer") == 1 &&
          count_identifier(portrait_invalidation,
              "remastered_party_portraits_attempted") == 1,
      "party portrait invalidation must clear textures, renderer identity, "
      "and the attempt latch");
  require(compact_shell_ensure.contains(
              "if(renderer!=this->remastered_shell_material_renderer)"
              "{this->remastered_shell_materials.reset();") &&
          compact_portrait_ensure.contains(
              "if(renderer!=this->remastered_party_portrait_renderer)"
              "{this->remastered_party_portraits.reset();"),
      "each public texture cache must independently invalidate when its SDL "
      "renderer identity changes");

  const std::string mode_switch = function_body(
      source, "set_presentation_mode");
  const std::string compact_mode_switch = without_whitespace(mode_switch);
  const std::size_t mode_invalidate_shell = compact_mode_switch.find(
      "this->invalidate_remastered_shell_materials();");
  const std::size_t mode_invalidate_portraits = compact_mode_switch.find(
      "this->invalidate_remastered_party_portraits();",
      mode_invalidate_shell);
  const std::size_t mutate_mode = compact_mode_switch.find(
      "this->presentation_host.set_mode(mode);", mode_invalidate_portraits);
  const std::size_t refresh_resource_assets = compact_mode_switch.find(
      "RealmzRefreshPresentationAssets();", mutate_mode);
  require(mode_invalidate_shell != std::string::npos &&
          mode_invalidate_portraits != std::string::npos &&
          mutate_mode != std::string::npos &&
          refresh_resource_assets != std::string::npos &&
          mode_invalidate_shell < mode_invalidate_portraits &&
          mode_invalidate_portraits < mutate_mode &&
          mutate_mode < refresh_resource_assets,
      "mode switches must drop both renderer caches before changing mode and "
      "refreshing Resource Manager-backed presentation assets");

  const std::string event_source = code_only(read_file(
      repository_root / "src/EventManager.cpp"));
  const std::string enqueue = function_body(event_source, "enqueue_sdl_event");
  const std::string compact_enqueue = without_whitespace(enqueue);
  const std::size_t device_reset = compact_enqueue.find(
      "caseSDL_EVENT_RENDER_DEVICE_RESET:");
  const std::size_t device_invalidate = compact_enqueue.find(
      "WindowManager::instance().invalidate_remastered_shell_materials();",
      device_reset);
  const std::size_t device_invalidate_portraits = compact_enqueue.find(
      "WindowManager::instance().invalidate_remastered_party_portraits();",
      device_invalidate);
  const std::size_t device_repaint = compact_enqueue.find(
      "WindowManager::instance().recomposite_all();",
      device_invalidate_portraits);
  const std::size_t targets_reset = compact_enqueue.find(
      "caseSDL_EVENT_RENDER_TARGETS_RESET:", device_repaint);
  const std::size_t targets_repaint = compact_enqueue.find(
      "WindowManager::instance().recomposite_all();", targets_reset);
  require(device_reset != std::string::npos &&
          device_invalidate != std::string::npos &&
          device_invalidate_portraits != std::string::npos &&
          device_repaint != std::string::npos &&
          targets_reset != std::string::npos &&
          targets_repaint != std::string::npos &&
          device_reset < device_invalidate &&
          device_invalidate < device_invalidate_portraits &&
          device_invalidate_portraits < device_repaint &&
          device_repaint < targets_reset && targets_reset < targets_repaint,
      "SDL render-device reset must invalidate both public texture caches "
      "before repaint, and render-target reset must repaint");
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
              body, "RealmzConsumeSemanticOpenCharacterSheetEvent") == 0,
      std::string(function_name) +
          " must leave Character Sheet tag consumption to EventManager");
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
              body, "RealmzConsumeSemanticSetSearchStateEvent") == 0,
      std::string(function_name) +
          " must leave tagged Search consumption to EventManager");
  require(count_identifier(
              body, "RealmzConsumeSemanticUseTorchEvent") == 0,
      std::string(function_name) +
          " must leave tagged Torch consumption to EventManager");
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
          " must retain one app1Evt case for guarded one-shot handoffs");

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

void verify_character_sheet_outer_loop(
    std::string_view body,
    std::string_view function_name,
    std::string_view handoff_label) {
  const std::string compact = without_whitespace(body);
  const std::size_t take = compact.find(
      "TakeSemanticOpenCharacterSheetMember(&semantic_character_member)");
  require(take != std::string::npos,
      std::string(function_name) +
          " must take one staged Character Sheet member in app1Evt");
  const std::size_t branch_start = compact.rfind("case", take);
  const std::size_t branch_end = compact.find("case", take + 1U);
  require(branch_start != std::string::npos &&
          branch_end != std::string::npos && branch_start < take &&
          take < branch_end,
      std::string(function_name) +
          " Character Sheet handoff must remain bounded to app1Evt");
  const std::string branch = compact.substr(
      branch_start, branch_end - branch_start);

  require(count_identifier(body, "TakeSemanticOpenCharacterSheetMember") == 1 &&
          count_identifier(branch,
              "TakeSemanticOpenCharacterSheetMember") == 1,
      std::string(function_name) +
          " must own exactly one Character Sheet one-shot take");
  require(count_identifier(branch, "FindControl") == 0 &&
          count_identifier(branch, "GlobalToLocal") == 0 &&
          count_identifier(branch, "mouseDown") == 0 &&
          count_identifier(branch, "keyDown") == 0 &&
          count_identifier(branch, "viewcharacter") == 0 &&
          count_identifier(branch, "buttonchoice") == 0,
      std::string(function_name) +
          " app1Evt must not forge a click or enter Character Sheet directly");

  const std::size_t maximum = branch.find(
      "constintmaximum_member=(int)charnum;");
  const std::size_t branch_take = branch.find(
      "TakeSemanticOpenCharacterSheetMember(&semantic_character_member)");
  const std::size_t bounded_maximum = branch.find(
      "(maximum_member>=0)&&(maximum_member<=5)", branch_take);
  const std::size_t member_in_range = branch.find(
      "((int)semantic_character_member<=maximum_member)", bounded_maximum);
  const std::size_t selected_new = branch.find(
      "((int)charselectnew==(int)semantic_character_member)",
      member_in_range);
  const std::size_t selected_old = branch.find(
      "((int)charselectold==(int)semantic_character_member)", selected_new);
  const std::size_t live_control = branch.find(
      "(charmainbut!=NIL)", selected_old);
  const std::size_t point = branch.find(
      "point.v=51*(short)semantic_character_member;", live_control);
  const std::size_t control = branch.find(
      "theControl=charmainbut;", point);
  const std::size_t reply = branch.find("reply=0;", control);
  const std::string goto_handoff = "goto" + std::string(handoff_label) + ";";
  const std::size_t jump = branch.find(goto_handoff, reply);
  require(maximum != std::string::npos &&
          branch_take != std::string::npos &&
          bounded_maximum != std::string::npos &&
          member_in_range != std::string::npos &&
          selected_new != std::string::npos &&
          selected_old != std::string::npos &&
          live_control != std::string::npos &&
          point != std::string::npos &&
          control != std::string::npos &&
          reply != std::string::npos &&
          jump != std::string::npos,
      std::string(function_name) +
          " Character Sheet handoff is missing a late identity or control "
          "guard");
  require(maximum < branch_take && branch_take < bounded_maximum &&
          bounded_maximum < member_in_range &&
          member_in_range < selected_new && selected_new < selected_old &&
          selected_old < live_control && live_control < point &&
          point < control && control < reply && reply < jump,
      std::string(function_name) +
          " must validate member identity and live control before staging the "
          "existing buttonchoice path");

  const std::string shared_handoff =
      std::string(handoff_label) + ":reply=buttonchoice(reply);";
  require(!branch.contains(std::string(handoff_label) + ":") &&
          compact.contains(shared_handoff),
      std::string(function_name) +
          " must jump out of app1Evt to an existing shared buttonchoice label");
}

void verify_search_outer_loop(
    std::string_view body,
    std::string_view function_name,
    std::string_view handoff_label) {
  const std::string compact = without_whitespace(body);
  const std::size_t take = compact.find(
      "TakeSemanticSetSearchStateDesired(&semantic_desired_searching)");
  require(take != std::string::npos,
      std::string(function_name) +
          " must take one staged absolute Search state in app1Evt");
  const std::size_t branch_start = compact.rfind("case", take);
  const std::size_t branch_end = compact.find("case", take + 1U);
  require(branch_start != std::string::npos &&
          branch_end != std::string::npos && branch_start < take &&
          take < branch_end,
      std::string(function_name) +
          " Search handoff must remain bounded to app1Evt");
  const std::string branch = compact.substr(
      branch_start, branch_end - branch_start);
  require(count_identifier(body, "TakeSemanticSetSearchStateDesired") == 1 &&
          count_identifier(branch,
              "TakeSemanticSetSearchStateDesired") == 1,
      std::string(function_name) +
          " must own exactly one Search one-shot take");
  require(count_identifier(branch, "FindControl") == 0 &&
          count_identifier(branch, "GlobalToLocal") == 0 &&
          count_identifier(branch, "mouseDown") == 0 &&
          count_identifier(branch, "keyDown") == 0 &&
          count_identifier(branch, "sound") == 0 &&
          count_identifier(branch, "ploticon3") == 0 &&
          count_identifier(branch, "buttonchoice") == 0,
      std::string(function_name) +
          " Search app1Evt must not forge input or duplicate Classic behavior");

  const std::size_t branch_take = branch.find(
      "TakeSemanticSetSearchStateDesired(&semantic_desired_searching)");
  const std::size_t live_control = branch.find("(search!=NIL)", branch_take);
  const std::size_t mismatch = branch.find(
      "((partycondition[PARTY_COND_SEARCH]!=0)!="
      "(semantic_desired_searching!=0))",
      live_control);
  const std::size_t control = branch.find("theControl=search;", mismatch);
  const std::size_t reply = branch.find("reply=0;", control);
  const std::string goto_handoff = "goto" + std::string(handoff_label) + ";";
  const std::size_t jump = branch.find(goto_handoff, reply);
  require(live_control != std::string::npos && mismatch != std::string::npos &&
          control != std::string::npos && reply != std::string::npos &&
          jump != std::string::npos && branch_take < live_control &&
          live_control < mismatch && mismatch < control && control < reply &&
          reply < jump,
      std::string(function_name) +
          " must burn Search authorization, then recheck the live control and "
          "absolute state mismatch before the shared Classic handoff");
  require(count_identifier(branch, "partycondition") == 1 &&
          count_identifier(branch, "PARTY_COND_SEARCH") == 1 &&
          count_text(branch, "partycondition[PARTY_COND_SEARCH]=") == 0,
      std::string(function_name) +
          " Search handoff may read the authoritative condition once but never "
          "mutate it directly");
  require(!branch.contains(std::string(handoff_label) + ":") &&
          compact.contains(
              std::string(handoff_label) + ":reply=buttonchoice(reply);"),
      std::string(function_name) +
          " must jump out of app1Evt to the existing buttonchoice label");
}

void verify_torch_outer_loop(
    std::string_view body,
    std::string_view function_name,
    std::string_view handoff_label) {
  const std::string compact = without_whitespace(body);
  const std::size_t take = compact.find(
      "TakeSemanticUseTorchSource(&semantic_torch_member,"
      "&semantic_torch_slot)");
  require(take != std::string::npos,
      std::string(function_name) +
          " must take one staged Torch source in app1Evt");
  const std::size_t branch_start = compact.rfind("case", take);
  const std::size_t branch_end = compact.find("case", take + 1U);
  require(branch_start != std::string::npos &&
          branch_end != std::string::npos && branch_start < take &&
          take < branch_end,
      std::string(function_name) +
          " Torch handoff must remain bounded to app1Evt");
  const std::string branch = compact.substr(
      branch_start, branch_end - branch_start);
  require(count_identifier(body, "TakeSemanticUseTorchSource") == 1 &&
          count_identifier(branch, "TakeSemanticUseTorchSource") == 1 &&
          count_identifier(
              branch, "RealmzCurrentFirstUsableTorchSourceMatches") == 1,
      std::string(function_name) +
          " must own one Torch take and one fresh nonmutating source check");
  require(count_identifier(branch, "FindControl") == 0 &&
          count_identifier(branch, "GlobalToLocal") == 0 &&
          count_identifier(branch, "mouseDown") == 0 &&
          count_identifier(branch, "keyDown") == 0 &&
          count_identifier(branch, "checkforitem") == 0 &&
          count_identifier(branch, "loaditem") == 0 &&
          count_identifier(branch, "resolvespell") == 0 &&
          count_identifier(branch, "partycondition") == 1 &&
          count_identifier(branch, "buttonchoice") == 0,
      std::string(function_name) +
          " Torch app1Evt must not forge input or duplicate Classic item/light "
          "behavior");

  const std::size_t branch_take = branch.find(
      "TakeSemanticUseTorchSource(&semantic_torch_member,"
      "&semantic_torch_slot)");
  const std::size_t live_control = branch.find("(torch!=NIL)", branch_take);
  const std::size_t fresh_source = branch.find(
      "RealmzCurrentFirstUsableTorchSourceMatches(semantic_torch_member,"
      "semantic_torch_slot)", live_control);
  const std::size_t control = branch.find("theControl=torch;", fresh_source);
  const std::size_t reply = branch.find("reply=0;", control);
  const std::string goto_handoff = "goto" + std::string(handoff_label) + ";";
  const std::size_t jump = branch.find(goto_handoff, reply);
  require(live_control != std::string::npos &&
          fresh_source != std::string::npos &&
          control != std::string::npos && reply != std::string::npos &&
          jump != std::string::npos && branch_take != std::string::npos &&
          branch_take < live_control &&
          live_control < fresh_source && fresh_source < control &&
          control < reply && reply < jump,
      std::string(function_name) +
          " must burn Torch authorization, then recheck the live real control "
          "and exact first usable source before the Classic handoff");
  require(!branch.contains(std::string(handoff_label) + ":") &&
          compact.contains(
              std::string(handoff_label) + ":reply=buttonchoice(reply);"),
      std::string(function_name) +
          " must jump out of app1Evt to the existing buttonchoice label");
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
  const std::string buttonchoice_source = code_only(
      read_file(legacy_root / "buttonchoice.c"));
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
  verify_character_sheet_outer_loop(mainscreen, "mainscreen", "goback2");
  verify_character_sheet_outer_loop(threed, "threed", "goback");
  verify_search_outer_loop(mainscreen, "mainscreen", "goback2");
  verify_search_outer_loop(threed, "threed", "goback");
  verify_torch_outer_loop(mainscreen, "mainscreen", "goback2");
  verify_torch_outer_loop(threed, "threed", "goback");
  const std::string buttonchoice = function_body(
      buttonchoice_source, "buttonchoice");
  const std::string compact_buttonchoice = without_whitespace(buttonchoice);
  const std::size_t classic_search = compact_buttonchoice.find(
      "if(theControl==search)");
  const std::size_t classic_torch = compact_buttonchoice.find(
      "if(theControl==torch)", classic_search);
  require(classic_search != std::string::npos &&
          classic_torch != std::string::npos && classic_search < classic_torch,
      "Classic buttonchoice Search branch must remain present before Torch");
  const std::string classic_search_branch = compact_buttonchoice.substr(
      classic_search, classic_torch - classic_search);
  for (const std::string_view required : {
           "sound(141);",
           "if(partycondition[PARTY_COND_SEARCH])",
           "partycondition[PARTY_COND_SEARCH]=0;",
           "GetControlBounds(search,&r);",
           "ploticon3(128,r);",
           "partycondition[PARTY_COND_SEARCH]=-1;",
       }) {
    require(classic_search_branch.contains(required),
        std::string("Classic Search behavior must retain ") +
            std::string(required));
  }
  require(count_identifier(classic_search_branch,
              "TakeSemanticSetSearchStateDesired") == 0 &&
          count_identifier(classic_search_branch, "app1Evt") == 0,
      "Classic buttonchoice Search behavior must remain semantic-boundary free");
  const std::size_t classic_bar = compact_buttonchoice.find(
      "if(theControl==barbut)", classic_torch);
  require(classic_bar != std::string::npos && classic_torch < classic_bar,
      "Classic buttonchoice Torch branch must remain bounded before Heal");
  const std::string classic_torch_branch = compact_buttonchoice.substr(
      classic_torch, classic_bar - classic_torch);
  for (const std::string_view required : {
           "if(checkforitem(805,TRUE,-1))",
           "loaditem(805);",
           "loadspell2(item.sp2);",
           "powerlevel=abs(item.sp1);",
           "sound(600+spellinfo.sound2);",
           "resolvespell();",
       }) {
    require(classic_torch_branch.contains(required),
        std::string("Classic Torch behavior must retain ") +
            std::string(required));
  }
  require(count_identifier(classic_torch_branch,
              "TakeSemanticUseTorchSource") == 0 &&
          count_identifier(classic_torch_branch, "app1Evt") == 0 &&
          count_identifier(classic_torch_branch,
              "RealmzCurrentFirstUsableTorchSourceMatches") == 0,
      "Classic buttonchoice Torch behavior must remain semantic-boundary free");

  const std::string torch_source_adapter = code_only(read_file(
      repository_root / "src/presentation/LegacyTorchSource.c"));
  const std::string find_torch = function_body(
      torch_source_adapter, "RealmzFindFirstUsableTorchSource");
  const std::string compact_find_torch = without_whitespace(find_torch);
  require(compact_find_torch.contains(
              "if((maximum_member<0)||(maximum_member>5)){return0;}") &&
          compact_find_torch.contains(
              "if((item_count<0)||(item_count>30)){return0;}") &&
          compact_find_torch.contains(
              "if(c[member].items[slot].id==805){"
              "if(c[member].items[slot].charge<=0){return0;}") &&
          compact_find_torch.contains(
              "source->member=(uint8_t)member;"
              "source->slot=(uint8_t)slot;return1;") &&
          count_identifier(find_torch, "checkforitem") == 0 &&
          count_identifier(find_torch, "loaditem") == 0 &&
          count_identifier(find_torch, "theItem") == 0,
      "the shared Torch source scan must fail closed, stop at the first exact "
      "+805 even when uncharged, and remain nonmutating");
  const std::string match_torch = function_body(
      torch_source_adapter, "RealmzCurrentFirstUsableTorchSourceMatches");
  require(count_identifier(
              match_torch, "RealmzFindFirstUsableTorchSource") == 1 &&
          without_whitespace(match_torch).contains(
              "return(current.member==member)&&(current.slot==slot);"),
      "the outer-loop Torch freshness helper must reuse the exact shared scan "
      "and compare both locator components");
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
  std::size_t global_character_sheet_consumer_count = 0;
  std::size_t global_inventory_consumer_count = 0;
  std::size_t global_spellbook_consumer_count = 0;
  std::size_t global_save_consumer_count = 0;
  std::size_t global_load_consumer_count = 0;
  std::size_t global_search_consumer_count = 0;
  std::size_t global_torch_consumer_count = 0;
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
  std::size_t global_character_sheet_take_count = 0;
  std::size_t global_search_take_count = 0;
  std::size_t global_torch_take_count = 0;
  std::vector<fs::path> character_sheet_take_callers;
  std::vector<fs::path> search_take_callers;
  std::vector<fs::path> torch_take_callers;
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
    global_character_sheet_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenCharacterSheetEvent");
    global_inventory_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenInventoryEvent");
    global_spellbook_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenSpellbookEvent");
    global_save_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenSaveGameEvent");
    global_load_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticOpenLoadGameEvent");
    global_search_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticSetSearchStateEvent");
    global_torch_consumer_count += count_identifier(
        source, "RealmzConsumeSemanticUseTorchEvent");
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
    const std::size_t character_sheet_takes = count_identifier(
        source, "TakeSemanticOpenCharacterSheetMember");
    global_character_sheet_take_count += character_sheet_takes;
    if (character_sheet_takes != 0) {
      character_sheet_take_callers.emplace_back(
          fs::relative(path, legacy_root));
    }
    const std::size_t search_takes = count_identifier(
        source, "TakeSemanticSetSearchStateDesired");
    global_search_take_count += search_takes;
    if (search_takes != 0) {
      search_take_callers.emplace_back(fs::relative(path, legacy_root));
    }
    const std::size_t torch_takes = count_identifier(
        source, "TakeSemanticUseTorchSource");
    global_torch_take_count += torch_takes;
    if (torch_takes != 0) {
      torch_take_callers.emplace_back(fs::relative(path, legacy_root));
    }
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
  require(global_character_sheet_consumer_count == 0,
      "legacy loops must not consume Character Sheet tags directly");
  require(global_inventory_consumer_count == 0,
      "legacy loops must not consume tagged semantic inventory directly");
  require(global_spellbook_consumer_count == 0,
      "legacy loops must not consume tagged semantic spellbook input directly");
  require(global_save_consumer_count == 0,
      "legacy loops must not consume tagged semantic save input directly");
  require(global_load_consumer_count == 0,
      "legacy loops must not consume tagged semantic load input directly");
  require(global_search_consumer_count == 0,
      "legacy loops must not consume tagged semantic Search input directly");
  require(global_torch_consumer_count == 0,
      "legacy loops must not consume tagged semantic Torch input directly");
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
  require(global_character_sheet_take_count == 2 &&
          character_sheet_take_callers == std::vector<fs::path>{
              fs::path("misc.c"), fs::path("threed.c")},
      "only misc.c mainscreen and threed.c may take the staged Character "
      "Sheet member");
  std::ranges::sort(search_take_callers);
  require(global_search_take_count == 2 &&
          search_take_callers == std::vector<fs::path>{
              fs::path("misc.c"), fs::path("threed.c")},
      "only misc.c mainscreen and threed.c may take the staged Search state");
  std::ranges::sort(torch_take_callers);
  require(global_torch_take_count == 2 &&
          torch_take_callers == std::vector<fs::path>{
              fs::path("misc.c"), fs::path("threed.c")},
      "only misc.c mainscreen and threed.c may take the staged Torch source");

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
  std::size_t character_sheet_consume_calls = 0;
  std::size_t inventory_consume_calls = 0;
  std::size_t spellbook_consume_calls = 0;
  std::size_t world_scroll_case_consume_calls = 0;
  std::size_t save_consume_calls = 0;
  std::size_t load_consume_calls = 0;
  std::size_t rest_consume_calls = 0;
  std::size_t camp_consume_calls = 0;
  std::size_t search_consume_calls = 0;
  std::size_t torch_consume_calls = 0;
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
  std::size_t character_sheet_take_calls = 0;
  std::size_t search_take_calls = 0;
  std::size_t torch_take_calls = 0;
  std::vector<fs::path> wrapper_callers;
  std::vector<fs::path> character_sheet_take_callers;
  std::vector<fs::path> search_take_callers;
  std::vector<fs::path> torch_take_callers;

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
    character_sheet_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenCharacterSheetEvent");
    inventory_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenInventoryEvent");
    spellbook_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenSpellbookEvent");
    world_scroll_case_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenScrollCaseEvent");
    save_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenSaveGameEvent");
    load_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticOpenLoadGameEvent");
    rest_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticRestPartyEvent");
    camp_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticSetCampStateEvent");
    search_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticSetSearchStateEvent");
    torch_consume_calls += count_identifier(
        source, "RealmzConsumeSemanticUseTorchEvent");
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
    const std::size_t file_character_sheet_takes = count_identifier(
        source, "TakeSemanticOpenCharacterSheetMember");
    character_sheet_take_calls += file_character_sheet_takes;
    if (file_character_sheet_takes != 0) {
      character_sheet_take_callers.emplace_back(relative);
    }
    const std::size_t file_search_takes = count_identifier(
        source, "TakeSemanticSetSearchStateDesired");
    search_take_calls += file_search_takes;
    if (file_search_takes != 0) {
      search_take_callers.emplace_back(relative);
    }
    const std::size_t file_torch_takes = count_identifier(
        source, "TakeSemanticUseTorchSource");
    torch_take_calls += file_torch_takes;
    if (file_torch_takes != 0) {
      torch_take_callers.emplace_back(relative);
    }
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
  require(character_sheet_consume_calls == 0,
      "only EventManager may call "
      "RealmzConsumeSemanticOpenCharacterSheetEvent");
  require(inventory_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenInventoryEvent");
  require(spellbook_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenSpellbookEvent");
  require(world_scroll_case_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenScrollCaseEvent");
  require(save_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenSaveGameEvent");
  require(load_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticOpenLoadGameEvent");
  require(rest_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticRestPartyEvent");
  require(camp_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticSetCampStateEvent");
  require(search_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticSetSearchStateEvent");
  require(torch_consume_calls == 0,
      "only EventManager may call RealmzConsumeSemanticUseTorchEvent");
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
  std::ranges::sort(character_sheet_take_callers);
  require(character_sheet_take_calls == 2 &&
          character_sheet_take_callers == std::vector<fs::path>{
              fs::path("realmz_orig/misc.c"),
              fs::path("realmz_orig/threed.c")},
      "only the outdoor and dungeon top-level loops may take a staged "
      "Character Sheet member");
  std::ranges::sort(search_take_callers);
  require(search_take_calls == 2 &&
          search_take_callers == std::vector<fs::path>{
              fs::path("realmz_orig/misc.c"),
              fs::path("realmz_orig/threed.c")},
      "only the outdoor and dungeon top-level loops may take a staged Search "
      "state");
  std::ranges::sort(torch_take_callers);
  require(torch_take_calls == 2 &&
          torch_take_callers == std::vector<fs::path>{
              fs::path("realmz_orig/misc.c"),
              fs::path("realmz_orig/threed.c")},
      "only the outdoor and dungeon top-level loops may take a staged Torch "
      "source");
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
    verify_noncombat_scroll_case_contract(repository_root);
    verify_legacy_loop_ownership(repository_root);
    verify_production_call_ownership(repository_root);
    verify_window_manager_named_combat_sinks(repository_root);
    verify_window_manager_shell_dispatch_freshness(repository_root);
    verify_character_sheet_window_manager_contract(repository_root);
    verify_rest_party_window_manager_contract(repository_root);
    verify_set_camp_state_window_manager_contract(repository_root);
    verify_set_search_state_window_manager_contract(repository_root);
    verify_use_torch_window_manager_contract(repository_root);
    verify_selected_party_details_renderer_contract(repository_root);
    verify_gameplay_chrome_coverage_contract(repository_root);
    verify_remastered_runtime_asset_integration(repository_root);
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
