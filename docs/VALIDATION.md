# Validation evidence and remaining gaps

Recorded **2026-10-23**. No screenshots, Wokwi link, recordings, hardware tests or
simulator pass results are claimed. Expected outputs in README/TEST_PLAN are
procedures, not observed simulator evidence.

## Commands actually executed

Working directory: repository root, unless otherwise stated.

| Command/check | Actual result |
|---|---|
| `command -v gcc; command -v cmake` | GCC and CMake available |
| `command -v pio; command -v idf.py; command -v wokwi-cli` | None initially available |
| `python -m venv /tmp/scms-pio` then `/tmp/scms-pio/bin/pip install platformio==6.1.19` | Successful isolated installation |
| `/tmp/scms-pio/bin/pio system info` | Core 6.1.19, Python 3.12.3, Linux x86_64; zero installed development platforms/toolchains |
| `/tmp/scms-pio/bin/pio pkg install -e esp32dev` | **FAILED**: `Platform Manager: Installing espressif32 @ 6.9.0` then `HTTPClientError`, exit 1 |
| `/tmp/scms-pio/bin/pio run -e esp32dev` | **FAILED**, exit 1 at the same platform download (`HTTPClientError`); compilation never started |
| `curl -I --max-time 20 https://api.registry.platformio.org/v3/platforms/platformio/espressif32` | **FAILED**: curl exit 6, cannot resolve registry hostname |
| `/tmp/scms-pio/bin/pio project config --json-output` | Parsed root `src_dir`, pinned platform, espidf framework and post-build action |
| `cmake -S tests -B build-host` | Successful host configuration |
| `cmake --build build-host` | Successful compilation of production policy/parser/logger/lighting sources with warnings treated as errors |
| `ctest --test-dir build-host --output-on-failure` | **PASS: 1/1**, seven deterministic test groups |
| `cmake -S tests -B build-host-sanitized -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'` | Successful sanitizer configuration |
| `cmake --build build-host-sanitized` and `ctest --test-dir build-host-sanitized --output-on-failure` | **PASS: 1/1**, ASan/UBSan; updated model rerun after corrections |
| `python -m unittest discover -s tests -p test_merge_firmware.py -v` | **PASS: 1/1**, production merge-action command construction |
| Python standard-library JSON/TOML parsing of diagram, VS Code and Wokwi configs; AST parsing of merge script | Passed syntax checks only; not a circuit or firmware validation |
| `git diff --check` | Passed at implementation checkpoints |

Host model tests also exercised real command paths and produced output checked
for OPEN/CLOSED, energy fields, three frame-discard diagnostics and no oversized
payload echo. Build directories were removed after the host checks. The tests do
not compile `main.c`, `sync_bus.c` or `storage.c`, and do not emulate FreeRTOS,
GPIO, FAT, NVS or UART drivers.

**Firmware build status:** no successful firmware compilation or merged image has
been produced in this environment. Toolchain acquisition is blocked as above.
Standalone `idf.py build` is **NOT RUN**: IDF is not installed.

**Simulation status:** **NOT RUN**. `wokwi-cli` is absent and no
`WOKWI_CLI_TOKEN` is configured. No token value was printed or committed.
`tests/smoke.scenario.yaml` uses verified official automation syntax but is unrun.
Use README's VS Code path or TEST_PLAN's exact CLI command once built.

