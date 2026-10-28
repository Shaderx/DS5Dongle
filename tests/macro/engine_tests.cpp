#include "macro_engine.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

using gamepad_macro::Button;
using gamepad_macro::Dpad;
using gamepad_macro::GamepadState;
using gamepad_macro::MacroEngine;
using gamepad_macro::Program;
using gamepad_macro::Step;
using gamepad_macro::Status;
using gamepad_macro::button_mask;
using gamepad_macro::kMaxSteps;
using gamepad_macro::neutral_state;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " \
                << #condition << '\n';                                      \
      std::abort();                                                          \
    }                                                                        \
  } while (false)

namespace {

bool same_state(const GamepadState& left, const GamepadState& right) {
  return left.buttons == right.buttons && left.dpad == right.dpad &&
         left.left_x == right.left_x && left.left_y == right.left_y &&
         left.right_x == right.right_x && left.right_y == right.right_y &&
         left.left_trigger == right.left_trigger &&
         left.right_trigger == right.right_trigger;
}

GamepadState pressed(Button button) {
  GamepadState state = neutral_state();
  state.buttons = button_mask(button);
  return state;
}

void fill_program(Program& result, std::initializer_list<Step> steps,
                  std::uint16_t repeats) {
  result.step_count = 0;
  result.repeat_count = repeats;
  for (const Step& step : steps) {
    CHECK(result.step_count < kMaxSteps);
    result.steps[result.step_count++] = step;
  }
}

void submit_initial_neutral(MacroEngine& engine) {
  CHECK(same_state(engine.next_report(0), neutral_state()));
  CHECK(engine.status().output_pending);
  engine.report_submitted(0);
  CHECK(!engine.status().output_pending);
}

void test_busy_endpoint_and_step_timing() {
  Program loaded_program;
  Program replacement;
  MacroEngine engine;
  submit_initial_neutral(engine);

  const GamepadState cross = pressed(Button::kCross);
  const GamepadState neutral = neutral_state();
  fill_program(loaded_program, {Step{cross, 10}, Step{neutral, 5}}, 1);
  CHECK(engine.load(loaded_program));
  CHECK(engine.start(100));
  CHECK(same_state(engine.next_report(100), cross));
  CHECK(engine.status().output_pending);
  CHECK(!engine.start(100));
  fill_program(replacement, {Step{pressed(Button::kCircle), 1}}, 1);
  CHECK(!engine.load(replacement));

  // A busy endpoint keeps the same step pending, even after a long delay.
  CHECK(same_state(engine.next_report(1'000'000), cross));
  CHECK(engine.status().output_pending);
  engine.report_submitted(1'000'000);
  CHECK(!engine.status().output_pending);

  CHECK(same_state(engine.next_report(1'009'999), cross));
  CHECK(!engine.status().output_pending);
  CHECK(same_state(engine.next_report(1'010'000), neutral));
  CHECK(engine.status().output_pending);
  CHECK(engine.status().running);
  CHECK(same_state(engine.next_report(1'050'000), neutral));
  CHECK(engine.status().output_pending);
  engine.report_submitted(1'050'000);

  // The last step gets its full dwell. Completion queues a separate neutral
  // transition, even though the last step already had the neutral state.
  CHECK(same_state(engine.next_report(1'054'999), neutral));
  CHECK(engine.status().running);
  CHECK(!engine.status().output_pending);
  CHECK(same_state(engine.next_report(1'055'000), neutral));
  CHECK(!engine.status().running);
  CHECK(engine.status().output_pending);
  CHECK(!engine.start(1'055'000));
  engine.report_submitted(1'055'001);
  CHECK(engine.start(1'055'002));
  CHECK(same_state(engine.next_report(1'055'002), cross));
}

void test_stop_requires_neutral_submission() {
  Program loaded_program;
  Program replacement;
  MacroEngine engine;
  submit_initial_neutral(engine);
  const GamepadState square = pressed(Button::kSquare);
  fill_program(loaded_program, {Step{square, 100}}, 0);
  CHECK(engine.load(loaded_program));
  CHECK(engine.start(10));
  engine.report_submitted(20);
  CHECK(same_state(engine.next_report(30), square));

  engine.stop();
  Status state = engine.status();
  CHECK(!state.running);
  CHECK(state.output_pending);
  CHECK(same_state(engine.next_report(1'000'000), neutral_state()));
  CHECK(!engine.start(1'000'000));

  // A new program can load while idle, but cannot erase the neutral debt.
  fill_program(replacement, {Step{pressed(Button::kCircle), 1}}, 1);
  CHECK(engine.load(replacement));
  CHECK(engine.status().output_pending);
  CHECK(!engine.start(1'000'000));
  engine.report_submitted(1'000'001);
  CHECK(engine.start(1'000'002));
  CHECK(same_state(engine.next_report(1'000'002), pressed(Button::kCircle)));
}

void test_disconnect_aborts_without_auto_resume() {
  Program loaded_program;
  MacroEngine engine;
  submit_initial_neutral(engine);
  const GamepadState triangle = pressed(Button::kTriangle);
  fill_program(loaded_program, {Step{triangle, 1}}, 0);
  CHECK(engine.load(loaded_program));
  CHECK(engine.start(10));
  engine.report_submitted(11);
  CHECK(same_state(engine.next_report(11), triangle));

  engine.disconnect();
  CHECK(!engine.status().running);
  CHECK(engine.status().output_pending);
  CHECK(same_state(engine.next_report(9'999'999), neutral_state()));
  engine.report_submitted(10'000'000);
  CHECK(!engine.status().running);
  CHECK(!engine.status().output_pending);
  CHECK(same_state(engine.next_report(10'000'001), neutral_state()));

  // Playback can start only after an explicit request.
  CHECK(engine.start(10'000'002));
  CHECK(engine.status().running);
  CHECK(same_state(engine.next_report(10'000'002), triangle));
}

void test_finite_repeats_and_last_dwell() {
  Program loaded_program;
  MacroEngine engine;
  submit_initial_neutral(engine);
  const GamepadState square = pressed(Button::kSquare);
  const GamepadState circle = pressed(Button::kCircle);
  fill_program(loaded_program, {Step{square, 1}, Step{circle, 2}}, 2);
  CHECK(engine.load(loaded_program));
  CHECK(engine.start(0));
  CHECK(engine.status().current_step_index == 0);
  CHECK(engine.status().current_repetition == 1);
  CHECK(engine.status().repeat_count == 2);

  CHECK(same_state(engine.next_report(0), square));
  engine.report_submitted(100);
  CHECK(same_state(engine.next_report(1'099), square));
  CHECK(same_state(engine.next_report(1'100), circle));
  CHECK(engine.status().current_step_index == 1);
  CHECK(engine.status().current_repetition == 1);
  engine.report_submitted(1'100);
  CHECK(same_state(engine.next_report(3'099), circle));
  CHECK(same_state(engine.next_report(3'100), square));
  CHECK(engine.status().current_step_index == 0);
  CHECK(engine.status().current_repetition == 2);
  engine.report_submitted(3'100);
  CHECK(same_state(engine.next_report(4'100), circle));
  engine.report_submitted(4'100);
  CHECK(same_state(engine.next_report(6'099), circle));
  CHECK(engine.status().running);
  CHECK(same_state(engine.next_report(6'100), neutral_state()));
  CHECK(!engine.status().running);
  CHECK(engine.status().output_pending);
}

void test_infinite_repeat_and_large_clock() {
  Program loaded_program;
  MacroEngine engine;
  submit_initial_neutral(engine);
  const GamepadState dpad_state = [] {
    GamepadState state = neutral_state();
    state.dpad = static_cast<std::uint8_t>(Dpad::kDownLeft);
    state.left_x = 0;
    state.right_trigger = 255;
    return state;
  }();
  fill_program(loaded_program, {Step{dpad_state, 2}}, 0);
  CHECK(engine.load(loaded_program));
  const std::uint64_t near_max = UINT64_MAX - 3'000;
  CHECK(engine.start(near_max));
  CHECK(engine.status().repeat_count == 0);
  CHECK(engine.status().current_repetition == 1);
  engine.report_submitted(near_max);
  CHECK(same_state(engine.next_report(near_max + 1'999), dpad_state));
  CHECK(same_state(engine.next_report(near_max + 2'000), dpad_state));
  CHECK(engine.status().output_pending);
  CHECK(engine.status().running);
  CHECK(engine.status().current_repetition == 2);
  CHECK(same_state(engine.next_report(UINT64_MAX), dpad_state));
  CHECK(engine.status().output_pending);
  CHECK(engine.status().running);
}

void test_invalid_load_is_atomic_and_capacity_is_bounded() {
  Program valid_program;
  Program invalid;
  Program too_many;
  Program full;
  MacroEngine engine;
  submit_initial_neutral(engine);
  const GamepadState cross = pressed(Button::kCross);
  fill_program(valid_program, {Step{cross, 20}}, 3);
  CHECK(engine.load(valid_program));

  fill_program(invalid, {Step{pressed(Button::kCircle), 10},
                         Step{neutral_state(), 0}},
               7);
  CHECK(!engine.load(invalid));
  fill_program(invalid, {Step{neutral_state(), 10}}, 1);
  invalid.steps[0].state.dpad = 9;
  CHECK(!engine.load(invalid));
  invalid.steps[0].state = neutral_state();
  invalid.steps[0].state.buttons = 1u << 16;
  CHECK(!engine.load(invalid));
  invalid.steps[0].state = neutral_state();
  invalid.steps[0].state.buttons = 1u << 15;
  CHECK(!engine.load(invalid));
  invalid.steps[0].state = neutral_state();
  invalid.steps[0].duration_ms = 600'001;
  CHECK(!engine.load(invalid));

  too_many.step_count = static_cast<std::uint16_t>(kMaxSteps + 1);
  CHECK(!engine.load(too_many));
  CHECK(engine.status().loaded_step_count == 1);
  CHECK(engine.status().repeat_count == 3);
  CHECK(engine.start(50));
  CHECK(same_state(engine.next_report(50), cross));

  // A full-capacity program is valid and every indexed access stays in bounds.
  engine.stop();
  engine.report_submitted(51);
  full.step_count = static_cast<std::uint16_t>(kMaxSteps);
  full.repeat_count = 1;
  for (std::size_t i = 0; i < kMaxSteps; ++i) {
    full.steps[i] = Step{neutral_state(), 1};
  }
  full.steps[0].duration_ms = 600'000;
  CHECK(engine.load(full));
  CHECK(engine.status().loaded_step_count == kMaxSteps);
}

}  // namespace

int main() {
  test_busy_endpoint_and_step_timing();
  test_stop_requires_neutral_submission();
  test_disconnect_aborts_without_auto_resume();
  test_finite_repeats_and_last_dwell();
  test_infinite_repeat_and_large_clock();
  test_invalid_load_is_atomic_and_capacity_is_bounded();
  std::cout << "All macro engine tests passed.\n";
  return 0;
}
