# SCMS4CoCIS

Smart Control and Monitoring System for the College of Computing and Information Sciences (CoCIS), Makerere University.

This repository contains a **Wokwi-first ESP32 simulation** of a distributed smart control system with:

- RFID-style access control (simulated)
- Building and room occupancy tracking
- Automated lighting for 2 lecture rooms
- Configurable access schedule
- Event logging and status reporting
- Multi-node coordination model (simulated)

---

## 1) Project Scope (Simulation)

Pilot deployment in simulation covers:

- Main Entrance terminal
- Lecture Room 1 terminal
- Lecture Room 2 terminal

Target behavior:

1. Authenticate users at entry/exit
2. Keep correct inside/outside occupancy state
3. Switch room lights based on occupancy
4. Enforce configured access time window
5. Provide management menu over serial
6. Log all major events and decisions

---

## 2) Repository Structure

```text
SCMS4CoCIS/
├─ README.md
├─ docs/
│  ├─ ARCHITECTURE.md
│  └─ TEST_PLAN.md
├─ wokwi/
│  ├─ diagram.json
│  └─ wokwi.toml
├─ firmware/
│  ├─ CMakeLists.txt
│  ├─ sdkconfig.defaults
│  └─ main/
│     ├─ CMakeLists.txt
│     ├─ main.c
│     ├─ scms_types.h
│     ├─ scms_config.h
│     ├─ access_control.c
│     ├─ access_control.h
│     ├─ occupancy.c
│     ├─ occupancy.h
│     ├─ lighting.c
│     ├─ lighting.h
│     ├─ scheduler.c
│     ├─ scheduler.h
│     ├─ logger.c
│     ├─ logger.h
│     ├─ serial_menu.c
│     ├─ serial_menu.h
│     ├─ sync_bus.c
│     └─ sync_bus.h
└─ .gitignore
```

---

## 3) Wokwi Quick Start

### Option A: Use Wokwi VS Code extension

1. Open this repo in VS Code
2. Install **Wokwi for VS Code** extension
3. Open `wokwi/diagram.json`
4. Start simulation

### Option B: Use Wokwi web editor

1. Create/import project
2. Paste `diagram.json` content
3. Build/attach firmware as needed

---

## 4) ESP-IDF Build (local)

From `firmware/`:

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

> In simulation mode, you can also test logic via serial output and scripted test inputs.

---

## 5) Core Features Planned

- [x] Initial architecture and module scaffolding
- [ ] Access rules and duplicate-scan protection
- [ ] Occupancy state machine and consistency guards
- [ ] Access schedule enforcement
- [ ] Automated room lighting controller
- [ ] Serial command interface
- [ ] Event logging and summaries
- [ ] Reliability tests and demo scenarios

---

## 6) Recommended Demo Flow

1. Valid entry -> occupancy increments
2. Duplicate entry attempt -> rejected as already inside
3. Room occupancy update -> light turns ON
4. Room empties -> light turns OFF
5. Outside schedule window -> entry denied
6. Show logs + current status summary

---

## 7) Team Workflow

- Keep each module in separate files
- Use small commits with clear messages
- Track per-member contributions in report
- Add screenshots/log excerpts for evidence

---

## 8) Notes

This starter scaffold emphasizes **clarity and grading alignment** for coursework deliverables.
