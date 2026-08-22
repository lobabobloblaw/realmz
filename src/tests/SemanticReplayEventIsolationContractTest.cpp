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

// Remove literals and comments while preserving byte offsets. Contract checks
// therefore cannot be satisfied by diagnostics or explanatory prose.
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
        result[index] = result[index + 1] = ' ';
        ++index;
        state = State::line_comment;
      } else if (current == '/' && next == '*') {
        result[index] = result[index + 1] = ' ';
        ++index;
        state = State::block_comment;
      } else if (current == '"') {
        result[index] = ' ';
        state = State::string_literal;
        escaped = false;
      } else if (current == '\'') {
        result[index] = ' ';
        state = State::character_literal;
        escaped = false;
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

[[nodiscard]] std::string_view function_body(
    const std::string& source,
    std::string_view signature) {
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

[[nodiscard]] bool ordered(
    std::string_view body,
    std::string_view earlier,
    std::string_view later) {
  const std::size_t first = body.find(earlier);
  const std::size_t second = body.find(later);
  return first != std::string_view::npos &&
      second != std::string_view::npos && first < second;
}

void verify_poll_isolation(const std::string& event_source) {
  const std::string_view raw =
      function_body(event_source, "EventRecord get_next_event(");
  require(ordered(raw, "active_replay_runtime()", "enqueue_pending_events"),
      "replay raw polling must return before SDL ingestion");
  require(raw.find("get_next_replay_event") == std::string_view::npos,
      "raw replay polling must never enter semantic command selection");

  const std::string_view semantic =
      function_body(event_source, "EventRecord get_next_semantic_event(");
  require(ordered(
              semantic, "get_next_replay_event()", "get_next_event(wait_ms)"),
      "only semantic polling may select replay commands");

  const std::string_view replay =
      function_body(event_source, "EventRecord get_next_replay_event()");
  require(replay.find("SDL_") == std::string_view::npos,
      "replay event selection must not call SDL");
  require(replay.find("REALMZ_SEMANTIC_INPUT_NONE") !=
          std::string_view::npos &&
          replay.find("RealmzIsSemanticGameplayTag") !=
          std::string_view::npos,
      "only an active guarded semantic surface may consume replay commands");
  require(ordered(replay, "++candidate", "event_queue.erase(candidate)"),
      "replay semantic polling must skip ordinary queued events");
  require(replay.find("ev.modifiers = 0") != std::string_view::npos,
      "replay semantic events must clear ambient modifiers");

  const std::string_view ingestion =
      function_body(event_source, "void enqueue_pending_events(");
  require(ordered(ingestion, "active_replay_runtime()", "SDL_WaitEventTimeout"),
      "all replay event ingestion must stop before SDL wait/poll calls");

  const std::string_view flush =
      function_body(event_source, "void flush_events()");
  require(ordered(flush, "active_replay_runtime()", "event_queue.clear()"),
      "replay FlushEvents must preserve future semantic commands");
}

void verify_ambient_api_isolation(const std::string& event_source) {
  const std::string_view tick =
      function_body(event_source, "uint32_t TickCount(void)");
  require(ordered(tick, "next_event_tick()", "SDL_GetTicks()"),
      "replay TickCount must use the logical clock before wall time");

  const std::string_view system_task =
      function_body(event_source, "void SystemTask(void)");
  require(ordered(system_task, "active_replay_runtime()", "SDL_Delay(10)"),
      "replay SystemTask must return before host delay");

  for (const std::string_view signature : {
           "void GetMouse(Point* ret)",
           "void GetMouseGlobal(Point* ret)",
           "void SetMouseLocation(const Point* mouseLoc)",
           "Boolean Button(void)",
           "Boolean StillDown(void)",
       }) {
    const std::string_view body = function_body(event_source, signature);
    require(body.find("active_replay_runtime()") != std::string_view::npos,
        "mouse APIs must have an explicit replay isolation branch");
  }

  const std::string_view semantic = function_body(
      event_source, "Boolean GetNextSemanticGameplayEvent(");
  require(semantic.find("replay->presentation_mode()") !=
          std::string_view::npos &&
          semantic.find("ret->modifiers = 0") != std::string_view::npos,
      "semantic replay polling must use configured presentation and zero modifiers");
}

void verify_replay_gameplay_controller(
    const std::string& event_source,
    const std::string& window_source) {
  const std::string_view semantic = function_body(
      event_source, "Boolean GetNextSemanticGameplayEvent(");
  require(ordered(
              semantic, "active_replay_runtime()",
              "ReplayExceptionBoundary replay_exception_boundary"),
      "active replay must install a terminal exception boundary before polling");
  require(semantic.find(
              "if (replay && replay->action_plan_started())") !=
          std::string_view::npos,
      "startup isolation must not drive an action plan before preflight");
  require(ordered(
              semantic, "next_gameplay_poll()",
              "record_live_replay_checkpoint") &&
          ordered(
              semantic, "record_live_replay_checkpoint",
              "complete_semantic_replay_child"),
      "each gameplay poll must capture its checkpoint before action or completion");

  const std::size_t classic = semantic.find("ReplayRoute::classic");
  const std::size_t classic_ack =
      semantic.find("acknowledge_action_delivery", classic);
  const std::size_t classic_return = semantic.find("return true", classic_ack);
  require(classic != std::string_view::npos &&
          classic_ack != std::string_view::npos &&
          classic_return != std::string_view::npos &&
          classic < classic_ack && classic_ack < classic_return,
      "Classic replay must acknowledge the exact injected key event before return");

  const std::size_t classic_switch_ack =
      semantic.find("acknowledge_switch_weapon_delivery", classic);
  const std::size_t classic_switch_return =
      semantic.find("return true", classic_switch_ack);
  require(classic_switch_ack != std::string_view::npos &&
          classic_switch_return != std::string_view::npos &&
          classic_switch_ack < classic_switch_return,
      "Classic weapon switching must acknowledge its typed key receipt before return");

  const std::size_t classic_selection_apply =
      semantic.find("RealmzApplyPartyMemberSelection", classic);
  const std::size_t classic_selection_ack =
      semantic.find(
          "acknowledge_party_selection_delivery", classic_selection_apply);
  const std::size_t classic_selection_null =
      semantic.find("ret->what = nullEvent", classic_selection_ack);
  const std::size_t classic_selection_return =
      semantic.find("return false", classic_selection_null);
  require(classic_selection_apply != std::string_view::npos &&
          classic_selection_ack != std::string_view::npos &&
          classic_selection_null != std::string_view::npos &&
          classic_selection_return != std::string_view::npos &&
          classic_selection_apply < classic_selection_ack &&
          classic_selection_ack < classic_selection_null &&
          classic_selection_null < classic_selection_return,
      "Classic selection must acknowledge only after adapter application and return nullEvent");

  require(ordered(
              semantic, "SemanticInputScope semantic_scope(surface)",
              "dispatch_replay_semantic_action") &&
          ordered(
              semantic, "dispatch_replay_semantic_action",
              "get_next_semantic_event(0)"),
      "semantic replay dispatch must occur inside the guarded gameplay scope");
  const std::size_t movement_consume =
      semantic.find("RealmzConsumeSemanticMovementEvent");
  const std::size_t semantic_ack =
      semantic.rfind("acknowledge_action_delivery");
  require(movement_consume != std::string_view::npos &&
          semantic_ack != std::string_view::npos &&
          movement_consume < semantic_ack,
      "semantic replay acknowledgement must observe late-translated output");
  const std::size_t selection_consume =
      semantic.find("RealmzConsumeSemanticPartySelectionEvent");
  const std::size_t selection_apply =
      semantic.find("RealmzApplyPartyMemberSelection", selection_consume);
  const std::size_t semantic_selection_ack =
      semantic.rfind("acknowledge_party_selection_delivery");
  require(selection_consume != std::string_view::npos &&
          selection_apply != std::string_view::npos &&
          semantic_selection_ack != std::string_view::npos &&
          selection_consume < selection_apply &&
          selection_apply < semantic_selection_ack,
      "semantic selection acknowledgement must follow late validation and adapter application");
  const std::size_t switch_consume =
      semantic.find("RealmzConsumeSemanticSwitchWeaponEvent");
  const std::size_t semantic_switch_ack =
      semantic.rfind("acknowledge_switch_weapon_delivery");
  const std::size_t switch_mapper_call =
      semantic.find("replay_switch_weapon_key_message");
  const std::size_t switch_mapper_rejection =
      semantic.find("if (!expected)", switch_mapper_call);
  const std::size_t expected_switch_tag =
      semantic.find("semantic_switch_weapon_tag", switch_mapper_rejection);
  const std::size_t exact_switch_tag_match = semantic.find(
      "ret->message == *replay_expected_switch_tag", expected_switch_tag);
  const std::size_t gated_switch_consume = semantic.find(
      "replay_switch_tag_matches &&", exact_switch_tag_match);
  require(switch_mapper_call != std::string_view::npos &&
          switch_mapper_rejection != std::string_view::npos &&
          expected_switch_tag != std::string_view::npos &&
          exact_switch_tag_match != std::string_view::npos &&
          gated_switch_consume != std::string_view::npos &&
          switch_consume != std::string_view::npos &&
          semantic_switch_ack != std::string_view::npos &&
          switch_mapper_call < switch_mapper_rejection &&
          switch_mapper_rejection < expected_switch_tag &&
          expected_switch_tag < exact_switch_tag_match &&
          exact_switch_tag_match < gated_switch_consume &&
          gated_switch_consume < switch_consume &&
          switch_consume < semantic_switch_ack,
      "semantic weapon-switch acknowledgement must follow mapper validation, "
      "exact dequeued-tag matching, and late translation");

  const std::string_view mapper = function_body(
      window_source, "WindowManager::replay_movement_key_message(");
  require(mapper.find("capture_runtime_legacy_command_context") !=
              std::string_view::npos &&
          mapper.find("legacy_key_message_for_movement") !=
              std::string_view::npos &&
          mapper.find("context.adaptive_eligible") !=
              std::string_view::npos,
      "both replay routes must use the production live-context movement mapper");

  const std::string_view selection_mapper = function_body(
      window_source, "WindowManager::replay_party_selection_member(");
  require(selection_mapper.find("capture_runtime_legacy_command_context") !=
              std::string_view::npos &&
          selection_mapper.find("LegacyGameSnapshotSource") !=
              std::string_view::npos &&
          selection_mapper.find("snapshot.party.member") !=
              std::string_view::npos &&
          selection_mapper.find("context.adaptive_eligible") !=
              std::string_view::npos,
      "both replay selection routes must validate live surface and roster context");

  const std::string_view switch_mapper = function_body(
      window_source, "WindowManager::replay_switch_weapon_key_message(");
  require(switch_mapper.find("capture_runtime_legacy_command_context") !=
              std::string_view::npos &&
          switch_mapper.find("LegacyGameSnapshotSource") !=
              std::string_view::npos &&
          switch_mapper.find("acting_combatant") !=
              std::string_view::npos &&
          switch_mapper.find("legacy_key_message_for_switch_weapon") !=
              std::string_view::npos &&
          switch_mapper.find("context.adaptive_eligible") !=
              std::string_view::npos,
      "both replay weapon-switch routes must validate the live combat actor and production key mapper");

  const std::string_view dispatch = function_body(
      window_source, "WindowManager::dispatch_replay_semantic_action(");
  require(dispatch.find("action_plan_started") != std::string_view::npos &&
          dispatch.find("ReplayRoute::semantic") != std::string_view::npos &&
          dispatch.find("ReplayPresentationMode::remastered") !=
              std::string_view::npos &&
          dispatch.find("RealmzCurrentSemanticInputSurface") !=
              std::string_view::npos &&
          dispatch.find("runtime_legacy_command_bridge->dispatch(action)") !=
              std::string_view::npos,
      "replay semantic dispatch must retain runtime, route, mode, scope, and bridge guards");
}

void verify_non_replay_paths_remain(const std::string& event_source) {
  const std::string_view raw =
      function_body(event_source, "EventRecord get_next_event(");
  require(raw.find("enqueue_pending_events(wait_ms)") !=
          std::string_view::npos &&
          raw.find("WindowManager::instance().front_window()") !=
          std::string_view::npos &&
          raw.find("event_queue.pop_front()") != std::string_view::npos,
      "ordinary polling must retain SDL ingestion, caret idle, and FIFO dequeue");

  const std::string_view flush =
      function_body(event_source, "void flush_events()");
  require(flush.find("enqueue_pending_events(0)") !=
          std::string_view::npos &&
          flush.find("event_queue.clear()") != std::string_view::npos,
      "ordinary FlushEvents behavior must remain available");

  const std::string_view mouse =
      function_body(event_source, "void GetMouse(Point* ret)");
  require(mouse.find("CCGrafPort::as_port(qd.thePort)") !=
          std::string_view::npos &&
          mouse.find("to_local_space") != std::string_view::npos,
      "ordinary GetMouse must retain QuickDraw local-space conversion");

  const std::string_view wait =
      function_body(event_source, "Boolean WaitNextEvent(");
  require(wait.find("em.get_next_event(sleep)") != std::string_view::npos,
      "ordinary WaitNextEvent must retain its wait duration");
}

void verify_logical_clock(const std::string& runtime_source) {
  const std::string_view tick = function_body(
      runtime_source, "std::uint32_t ReplayRuntime::next_event_tick()");
  require(tick.find("fetch_add(1, std::memory_order_relaxed)") !=
          std::string_view::npos,
      "logical replay time must advance exactly once per TickCount read");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error(
          "usage: SemanticReplayEventIsolationContractTest <repository-root>");
    }
    const fs::path root = fs::weakly_canonical(argv[1]);
    const std::string event_source =
        code_only(read_file(root / "src/EventManager.cpp"));
    const std::string runtime_source =
        code_only(read_file(root / "src/replay/ReplayRuntime.cpp"));
    const std::string window_source =
        code_only(read_file(root / "src/WindowManager.cpp"));
    verify_poll_isolation(event_source);
    verify_ambient_api_isolation(event_source);
    verify_replay_gameplay_controller(event_source, window_source);
    verify_non_replay_paths_remain(event_source);
    verify_logical_clock(runtime_source);
    std::cout << "SemanticReplayEventIsolationContractTest passed ("
              << checks_run << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SemanticReplayEventIsolationContractTest failed after "
              << checks_run << " checks: " << error.what() << '\n';
    return 1;
  }
}
