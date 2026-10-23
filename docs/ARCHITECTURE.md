# Architecture Overview

## System Model (Wokwi Simulation)

The system is modeled as three logical terminals:

1. **Main Entrance Terminal**
   - Simulates RFID authentication for entry/exit
   - Applies access schedule rules
   - Maintains person-in-building state transitions

2. **Lecture Room 1 Terminal**
   - Simulates room occupancy events
   - Controls Room 1 light state

3. **Lecture Room 2 Terminal**
   - Simulates room occupancy events
   - Controls Room 2 light state

In the initial scaffold, all terminals can be simulated in one firmware process using module boundaries.
Later, this can be split across multiple simulated nodes if needed.

---

## Data Flow

- Access event (entry/exit) -> access control -> occupancy update -> event logger
- Room occupancy event -> lighting engine -> logger
- Schedule checks gate access attempts
- Serial menu exposes status, logs, and config controls

---

## Reliability and Consistency

To prevent inconsistent occupancy:

- Person state map: each ID is either `INSIDE` or `OUTSIDE`
- Duplicate entry rejected if already inside
- Duplicate exit rejected if no prior entry
- Occupancy floor is always zero

---

## Suggested Extensions

- Estimated energy-savings counters
- Repeated denied-access alerts
- Closing-soon warnings