**CI status:** the repository initially had only the active Copilot run; GitHub
Actions lookup found no failed jobs/logs to diagnose. After the implementation
push, [PR run 39981815449](https://github.com/mcliff149-maker/SCMS4CoCIS/actions/runs/39981815449)
and push run 39981810964 concluded **`action_required`**, with zero jobs on the
PR run. Approval is needed; no host or firmware CI success is claimed.

**Security/review:** changed-file secret scans found no secrets. Automated CodeQL
reported **zero alerts** for actions, Python and C/C++. The bundled code-review
binary was unavailable despite the wrapper's “success” heading; a separate
read-only review of the committed branch found **no significant issues**.
These checks are not proof that an uncompiled ESP-IDF binary is correct.

## Requirement assessment

“Implemented + tested” below means portable host testing, **not Wokwi validation**.

| Requirement | Assessment | Evidence / limitation |
|---|---|---|
| ESP32 ESP-IDF; root VS Code/PlatformIO project | Implemented-not-simulated | `platformio.ini`, root/firmware CMake; platform download blocked before compiler |
| Complete bootloader/partition/application loading | Implemented-not-simulated | Actual-image merge action unit-tested; real merged binary not produced; alternate native-IDF manifest provided |
| Valid RFID identity on distinct entry and exit | Partial | Production identity/access logic host-tested; serial/buttons substitute for RFID reader, LEDs for mechanisms |
| Identifiable entry/exit log and who inside | Implemented + tested | `access_control.c`, `logger.c`, tests; day/time/user/direction/reason and counts |
| Building/room consistency; no duplicate count | Implemented + tested | State-transition tests, room exclusivity and exit reconciliation |
| Room occupancy detection | Partial | Identified button/serial events replace physical sensors; no independent sensor accuracy claim |
| Automatic room lighting | Implemented-not-simulated | Host light decisions pass; GPIO output code and resistor wiring need simulator evidence |
| Configurable authorized access hours | Implemented + tested | Auth + minute-boundary tests; same-day windows only, invalid/equal/overnight rejected |
| Both entry/exit restricted outside hours | Implemented + tested | Occupied exit denial test; **unsafe real-world egress policy**, lecturer clarification outstanding |
| Management-only schedule changes | Partial | Demo admin validation host-tested, NVS code unrun; public credentials and unrestricted simulated time are educational, not production security |
| Useful serial menus | Implemented + tested / driver unrun | Portable parser and commands tested; UART0 driver integration not simulated |
| Three coordinated terminals | Implemented-not-simulated | Three actual worker tasks, queues, ordered owner in `sync_bus.c`; no FreeRTOS runtime evidence yet |
| Three independent ESP32 controllers | Missing (optional variant) | Not verified supported by current workflow; one ESP32 does not prove physical multi-controller behavior |
| Shared-state/resource synchronization | Implemented-not-simulated | Single owner invariant; ordered fan-in/explicit overload/offline behavior; no network ACK/retry/dedup claim |
| Memory-card storage | Implemented-not-simulated | Actual ESP-IDF FAT/SPI SD append/read code and supported module; mount/write/read/restart not demonstrated |
| Persistent configuration | Implemented-not-simulated | NVS commit/load and error reporting implemented; simulator flash retention not established |
| SD unavailable / bounded volatile fallback | Implemented-not-simulated | Runtime paths present; host sink-failure flag tested, actual SD failure not injected |
| Logger wrap/order | Implemented + tested | 273 events into 256-slot ring; chronological getters/truncation checked |
| Duplicate-scan originality extra | Implemented + tested | Entry/exit denial tests |
| Closing-soon / closing occupied warning | Implemented + tested | Empty/occupied, threshold crossing, rewinds, changed windows tested |
| Estimated lighting savings extra | Implemented + tested | Actual model arithmetic, baseline/on-time/savings; 60 W assumption, no measurements |
| Button debounce and timed indicators | Implemented-not-simulated | 40 ms debounce, ~700 ms monotonic reset code; no waveform evidence |
| Wokwi wiring / automated smoke | Implemented-not-simulated | Root JSON syntax checked; circuit, scenario and visual layout still need a run |
| Report/Wokwi link/individual recordings/contributions | Partial / student deliverables | Architecture, demo, matrix, contribution template provided; report, working share link and recordings not created |

## Required before calling the assignment demonstrated

1. Run the pinned PlatformIO build on a network that resolves the registry; fix
   any compilation/API issues and record resolved packages, warnings and artifact sizes.
2. Run the smoke scenario and visual/button/UART tests from TEST_PLAN in Wokwi.
3. Capture real FAT append/read/writeback/restart and SD-unavailable evidence.
   Distinguish CPU reset, simulator stop/start and new-image flash/NVS behavior.
4. Test queue ordering/offline/overflow and GPIO reset timing on the ESP32 runtime.
5. Ask the lecturer to accept the simulated RFID/sensor/actuator substitutions
   and logical-task terminals, and clarify the outside-hours exit policy.
6. Supply the final report, valid Wokwi project link and individual recordings;
   deadline 2026-11-02, team size at most five.

## Research evidence

Official docs host DNS failed here; current documentation was retrieved through
its official GitHub source on `main` (not the stale default branch):

* [PlatformIO full-image guidance and merge action](https://github.com/wokwi/wokwi-docs/blob/main/docs/vscode/platformio.md):
  application-only images get Wokwi defaults; full images are recommended for IDF.
  PlatformIO native-IDF manifests can name the wrong output files.
* [Native-IDF manifest loading](https://github.com/wokwi/wokwi-docs/blob/main/docs/vscode/esp-idf.md).
* [microSD SPI pins](https://github.com/wokwi/wokwi-docs/blob/main/docs/parts/wokwi-microsd-card.md),
  [current SD writeback/version requirements](https://github.com/wokwi/wokwi-docs/blob/main/docs/vscode/project-config.md),
  [CLI usage](https://github.com/wokwi/wokwi-docs/blob/main/docs/wokwi-ci/cli-usage.md)
  and [scenario serial commands](https://github.com/wokwi/wokwi-docs/blob/main/docs/wokwi-ci/automation-scenarios.md).
* [PlatformIO 6.9.0 package declarations](https://github.com/platformio/platform-espressif32/blob/v6.9.0/platform.json):
  ESP-IDF package `~3.50301.0`, Xtensa toolchain `13.2.0+20240530`.
  This is a platform pin, not an exhaustive exact transitive lock.
* [Open multi-MCU discussion](https://github.com/wokwi/wokwi-features/issues/186)
  and [single-image CLI configuration](https://github.com/wokwi/wokwi-cli/blob/main/packages/cli/src/WokwiConfig.ts):
  independent multiple-firmware operation has not been verified here.
