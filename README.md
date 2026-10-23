# SCMS4CoCIS

ESP32 / ESP-IDF coursework simulation for the College of Computing and Information
Sciences: one entrance, two lecture rooms, three **logical FreeRTOS terminals on
one ESP32**, and one serialized state owner.

> **Simulation-only policy:** the brief explicitly restricts **both entry and
> exit** outside permitted hours, even with people inside. This firmware follows
> that rule. It is **not a safe real-building egress design**. Ask the lecturer to
> clarify the requirement; do not deploy this system to control real doors.

**Evidence:** portable production-code tests pass; firmware compilation is blocked
by toolchain download access in the implementation environment; Wokwi is **NOT RUN**.
See [validation and requirement matrix](docs/VALIDATION.md). This is not a claim
that the assignment has already passed an end-to-end simulation.

## VS Code + PlatformIO + Wokwi (primary)

1. Clone `https://github.com/mcliff149-maker/SCMS4CoCIS.git` and open its **root**
   folder in VS Code, not the `firmware` folder.
2. Install the recommended **PlatformIO IDE** and **Wokwi for VS Code** extensions.
   Activate your Wokwi license as prompted. For SD writeback use Wokwi extension
   **4.0.0+** (CLI **0.28.0+**).
3. Let PlatformIO install dependencies. Its own ESP-IDF toolchain is managed
   automatically: a separate Espressif extension is **not required** for this path.
4. In a PlatformIO terminal, run `pio run -e esp32dev`, or use
   **Tasks: Run Build Task → SCMS: PlatformIO build**. The platform is pinned to
   `espressif32@6.9.0` (ESP-IDF 5.3.1); PlatformIO Core 6.1.19 is used in CI.
   Transitive tools retain the platform's upstream version constraints, not a
   complete dependency lock. Internet access to the PlatformIO registry is needed.
5. Confirm `.pio/build/esp32dev/firmware.elf` and `firmware.merged.bin` exist.
   The documented PlatformIO post-build action merges **its actual** extra images
   (bootloader, partition table, etc.) and application offset. Do not substitute
   PlatformIO's generated native-IDF `flasher_args.json`: filenames can differ.
6. Leave root `wokwi.toml` selected, open root `diagram.json`, and press **F1 →
   Wokwi: Start Simulator**. No physical upload, USB device, or `pio upload` needed.
7. Use Wokwi's serial monitor at 115200 baud; send commands with newline. Start
   with `help`, `users`, `status`. Do not use a hardware serial monitor for this.

The old `wokwi/` diagram/config were removed: **root `diagram.json` is the only
circuit**. Generated `.pio`, build outputs, SD output and local VS Code files are
ignored; only portable tasks/settings/extensions are tracked.

### Standalone ESP-IDF (alternative, do not mix outputs)

With ESP-IDF 5.3.1 installed and its environment activated:

```sh
cd firmware
idf.py set-target esp32
idf.py build
```

Use the root `wokwi.idf.toml` contents **instead of** root `wokwi.toml`: copy the
alternate onto `wokwi.toml` locally, then start Wokwi from the root as above.
Restore the PlatformIO config afterwards (`git restore wokwi.toml`).
The alternate loads `firmware/build/flasher_args.json` and
`firmware/build/scms4cocis.elf`, including all flash images. Do not flash hardware.
The standalone build was **NOT RUN** here.

## Commands and rules

Commands are case-sensitive. IDs **1001, 1002, 2001** are authorized; e.g. **9999**
is unknown. `scan_*` and buttons inject **simulated RFID identities**—there is no
RFID reader driver, radio security, or proof someone actually passed a door.

| Command | Meaning |
|---|---|
| `help`, `status`, `users`, `who` | Menu, time/open state, registered people, people inside |
| `logs [1..256]` | Recent RAM events, oldest-to-newest even after wrap |
| `storage_logs [1..50]` | Read the first bounded lines/chunks from SD; fallback shown explicitly |
| `scan_entry 1001`, `scan_exit 1001` | Separate simulated entrance and exit |
| `select 1002` | Identity used by all four buttons (can also select unknown 9999) |
| `button 0`, `button 1` | Equivalent entry/exit button events |
| `room 1 1001 in`, `room 1 1001 out` | Identifiable room events; rooms 1 or 2 |
| `button 2`, `button 3` | Toggle selected person's room-1/room-2 membership |
| `time 19:54` | Set clock within current simulated day; **no elapsed energy charged** |
| `advance 6` | Advance elapsed minutes (0..10080), carrying across midnight |
| `login admin coursework`, `logout` | Educational management session, not production security |
| `set_schedule 08:30 17:45` | Authenticated change, saved to NVS before applying |
| `terminal 1 off`, `terminal 1 on` | Reject/restore logical room-1 events; 0 entrance, 2 room-2 |

Default schedule is **[07:00,20:00)**: opening included, closing excluded.
Only same-day `00:00..23:59` windows with start < end are supported. Equal,
overnight and malformed windows are rejected, not silently clamped.
Clock starts at 07:00 on day 0 and advances **only by commands**, not wall time.
Occupancy and lights survive clock changes; door pulses use independent monotonic
real time. Duplicate entry and exit-without-entry cannot change counts.
Room occupants must be inside the building and can occupy at most one room.
A granted building exit clears room membership and lights if the room becomes empty.

