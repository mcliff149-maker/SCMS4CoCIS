# Tests and manual simulation checks

See [VALIDATION.md](VALIDATION.md) for **executed** commands and outcomes; the
instructions below are reproducible procedures, not claims they all ran.

## Host tests (actual production modules)

From repository root:

```sh
cmake -S tests -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python -m unittest discover -s tests -p test_merge_firmware.py -v
```

`tests/test_scms.c` tests seven groups compiled with GCC/Clang
`-Wall -Wextra -Werror -pedantic`:

1. Valid/unknown entry, duplicate entry/exit, no-entry exit, exact schedule
   boundaries, **occupied-person exit denied at closing**.
2. Inside/room identity consistency, no two rooms, multiple occupants,
   exit reconciliation and lighting, selected-identity button logic.
3. Unauthorized/invalid configuration, failed login/logout, same-day minute
   validation, failed persistence retaining old policy and independent SD state,
   no passwords in logs.
4. Energy used/saved/baseline arithmetic, zero-charge clock jumps, midnight/week
   advances, occupied/empty closing windows, rewinds and changed schedules.
5. Logger wrap ordering, truncation, reset and sink behavior.
6. CR/LF/CRLF, empty lines, maximum valid frame, oversized-frame suffix rejection,
   control/binary input, reset and callback dispatch.
7. Invalid and overflowing numeric inputs, malformed commands, state reset.

The Python test executes the actual merge-script registration/action with a
mock build environment and checks actual image list handling, OTA extra images,
nondefault application offsets, paths with spaces, and failure propagation
configuration. It does **not** create a firmware image or run esptool.

Optional GCC/Clang sanitizer build:

```sh
cmake -S tests -B build/sanitized \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sanitized
ctest --test-dir build/sanitized --output-on-failure
```

## Firmware build

```sh
pio run -e esp32dev
```

Expected artifacts: `.pio/build/esp32dev/firmware.elf`, `bootloader.bin`,
`partitions.bin`, `firmware.bin`, `firmware.merged.bin`. Post-build merging must
succeed; an application-only artifact is not the configured simulation input.
Standalone alternative: activate IDF 5.3.1 and run `idf.py build` in `firmware/`.

## Wokwi automation (NOT RUN in implementation environment)

Prerequisites: successful firmware build, Wokwi CLI >=0.28.0, active CI token
set securely in the environment (never commit it). Start with default
07:00–20:00 schedule / fresh simulation flash:

```sh
wokwi-cli . --scenario tests/smoke.scenario.yaml --timeout 60000 \
  --serial-log-file build/wokwi-serial.log
```

The scenario uses official `write-serial` / `wait-serial` automation syntax and
waits for each command's result. It covers entry, duplicate/unknown rejection,
room exclusivity, elapsed time, exit reconciliation and outside-hours exit.
It is not a GPIO/debounce/SD persistence test. Do not report a pass until the CLI
actually completes successfully; capture its output/exit status.

## Manual VS Code checks (also NOT RUN here)

Follow root README build/start instructions, then:

| Action | Expected evidence |
|---|---|
| Run README demo line by line | Identified grants/denials, exact building counts, room/light state, estimated Wh |
| `select 1001`, press entry, hold >1 second | One transition; green pulse resets ~700 ms |
| Press same button again; `select 9999`, press exit | Duplicate/unknown denied, red pulse, counts unchanged |
| Select inside person; press each room button | Light follows membership; second room denied until first exited |
| Scan building exit with room occupied | Membership clears, last light OFF |
| `time 19:54`, `advance 1`, `advance 5` with occupant | One soon warning, one close warning, number inside |
| Attempt entry/exit at 20:00 | Both denied by simulation-only schedule |
| `time 06:59`, entry; `time 07:00`, entry | Boundary denied then granted |
| `set_schedule` before login | `DENY_AUTH`; settings unchanged |
| Login, change schedule; logout; invalid/equal/overnight settings | Saved or explicit persistence failure; invalid changes rejected |
| `terminal 1 off`; room event including leading spaces; status | Room-1 rejection, no mutation; management still usable |
| `terminal 1 on`; resubmit event | One new event applied, no offline replay |
| Burst >16 commands | Explicit queue-overload errors; accepted commands remain ordered |
| Send >127-character line ending in valid command text | Entire line discarded; no suffix command executed |
| Send many events; `logs 256` | Newest retained events ordered oldest-to-newest after wrap |
| Run `status` twice without advance | Energy unchanged |
| Jump clock backward/forward during a door pulse | Pulse still resets against monotonic time; no synthetic energy |

### SD and restart evidence to capture

1. On recent Wokwi, inspect boot `storage=SD` and `schedule_nvs=ready`. A volatile
   status is a failed SD requirement, not a successful persistent-storage test.
2. Create identifiable events, allow staged writes to drain, then `storage_logs 50`;
   match them to serial output. This prints the **first**, not latest, SD lines.
3. Stop simulation. Verify ignored `sdcard/scms.log` exists and has those events.
4. Restart; read the historical file again and verify new events append.
   Occupancy intentionally resets at boot; historical logs are not a recovery database.
5. Save a different schedule, perform CPU reset and inspect restored schedule.
   Repeat stop/start/new-image loading separately: record whether simulator NVS
   flash persists; do not assume SD writeback preserves flash.
6. Stop, locally remove SD part/wiring from a disposable diagram copy, restart:
   expect explicit mount failure, RAM-only status, working access policy, RAM
   history; restore the canonical diagram afterwards.
7. For read/write failures (e.g. full card), capture explicit failure and volatile
   fallback; no fault injection is claimed unless actually performed.

FreeRTOS ordering, GPIO timing, FAT/NVS persistence and Wokwi circuit validity
need this end-to-end evidence. Host policy tests alone cannot prove them.
