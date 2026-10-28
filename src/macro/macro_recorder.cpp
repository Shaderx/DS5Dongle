#include "macro_recorder.hpp"

#include <algorithm>

namespace gamepad_macro {

bool Recorder::valid_state(const GamepadState& state) noexcept {
  return (state.buttons & ~kValidButtonMask) == 0 &&
         state.dpad <= static_cast<std::uint8_t>(Dpad::kNeutral);
}

bool Recorder::same_state(const GamepadState& left,
                          const GamepadState& right) noexcept {
  return left.buttons == right.buttons && left.dpad == right.dpad &&
         left.left_x == right.left_x && left.left_y == right.left_y &&
         left.right_x == right.right_x && left.right_y == right.right_y &&
         left.left_trigger == right.left_trigger &&
         left.right_trigger == right.right_trigger;
}

bool Recorder::begin(Program& program, const GamepadState& initial,
                     std::uint64_t now_us) noexcept {
  if (recording() || !valid_state(initial)) {
    return false;
  }

  program_ = &program;
  program.step_count = 0;
  program.repeat_count = 0;
  current_state_ = initial;
  state_started_at_us_ = now_us;
  last_sample_at_us_ = now_us;
  total_duration_ms_ = 0;
  phase_ = RecorderPhase::kRecording;
  return true;
}

bool Recorder::observe(const GamepadState& state,
                       std::uint64_t now_us) noexcept {
  if (!recording()) {
    return false;
  }
  if (!valid_state(state) || now_us < last_sample_at_us_) {
    fail();
    return false;
  }

  last_sample_at_us_ = now_us;
  const std::uint64_t elapsed_us = now_us - state_started_at_us_;
  if (same_state(state, current_state_)) {
    const std::uint64_t elapsed_ms =
        std::max<std::uint64_t>(1, elapsed_us / 1'000u);
    const std::uint32_t remaining_ms =
        kMaxRecordingDurationMs - total_duration_ms_;
    if (elapsed_ms >= remaining_ms) {
      if (!append_elapsed(elapsed_us)) {
        return false;
      }
      mark_full();
      return false;
    }
    return true;
  }

  if (!append_elapsed(elapsed_us)) {
    return false;
  }
  if (total_duration_ms_ >= kMaxRecordingDurationMs ||
      program_->step_count >= kMaxSteps) {
    mark_full();
    return false;
  }

  current_state_ = state;
  state_started_at_us_ = now_us;
  return true;
}

bool Recorder::finish(std::uint64_t now_us) noexcept {
  if (!recording()) {
    return false;
  }
  if (now_us < last_sample_at_us_) {
    fail();
    return false;
  }

  last_sample_at_us_ = now_us;
  if (!append_elapsed(now_us - state_started_at_us_)) {
    return false;
  }

  phase_ = RecorderPhase::kComplete;
  return true;
}

void Recorder::cancel() noexcept {
  if (recording()) {
    phase_ = RecorderPhase::kCancelled;
  }
}

RecorderStatus Recorder::status() const noexcept {
  RecorderStatus result;
  result.phase = phase_;
  if (program_ != nullptr) {
    result.step_count = program_->step_count;
  }
  result.total_duration_ms = total_duration_ms_;
  return result;
}

bool Recorder::append_elapsed(std::uint64_t elapsed_us) noexcept {
  std::uint64_t remaining_ms = elapsed_us / 1'000u;
  if (remaining_ms == 0) {
    remaining_ms = 1;
  }

  while (remaining_ms > 0) {
    if (program_ == nullptr || program_->step_count >= kMaxSteps ||
        total_duration_ms_ >= kMaxRecordingDurationMs) {
      mark_full();
      return false;
    }

    const std::uint32_t available_duration_ms =
        kMaxRecordingDurationMs - total_duration_ms_;
    const std::uint64_t segment_ms = std::min<std::uint64_t>(
        remaining_ms,
        std::min<std::uint64_t>(kMaxStepDurationMs, available_duration_ms));
    if (segment_ms == 0) {
      mark_full();
      return false;
    }

    // Check the fixed buffer bound immediately before each write.
    if (program_->step_count >= kMaxSteps) {
      mark_full();
      return false;
    }
    program_->steps[program_->step_count] = Step{
        current_state_, static_cast<std::uint32_t>(segment_ms)};
    ++program_->step_count;
    total_duration_ms_ += static_cast<std::uint32_t>(segment_ms);
    remaining_ms -= segment_ms;

    if (remaining_ms > 0 &&
        (program_->step_count >= kMaxSteps ||
         total_duration_ms_ >= kMaxRecordingDurationMs)) {
      mark_full();
      return false;
    }
  }

  return true;
}

void Recorder::fail() noexcept { phase_ = RecorderPhase::kFailed; }

void Recorder::mark_full() noexcept { phase_ = RecorderPhase::kFull; }

}  // namespace gamepad_macro
