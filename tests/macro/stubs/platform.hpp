#pragma once
#include <cstddef>
#include <cstdint>

#define __not_in_flash_func(name) name
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define PICO_FLASH_BANK_STORAGE_OFFSET 0x3fd000u
#define XIP_BASE reinterpret_cast<std::uintptr_t>(test_flash)
#define PICO_OK 0
#define CYW43_WL_GPIO_LED_PIN 0
extern std::uint8_t test_flash[4 * 1024 * 1024];

// The same 63-byte wire layout used by utils.h, with named fields accessed by
// macro.cpp. Packed fields are copied through bytes in production and tests.
#pragma pack(push, 1)
struct TestFinger {
    std::uint32_t Index : 7;
    std::uint32_t NotTouching : 1;
    std::uint32_t FingerX : 12;
    std::uint32_t FingerY : 12;
};
struct USBGetStateData {
    std::uint8_t controls[15];
    std::int16_t AngularVelocityX, AngularVelocityZ, AngularVelocityY;
    std::int16_t AccelerometerX, AccelerometerY, AccelerometerZ;
    std::uint8_t counters[5];
    struct { TestFinger Finger[2]; std::uint8_t Timestamp; } Touch;
    std::uint8_t status[22];
};
#pragma pack(pop)
static_assert(sizeof(USBGetStateData) == 63);

struct TestConfig { std::uint8_t controller_mode = 0; };
const TestConfig& get_config();
extern bool is_dse, usb_reconfiguring;
bool bt_is_connected();
void bt_bootsel_click_action();
void button_remap_apply(USBGetStateData&);
void usb_reconnect(bool keyboard_only, bool force = false);
std::uint64_t time_us_64();
std::uint64_t get_absolute_time();
std::uint32_t to_ms_since_boot(std::uint64_t time);
bool tud_mounted();
bool tud_suspended();
void cyw43_arch_gpio_put(unsigned, bool);
std::uint32_t save_and_disable_interrupts();
void restore_interrupts(std::uint32_t);
void flash_range_erase(std::uint32_t offset, std::size_t length);
void flash_range_program(std::uint32_t offset, const std::uint8_t* data, std::size_t length);
int flash_safe_execute(void (*callback)(void*), void* data, std::uint32_t timeout);
