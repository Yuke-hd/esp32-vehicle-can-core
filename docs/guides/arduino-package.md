# Generated Arduino package

This package composes the canonical `vehicle_core`, `can_bus` and injected
`vehicle_telemetry::Runtime` APIs on one classic ESP32 configuration. It requires
C++17 or newer. The private probe proves compilation and linking only; it has
not been uploaded and supplies placeholder pins. No physical CAN, runtime
coexistence, timing or hardware acceptance is claimed.

## Exact configuration

The machine-readable profile is [`tools/arduino/profile.json`](../../tools/arduino/profile.json),
also copied into the package as `ARDUINO_PROFILE.json`. Configuration selection
comes from the [Arduino-ESP32 3.3.8 release](https://github.com/espressif/arduino-esp32/releases/tag/3.3.8),
the [official package index](https://espressif.github.io/arduino-esp32/package_esp32_index.json),
installed `boards.txt`/`platform.txt`, and actual compiler/SDK headers.

- Arduino CLI `1.3.1`, commit `08ff7e2b`; `esp32:esp32@3.3.8`.
- Bundled IDF `v5.5.4`, `esp32-libs@3.3.8`; `esp-x32@2601`, GCC `14.2.0`,
  crosstool-NG `esp-14.2.0_20260121`.
- ESP32 Dev Module; CPU 240 MHz, QIO flash 80 MHz/4 MB, default partitions,
  no PSRAM/debug, loop/event cores 1, upload 921600, no erase, default JTAG/Zigbee.
  Every menu option is explicit in the profile's FQBN.
- `qio_qspi/include/sdkconfig.h`: 1000 Hz ticks (1 ms), dual-core, exceptions on,
  RTTI off, interrupt watchdog 300 ms including CPU1, task watchdog enabled,
  and listen-only TWAI erratum fix enabled. `null` in the profile means unset.
  SDK default CPU frequency is 160 MHz; the board compile option is 240 MHz.
- `flags/defines` supplies `ESP_PLATFORM` and `IDF_VER="v5.5.4"`. The packaged
  `runtime.cpp` therefore selects the FreeRTOS/ESP timer backend.
- The bundled C++ flags end in `-std=gnu++2a`; the clean sketch's generated `build_opt.h` appends
  `-std=gnu++17`. Actual compilation asserts the C++17 language value, IDF,
  compiler, ESP32 target, tick and relevant watchdog/core/exception settings.

Runtime defaults remain 16 frames/2 ms per batch, 512 receive calls/20 ms per
runnable period, and a requested 1 ms pause rounded up to at least one tick.
Observed delay depends on scheduling. Observers run synchronously on the worker;
cooperative budgets cannot preempt them. Header-only `NotificationChannel`
composition does not create an independent Runtime callback task.

## Clean staging, installation and compilation

The following Linux x86-64 commands use only canonical Arduino tools. Run from
a fresh checkout of packaging-tool commit
`49fdb9aea7a5109ea638f63706f806ca0f901ec8`, with Python 3.9+, curl, tar,
sha256sum and an unused
`/tmp/evcc-arduino-clean` path. The CLI/core/SDK are external dependencies, not
copied into the generated library. The exact source revision is independent of
uncommitted source edits: staging reads the selected Git commit's blobs.

```sh
package_root=/tmp/evcc-arduino-clean
mkdir -p "$package_root/bin"
curl --fail --location \
  https://github.com/arduino/arduino-cli/releases/download/v1.3.1/arduino-cli_1.3.1_Linux_64bit.tar.gz \
  --output "$package_root/cli.tar.gz"
echo "376428d7d45be640c00812a71612e1742edc2f5f9ee3742a2d6da7870e079588  $package_root/cli.tar.gz" | sha256sum --check
tar -xzf "$package_root/cli.tar.gz" -C "$package_root/bin" arduino-cli
cat > "$package_root/config.yaml" <<EOF
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $package_root/data
  downloads: $package_root/downloads
  user: $package_root/user
library:
  enable_unsafe_install: true
EOF
cli="$package_root/bin/arduino-cli"
"$cli" --config-file "$package_root/config.yaml" version
"$cli" --config-file "$package_root/config.yaml" core update-index
"$cli" --config-file "$package_root/config.yaml" core install esp32:esp32@3.3.8
source_revision=b610463d0c7c812117f02fe0f7c77521e3506bd2
python3 tools/stage_arduino.py --revision "$source_revision" --output "$package_root/stage-a"
python3 tools/stage_arduino.py --revision "$source_revision" --output "$package_root/stage-b"
cmp "$package_root/stage-a/ESP32VehicleCANCore.zip" "$package_root/stage-b/ESP32VehicleCANCore.zip"
(cd "$package_root/stage-a" && sha256sum --check ESP32VehicleCANCore.zip.sha256)
"$cli" --config-file "$package_root/config.yaml" lib install --no-deps \
  --zip-path "$package_root/stage-a/ESP32VehicleCANCore.zip"
mkdir -p "$package_root/probe/package_probe"
cp tools/arduino/package_probe/*.ino "$package_root/probe/package_probe/"
python3 -c 'import json; print(json.load(open("tools/arduino/profile.json"))["language_option"])' \
  > "$package_root/probe/package_probe/build_opt.h"
fqbn=$(python3 -c 'import json; print(json.load(open("tools/arduino/profile.json"))["fqbn"])')
"$cli" --config-file "$package_root/config.yaml" compile --verbose \
  --fqbn "$fqbn" --build-path "$package_root/build" \
  "$package_root/probe/package_probe" > "$package_root/compile.log" 2>&1
```

`enable_unsafe_install` enables CLI installation of this locally generated ZIP;
it does not fetch arbitrary libraries. No `--library` or repository include
path is needed. The installed folder is
`user/libraries/ESP32_Vehicle_CAN_Core`. Include `ESP32VehicleCANCore.h` first
for Arduino discovery; it only includes canonical APIs in their existing
namespaces. Generate the sketch response file from the profile as shown above; it is not
a C++ header and is not stored among repository sources.
`build/compile_commands.json` contains `directory`, `arguments`
and `file` entries; compiler depfiles and the verbose log record resolved paths.

## Package boundary and provenance

Staging emits a fresh `ESP32VehicleCANCore/` tree, uncompressed ZIP with sorted
entries, fixed timestamps/permissions, and a companion ZIP SHA-256 file. The
ZIP hash is external to avoid hashing itself. `PROVENANCE.json` records generator/profile content hashes separately from the
immutable source revision, source-to-package mapping, each source's SHA-256,
header visibility, language requirement and exact dependency provenance.
`LICENSE`, `THIRD_PARTY_NOTICES.md` and `ARDUINO_DEPENDENCIES.md` travel with it.
No third-party implementation source is bundled.

Canonical public headers reside at `src/vehicle_core/`, `src/can_bus/` and
`src/vehicle_telemetry/`. `can_bus/driver_binding.hpp` is the application binding
contract. Packaged-internal `can_bus` private headers share `src/can_bus/` to
preserve existing include directives; `runtime_budget.hpp` is adjacent to
`runtime.cpp` in `src/vehicle_telemetry/`. These internal headers are available
to the compiler, not a supported application API.

Exactly four implementation files compile: `vehicle_core.cpp`, `can_bus.cpp`,
`runtime.cpp` and `can_bus_source.cpp`. Bench bindings, firmware `app_main`,
host tests, fake IDF headers and controller/product code are excluded. The
probe links the real Runtime with injected CanBusSource, FrameProcessor and
Observer, lifecycle/diagnostics, generic Reading/Notification and all five
NotificationChannel composition calls. It supplies exactly one sketch-side
`configure_driver` binding with `TWAI_MODE_LISTEN_ONLY` and `tx_queue_len=0`.

## Recorded evidence and limitations

The generator/profile revision `49fdb9aea7a5109ea638f63706f806ca0f901ec8`
and baseline source revision above produced two identical ZIPs with SHA-256
`11b70614ea232f9d3e0557933aabd647cf8aaa5174e3d07dc0a1ba0b91079a35`.
Staging defaults to `HEAD`; use that default or `--revision <current-pr-sha>`
for current or integrated sources. The recorded historical source pin does not
restrict generation to that baseline. A clean installed probe linked with 335616 bytes of flash and 41364 bytes of
static RAM. Its compile database contained exactly the four project sources,
all under the installed library. Depfiles contained no repository source path.
`nm` on `runtime.cpp.o` recorded references to `esp_timer_get_time`,
`xTaskCreatePinnedToCore` and `xTaskGetCurrentTaskHandle`, and no host thread
backend. These sizes describe this private probe, not an application budget.

Local evidence used macOS ARM64, CLI 1.3.1 (archive SHA-256
`723f8ddaf4875e87a20380c88c15ff9d55916c3c80cb432f1f980ea32cec2341`). Its
bundled ctags 5.8-arduino11 is Intel-only and could not execute without Rosetta.
For this local build only, canonical [ctags 5.8-arduino11](https://github.com/arduino/ctags/releases/tag/5.8-arduino11),
commit `abc8fca7499f44c725122881cd380a88c37abe0e`, was rebuilt for ARM64 after
renaming its reserved `__unused__` macro to `ARDUINO_UNUSED` in `.c`/`.h` files
to avoid a current Apple SDK macro collision. No parser logic changed. Configure
with a temporary prefix, run `make && make install`, and add
`--build-property tools.ctags.path=<prefix>/bin` to the compile command. This
GPL-2.0 helper remains outside the package; Linux commands above use the
unmodified canonical bundled helper. The local helper workaround is not Linux
validation evidence.

Native source/configuration and pins are unchanged. Local CMake 4.4.3/Ninja
1.13.2/Apple Clang 17 host tests passed 59/59 including architecture checks;
ESP-IDF v5.5.4 isolated-bench and runtime-synthetic builds passed. Linux Clang 14
ASan/UBSan remains the supported sanitizer gate in the existing CI workflow;
it passed on the initial PR head. Required CI must still pass on the final head. See [host validation](host-build.md) and
[bench build commands](bench-firmware.md). Public sketches, Arduino runtime
coexistence, CI package automation and physical isolated-bench evidence belong
to the following milestone issues.
