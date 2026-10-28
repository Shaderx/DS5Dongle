# Gamepad macros on Pico 2 W

The `macro` branch adds recording and playback to the original DS5Dongle firmware. It keeps the DualSense USB presentation. A physical controller supplies the recording; playback can continue without it. One recording is saved across power cycles.

The fork's `master` branch tracks the original upstream. Use the macro build on the second Pico 2 W. The main Pico can continue to run the original release firmware.

## Controls

Use the Pico's BOOTSEL button while the firmware is running. Hold gestures take effect when you release the button. A 10-second hold selects pairing without first starting a recording at 3 seconds.

| Gesture | Action |
| --- | --- |
| Hold for at least 10 seconds, then release | Pair or switch the physical controller. Put the DualSense in pairing mode with PS + Create. |
| Hold for at least 3 seconds but less than 10, then release | Start a new recording from the connected controller. |
| Short press while recording | Finish the recording, save it, and start continuous playback. |
| Short press while saving | Toggle whether playback starts after the save. The save itself continues. |
| Short press while playing | Stop playback and release the macro controls. |
| Short press while stopped with a saved macro | Start the macro from its beginning. |
| Short press after the buffer-full indication | Save the complete recorded prefix and start playback. |

The button is sampled every 100 ms and debounced. Keep the hold until its LED indication appears before releasing it. Long holds are ignored while saving. The original double-click reboot, triple-click reboot, and short hold that clears pairings are replaced by these macro controls. Holding BOOTSEL while connecting USB still enters the Pico bootloader.

## Record and replay

1. Connect the macro Pico to the computer and pair a DualSense.
2. Hold BOOTSEL for about 3 seconds. Release it when the LED shows the record-hold indication.
3. Perform the controller actions. Recording captures buttons, the D-pad, both sticks, and both triggers. It does not record motion sensors or touch coordinates.
4. Short-press BOOTSEL. The LED blinks quickly during saving, then stays on during playback.
5. Short-press again to stop. A further short press restarts the saved sequence.

Playback loops until stopped. It continues if the physical controller disconnects. Losing the controller during recording cancels that recording and preserves the previous save. After power-up, the Pico loads the saved macro but waits for a short press before playing it. USB disconnect or suspend stops playback; reconnection does not start it automatically.

If a replacement recording changes the saved controller identity, the Pico deliberately reconnects its USB interface after the save succeeds. Playback requested by the finish button starts after that reconnect. The previous identity stays stable while the replacement is recorded and saved.

During normal stopped operation, connected-controller input passes through. Recorded controls use the remapping that was active while recording. Playback does not apply that remapping a second time.

## LED indications

| Pattern | Meaning |
| --- | --- |
| Brief pulse every 2 seconds | Stopped and ready |
| Two short flashes each second | Bluetooth inquiry/pairing |
| Regular 150 ms on/off blink | Recording |
| Steady light | Playing, or waiting to start after a neutral report |
| Fast 50 ms on/off blink | Saving |
| On for about 700 ms each second | Recording limit reached; the complete prefix is available to save with a short press |
| Three short flashes each second | Recording or save could not complete |
| While held: 100 ms on/off after 3 seconds | Releasing now starts recording |
| While held: 50 ms on/off after 10 seconds | Releasing now selects pairing |

Macro mode owns the onboard LED. The normal connection and low-battery LED policies do not override these patterns.

## Memory limits and saved data

- The recording buffer holds **1,024 complete steps**, including the initial state and any split long holds. Its step array occupies **16 KiB**. It does not grow or allocate more memory when full.
- Consecutive identical control states share one step. Moving sticks can consume the available steps faster than button-only actions.
- Timing uses 1 ms units. Long holds are split into steps of at most 10 minutes. A recording also has a 24-hour duration limit; the step limit can be reached much earlier.
- Reaching a limit stops recording and displays the full indication. The buffer does not wrap, and playback does not start automatically.
- Two 64 KiB flash slots alternate saves. A save becomes active only after its data and final commit record pass validation. An interrupted save leaves the previous committed slot available.
- The saved data includes bounded controller feature reports needed to start the DualSense interface without the controller. CRC and size checks reject invalid saved data before playback.

See [the memory budget](macro-memory.md) for the static allocation and linker checks.

## Build and test

Use the same dependencies as the original firmware build: Pico SDK **2.3.1**, TinyUSB commit **98741b55959564746998d0453c32d87064629d7c**, and Arm GNU Toolchain **15.2.Rel1**. Initialize the repository's WDL and Opus submodules. The existing firmware workflow pins these dependencies.

```sh
git submodule update --init --recursive
cmake -S . -B build/macro-pico2w -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH=/absolute/path/to/pico-sdk \
  -DPICO_BOARD=pico2_w \
  -DENABLE_GAMEPAD_MACRO=ON \
  -DVERSION=macro-dev
cmake --build build/macro-pico2w --target ds5-bridge --parallel
```

The output is `build/macro-pico2w/ds5-bridge.uf2`. Build with `ENABLE_GAMEPAD_MACRO=OFF` to compile the original runtime paths on this branch. The clean upstream branch remains the source for the main Pico's releases.

The native tests need a C++17 compiler and CMake; they do not need a Pico SDK or a connected board:

```sh
cmake -S tests/macro -B build/macro-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/macro-tests --parallel
ctest --test-dir build/macro-tests --output-on-failure
```

Native tests and a successful UF2 build do not establish target-game compatibility. Hardware verification must cover recording, playback after turning off the controller, saved playback after unplugging the Pico, full-buffer feedback, USB suspend/resume, and the target game's native DualSense input path.
