# Software-in-the-Loop Spacecraft Refueling Safety Supervisor

This project is a software-in-the-loop simulation of a spacecraft refueling safety system.

It includes a C-based embedded-style refueling controller simulator and a Python safety supervisor. The C controller simulates a spacecraft refueling subsystem, while the Python supervisor acts as a ground-station test harness that sends commands, monitors telemetry, detects unsafe conditions, and automatically sends an abort command.

This project was built for a small embedded or hardware-related project challenge. I did not have physical embedded hardware available during the challenge window, so I focused on software-in-the-loop validation of embedded control logic.

---

## Project Goals

The goal of this project is to demonstrate embedded software concepts relevant to spacecraft servicing and refueling:

- Command and telemetry protocol design
- State-machine based control logic
- Invalid state transition blocking
- Simulated sensor inputs
- Fault detection and abort handling
- Supervisor-based safety monitoring
- Event logging
- CSV telemetry logging
- Automated validation using Python

---

## System Architecture

```text
+----------------------------+
| Python Safety Supervisor   |
|                            |
| - Sends command sequence   |
| - Reads telemetry          |
| - Detects unsafe states    |
| - Sends ABORT command      |
| - Logs telemetry to CSV    |
+-------------+--------------+
              |
              | command / telemetry protocol
              |
+-------------v--------------+
| C Refueling Controller     |
|                            |
| - Refueling state machine  |
| - Command parser           |
| - Telemetry generation     |
| - Event log                |
| - Fault / abort handling   |
+----------------------------+
```

---

## Refueling State Machine

The controller simulates a simplified spacecraft refueling sequence:

```text
IDLE
  -> APPROACH
  -> ALIGNMENT_CHECK
  -> DOCK_LOCKED
  -> GATE_OPEN
  -> PRESSURE_CHECK
  -> REFUELING
  -> COMPLETE
```

Safety and fault paths:

```text
REFUELING -> ABORT
Unsafe validation -> FAULT
RESET -> SAFE
```

The controller blocks invalid state transitions. For example, refueling cannot begin unless the approach, alignment check, dock lock, gate opening, and pressure check have completed successfully.

---

## Supported Commands

```text
PING
GET_STATUS
RESET
SIM_ALIGN 0-100
SIM_PRESSURE 0-100
START_APPROACH
CHECK_ALIGNMENT
LOCK_DOCK
OPEN_GATE
CHECK_PRESSURE
START_REFUEL
STOP_REFUEL
ABORT <CAUSE>
INJECT_FAULT
GET_LOG
```

Example command:

```text
ABORT ALIGNMENT_LOST
```

---

## Example Telemetry

```text
TLM,STATE=REFUELING,ALIGN=85,PRESSURE=40,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE
```

Example abort telemetry:

```text
TLM,STATE=ABORT,ALIGN=30,PRESSURE=40,FUEL=0,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
```

---

## Safety Supervisor Behavior

The Python supervisor automatically runs three tests.

### Test 1: Invalid State Transition

The supervisor tries to start refueling before the required checks are complete.

Expected result:

```text
ERR,REFUEL_BLOCKED_PRESSURE_CHECK_REQUIRED
```

This verifies that the controller blocks unsafe command sequences.

### Test 2: Nominal Refueling Sequence

The supervisor runs the correct sequence:

```text
RESET
SIM_ALIGN 85
SIM_PRESSURE 40
START_APPROACH
CHECK_ALIGNMENT
LOCK_DOCK
OPEN_GATE
CHECK_PRESSURE
START_REFUEL
```

Expected result:

```text
TLM,STATE=REFUELING,ALIGN=85,PRESSURE=40,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE
```

### Test 3: Telemetry-Based Fault Detection and Automatic Abort

The supervisor injects simulated alignment loss:

```text
SIM_ALIGN 30
```

The supervisor detects that alignment is below the safe threshold during refueling and sends:

```text
ABORT ALIGNMENT_LOST
```

Expected result:

```text
ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=ALIGNMENT_LOST
TLM,STATE=ABORT,ALIGN=30,PRESSURE=40,FUEL=0,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
```

---

## Build Instructions

### Windows

Compile the C controller:

```powershell
gcc .\controller\controller.c -o .\controller.exe
```

Run the Python supervisor:

```powershell
python .\tools\safety_supervisor.py
```

### macOS / Linux

Compile the C controller:

```bash
gcc ./controller/controller.c -o ./controller
```

Run the Python supervisor:

```bash
python3 ./tools/safety_supervisor.py
```

---

## Project Structure

```text
spacecraft-refueling-safety-sim/
├── controller/
│   └── controller.c
├── tools/
│   └── safety_supervisor.py
├── logs/
│   └── telemetry_log.csv
├── controller.exe
└── README.md
```

---

## Example Demo Output

```text
--- Test 1: Invalid state transition ---
TX: START_REFUEL
RX: ERR,REFUEL_BLOCKED_PRESSURE_CHECK_REQUIRED
PASS: START_REFUEL was blocked before docking, gate opening, and pressure check.

--- Test 2: Nominal refueling sequence ---
TX: START_REFUEL
RX: ACK,REFUELING_STARTED
RX: TLM,STATE=REFUELING,ALIGN=85,PRESSURE=40,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE
PASS: Controller reached REFUELING state.

--- Test 3: Telemetry-based fault detection and automatic abort ---
TX: SIM_ALIGN 30
RX: TLM,STATE=REFUELING,ALIGN=30,PRESSURE=40,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE
SUPERVISOR: Unsafe telemetry detected: ALIGNMENT_LOST
TX: ABORT ALIGNMENT_LOST
RX: ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=ALIGNMENT_LOST
RX: TLM,STATE=ABORT,ALIGN=30,PRESSURE=40,FUEL=0,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
PASS: Controller entered ABORT state.
```

---

## Event Log Example

```text
LOG,BEGIN
LOG,1,SYSTEM_BOOT
LOG,2,RESET_TO_SAFE
LOG,3,RESET_TO_SAFE
LOG,4,START_APPROACH
LOG,5,ALIGNMENT_OK
LOG,6,DOCK_LOCKED
LOG,7,GATE_OPEN
LOG,8,PRESSURE_OK
LOG,9,REFUELING_STARTED
LOG,10,ABORT_ALIGNMENT_LOST
LOG,END
```

---

## Engineering Challenge

One engineering challenge was deciding how to separate responsibilities between the controller and the supervisor.

The C controller is responsible for command parsing, state-machine control, telemetry reporting, and abort handling. The Python supervisor is responsible for monitoring telemetry, detecting unsafe conditions, and commanding aborts when safety limits are violated.

This separation makes the project closer to a software-in-the-loop validation setup, where an embedded controller can be tested by an external diagnostic or ground-station tool.

Another challenge was preventing unsafe command sequences. The controller blocks refueling unless the required steps are completed in order: approach, alignment check, docking lock, gate opening, and pressure check.

---

## AI Usage

I used AI to help brainstorm the project structure, review possible edge cases, and improve the explanation of the architecture.

I customized the project around spacecraft servicing and refueling, and I implemented the controller and supervisor logic with a focus on embedded-style state-machine control, telemetry monitoring, fault detection, and automated validation.

---

## Limitations

This project is not flight-qualified hardware or flight software.

It is a software-in-the-loop prototype designed to demonstrate embedded control concepts without requiring physical hardware. In a real spacecraft system, the controller would need hardware-in-the-loop testing, formal verification, fault tolerance analysis, timing validation, redundancy, and environmental qualification.

---

## Future Improvements

- Port the C controller to a Teensy or another microcontroller
- Add UART or CAN-style packet framing
- Add checksum or CRC validation
- Add watchdog timeout behavior
- Add unit tests for the command parser
- Add a graphical ground-station dashboard
- Add real sensor input and actuator output
- Add more detailed fault recovery modes
