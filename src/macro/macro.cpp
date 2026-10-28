#include "macro.h"
#include "format.hpp"
#include "macro_recorder.hpp"
#include "bootsel_gesture.hpp"
#include "bt.h"
#include "button_remap.h"
#include "config.h"
#include "usb.h"
#include "utils.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/btstack_flash_bank.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "tusb.h"
#include <algorithm>
#include <cstring>

using namespace dongle_macro;
using namespace gamepad_macro;
extern bool bt_inquiring;
namespace {
constexpr uint32_t kConfigOffset = PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
constexpr uint32_t kSlotA = kConfigOffset - 2 * kSlotBytes;
static_assert(kConfigOffset >= 2 * kSlotBytes);
static_assert(kSlotA % FLASH_SECTOR_SIZE == 0);
static_assert(FLASH_PAGE_SIZE == kPageBytes);
Program program; // The only 16 KiB step buffer.
MacroEngine engine;
Recorder recorder;
BootselGesture gesture;
Snapshot snapshot;
Snapshot live_snapshot; // Bounded controller cache. init_feature never clears the saved snapshot.
GamepadState live_controls;
enum class Mode { Idle, RecordReady, Record, Full, Save, Error };
Mode mode = Mode::Idle;
int active_slot = -1;
uint32_t generation = 0;
bool program_ready = false;
bool start_pending = false;
bool rebind_pending = false;
uint32_t held_ms = 0;
uint32_t last_led_ms = 0;
bool led_state = false;
extern "C" char __flash_binary_end;

const uint8_t* slot_address(unsigned slot) {
    return reinterpret_cast<const uint8_t*>(XIP_BASE + kSlotA + slot * kSlotBytes);
}
const Snapshot& usb_snapshot() {
    // A replacement recording must not change the enumerated controller's
    // features or persona until its save has committed successfully.
    if (active_slot >= 0)
        return *reinterpret_cast<const Snapshot*>(slot_address(active_slot) + kPayloadStart);
    return snapshot;
}
bool same_identity(const Snapshot& left, const Snapshot& right) {
    return left.ds_mode == right.ds_mode &&
        std::memcmp(left.features, right.features, sizeof(left.features)) == 0;
}
GamepadState controls(const USBGetStateData& report) {
    const auto* b = reinterpret_cast<const uint8_t*>(&report);
    GamepadState s{};
    s.left_x = b[0]; s.left_y = b[1]; s.right_x = b[2]; s.right_y = b[3];
    s.left_trigger = b[4]; s.right_trigger = b[5];
    s.dpad = b[7] & 15;
    s.buttons = (b[7] >> 4) | (uint32_t(b[8]) << 4) | (uint32_t(b[9] & 7) << 12);
    return s;
}
void apply_controls(USBGetStateData& report, const GamepadState& s) {
    auto* b = reinterpret_cast<uint8_t*>(&report);
    b[0] = s.left_x; b[1] = s.left_y; b[2] = s.right_x; b[3] = s.right_y;
    b[4] = s.left_trigger; b[5] = s.right_trigger;
    b[7] = s.dpad | ((s.buttons & 15) << 4);
    b[8] = static_cast<uint8_t>(s.buttons >> 4);
    // Unsupported Edge buttons are neutral in a recorded gamepad state.
    b[9] = (s.buttons >> 12) & 7;
}
bool load_slot(int index) {
    if (index < 0 || !slot_valid(slot_address(index))) return false;
    Header h{};
    std::memcpy(&h, slot_address(index), sizeof(h));
    std::memcpy(&snapshot, slot_address(index) + kPayloadStart, sizeof(snapshot));
    std::memcpy(program.steps.data(), slot_address(index) + kPayloadStart + sizeof(snapshot), h.step_count * sizeof(Step));
    program.step_count = h.step_count;
    program.repeat_count = h.repeat_count;
    generation = h.generation;
    program_ready = engine.load(program);
    return program_ready;
}
void restore_saved() {
    program_ready = false;
    if (active_slot >= 0) load_slot(active_slot);
}

enum class SavePhase { None, Erase, Header, Payload, Verify, Commit, FinalVerify };
SavePhase save_phase = SavePhase::None;
Header save_header{};
unsigned save_slot = 0;
uint32_t save_position = 0;
alignas(4) uint8_t page[FLASH_PAGE_SIZE];
struct FlashOperation { uint32_t offset; bool erase; };
void __not_in_flash_func(flash_operation)(void* arg) {
    const auto& operation = *static_cast<FlashOperation*>(arg);
    const uint32_t irq = save_and_disable_interrupts();
    if (operation.erase) flash_range_erase(operation.offset, FLASH_SECTOR_SIZE);
    else flash_range_program(operation.offset, page, sizeof(page));
    restore_interrupts(irq);
}
bool flash_write(uint32_t position, bool erase) {
    FlashOperation op{kSlotA + save_slot * kSlotBytes + position, erase};
    return flash_safe_execute(flash_operation, &op, 100) == PICO_OK;
}
void save_failed() {
    save_phase = SavePhase::None;
    start_pending = false;
    restore_saved();
    mode = Mode::Error;
}
void begin_save() {
    start_pending = false;
    if (!snapshot_valid(snapshot) || !steps_valid(program.steps.data(), program.step_count)) {
        save_failed();
        return;
    }
    program.repeat_count = 0;
    program_ready = engine.load(program);
    if (!program_ready) { save_failed(); return; }
    save_header = {kMagic, 1, generation + 1,
                   uint32_t(sizeof(snapshot) + program.step_count * sizeof(Step)),
                   0, program.step_count, 0, sizeof(snapshot), 0};
    uint32_t crc = crc_update(0xffffffffu, &snapshot, sizeof(snapshot));
    crc = crc_update(crc, program.steps.data(), program.step_count * sizeof(Step));
    save_header.payload_crc = ~crc;
    save_header.header_crc = checksum(&save_header, offsetof(Header, header_crc));
    save_slot = active_slot == 0 ? 1 : 0;
    save_position = 0;
    save_phase = SavePhase::Erase;
    mode = Mode::Save;
}
void save_tick() {
    // One sector or page per main-loop pass. USB, BT and watchdog run between operations.
    switch (save_phase) {
    case SavePhase::None: return;
    case SavePhase::Erase:
        if (!flash_write(save_position, true)) { save_failed(); return; }
        save_position += FLASH_SECTOR_SIZE;
        if (save_position == kSlotBytes) save_phase = SavePhase::Header;
        return;
    case SavePhase::Header:
        std::memset(page, 0xff, sizeof(page));
        std::memcpy(page, &save_header, sizeof(save_header));
        if (!flash_write(0, false)) { save_failed(); return; }
        save_position = 0;
        save_phase = SavePhase::Payload;
        return;
    case SavePhase::Payload:
        std::memset(page, 0xff, sizeof(page));
        for (size_t i = 0; i < sizeof(page) && save_position + i < save_header.payload_bytes; ++i) {
            const size_t offset = save_position + i;
            page[i] = offset < sizeof(snapshot) ? reinterpret_cast<const uint8_t*>(&snapshot)[offset]
                : reinterpret_cast<const uint8_t*>(program.steps.data())[offset - sizeof(snapshot)];
        }
        if (!flash_write(kPayloadStart + save_position, false)) { save_failed(); return; }
        save_position += sizeof(page);
        if (save_position >= save_header.payload_bytes) save_phase = SavePhase::Verify;
        return;
    case SavePhase::Verify:
        if (!slot_valid(slot_address(save_slot), false)) { save_failed(); return; }
        save_phase = SavePhase::Commit;
        return;
    case SavePhase::Commit: {
        const Commit commit{kCommitMagic, save_header.generation, save_header.payload_crc, save_header.header_crc};
        std::memset(page, 0xff, sizeof(page));
        std::memcpy(page, &commit, sizeof(commit));
        if (!flash_write(kCommitStart, false)) { save_failed(); return; }
        save_phase = SavePhase::FinalVerify;
        return;
    }
    case SavePhase::FinalVerify: {
        if (!slot_valid(slot_address(save_slot))) { save_failed(); return; }
        const bool rebind = macro_keep_usb() && !same_identity(usb_snapshot(), snapshot);
        active_slot = save_slot;
        generation = save_header.generation;
        program_ready = engine.load(program);
        save_phase = SavePhase::None;
        mode = program_ready ? Mode::Idle : Mode::Error;
        if (program_ready && rebind) {
            // Descriptor and feature changes need a new USB enumeration.
            // Recording always finishes stopped, including after this rebind.
            rebind_pending = true;
            usb_reconnect(false, true);
        }
        return;
    }
    }
}
void start_recording() {
    start_pending = false;
    if (!bt_is_connected() || !snapshot_valid(live_snapshot)) { mode = Mode::Error; return; }
    engine.stop();
    program_ready = false;
    snapshot = live_snapshot;
    const auto& cfg = get_config();
    snapshot.ds_mode = cfg.controller_mode == 2 ? !is_dse : cfg.controller_mode == 0;
    recorder.cancel();
    mode = recorder.begin(program, live_controls, time_us_64()) ? Mode::Record : Mode::Error;
}
void enter_record_ready() {
    if (mode == Mode::Save) return;
    if (mode == Mode::Record || mode == Mode::Full) {
        recorder.cancel();
        restore_saved();
    }
    engine.stop();
    start_pending = false;
    mode = Mode::RecordReady;
}
void pair_controller() {
    if (mode != Mode::Save && (mode == Mode::Record || mode == Mode::Full)) {
        recorder.cancel();
        restore_saved();
    }
    engine.stop();
    start_pending = false;
    if (mode != Mode::Save) mode = Mode::Idle;
    bt_bootsel_click_action();
}
void short_press() {
    if (mode == Mode::Record) {
        if (recorder.finish(time_us_64())) begin_save();
        else if (recorder.full()) mode = Mode::Full;
        else { restore_saved(); mode = Mode::Error; }
    } else if (mode == Mode::Full) {
        begin_save(); // Explicitly commit the full prefix; playback stays stopped.
    } else if (mode == Mode::RecordReady) {
        start_recording();
    } else if (mode == Mode::Save) {
        // A save commits the completed recording and never starts playback.
    } else if (engine.status().running || start_pending) {
        engine.stop();
        start_pending = false;
    } else {
        if (!program_ready) restore_saved();
        if (program_ready && macro_keep_usb()) { mode = Mode::Idle; start_pending = true; }
        else mode = Mode::Error;
    }
}
void led_tick(uint32_t now) {
    bool on;
    if (held_ms >= 6000) on = now % 100 < 50;
    else if (held_ms >= 3000 || mode == Mode::RecordReady) on = now % 1000 < 500;
    else if (mode == Mode::Error) on = now % 1000 < 100 || (now % 1000 >= 200 && now % 1000 < 300) || (now % 1000 >= 400 && now % 1000 < 500);
    else if (mode == Mode::Full) on = now % 1000 < 700;
    else if (mode == Mode::Save) on = now % 100 < 50;
    else if (mode == Mode::Record) on = now % 300 < 150;
    else if (engine.status().running || start_pending) on = true;
    else if (bt_inquiring) on = now % 1000 < 100 || (now % 1000 >= 200 && now % 1000 < 300);
    else on = now % 2000 < 80;
    if (now - last_led_ms >= 20 && on != led_state) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        led_state = on;
        last_led_ms = now;
    }
}
} // namespace

