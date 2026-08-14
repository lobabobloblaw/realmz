#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "realmz_orig/structs.h"

void centerstage(short way);
}

namespace {

static_assert(
    std::numeric_limits<char>::is_signed,
    "Classic combat queues require signed char for their -1 empty sentinel");

enum class CallKind {
  centerfield,
  sound,
  drawbody,
  combatupdate2,
};

struct TraceEntry {
  CallKind kind = CallKind::centerfield;
  short first = 0;
  short second = 0;
  short third = 0;
  short infocombat_during_call = 0;

  bool operator==(const TraceEntry&) const = default;
};

std::vector<TraceEntry> trace;
int checks_run = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

} // namespace

extern "C" {

char aimindex = 0;
char q[110] = {};
char pos[6][2] = {};
char monpos[100][2] = {};
char spellx = 0;
char spelly = 0;
short inspell = 0;
short infocombat = 0;
struct character c[6] = {};
struct monster monster[100] = {};

void centerfield(short x, short y) {
  trace.push_back(TraceEntry{
      .kind = CallKind::centerfield,
      .first = x,
      .second = y,
      .infocombat_during_call = infocombat,
  });
}

void sound(short id) {
  trace.push_back(TraceEntry{
      .kind = CallKind::sound,
      .first = id,
      .infocombat_during_call = infocombat,
  });
}

void drawbody(short body, short force, short where) {
  trace.push_back(TraceEntry{
      .kind = CallKind::drawbody,
      .first = body,
      .second = force,
      .third = where,
      .infocombat_during_call = infocombat,
  });
}

void combatupdate2(short body) {
  trace.push_back(TraceEntry{
      .kind = CallKind::combatupdate2,
      .first = body,
      .infocombat_during_call = infocombat,
  });
}

} // extern "C"

namespace {

struct Outcome {
  std::vector<TraceEntry> calls;
  char final_aimindex = 0;
  short final_infocombat = 0;

