// Compile the production runtime with deterministic SDK and transport stubs.
// Including it also lets each test reset its static boot state without adding
// a reset API to the firmware.
#include "../../src/macro/macro.cpp"
#include <array>
#include <cassert>
#include <cstdio>

alignas(4) std::uint8_t test_flash[4 * 1024 * 1024];
bool is_dse = false, usb_reconfiguring = false, bt_inquiring = false;
namespace {
std::uint64_t clock_us;
bool connected, mounted, suspended, remap, led_output;
int flash_operations, fail_operation, corrupt_operation, reconnects, pair_calls;
TestConfig config;

void boot(bool erase) {
    if (erase) std::memset(test_flash, 0xff, sizeof(test_flash));
    program = {}; engine = {}; recorder = {}; gesture = {};
    snapshot = {}; live_snapshot = {}; live_controls = {};
    mode = Mode::Idle; active_slot = -1; generation = 0;
    program_ready = start_pending = rebind_pending = false;
    held_ms = last_led_ms = 0; led_state = led_output = false;
    save_phase = SavePhase::None; save_position = 0;
    clock_us = 1'000'000; connected = true; mounted = true;
    suspended = remap = usb_reconfiguring = is_dse = false;
    flash_operations = fail_operation = corrupt_operation = reconnects = pair_calls = 0;
    config = {};
    macro_init();
}
USBGetStateData input(std::uint8_t buttons = 0) {
    USBGetStateData report{};
    auto* bytes = reinterpret_cast<std::uint8_t*>(&report);
    bytes[0] = bytes[1] = bytes[2] = bytes[3] = 128;
    bytes[7] = 8 | (buttons << 4);
    report.AngularVelocityX = 123;
    return report;
}
void controller(std::uint8_t identity = 1, bool ds = true) {
    macro_controller_init();
    config.controller_mode = ds ? 0 : 1;
    std::uint8_t data[64]{};
    const std::uint8_t ids[] = {0x05, 0x09, 0x20, 0x22};
    const std::uint8_t sizes[] = {40, 19, 63, 63};
    data[0] = identity;
    for (unsigned i = 0; i < 4; ++i) macro_feature(ids[i], data, sizes[i]);
    macro_live_input(input());
    assert(snapshot_valid(live_snapshot));
}
void press(std::uint64_t duration_ms) {
    macro_button_sample(true, true, 0);
    clock_us += duration_ms * 1000;
    macro_button_sample(true, false, 0);
}
USBGetStateData submit(bool accepted = true) {
    auto report = input();
    if (macro_override(report) && accepted) macro_submitted();
    return report;
}
void finish_save() {
    for (unsigned i = 0; save_phase != SavePhase::None && i < 1000; ++i) macro_task();
    assert(save_phase == SavePhase::None);
}
void record(std::uint8_t buttons) {
    press(3000);
    assert(mode == Mode::RecordReady && !recorder.recording());
    press(100);
    assert(mode == Mode::Record);
    clock_us += 10'000;
    macro_live_input(input(buttons));
    clock_us += 20'000;
    press(100);
    assert(mode == Mode::Save && !start_pending);
}
void save_initial() {
    controller(); record(1); finish_save();
    assert(active_slot == 0 && program_ready && macro_keep_usb());
    assert(mode == Mode::Idle && !start_pending && !engine.status().running);
}
void test_ready_is_not_capture_and_clicks_save_then_toggle_playback() {
    boot(true); controller();
    press(3000);
    assert(mode == Mode::RecordReady && !recorder.recording());
    assert(program.step_count == 0 && !start_pending);

    // Input and elapsed time during the armed interval must not enter the macro.
    clock_us += 1'500'000;
    macro_live_input(input(2));
    clock_us += 500'000;
    macro_task();
    assert(mode == Mode::RecordReady && !recorder.recording());
    press(100);
    assert(mode == Mode::Record && recorder.recording());

    clock_us += 25'000;
    macro_live_input(input(4));
    clock_us += 25'000;
    press(100);
    assert(mode == Mode::Save && !start_pending);
    finish_save();
    assert(mode == Mode::Idle && !start_pending && !engine.status().running);
    assert(program.step_count == 2);
    assert(program.steps[0].state.buttons == 2 && program.steps[0].duration_ms == 25);
    assert(program.steps[1].state.buttons == 4);

    // The next ordinary clicks start and stop the committed loop.
    submit();
    press(100); macro_task();
    assert(engine.status().running && !start_pending);
    press(100);
    assert(!engine.status().running && !start_pending);
    submit();
}
void test_rearm_cancels_unsaved_capture_and_armed_bt_loss_is_safe() {
    boot(true); save_initial();
    const auto saved_steps = program.step_count;
    controller(2);
    press(3000);
    assert(mode == Mode::RecordReady && !recorder.recording());
    press(100);
    assert(mode == Mode::Record && recorder.recording());
    clock_us += 10'000;
    macro_live_input(input(3));

    press(3000); // Cancel the unsaved recording and return to record-ready.
    assert(mode == Mode::RecordReady && !recorder.recording());
    assert(active_slot == 0 && program_ready && program.step_count == saved_steps);
    assert(snapshot.features[0].data[0] == 1);

    connected = false;
    macro_bt_disconnect();
    assert(mode == Mode::RecordReady && !recorder.recording());
    press(100);
    assert(mode == Mode::Error && !recorder.recording() && pair_calls == 0);
    assert(active_slot == 0 && program_ready);

    connected = true; controller(3);
    press(3000); press(100);
    assert(mode == Mode::Record && recorder.recording());
    press(6000); // Pairing cancels the unsaved recording and restores the save.
    assert(pair_calls == 1 && mode == Mode::Idle && !start_pending);
    assert(!recorder.recording() && active_slot == 0 && program_ready);
    assert(program.step_count == saved_steps && snapshot.features[0].data[0] == 1);
}
void test_runtime_pairing_boundary_is_5999_record_and_6000_pair() {
    boot(true); save_initial(); controller(2);
    press(5'999);
    assert(mode == Mode::RecordReady && pair_calls == 0 && !recorder.recording());
    press(6'000);
    assert(mode == Mode::Idle && pair_calls == 1 && active_slot == 0 && program_ready);

    press(5'999);
    assert(mode == Mode::RecordReady && pair_calls == 1 && !recorder.recording());
    press(100);
    assert(mode == Mode::Record && recorder.recording());
    press(6'000);
    assert(mode == Mode::Idle && pair_calls == 2 && !recorder.recording());
    assert(active_slot == 0 && program_ready && !start_pending);
}
void test_short_press_without_saved_macro_does_not_start_pairing() {
    boot(true); connected = false;
    press(100);
    assert(mode == Mode::Error && pair_calls == 0 && !start_pending);
}
void test_record_ready_led_differs_from_recording_led() {
    boot(true); controller(); press(3000);
    assert(mode == Mode::RecordReady);
    last_led_ms = 0; led_state = led_output = false;
    led_tick(20); assert(led_output);
    led_tick(499); assert(led_output);
    led_tick(500); assert(!led_output);
    led_tick(1000); assert(led_output);

    press(100);
    assert(mode == Mode::Record);
    last_led_ms = 0; led_state = led_output = false;
    led_tick(20); assert(led_output);
    led_tick(150); assert(!led_output);
    led_tick(300); assert(led_output);
}
void test_saved_boot_and_usb_backpressure() {
    boot(true); save_initial(); boot(false);
    connected = false;
    assert(macro_keep_usb());
    assert(!engine.status().running && !start_pending);
    submit(); press(100); macro_task();
    assert(engine.status().running);
    submit(false); clock_us += 5'000'000; submit(false);
    assert(engine.status().current_step_index == 0);
    submit(); clock_us += 9'000; submit();
    assert(engine.status().current_step_index == 0);
    clock_us += 1'000;
    auto report = submit();
    assert(reinterpret_cast<const std::uint8_t*>(&report)[7] == 0x18);
    macro_bt_disconnect();
    assert(engine.status().running);
    macro_usb_stop(); submit(); macro_task();
    assert(!engine.status().running && !start_pending);
}
void test_saved_boot_gestures_leave_pairing_to_six_second_hold() {
    boot(true); save_initial(); boot(false);
    connected = false;
    bt_inquiring = false;
    assert(macro_keep_usb() && !engine.status().running && !start_pending);
    last_led_ms = 0; led_state = led_output = false;
    led_tick(20); assert(led_output);
    led_tick(80); assert(!led_output); // Idle pulse, not inquiry flashes.
    submit();
    press(100); macro_task();
    assert(engine.status().running && pair_calls == 0);
    press(100);
    press(3000);
    assert(mode == Mode::RecordReady && pair_calls == 0);
    press(100);
    assert(mode == Mode::Error && pair_calls == 0);
    press(6000);
    assert(mode == Mode::Idle && pair_calls == 1);
    macro_task();
    assert(pair_calls == 1); // No deferred runtime retry after disconnect.
}
void test_replacement_identity_stays_pinned_until_commit() {
    boot(true); save_initial();
    controller(2, false); record(2);
    std::uint8_t feature[64]{};
    assert(macro_get_feature(0x09, feature, sizeof(feature)) == 19 && feature[0] == 1);
    assert(macro_persona(false));
    finish_save();
    assert(active_slot == 1 && reconnects == 1 && rebind_pending);
    assert(!macro_persona(true));
    assert(macro_get_feature(0x09, feature, sizeof(feature)) == 19 && feature[0] == 2);
    macro_task(); assert(!engine.status().running);
    usb_reconfiguring = false; mounted = true;
    submit(); macro_task();
    assert(!engine.status().running && !start_pending && !rebind_pending);
}
void test_every_flash_operation_failure_retains_previous() {
    boot(true); save_initial();
    std::array<std::uint8_t, kSlotBytes> previous;
    std::memcpy(previous.data(), slot_address(0), previous.size());
    flash_operations = 0;
    controller(2); record(2); finish_save();
    const int operations = flash_operations;
    // Fail every erase and program operation in a replacement save.
    for (int operation = 1; operation <= operations; ++operation) {
        boot(true);
        std::memcpy(const_cast<std::uint8_t*>(slot_address(0)), previous.data(), previous.size());
        boot(false); controller(2); record(2);
        fail_operation = operation;
        finish_save();
        assert(mode == Mode::Error);
        assert(active_slot == 0 && program_ready && snapshot.features[0].data[0] == 1);
        assert(std::memcmp(previous.data(), slot_address(0), previous.size()) == 0);
        assert(!start_pending && !rebind_pending);
        boot(false);
        assert(active_slot == 0 && program_ready);
    }
    // Successful SDK calls can still leave a torn/corrupt flash page. Verify
    // catches header, payload, and final commit corruption before activation.
    const int corruptions[] = {17, 18, operations};
    for (const int operation : corruptions) {
        boot(true);
        std::memcpy(const_cast<std::uint8_t*>(slot_address(0)), previous.data(), previous.size());
        boot(false); controller(2); record(2);
        corrupt_operation = operation;
        finish_save();
        assert(mode == Mode::Error && active_slot == 0 && program_ready);
        boot(false);
        assert(active_slot == 0 && program_ready);
    }
}
void test_cancel_restore_remap_and_stale_features() {
    boot(true); save_initial();
    controller(2); remap = true; press(3000); assert(mode == Mode::RecordReady);
    press(100);
    assert(mode == Mode::Record && live_controls.buttons == 0);
    clock_us += 10'000; macro_live_input(input(1));
    assert(live_controls.buttons == 2);
    connected = false; macro_bt_disconnect();
    assert(mode == Mode::Error && program_ready && snapshot.features[0].data[0] == 1);
    macro_controller_init();
    std::uint8_t bytes[64]{};
    macro_feature(0x05, bytes, 40);
    assert(live_snapshot.features[0].size == 0);
    connected = true; controller();
    assert(reinterpret_cast<const USBGetStateData*>(live_snapshot.input)->AngularVelocityX == 0);
    press(6'000);
    assert(pair_calls == 1 && mode == Mode::Idle && !start_pending);
}
void test_usb_stop_during_save_or_rebind_cancels_start() {
    boot(true); save_initial(); controller(2); record(2);
    macro_usb_stop();
    finish_save();
    assert(reconnects == 1 && rebind_pending && !start_pending);
    usb_reconfiguring = false; mounted = true;
    submit(); macro_task();
    assert(!engine.status().running && !rebind_pending && !start_pending);

    controller(3); record(3); finish_save();
    assert(rebind_pending);
    suspended = true;
    macro_usb_stop(); // Genuine suspend also cancels an intentional rebind.
    usb_reconfiguring = false; mounted = true; suspended = false;
    submit(); macro_task();
    assert(!engine.status().running && !rebind_pending && !start_pending);
}
void test_full_prefix_waits_for_explicit_save() {
    boot(true); controller(); press(3000);
    assert(mode == Mode::RecordReady && !recorder.recording());
    press(100);
    for (std::size_t i = 1; i <= kMaxSteps; ++i) {
        auto report = input();
        reinterpret_cast<std::uint8_t*>(&report)[0] = static_cast<std::uint8_t>(i);
        clock_us += 1000;
        macro_live_input(report);
    }
    assert(mode == Mode::Full && !engine.status().running && !program_ready);
    assert(program.step_count == kMaxSteps && active_slot == -1);
    macro_task();
    assert(save_phase == SavePhase::None);
    press(100); finish_save();
    assert(program_ready && active_slot == 0);
    assert(mode == Mode::Idle && !start_pending && !engine.status().running);
}
void test_short_press_during_save_is_ignored_and_never_autoplays() {
    boot(true); save_initial(); controller(2); record(2);
    assert(!start_pending && save_phase != SavePhase::None);
    const auto phase = save_phase;
    press(100);
    assert(!start_pending && mode == Mode::Save && save_phase == phase);
    press(6'000);
    assert(mode == Mode::Save && save_phase == phase && pair_calls == 1);
    finish_save();
    assert(active_slot == 1 && program_ready && reconnects == 1);
    assert(rebind_pending && !start_pending);
    usb_reconfiguring = false; mounted = true;
    submit(); macro_task();
    assert(!engine.status().running && !rebind_pending && !start_pending);

    controller(2); record(3);
    press(100);
    assert(!start_pending);
    press(100);
    assert(!start_pending && mode == Mode::Save);
    finish_save();
    submit(); macro_task();
    assert(!engine.status().running && !start_pending);
}
}

const TestConfig& get_config() { return config; }
bool bt_is_connected() { return connected; }
void bt_bootsel_click_action() { ++pair_calls; }
void button_remap_apply(USBGetStateData& report) {
    if (!remap) return;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&report);
    const auto a = bytes[7] & 0x10, b = bytes[7] & 0x20;
    bytes[7] = static_cast<std::uint8_t>((bytes[7] & 0xcf) | (a << 1) | (b >> 1));
}
void usb_reconnect(bool keyboard_only, bool force) {
    assert(!keyboard_only && force);
    ++reconnects; usb_reconfiguring = true; mounted = false; tud_umount_cb();
}
std::uint64_t time_us_64() { return clock_us; }
std::uint64_t get_absolute_time() { return clock_us; }
std::uint32_t to_ms_since_boot(std::uint64_t time) { return static_cast<std::uint32_t>(time / 1000); }
bool tud_mounted() { return mounted; }
bool tud_suspended() { return suspended; }
void cyw43_arch_gpio_put(unsigned, bool value) { led_output = value; }
std::uint32_t save_and_disable_interrupts() { return 0; }
void restore_interrupts(std::uint32_t) {}
void flash_range_erase(std::uint32_t offset, std::size_t length) { std::memset(test_flash + offset, 0xff, length); }
void flash_range_program(std::uint32_t offset, const std::uint8_t* data, std::size_t length) {
    if (flash_operations == corrupt_operation) length = 8;
    for (std::size_t i = 0; i < length; ++i) test_flash[offset + i] &= data[i];
}
int flash_safe_execute(void (*callback)(void*), void* data, std::uint32_t) {
    ++flash_operations;
    if (flash_operations == fail_operation) return -1;
    callback(data); return 0;
}
int main() {
    test_ready_is_not_capture_and_clicks_save_then_toggle_playback();
    test_rearm_cancels_unsaved_capture_and_armed_bt_loss_is_safe();
    test_runtime_pairing_boundary_is_5999_record_and_6000_pair();
    test_short_press_without_saved_macro_does_not_start_pairing();
    test_record_ready_led_differs_from_recording_led();
    test_saved_boot_and_usb_backpressure();
    test_saved_boot_gestures_leave_pairing_to_six_second_hold();
    test_replacement_identity_stays_pinned_until_commit();
    test_every_flash_operation_failure_retains_previous();
    test_cancel_restore_remap_and_stale_features();
    test_usb_stop_during_save_or_rebind_cancels_start();
    test_full_prefix_waits_for_explicit_save();
    test_short_press_during_save_is_ignored_and_never_autoplays();
    std::puts("Macro runtime integration tests passed");
}
