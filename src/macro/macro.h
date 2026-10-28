#pragma once
#include <cstdint>
struct USBGetStateData;

#if ENABLE_GAMEPAD_MACRO
void macro_init();
void macro_task();
void macro_live_input(const USBGetStateData& report);
void macro_feature(uint8_t id, const uint8_t* data, uint16_t size);
void macro_controller_init();
uint16_t macro_get_feature(uint8_t id, uint8_t* data, uint16_t size);
bool macro_keep_usb();
bool macro_persona(bool fallback);
bool macro_override(USBGetStateData& report);
void macro_submitted();
void macro_bt_disconnect();
void macro_usb_stop();
void macro_button_sample(bool valid, bool pressed, uint32_t now_ms);
#else
inline void macro_init() {}
inline void macro_task() {}
inline void macro_live_input(const USBGetStateData&) {}
inline void macro_feature(uint8_t, const uint8_t*, uint16_t) {}
inline void macro_controller_init() {}
inline uint16_t macro_get_feature(uint8_t, uint8_t*, uint16_t) { return 0; }
inline bool macro_keep_usb() { return false; }
inline bool macro_persona(bool fallback) { return fallback; }
inline bool macro_override(USBGetStateData&) { return false; }
inline void macro_submitted() {}
inline void macro_bt_disconnect() {}
inline void macro_usb_stop() {}
inline void macro_button_sample(bool, bool, uint32_t) {}
#endif
