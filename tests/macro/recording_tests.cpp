#include "bootsel_gesture.hpp"
#include "macro_recorder.hpp"

#include <cstdlib>
#include <iostream>

using gamepad_macro::BootselGesture;
using gamepad_macro::Dpad;
using gamepad_macro::GamepadState;
using gamepad_macro::GestureAction;
using gamepad_macro::Program;
using gamepad_macro::Recorder;
using gamepad_macro::RecorderPhase;
using gamepad_macro::kMaxRecordingDurationMs;
using gamepad_macro::kMaxStepDurationMs;
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

GamepadState dpad_state(Dpad direction) {
  GamepadState state = neutral_state();
  state.dpad = static_cast<std::uint8_t>(direction);
  return state;
}

void test_changed_state_compression_and_completion() {
  Program program;
  Recorder recorder;
  const GamepadState neutral = neutral_state();
  GamepadState cross = neutral;
  cross.buttons = 1u << 1;

  CHECK(recorder.begin(program, neutral, 1'000'000));
  CHECK(recorder.observe(neutral, 1'500'000));
  CHECK(recorder.status().step_count == 0);
  CHECK(recorder.observe(cross, 2'250'000));
  CHECK(recorder.status().step_count == 1);
  CHECK(recorder.status().total_duration_ms == 1'250);
  CHECK(recorder.observe(cross, 2'750'000));
  CHECK(recorder.status().step_count == 1);
  CHECK(recorder.finish(3'500'000));

  CHECK(recorder.status().phase == RecorderPhase::kComplete);
  CHECK(recorder.status().step_count == 2);
  CHECK(recorder.status().total_duration_ms == 2'500);
  CHECK(program.repeat_count == 0);
  CHECK(program.steps[0].duration_ms == 1'250);
  CHECK(same_state(program.steps[0].state, neutral));
  CHECK(program.steps[1].duration_ms == 1'250);
  CHECK(same_state(program.steps[1].state, cross));
}

void test_long_holds_split_and_total_duration_stops_full() {
  Program split_program;
  Recorder split_recorder;
  GamepadState trigger = neutral_state();
  trigger.right_trigger = 255;
  CHECK(split_recorder.begin(split_program, trigger, 0));
  CHECK(split_recorder.finish(
      static_cast<std::uint64_t>(kMaxStepDurationMs + 123'456) * 1'000u));
  CHECK(split_program.step_count == 2);
  CHECK(split_program.steps[0].duration_ms == kMaxStepDurationMs);
  CHECK(split_program.steps[1].duration_ms == 123'456);
  CHECK(same_state(split_program.steps[0].state, trigger));
  CHECK(same_state(split_program.steps[1].state, trigger));

  Program limited_program;
  Recorder limited_recorder;
  CHECK(limited_recorder.begin(limited_program, neutral_state(), 0));
  CHECK(!limited_recorder.observe(
      neutral_state(),
      static_cast<std::uint64_t>(kMaxRecordingDurationMs) * 1'000u));
  CHECK(limited_recorder.full());
  CHECK(limited_recorder.status().phase == RecorderPhase::kFull);
  CHECK(limited_recorder.status().total_duration_ms ==
        kMaxRecordingDurationMs);
  CHECK(limited_program.repeat_count == 0);
  CHECK(limited_program.step_count ==
        kMaxRecordingDurationMs / kMaxStepDurationMs);
  for (std::uint16_t i = 0; i < limited_program.step_count; ++i) {
    CHECK(limited_program.steps[i].duration_ms == kMaxStepDurationMs);
  }
  CHECK(!limited_recorder.finish(
      static_cast<std::uint64_t>(kMaxRecordingDurationMs) * 1'000u));
}

void test_step_capacity_stops_without_wrapping() {
  Program program;
  Recorder recorder;
  CHECK(recorder.begin(program, neutral_state(), 0));

  bool continued = true;
  for (std::uint16_t i = 1; i <= gamepad_macro::kMaxSteps; ++i) {
    GamepadState state = neutral_state();
    state.left_x = static_cast<std::uint8_t>(i % 256);
    continued = recorder.observe(state, static_cast<std::uint64_t>(i) * 1'000u);
    if (!continued) {
      break;
    }
  }

  CHECK(!continued);
  CHECK(recorder.full());
  CHECK(!recorder.recording());
  CHECK(program.step_count == gamepad_macro::kMaxSteps);
  CHECK(recorder.status().total_duration_ms == gamepad_macro::kMaxSteps);
  CHECK(same_state(program.steps[0].state, neutral_state()));
  CHECK(program.steps[gamepad_macro::kMaxSteps - 1].state.left_x == 255);
  CHECK(!recorder.finish(9'000'000));
}

void test_invalid_input_time_and_cancel() {
  Program program;
  program.step_count = 1;
  program.repeat_count = 1;
  Recorder recorder;

  GamepadState invalid = neutral_state();
  invalid.dpad = 9;
  CHECK(!recorder.begin(program, invalid, 0));
  CHECK(program.step_count == 1);
  CHECK(program.repeat_count == 1);

  CHECK(recorder.begin(program, neutral_state(), 100));
  invalid = neutral_state();
  invalid.buttons = 1u << 15;
  CHECK(!recorder.observe(invalid, 101));
  CHECK(recorder.status().phase == RecorderPhase::kFailed);
  CHECK(!recorder.finish(102));

  CHECK(recorder.begin(program, neutral_state(), 200));
  CHECK(!recorder.observe(neutral_state(), 199));
  CHECK(recorder.status().phase == RecorderPhase::kFailed);

  CHECK(recorder.begin(program, neutral_state(), 300));
  recorder.cancel();
  CHECK(recorder.status().phase == RecorderPhase::kCancelled);
  CHECK(!recorder.recording());
}

void test_bootsel_release_actions_and_hold_time() {
  BootselGesture gesture;
  CHECK(gesture.sample(false, false, 0) == GestureAction::None);
  CHECK(gesture.sample(true, true, 100) == GestureAction::None);
  CHECK(gesture.sample(true, true, 1'000) == GestureAction::None);
  CHECK(gesture.pressed());
  CHECK(gesture.held_ms(3'099) == 2'999);
  CHECK(gesture.sample(false, false, 3'099) == GestureAction::None);
  CHECK(gesture.pressed());
  CHECK(gesture.held_ms(3'100) == 3'000);
  CHECK(gesture.sample(true, false, 3'100) == GestureAction::Record);
  CHECK(!gesture.pressed());
  CHECK(gesture.held_ms(3'101) == 0);
  CHECK(gesture.sample(true, false, 3'102) == GestureAction::None);

  CHECK(gesture.sample(true, true, 10'000) == GestureAction::None);
  CHECK(gesture.held_ms(12'999) == 2'999);
  CHECK(gesture.sample(true, false, 12'999) == GestureAction::Short);

  CHECK(gesture.sample(true, true, 20'000) == GestureAction::None);
  CHECK(gesture.held_ms(29'999) == 9'999);
  CHECK(gesture.sample(true, false, 30'000) == GestureAction::Pair);
}

void test_bootsel_ignores_backward_clock_samples() {
  BootselGesture gesture;
  CHECK(gesture.sample(true, true, 1'000) == GestureAction::None);
  CHECK(gesture.sample(true, false, 999) == GestureAction::None);
  CHECK(gesture.pressed());
  CHECK(gesture.sample(true, false, 1'000) == GestureAction::Short);
}

}  // namespace

int main() {
  test_changed_state_compression_and_completion();
  test_long_holds_split_and_total_duration_stops_full();
  test_step_capacity_stops_without_wrapping();
  test_invalid_input_time_and_cancel();
  test_bootsel_release_actions_and_hold_time();
  test_bootsel_ignores_backward_clock_samples();
  std::cout << "All recording and BOOTSEL tests passed.\n";
  return 0;
}
