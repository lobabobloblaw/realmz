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
              semantic_wrapper, "RealmzApplyPartyMemberSelection") == 1,
      "semantic gameplay wrapper must use one narrow selection adapter");
  require(count_identifier(semantic_wrapper, "get_next_event") == 2,
      "semantic gameplay wrapper must have one Classic and one scoped poll");
  require(count_identifier(semantic_wrapper, "app1Evt") == 10,
      "semantic gameplay wrapper must recognize all ten tagged paths");
  require(count_identifier(semantic_wrapper, "keyDown") == 7,
      "only late movement, inventory, spellbook, guard, finish, delay, and "
      "center validation may produce keyDown");
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
      "*ret=em.get_next_event(0)", scope_instance);
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
  require(classic_branch != std::string::npos &&
          first_poll != std::string::npos &&
          scope_type != std::string::npos &&
          begin_scope != std::string::npos &&
          end_scope != std::string::npos &&
          scope_instance != std::string::npos &&
          scoped_poll != std::string::npos &&
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
          center_rejected_message != std::string::npos,
      "semantic gameplay wrapper is missing its centralized fail-closed route");
  require(classic_branch < first_poll && first_poll < scope_type &&
          scope_type < begin_scope && begin_scope < end_scope &&
          end_scope < scope_instance && scope_instance < scoped_poll &&
          scoped_poll < tagged_branch && tagged_branch < late_consume &&
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
          center_null < center_rejected_message,
      "semantic wrapper must scope only its poll and translate afterward");
  require(compact_semantic.contains(
              "if(!remastered){*ret=em.get_next_event(0);"
              "return(ret->what!=nullEvent);}"),
      "Classic mode must remain the ordinary inactive-surface dequeue route");

  const std::string get_next = function_body(source, "GetNextEvent");
  const std::string wait_next = function_body(source, "WaitNextEvent");
  require(count_identifier(get_next, "get_next_event") == 1,
      "GetNextEvent must use the guarded EventManager dequeue path");
  require(count_identifier(wait_next, "get_next_event") == 1,
      "WaitNextEvent must use the guarded EventManager dequeue path");
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
  const std::string getchoice_source = code_only(
      read_file(legacy_root / "getchoice.c"));

  const std::string mainscreen = function_body(misc, "mainscreen");
  const std::string threed = function_body(threed_source, "threed");
  const std::string combat = function_body(combat_source, "combat");
  const std::string combat_raw = function_body(combat_raw_source, "combat");
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
  const std::size_t center_case = compact_combat.find("case'c':");
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
  require(global_selection_apply_count == 0,
      "legacy loops must not apply semantic selection directly");

  const std::string getchoice = function_body(getchoice_source, "getchoice");
  require(count_identifier(getchoice, "WaitNextEvent") == 1,
      "getchoice must retain its nested WaitNextEvent loop");
  require(count_identifier(getchoice, "GetNextEvent") == 0,
      "getchoice must not substitute a top-level GetNextEvent");
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
    verify_legacy_loop_ownership(repository_root);
    verify_production_call_ownership(repository_root);
    verify_window_manager_named_combat_sinks(repository_root);
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
