#include "format.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace dongle_macro;
using namespace gamepad_macro;
namespace {
alignas(4) std::array<uint8_t, kSlotBytes> image;
Snapshot snapshot;
Step step;
Header header;

void put_header() {
    header.header_crc = checksum(&header, offsetof(Header, header_crc));
    std::memcpy(image.data(), &header, sizeof(header));
}
void put_commit() {
    const Commit commit{kCommitMagic, header.generation, header.payload_crc, header.header_crc};
    std::memcpy(image.data() + kCommitStart, &commit, sizeof(commit));
}
void valid_image() {
    image.fill(0xff);
    snapshot = {};
    snapshot.input_valid = 1;
    snapshot.input[0] = snapshot.input[1] = snapshot.input[2] = snapshot.input[3] = 128;
    snapshot.input[7] = 8;
    snapshot.input[32] = snapshot.input[36] = 0x80;
    const uint8_t ids[] = {0x05, 0x09, 0x20, 0x22};
    const uint8_t sizes[] = {40, 19, 63, 63};
    for (size_t i = 0; i < 4; ++i) {
        snapshot.features[i].id = ids[i];
        snapshot.features[i].size = sizes[i];
    }
    step = {};
    step.duration_ms = 25;
    std::memcpy(image.data() + kPayloadStart, &snapshot, sizeof(snapshot));
    std::memcpy(image.data() + kPayloadStart + sizeof(snapshot), &step, sizeof(step));
    header = {kMagic, 1, 7, uint32_t(sizeof(snapshot) + sizeof(step)), 0, 1, 0, sizeof(snapshot), 0};
    header.payload_crc = checksum(image.data() + kPayloadStart, header.payload_bytes);
    put_header();
    put_commit();
    assert(slot_valid(image.data()));
}
void test_lengths_checked_before_access() {
    valid_image();
    header.step_count = 65535;
    put_header(); put_commit();
    assert(!slot_valid(image.data()));
    valid_image();
    header.step_count = 0;
    put_header(); put_commit();
    assert(!slot_valid(image.data()));
    valid_image();
    header.payload_bytes = 0xffffffff;
    put_header(); put_commit();
    assert(!slot_valid(image.data()));
    valid_image();
    header.snapshot_bytes = sizeof(snapshot) + 4;
    put_header(); put_commit();
    assert(!slot_valid(image.data()));
}
void test_corruption_and_interruption() {
    valid_image();
    image[kPayloadStart + 50] ^= 1;
    assert(!slot_valid(image.data()));
    valid_image();
    image[4] ^= 1;
    assert(!slot_valid(image.data()));
    valid_image();
    Commit committed{};
    std::memcpy(&committed, image.data() + kCommitStart, sizeof(committed));
    std::memset(image.data() + kCommitStart, 0xff, kPageBytes);
    assert(slot_valid(image.data(), false));
    assert(!slot_valid(image.data()));
    // Every incomplete commit record is rejected. The previous slot is untouched.
    for (size_t bytes = 0; bytes < sizeof(committed); ++bytes) {
        std::memset(image.data() + kCommitStart, 0xff, kPageBytes);
        std::memcpy(image.data() + kCommitStart, &committed, bytes);
        assert(!slot_valid(image.data()));
    }
    put_commit();
    assert(slot_valid(image.data()));
    // Erased pages, missing payload pages and a commit from another generation fail.
    image.fill(0xff);
    assert(!slot_valid(image.data()));
    valid_image();
    std::memset(image.data() + kPayloadStart + 256, 0xff, 256);
    assert(!slot_valid(image.data()));
    valid_image();
    image[kCommitStart + 4] ^= 1;
    assert(!slot_valid(image.data()));
}
void test_semantic_validation() {
    valid_image();
    assert(snapshot_valid(snapshot));
    snapshot.features[0].size = 65;
    assert(!snapshot_valid(snapshot));
    valid_image();
    snapshot.features[0].size = 0;
    assert(!snapshot_valid(snapshot));
    valid_image();
    snapshot.features[0].size = 41;
    assert(!snapshot_valid(snapshot));
    valid_image();
    snapshot.features[4] = snapshot.features[0];
    assert(!snapshot_valid(snapshot));
    valid_image();
    snapshot.input[4] = 1;
    assert(!snapshot_valid(snapshot));
    valid_image();
    snapshot.input[32] = 0;
    assert(!snapshot_valid(snapshot));
    assert(steps_valid(&step, 1));
    step.duration_ms = 0;
    assert(!steps_valid(&step, 1));
    step.duration_ms = kMaxStepDurationMs + 1;
    assert(!steps_valid(&step, 1));
    step.duration_ms = 1;
    step.state.dpad = 9;
    assert(!steps_valid(&step, 1));
    step.state.dpad = 8;
    step.state.buttons = 0x8000;
    assert(!steps_valid(&step, 1));
    std::array<Step, 145> too_long{};
    for (auto& s : too_long) s.duration_ms = kMaxStepDurationMs;
    assert(!steps_valid(too_long.data(), too_long.size()));
}
}
int main() {
    test_lengths_checked_before_access();
    test_corruption_and_interruption();
    test_semantic_validation();
    std::puts("Macro flash format tests passed");
}
