#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamepad_macro {

inline constexpr std::size_t kMaxSteps = 1024;
inline constexpr std::uint32_t kMinStepDurationMs = 1;
inline constexpr std::uint32_t kMaxStepDurationMs = 600'000;

// Button bits follow the first two DualSense button bytes.
// bit 0 Square, 1 Cross, 2 Circle, 3 Triangle, 4 L1, 5 R1, 6 L2, 7 R2,
// 8 Create, 9 Options, 10 L3, 11 R3, 12 PS, 13 touchpad click, 14 mute.
// Bit 15 is reserved and rejected. Bits 16 through 31 are also invalid.
enum class Button : std::uint8_t {
  kSquare = 0,
  kCross = 1,
  kCircle = 2,
  kTriangle = 3,
  kL1 = 4,
  kR1 = 5,
  kL2 = 6,
  kR2 = 7,
  kCreate = 8,
  kOptions = 9,
  kL3 = 10,
  kR3 = 11,
  kPs = 12,
  kTouchpad = 13,
  kMute = 14,
  kReserved15 = 15,
};

inline constexpr std::uint32_t kValidButtonMask = 0x0000'7FFFu;

constexpr std::uint32_t button_mask(Button button) noexcept {
  return std::uint32_t{1} << static_cast<std::uint8_t>(button);
}

// Values 0 through 7 use the standard clockwise hat-switch order.
enum class Dpad : std::uint8_t {
  kUp = 0,
  kUpRight = 1,
  kRight = 2,
  kDownRight = 3,
  kDown = 4,
  kDownLeft = 5,
  kLeft = 6,
  kUpLeft = 7,
  kNeutral = 8,
};

struct GamepadState {
  std::uint32_t buttons = 0;
  std::uint8_t dpad = static_cast<std::uint8_t>(Dpad::kNeutral);
  std::uint8_t left_x = 128;
  std::uint8_t left_y = 128;
  std::uint8_t right_x = 128;
  std::uint8_t right_y = 128;
  std::uint8_t left_trigger = 0;
  std::uint8_t right_trigger = 0;
};

constexpr GamepadState neutral_state() noexcept { return {}; }

struct Step {
  GamepadState state{};
  std::uint32_t duration_ms = kMinStepDurationMs;
};

static_assert(sizeof(Step) == 16,
              "Step must stay 16 bytes for bounded playback storage.");
static_assert(kMaxSteps * sizeof(Step) == 16 * 1024,
              "The fixed macro payload must stay 16 KiB.");

// repeat_count is the total number of complete program passes. Zero means
// repeat forever. A program must contain at least one step.
// The caller owns this fixed buffer. Keep it alive and unchanged while the
// engine has it loaded. A load call borrows the program; it does not copy it.
struct Program {
  std::array<Step, kMaxSteps> steps{};
  std::uint16_t step_count = 0;
  std::uint16_t repeat_count = 1;
};

struct Status {
  bool running = false;
  bool loaded = false;
  std::uint16_t loaded_step_count = 0;
  // current_step_index is zero-based and is meaningful while running.
  std::uint16_t current_step_index = 0;
  // current_repetition is one-based while running, and zero while idle.
  std::uint64_t current_repetition = 0;
  // Zero means infinite repetition. Otherwise this is the total pass count.
  std::uint16_t repeat_count = 0;
  // True while the current output transition still needs a successful submit.
  bool output_pending = false;
};

class MacroEngine {
 public:
  MacroEngine() noexcept = default;

  // Borrows a valid program. Invalid input and calls while running leave the
  // loaded program unchanged. The caller must keep the program alive and
  // unchanged until it is replaced by another successful load.
  bool load(const Program& program) noexcept;

  // Starts at step zero. A pending output, including neutral output, must be
  // submitted before a new run can start.
  bool start(std::uint64_t now_us) noexcept;

  // Aborts playback and requires neutral output to be submitted.
  void stop() noexcept;

  // Aborts playback and leaves neutral output pending for reconnect.
  // Playback never resumes automatically.
  void disconnect() noexcept;

  // Returns the state to send now. It returns the same pending transition
  // until report_submitted() acknowledges a successful USB queue operation.
  // Once acknowledged, the state remains current until its dwell expires.
  // The call never sleeps and advances at most one step per acknowledgement.
  const GamepadState& next_report(std::uint64_t now_us) noexcept;

  // Call only after the returned report was accepted by the USB queue.
  // The dwell for a programmed step starts at this timestamp.
  void report_submitted(std::uint64_t now_us) noexcept;

  Status status() const noexcept;

 private:
  static bool valid_state(const GamepadState& state) noexcept;
  static bool valid_step(const Step& step) noexcept;

  void queue_step(std::uint16_t index) noexcept;
  void queue_neutral() noexcept;
  void finish_run() noexcept;
  void update_for_time(std::uint64_t now_us) noexcept;

  const Program* program_ = nullptr;
  std::uint16_t step_count_ = 0;
  std::uint16_t repeat_count_ = 0;
  std::uint16_t current_step_index_ = 0;
  std::uint64_t current_repetition_ = 0;
  std::uint64_t start_requested_at_us_ = 0;
  std::uint64_t step_submitted_at_us_ = 0;
  GamepadState output_ = neutral_state();
  bool loaded_ = false;
  bool running_ = false;
  bool output_pending_ = true;
  bool pending_is_step_ = false;
  bool step_timer_active_ = false;
};

}  // namespace gamepad_macro