The console reads bounded UART chunks, accepts at most **127 bytes per line** and
discards an entire oversized/binary-control frame through its newline. CRLF is
one terminator. It never executes a truncated command or prints a password.
It does not provide interactive line editing: send a complete corrected line.
Queue overload rejects commands explicitly; wait for a response before pasting
another command. A 700 ms door pulse delays the next state command; UART input
still runs. Demo credentials and access identities are public coursework data:
**anyone with the console can log in**, and unauthenticated simulation clock
control can move time. This is not a secure access-control product.

### Simple originality extras

* Duplicate scans are denied with identifiable reasons.
* Once-per-day/window occupied warnings occur in the last five minutes and at
  closing, including a crossed threshold during `advance`. Clock rewinds do not
  repeat the same warning.
* `status` reports light-on minutes and **estimated** used/saved Wh. Assumption:
  each room's light is **60 W**; baseline is both lights always on over explicitly
  advanced elapsed time. `Wh = W × minutes / 60`. `time` jumps do not manufacture
  elapsed time or savings; repeated status calls do not charge energy. These are
  estimates, **not measurements**.

## Storage and synchronization

The ESP-IDF implementation mounts the actual Wokwi microSD model with FAT/SPI and
appends `/sdcard/scms.log`. `logs` is a separate 256-event **volatile** RAM ring.
SD writes are deferred until door pulses finish; a bounded 32-line staging queue
warns before dropping its oldest line. Mount/write/read failure is explicit and
changes status to volatile; a separate bounded 32-line fallback is available via
`storage_logs`. There is no silent formatting or claim that RAM is SD storage.
No automatic SD retry/remount or unbounded offline backlog is implemented.

Root `wokwi.toml` uses a dedicated ignored `sdcard/` folder and `writeback=true`.
Recent Wokwi versions mirror card changes **including deletions** into that
folder when stopping/restarting. Do not point it at valuable files. Test append,
read and restart locally before claiming durability. Older versions/browser
defaults may discard writes. NVS settings survive only when the simulator retains
flash; SD writeback **does not** save NVS. Restarting/resetting a CPU, restarting a
simulation and loading a new flash image are not equivalent. Inspect `status`
and reapply the demo schedule as needed. People/rooms/clock/energy/RAM logs reset
at boot; building occupancy is intentionally not reconstructed from historical
logs, whose day numbers are session-relative. NVS failure rejects schedule changes
without erasing flash or falsely reporting a save.

See [architecture and pin map](docs/ARCHITECTURE.md).
Three real FreeRTOS terminal tasks send ordered messages through queues to one
state owner. This demonstrates shared-state serialization, not a physical
three-controller network. No network ACK/retry/event-ID protocol is claimed.
Offline injection rejects events without replaying them after recovery.

## Reproducible demo (expected results; Wokwi NOT RUN here)

Send one line at a time; begin with default schedule, all terminals online:

```text
time 10:00
scan_entry 1001
scan_entry 1001
scan_entry 9999
room 1 1001 in
room 2 1001 in
advance 60
status
scan_exit 1001
set_schedule 08:00 18:00
login admin coursework
set_schedule 08:00 18:00
logout
scan_entry 1002
time 17:54
advance 1
advance 5
scan_exit 1002
who
logs 20
storage_logs 10
```

Expect: entry granted; duplicate/unknown denied; room-1 light ON; second-room
denied; room-1 used 60 Wh, room-2 saved 60 Wh after the hour; exit clears room/light.
Unauthenticated schedule change denied, authenticated change saved if NVS ready.
At 17:55/18:00 occupied warnings show building=1; 18:00 exit is denied. Restore
an open time to exit in this simulation. Green/blue indicators pulse for their
respective door; red pulses on denial. Also test `select 9999` + entry button,
`terminal 1 off` + a room-1 event, recovery, and an oversized serial line.

## Tests, evidence and coursework deliverables

With CMake, a C compiler and Python installed, from the root:

```sh
cmake -S tests -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python -m unittest discover -s tests -p test_merge_firmware.py -v
```

See [test plan](docs/TEST_PLAN.md) for sanitizer commands and Wokwi smoke scenario.
CI runs host tests and a firmware build; a configured or pending job is **not**
evidence of success. [VALIDATION](docs/VALIDATION.md) separates actual results,
unrun steps, substitutions and outstanding assignment requirements.

Team size **at most five**; deadline **2026-11-02**. Submit the report, working
Wokwi project/link, individual recordings and contribution evidence separately.
No Wokwi link or recording is fabricated here.

Individual contribution template for the report:

| Member | Module/files | Design contribution | Tests personally run + output | Demo/recording timestamp | Commit |
|---|---|---|---|---|---|
| Fill in | | | | | |

### References

* [Wokwi PlatformIO](https://docs.wokwi.com/vscode/platformio)
  ([verified source](https://github.com/wokwi/wokwi-docs/blob/main/docs/vscode/platformio.md))
* [Wokwi ESP-IDF](https://docs.wokwi.com/vscode/esp-idf) and
  [project/SD configuration](https://docs.wokwi.com/vscode/project-config)
* [Wokwi microSD](https://docs.wokwi.com/parts/wokwi-microsd-card)
* [Multiple MCU feature discussion](https://github.com/wokwi/wokwi-features/issues/186):
  independent multi-firmware operation not verified for this project.
* [PlatformIO ESP-IDF](https://docs.platformio.org/en/latest/frameworks/espidf.html)
  and [pinned platform release](https://github.com/platformio/platform-espressif32/releases/tag/v6.9.0)
