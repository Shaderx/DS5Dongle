# Macro memory budget

The macro recorder has one fixed Program buffer. Playback borrows it instead of copying it. The step array is `1024 * 16 = 16384` bytes. Controller snapshots and the flash programming page also have fixed sizes. Recording checks the step limit before each write.

## Why the limit is 1,024 steps

The first Pico 2 W build with 2,048 steps left 83,800 bytes between `__end__` and `__HeapLimit`. The existing audio and queue initialization requires at least 84,685 bytes from that heap, before allocator overhead and feature-cache objects. That capacity was rejected before hardware use.

| Existing runtime allocation | Requested bytes |
| --- | ---: |
| Stereo restricted-CELT Opus encoder | 24,376 |
| Mono Opus decoder | 18,400 |
| Audio, microphone, haptics, and Bluetooth queues | 29,813 |
| WDL resampler buffers | 12,096 |
| **Total** | **84,685** |

The SDK queues allocate one extra element beyond their configured counts. Their requests are 12,288, 8,658, 639, 600, 192, and 7,436 bytes. The WDL buffers round their requests to 4,000 and 8,096 bytes. The Opus sizes were checked against the compiled ARM code and the pinned library sources.

Reducing the step array to 16 KiB returns 16,384 bytes to the available heap. The macro linker checks require at least **96 KiB** between `__end__` and `__HeapLimit`. That space must also cover allocator overhead, feature-cache containers, response copies, and temporary library allocations. The static Bluetooth pools, CYW43 state, and audio-core stack are already counted below `__end__`.

This budget preserves the original audio implementation. It does not substitute a hardware heap-watermark measurement. Recheck it when audio frame sizes, queue lengths, dependencies, snapshots, or macro capacity change.

## Flash boundaries

The layout derives from the SDK's `PICO_FLASH_BANK_STORAGE_OFFSET`. It leaves the existing configuration sector and Bluetooth storage in place.

For Pico 2 W with 4 MiB flash and the pinned SDK defaults:

| Region | Offset from flash start |
| --- | --- |
| Firmware limit | `0x3DC000` |
| Macro slot A, 64 KiB | `0x3DC000` to `0x3EBFFF` |
| Macro slot B, 64 KiB | `0x3EC000` to `0x3FBFFF` |
| Existing configuration sector | `0x3FC000` to `0x3FCFFF` |
| Existing Bluetooth storage | Starts at `0x3FD000` |

A linker assertion prevents the firmware from reaching the macro slots. Compile-time checks enforce sector alignment and payload capacity. Each save erases and writes the inactive slot in bounded operations, services the main loop between operations, verifies the payload, and writes a separate commit page last.

## Inspect a build

```sh
arm-none-eabi-nm -n build/macro-pico2w/ds5-bridge.elf
```

Check `__end__`, `__HeapLimit`, `__flash_binary_end`, and `__macro_flash_start`. Use the linker map to inspect static buffers and RAM-resident code. The ordinary `size` summary alone does not separate code copied into RAM from code that remains in flash.
