#pragma once

#include <cstdint>

namespace gamepad_macro {

enum class GestureAction : std::uint8_t {
  None,
  Short,
  ArmRecord,
  Pair,
};

// BOOTSEL timing helper. It does not debounce input. The platform must pass
// debounced levels; failed reads leave the current state and timestamps alone.
// Actions are emitted only when a pressed state is followed by a valid release.
class BootselGesture {
 public:
  GestureAction sample(bool sample_ok, bool pressed,
                       std::uint64_t now_ms) noexcept {
    if (!sample_ok || (has_time_ && now_ms < last_sample_ms_)) {
      return GestureAction::None;
    }
    has_time_ = true;
    last_sample_ms_ = now_ms;

    if (pressed) {
      if (!pressed_) {
        pressed_ = true;
        pressed_at_ms_ = now_ms;
      }
      return GestureAction::None;
    }

    if (!pressed_) {
      return GestureAction::None;
    }

    const std::uint64_t held_for_ms = now_ms - pressed_at_ms_;
    pressed_ = false;
    if (held_for_ms < 3'000u) {
      return GestureAction::Short;
    }
    if (held_for_ms < 6'000u) {
      return GestureAction::ArmRecord;
    }
    return GestureAction::Pair;
  }

  // Returns wall-clock hold time in milliseconds for LED thresholds.
  // It returns zero when the button is not pressed or time moved backwards.
  std::uint64_t held_ms(std::uint64_t now_ms) const noexcept {
    if (!pressed_ || now_ms < pressed_at_ms_) {
      return 0;
    }
    return now_ms - pressed_at_ms_;
  }

  bool pressed() const noexcept { return pressed_; }

 private:
  std::uint64_t pressed_at_ms_ = 0;
  std::uint64_t last_sample_ms_ = 0;
  bool has_time_ = false;
  bool pressed_ = false;
};

}  // namespace gamepad_macro
