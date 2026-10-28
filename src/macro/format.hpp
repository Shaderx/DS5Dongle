#pragma once

#include "macro_engine.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dongle_macro {
constexpr uint32_t kSlotBytes = 65536;
constexpr uint32_t kPageBytes = 256;
constexpr uint32_t kMagic = 0x354d5344; // DSM5
constexpr uint32_t kCommitMagic = 0x54494d43;
constexpr uint32_t kMaxTotalMs = 24u * 60u * 60u * 1000u;
constexpr size_t kFeatureCount = 32;
struct Feature {
    uint8_t id = 0;
    uint8_t size = 0;
    uint8_t data[64]{};
};
struct alignas(4) Snapshot {
    Feature features[kFeatureCount]{};
    uint8_t input[63]{};
    uint8_t input_valid = 0;
    uint8_t ds_mode = 1;
    uint8_t reserved[3]{};
};
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t payload_bytes;
    uint32_t payload_crc;
    uint16_t step_count;
    uint16_t repeat_count;
    uint32_t snapshot_bytes;
    uint32_t header_crc;
};
struct Commit {
    uint32_t magic;
    uint32_t generation;
    uint32_t payload_crc;
    uint32_t header_crc;
};
constexpr size_t kPayloadStart = kPageBytes;
constexpr size_t kCommitStart = kSlotBytes - kPageBytes;
static_assert(sizeof(Snapshot) + sizeof(gamepad_macro::Step) * gamepad_macro::kMaxSteps < kCommitStart - kPayloadStart);

inline uint32_t crc_update(uint32_t crc, const void* bytes, size_t size) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    while (size--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
inline uint32_t checksum(const void* bytes, size_t size) {
    return ~crc_update(0xffffffffu, bytes, size);
}
inline bool snapshot_valid(const Snapshot& snapshot) {
    if (snapshot.input_valid != 1 || snapshot.ds_mode > 1 || snapshot.reserved[0] ||
        snapshot.reserved[1] || snapshot.reserved[2]) return false;
    const auto* input = snapshot.input;
    if (input[0] != 128 || input[1] != 128 || input[2] != 128 || input[3] != 128 ||
        input[4] || input[5] || input[7] != 8 || input[8] || input[9] ||
        !(input[32] & 0x80) || !(input[36] & 0x80)) return false;
    bool required[4]{};
    const uint8_t ids[] = {0x05, 0x09, 0x20, 0x22};
    const uint8_t lengths[] = {40, 19, 63, 63};
    for (size_t i = 0; i < kFeatureCount; ++i) {
        const auto& f = snapshot.features[i];
        if (f.size > sizeof(f.data)) return false;
        if (!f.size) continue;
        for (size_t j = 0; j < i; ++j)
            if (snapshot.features[j].size && snapshot.features[j].id == f.id) return false;
        for (size_t j = 0; j < 4; ++j)
            if (f.id == ids[j]) {
                if (f.size != lengths[j]) return false;
                required[j] = true;
            }
    }
    return required[0] && required[1] && required[2] && required[3];
}
inline bool header_valid(const Header& h) {
    return h.magic == kMagic && h.version == 1 && h.step_count > 0 &&
           h.step_count <= gamepad_macro::kMaxSteps && h.repeat_count == 0 &&
           h.snapshot_bytes == sizeof(Snapshot) &&
           h.payload_bytes == sizeof(Snapshot) + h.step_count * sizeof(gamepad_macro::Step) &&
           h.payload_bytes <= kCommitStart - kPayloadStart &&
           h.header_crc == checksum(&h, offsetof(Header, header_crc));
}
inline bool steps_valid(const gamepad_macro::Step* steps, size_t count) {
    if (!count || count > gamepad_macro::kMaxSteps) return false;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& s = steps[i];
        if (s.state.buttons & ~0x7fffu || s.state.dpad > 8 ||
            s.duration_ms < gamepad_macro::kMinStepDurationMs ||
            s.duration_ms > gamepad_macro::kMaxStepDurationMs) return false;
        total += s.duration_ms;
        if (total > kMaxTotalMs) return false;
    }
    return true;
}
// Validate every size before using it to address memory-mapped flash.
inline bool slot_valid(const uint8_t* slot, bool require_commit = true) {
    Header h{};
    std::memcpy(&h, slot, sizeof(h));
    if (!header_valid(h)) return false;
    if (require_commit) {
        Commit c{};
        std::memcpy(&c, slot + kCommitStart, sizeof(c));
        if (c.magic != kCommitMagic || c.generation != h.generation ||
            c.payload_crc != h.payload_crc || c.header_crc != h.header_crc) return false;
    }
    if (checksum(slot + kPayloadStart, h.payload_bytes) != h.payload_crc) return false;
    const auto* snapshot = reinterpret_cast<const Snapshot*>(slot + kPayloadStart);
    if (!snapshot_valid(*snapshot)) return false;
    const auto* steps = reinterpret_cast<const gamepad_macro::Step*>(slot + kPayloadStart + sizeof(Snapshot));
    return steps_valid(steps, h.step_count);
}
} // namespace dongle_macro
