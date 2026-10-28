#pragma once

#include "macro_engine.hpp"

#include <cstdint>

namespace gamepad_macro {

inline constexpr std::uint32_t kMaxRecordingDurationMs = 86'400'000;

enum class RecorderPhase : std::uint8_t {
  kIdle,
  kRecording,
  kComplete,
  kFull,
  kFailed,
  kCancelled,
};

struct RecorderStatus {
  RecorderPhase phase = RecorderPhase::kIdle;
  std::uint16_t step_count = 0;
  std::uint32_t total_duration_ms = 0;
};

// Records into one caller-owned Program buffer. The caller must keep that
// buffer alive. Do not let a MacroEngine use it until recording has completed.
// The recorder quantizes elapsed microseconds down to whole milliseconds; a
// hold shorter than one millisecond becomes one millisecond. It merges
// identical consecutive states, splits longer holds at 600,000 ms, and stops
// at 1,024 steps or 24 hours without completing the program automatically.
class Recorder {
 public:
  Recorder() noexcept = default;

  // Starts a new recording in program. The finished program repeats forever.
  // Invalid input or an active recording leaves the supplied buffer unchanged.
  bool begin(Program& program, const GamepadState& initial,
             std::uint64_t now_us) noexcept;

  // Returns true while recording continues. Changed states close the prior
  // state's hold. Identical consecutive states are compressed into one step.
  bool observe(const GamepadState& state, std::uint64_t now_us) noexcept;

  // Closes the final hold and returns true only when the recording completes.
  // A full or failed recording does not become playable automatically.
  bool finish(std::uint64_t now_us) noexcept;

  // Stops recording and marks the buffer as cancelled. It does not clear it.
  void cancel() noexcept;

  bool full() const noexcept { return phase_ == RecorderPhase::kFull; }
  bool recording() const noexcept {
    return phase_ == RecorderPhase::kRecording;
  }
  RecorderStatus status() const noexcept;

 private:
  static bool valid_state(const GamepadState& state) noexcept;
  static bool same_state(const GamepadState& left,
                         const GamepadState& right) noexcept;

  bool append_elapsed(std::uint64_t elapsed_us) noexcept;
  void fail() noexcept;
  void mark_full() noexcept;

  Program* program_ = nullptr;
  GamepadState current_state_ = neutral_state();
  std::uint64_t state_started_at_us_ = 0;
  std::uint64_t last_sample_at_us_ = 0;
  std::uint32_t total_duration_ms_ = 0;
  RecorderPhase phase_ = RecorderPhase::kIdle;
};

}  // namespace gamepad_macro
