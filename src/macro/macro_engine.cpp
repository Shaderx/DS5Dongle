#include "macro_engine.hpp"

#include <limits>

namespace gamepad_macro {

bool MacroEngine::valid_state(const GamepadState& state) noexcept {
  return (state.buttons & ~kValidButtonMask) == 0 &&
         state.dpad <= static_cast<std::uint8_t>(Dpad::kNeutral);
}

bool MacroEngine::valid_step(const Step& step) noexcept {
  return step.duration_ms >= kMinStepDurationMs &&
         step.duration_ms <= kMaxStepDurationMs && valid_state(step.state);
}

bool MacroEngine::load(const Program& program) noexcept {
  if (running_ || program.step_count == 0 || program.step_count > kMaxSteps) {
    return false;
  }

  for (std::uint16_t i = 0; i < program.step_count; ++i) {
    if (!valid_step(program.steps[i])) {
      return false;
    }
  }

  // Validation completes before switching the borrowed reference, so a failed
  // load keeps the previous program intact. The count check bounds each read.
  program_ = &program;
  step_count_ = program.step_count;
  repeat_count_ = program.repeat_count;
  loaded_ = true;
  return true;
}

bool MacroEngine::start(std::uint64_t now_us) noexcept {
  if (!loaded_ || running_ || output_pending_) {
    return false;
  }

  running_ = true;
  current_step_index_ = 0;
  current_repetition_ = 1;
  start_requested_at_us_ = now_us;
  step_timer_active_ = false;
  queue_step(0);
  return true;
}

void MacroEngine::stop() noexcept {
  running_ = false;
  current_step_index_ = 0;
  current_repetition_ = 0;
  queue_neutral();
}

void MacroEngine::disconnect() noexcept {
  running_ = false;
  current_step_index_ = 0;
  current_repetition_ = 0;
  queue_neutral();
}

const GamepadState& MacroEngine::next_report(std::uint64_t now_us) noexcept {
  update_for_time(now_us);
  return output_;
}

void MacroEngine::report_submitted(std::uint64_t now_us) noexcept {
  if (!output_pending_) {
    return;
  }

  output_pending_ = false;
  if (pending_is_step_ && running_) {
    // start() establishes a lower bound for the first step's timestamp. Under
    // the documented monotonic-clock contract, this is normally now_us.
    if (current_step_index_ == 0 && current_repetition_ == 1 &&
        now_us < start_requested_at_us_) {
      step_submitted_at_us_ = start_requested_at_us_;
    } else {
      step_submitted_at_us_ = now_us;
    }
    step_timer_active_ = true;
  } else {
    step_timer_active_ = false;
  }
  pending_is_step_ = false;
}

Status MacroEngine::status() const noexcept {
  Status result;
  result.running = running_;
  result.loaded = loaded_;
  result.loaded_step_count = step_count_;
  result.current_step_index = current_step_index_;
  result.current_repetition = current_repetition_;
  result.repeat_count = repeat_count_;
  result.output_pending = output_pending_;
  return result;
}

void MacroEngine::queue_step(std::uint16_t index) noexcept {
  if (program_ == nullptr || index >= step_count_ || index >= kMaxSteps) {
    finish_run();
    return;
  }

  current_step_index_ = index;
  output_ = program_->steps[index].state;
  output_pending_ = true;
  pending_is_step_ = true;
  step_timer_active_ = false;
}

void MacroEngine::queue_neutral() noexcept {
  output_ = neutral_state();
  output_pending_ = true;
  pending_is_step_ = false;
  step_timer_active_ = false;
}

void MacroEngine::finish_run() noexcept {
  running_ = false;
  current_step_index_ = 0;
  current_repetition_ = 0;
  queue_neutral();
}

void MacroEngine::update_for_time(std::uint64_t now_us) noexcept {
  if (!running_ || output_pending_ || !step_timer_active_) {
    return;
  }

  if (program_ == nullptr || current_step_index_ >= step_count_ ||
      current_step_index_ >= kMaxSteps) {
    finish_run();
    return;
  }

  const std::uint64_t duration_us =
      static_cast<std::uint64_t>(
          program_->steps[current_step_index_].duration_ms) * 1'000u;
  if (now_us < step_submitted_at_us_ ||
      now_us - step_submitted_at_us_ < duration_us) {
    return;
  }

  step_timer_active_ = false;
  if (static_cast<std::size_t>(current_step_index_) + 1 < step_count_) {
    queue_step(static_cast<std::uint16_t>(current_step_index_ + 1));
    return;
  }

  if (repeat_count_ == 0 || current_repetition_ < repeat_count_) {
    if (current_repetition_ < std::numeric_limits<std::uint64_t>::max()) {
      ++current_repetition_;
    }
    queue_step(0);
    return;
  }

  finish_run();
}

}  // namespace gamepad_macro