  bool operator==(const Outcome&) const = default;
};

void reset_fixture() {
  aimindex = 0;
  std::fill(std::begin(q), std::end(q), static_cast<char>(-1));
  for (auto& coordinates : pos) {
    std::fill(
        std::begin(coordinates), std::end(coordinates), static_cast<char>(0));
  }
  for (auto& coordinates : monpos) {
    std::fill(
        std::begin(coordinates), std::end(coordinates), static_cast<char>(0));
  }
  spellx = 0;
  spelly = 0;
  inspell = 0;
  infocombat = 0;
  std::fill(std::begin(c), std::end(c), character{});
  const struct monster empty_monster{};
  std::fill(std::begin(monster), std::end(monster), empty_monster);
  trace.clear();
}

[[nodiscard]] Outcome outcome() {
  return Outcome{
      .calls = trace,
      .final_aimindex = aimindex,
      .final_infocombat = infocombat,
  };
}

[[nodiscard]] Outcome run_next_party_wrap() {
  reset_fixture();
  aimindex = 108;
  q[109] = -1;
  q[1] = 3;
  c[3].stamina = 12;
  pos[3][0] = 17;
  pos[3][1] = 23;

  centerstage(1);
  return outcome();
}

void test_next_wraps_skips_and_centers_party_member() {
  const auto actual = run_next_party_wrap();
  const std::vector expected{
      TraceEntry{
          .kind = CallKind::centerfield,
          .first = 17,
          .second = 23,
      },
      TraceEntry{
          .kind = CallKind::sound,
          .first = 147,
      },
      TraceEntry{
          .kind = CallKind::drawbody,
          .first = 3,
          .second = 1,
          .third = 0,
      },
      TraceEntry{
          .kind = CallKind::combatupdate2,
          .first = 3,
          .infocombat_during_call = 1,
      },
  };

  CHECK(actual.calls == expected);
  CHECK(actual.final_aimindex == 1);
  CHECK(actual.final_infocombat == 0);
  CHECK(q[109] == -1);
  CHECK(q[1] == 3);
  CHECK(c[3].stamina == 12);
  CHECK(pos[3][0] == 17);
  CHECK(pos[3][1] == 23);
}

void test_previous_wraps_skips_and_centers_monster() {
  reset_fixture();
  aimindex = 2;
  q[1] = -1;
  q[109] = 10;
  monster[0].stamina = 9;
  monpos[0][0] = 42;
  monpos[0][1] = 7;

  centerstage(-1);

  const std::vector expected{
      TraceEntry{
          .kind = CallKind::centerfield,
          .first = 42,
          .second = 7,
      },
      TraceEntry{
          .kind = CallKind::sound,
          .first = 147,
      },
      TraceEntry{
          .kind = CallKind::drawbody,
          .first = 10,
          .second = 1,
          .third = 0,
      },
      TraceEntry{
          .kind = CallKind::combatupdate2,
          .first = 10,
          .infocombat_during_call = 1,
      },
  };

  CHECK(trace == expected);
  CHECK(aimindex == 109);
  CHECK(infocombat == 0);
  CHECK(monster[0].stamina == 9);
  CHECK(monpos[0][0] == 42);
  CHECK(monpos[0][1] == 7);
}

void test_current_focus_applies_spell_offset_without_cycle_sound() {
  reset_fixture();
  aimindex = 35;
  q[35] = 2;
  c[2].stamina = 8;
  pos[2][0] = 21;
  pos[2][1] = 30;
  inspell = 1;
  spellx = 8;
  spelly = 3;

  centerstage(0);

  const std::vector expected{
      TraceEntry{
          .kind = CallKind::centerfield,
          .first = 18,
          .second = 32,
      },
      TraceEntry{
          .kind = CallKind::drawbody,
          .first = 2,
          .second = 1,
          .third = 0,
      },
      TraceEntry{
          .kind = CallKind::combatupdate2,
          .first = 2,
          .infocombat_during_call = 1,
      },
  };

  CHECK(trace == expected);
  CHECK(aimindex == 35);
  CHECK(infocombat == 0);
  CHECK(inspell == 1);
  CHECK(spellx == 8);
  CHECK(spelly == 3);
}

void test_dead_targets_skip_centering_but_keep_classic_refresh() {
  reset_fixture();
  aimindex = 4;
  q[4] = 1;
  c[1].stamina = 0;

  centerstage(0);

  const std::vector dead_party_expected{
      TraceEntry{
          .kind = CallKind::drawbody,
          .first = 1,
          .second = 1,
          .third = 0,
      },
      TraceEntry{
          .kind = CallKind::combatupdate2,
          .first = 1,
          .infocombat_during_call = 1,
      },
  };
  CHECK(trace == dead_party_expected);
  CHECK(infocombat == 0);

  reset_fixture();
  aimindex = 7;
  q[7] = 12;
  monster[2].stamina = 0;

  centerstage(0);

  const std::vector dead_monster_expected{
      TraceEntry{
          .kind = CallKind::drawbody,
          .first = 12,
          .second = 1,
          .third = 0,
      },
      TraceEntry{
          .kind = CallKind::combatupdate2,
          .first = 12,
          .infocombat_during_call = 1,
      },
  };
  CHECK(trace == dead_monster_expected);
  CHECK(infocombat == 0);
}

void test_empty_queue_terminates_for_every_direction() {
  struct EmptyQueueCase {
    short way = 0;
    char expected_final_aimindex = 0;
  };
  constexpr std::array cases{
      EmptyQueueCase{.way = -1, .expected_final_aimindex = 36},
      EmptyQueueCase{.way = 0, .expected_final_aimindex = 37},
      EmptyQueueCase{.way = 1, .expected_final_aimindex = 38},
  };

  for (const auto& test_case : cases) {
    reset_fixture();
    aimindex = 37;
    const auto queue_before = std::to_array(q);

    // `way == 0` is the historically hazardous case: an empty current slot
    // cannot make progress by moving through the queue. All three directions
    // must nevertheless return through the production helper's bounded count.
    centerstage(test_case.way);

    CHECK(trace.empty());
    CHECK(aimindex == test_case.expected_final_aimindex);
    CHECK(infocombat == 0);
    CHECK(std::equal(queue_before.begin(), queue_before.end(), std::begin(q)));
  }
}

void test_repeat_run_is_deterministic() {
  const auto first = run_next_party_wrap();
  const auto second = run_next_party_wrap();
  CHECK(first == second);
}

} // namespace

int main() {
  try {
    test_next_wraps_skips_and_centers_party_member();
    test_previous_wraps_skips_and_centers_monster();
    test_current_focus_applies_spell_offset_without_cycle_sound();
    test_dead_targets_skip_centering_but_keep_classic_refresh();
    test_empty_queue_terminates_for_every_direction();
    test_repeat_run_is_deterministic();
    std::cout << "LegacyCombatFocusTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LegacyCombatFocusTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
