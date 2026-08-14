#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "presentation/LegacyPartySelection.h"

extern "C" {
extern char charnum;
extern char charselectnew;
void updatecontrols(void);
}

namespace {

int checks_run = 0;
int update_controls_calls = 0;

void check(bool condition, const char* expression, int line) {
  ++checks_run;
  if (!condition) {
    throw std::runtime_error(
        "check failed at line " + std::to_string(line) + ": " + expression);
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

void reset(char final_member, char selected_member) {
  charnum = final_member;
  charselectnew = selected_member;
  update_controls_calls = 0;
}

void test_changed_selection_updates_once() {
  reset(2, 0);
  CHECK(RealmzApplyPartyMemberSelection(2) ==
      REALMZ_PARTY_SELECTION_CHANGED);
  CHECK(charselectnew == 2);
  CHECK(update_controls_calls == 1);
}

void test_selected_member_is_idempotent_and_never_reopens_portrait() {
  reset(2, 2);
  CHECK(RealmzApplyPartyMemberSelection(2) ==
      REALMZ_PARTY_SELECTION_UNCHANGED);
  CHECK(charselectnew == 2);
  CHECK(update_controls_calls == 0);

  CHECK(RealmzApplyPartyMemberSelection(2) ==
      REALMZ_PARTY_SELECTION_UNCHANGED);
  CHECK(update_controls_calls == 0);
}

void test_bounds_fail_closed_without_mutation() {
  reset(2, 1);
  CHECK(RealmzApplyPartyMemberSelection(3) ==
      REALMZ_PARTY_SELECTION_REJECTED);
  CHECK(charselectnew == 1);
  CHECK(update_controls_calls == 0);

  CHECK(RealmzApplyPartyMemberSelection(UINT8_MAX) ==
      REALMZ_PARTY_SELECTION_REJECTED);
  CHECK(charselectnew == 1);
  CHECK(update_controls_calls == 0);

  reset(static_cast<char>(-1), 0);
  CHECK(RealmzApplyPartyMemberSelection(0) ==
      REALMZ_PARTY_SELECTION_REJECTED);
  CHECK(charselectnew == 0);
  CHECK(update_controls_calls == 0);

  reset(6, 1);
  CHECK(RealmzApplyPartyMemberSelection(1) ==
      REALMZ_PARTY_SELECTION_REJECTED);
  CHECK(charselectnew == 1);
  CHECK(update_controls_calls == 0);
}

void test_classic_upper_bound() {
  reset(5, 0);
  CHECK(RealmzApplyPartyMemberSelection(5) ==
      REALMZ_PARTY_SELECTION_CHANGED);
  CHECK(charselectnew == 5);
  CHECK(update_controls_calls == 1);
}

} // namespace

extern "C" {

char charnum = 0;
char charselectnew = 0;

void updatecontrols(void) {
  ++update_controls_calls;
}

} // extern "C"

int main() {
  try {
    test_changed_selection_updates_once();
    test_selected_member_is_idempotent_and_never_reopens_portrait();
    test_bounds_fail_closed_without_mutation();
    test_classic_upper_bound();
    std::cout << "LegacyPartySelectionTest passed (" << checks_run
              << " checks)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LegacyPartySelectionTest failed after " << checks_run
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