void macro_init() {
    // Export the exact SDK-derived storage limit to the linker assertion.
#ifndef GAMEPAD_MACRO_HOST_TEST
    asm volatile(".global __macro_flash_start\n.equ __macro_flash_start, %c0" : : "i" (XIP_BASE + kSlotA));
#endif
    const bool a = slot_valid(slot_address(0));
    const bool b = slot_valid(slot_address(1));
    Header ha{}, hb{};
    if (a) std::memcpy(&ha, slot_address(0), sizeof(ha));
    if (b) std::memcpy(&hb, slot_address(1), sizeof(hb));
    if (a || b) {
        active_slot = b && (!a || int32_t(hb.generation - ha.generation) > 0) ? 1 : 0;
        load_slot(active_slot);
    }
}
void macro_task() {
    save_tick();
    if (rebind_pending && !usb_reconfiguring && tud_mounted() && !tud_suspended())
        rebind_pending = false;
    if (mode == Mode::Record) {
        recorder.observe(live_controls, time_us_64());
        if (recorder.full()) mode = Mode::Full;
        else if (!recorder.recording()) { restore_saved(); mode = Mode::Error; }
    }
    if (start_pending && mode != Mode::Save && program_ready && !engine.status().output_pending && tud_mounted() && !tud_suspended()) {
        if (engine.start(time_us_64())) start_pending = false;
    }
    led_tick(to_ms_since_boot(get_absolute_time()));
}
void macro_live_input(const USBGetStateData& report) {
    if (!bt_is_connected()) return; // The upstream synthetic disconnect report is not a sample.
    USBGetStateData mapped = report;
    button_remap_apply(mapped);
    const auto sample = controls(mapped);
    if (sample.dpad > 8) return;
    live_controls = sample;
    if (!live_snapshot.input_valid) {
        USBGetStateData neutral = report;
        apply_controls(neutral, neutral_state());
        neutral.Touch.Finger[0].NotTouching = 1;
        neutral.Touch.Finger[1].NotTouching = 1;
        neutral.AngularVelocityX = neutral.AngularVelocityY = neutral.AngularVelocityZ = 0;
        neutral.AccelerometerX = neutral.AccelerometerY = neutral.AccelerometerZ = 0;
        std::memcpy(live_snapshot.input, &neutral, sizeof(neutral));
        live_snapshot.input_valid = 1;
    }
    const auto& cfg = get_config();
    live_snapshot.ds_mode = cfg.controller_mode == 2 ? !is_dse : cfg.controller_mode == 0;
    if (mode == Mode::Record) {
        recorder.observe(live_controls, time_us_64());
        if (recorder.full()) mode = Mode::Full;
        else if (!recorder.recording()) { restore_saved(); mode = Mode::Error; }
    }
}
void macro_controller_init() { live_snapshot = {}; }
void macro_feature(uint8_t id, const uint8_t* data, uint16_t size) {
    if (!bt_is_connected() || !size || size > 64) return;
    Feature* free = nullptr;
    for (auto& f : live_snapshot.features) {
        if (f.size && f.id == id) { free = &f; break; }
        if (!f.size && !free) free = &f;
    }
    if (!free) return;
    *free = {};
    free->id = id;
    free->size = static_cast<uint8_t>(size);
    std::memcpy(free->data, data, size);
}
uint16_t macro_get_feature(uint8_t id, uint8_t* data, uint16_t size) {
    if (!macro_keep_usb()) return 0;
    for (const auto& f : usb_snapshot().features) {
        if (f.size && f.id == id) {
            const auto n = std::min<uint16_t>(size, f.size);
            std::memcpy(data, f.data, n);
            return n;
        }
    }
    return 0;
}
bool macro_keep_usb() { return (active_slot >= 0 || program_ready) && snapshot_valid(usb_snapshot()); }
bool macro_persona(bool fallback) { return macro_keep_usb() ? usb_snapshot().ds_mode : fallback; }
bool macro_override(USBGetStateData& report) {
    const auto status = engine.status();
    if (!status.running && !status.output_pending && (bt_is_connected() || !macro_keep_usb())) return false;
    if (snapshot_valid(snapshot)) std::memcpy(&report, snapshot.input, sizeof(report));
    apply_controls(report, engine.next_report(time_us_64()));
    return true;
}
void macro_submitted() { engine.report_submitted(time_us_64()); }
void macro_bt_disconnect() {
    if (mode == Mode::Record || mode == Mode::Full) {
        recorder.cancel();
        engine.stop();
        restore_saved();
        mode = Mode::Error;
    }
}
void macro_usb_stop() {
    start_pending = false;
    rebind_pending = false;
    engine.disconnect();
}
void macro_button_sample(bool valid, bool pressed, uint32_t now_ms) {
    (void) now_ms;
    const auto wall_ms = time_us_64() / 1000;
    const auto action = gesture.sample(valid, pressed, wall_ms);
    held_ms = static_cast<uint32_t>(std::min<uint64_t>(gesture.held_ms(wall_ms), UINT32_MAX));
    if (mode == Mode::Save) {
        // Saving is atomic. Ignore short and record-ready gestures; pairing can
        // proceed while the write finishes, without changing the save state.
        if (action == GestureAction::Pair) pair_controller();
        return;
    }
    if (action == GestureAction::Pair) {
        pair_controller();
    } else if (action == GestureAction::ArmRecord) {
        enter_record_ready();
    } else if (action == GestureAction::Short) {
        short_press();
    }
}

extern "C" void tud_umount_cb() {
    const bool resume_rebind = rebind_pending && usb_reconfiguring;
    macro_usb_stop();
    rebind_pending = resume_rebind;
}
