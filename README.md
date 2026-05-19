# Spacecraft Refueling Safety Supervisor

This project is an embedded and hardware-in-the-loop prototype of a spacecraft refueling safety system.

It started as a software-in-the-loop simulation using a C-based refueling controller and a Python safety supervisor. I then extended it to a Teensy 4.1 hardware-in-the-loop version using real sensor inputs, LED indicators, USB serial telemetry, and a Python serial supervisor.

The project demonstrates embedded-style state-machine control, command and telemetry design, fault detection, abort handling, event logging, and supervisor-based validation.

---

## Project Goals

The goal of this project is to demonstrate embedded software concepts relevant to spacecraft servicing and refueling:

- Command and telemetry protocol design
- State-machine based control logic
- Invalid state transition blocking
- Real and simulated sensor inputs
- Fault detection and abort handling
- Supervisor-based safety monitoring
- Hardware-in-the-loop testing with Teensy 4.1
- Serial communication between Python and embedded firmware
- LED-based hardware status indication
- Event logging
- CSV telemetry logging
- Automated validation using Python

---

## System Versions

This repository includes two related implementations.

### 1. Software-in-the-Loop Version

The software-in-the-loop version runs fully on a computer.

```text
Python Safety Supervisor  <->  C Refueling Controller
```

The C controller simulates the embedded refueling controller. The Python supervisor acts as a ground-station test harness that sends commands, monitors telemetry, detects unsafe conditions, and automatically sends abort commands.

### 2. Teensy Hardware-in-the-Loop Version

The hardware-in-the-loop version uses a Teensy 4.1 as the embedded controller.

```text
Python Serial Supervisor  <->  Teensy 4.1 Embedded Controller
                                      |
                                      +-- Foil capacitive alignment sensor
                                      +-- Analog temperature sensor used as pressure input
                                      +-- Red / Green / Yellow LED indicators
```

The Teensy reads physical sensor inputs, runs the refueling state machine, outputs telemetry over USB serial, and drives LEDs to indicate system state.

---

## System Architecture

### Software-in-the-Loop Architecture

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

### Hardware-in-the-Loop Architecture

```text
+----------------------------------+
| Python Teensy Serial Supervisor  |
|                                  |
| - Opens Teensy COM port          |
| - Sends command sequence         |
| - Reads live telemetry           |
| - Detects unsafe refueling state |
| - Sends ABORT <CAUSE>            |
| - Logs telemetry to CSV          |
+----------------+-----------------+
                 |
                 | USB serial
                 |
+----------------v-----------------+
| Teensy 4.1 Refueling Controller  |
|                                  |
| - Embedded state machine         |
| - Serial command parser          |
| - Real sensor input              |
| - Telemetry generation           |
| - Event log                      |
| - LED status output              |
+----------------+-----------------+
                 |
                 +-- Foil capacitive sensor: simulated docking alignment
                 +-- Analog temperature sensor: simulated pressure input
                 +-- Red LED: ABORT / FAULT
                 +-- Yellow LED: active operation / checking
                 +-- Green LED: safe / complete / idle
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

The software and Teensy controller versions use a similar text-based command protocol.

```text
PING
GET_STATUS
RESET
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

The software-in-the-loop C controller also supports simulated sensor commands:

```text
SIM_ALIGN 0-100
SIM_PRESSURE 0-100
```

Example abort command:

```text
ABORT ALIGNMENT_LOST
```

---

## Example Telemetry

Software-in-the-loop example:

```text
TLM,STATE=REFUELING,ALIGN=85,PRESSURE=40,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE
```

Hardware-in-the-loop example from Teensy:

```text
TLM,STATE=REFUELING,ALIGN=100,PRESSURE=29,FUEL=20,DOCK=1,GATE=OPEN,FAULT=NONE
```

Example abort telemetry:

```text
TLM,STATE=ABORT,ALIGN=30,PRESSURE=29,FUEL=20,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
```

---

## Safety Supervisor Behavior

The supervisor validates the controller using three main test ideas.

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

For the Teensy hardware version, the alignment and pressure values come from physical sensor inputs rather than fixed simulation commands.

### Test 3: Telemetry-Based Fault Detection and Automatic Abort

In the software-in-the-loop version, the supervisor injects simulated alignment loss:

```text
SIM_ALIGN 30
```

The supervisor detects that alignment is below the safe threshold during refueling and sends:

```text
ABORT ALIGNMENT_LOST
```

In the Teensy hardware-in-the-loop version, the same behavior is created by changing the real sensor input. For example, moving away from the foil sensor causes the reported alignment value to drop. The Python serial supervisor detects the unsafe telemetry and sends the abort command over USB serial.

Expected result:

```text
ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=ALIGNMENT_LOST
TLM,STATE=ABORT,ALIGN=30,PRESSURE=29,FUEL=20,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
```

---

## Teensy Hardware-in-the-Loop Implementation

The Teensy version ports the embedded controller logic into Arduino / Teensy firmware.

### Hardware Used

- Teensy 4.1
- Foil-based capacitive sensor
- Analog temperature sensor used as simulated pressure input
- Red LED for ABORT / FAULT
- Green LED for SAFE / NORMAL / COMPLETE
- Yellow LED for ACTIVE / CHECKING / REFUELING

### Hardware Pin Mapping

```text
Foil capacitive sensor:
SEND_PIN    = 17
RECEIVE_PIN = 8

Simulated pressure sensor:
TEMP_PIN = A7

LED indicators:
RED_LED    = Pin 16
GREEN_LED  = Pin 15
YELLOW_LED = Pin 14
```

### Teensy Firmware Files

```text
hardware_teensy/TeensyRefuelingHardwareTest.ino
hardware_teensy/TeensyRefuelingController.ino
```

`TeensyRefuelingHardwareTest.ino` is the sensor and LED validation version.

`TeensyRefuelingController.ino` is the full embedded controller version with state-machine logic, serial command handling, telemetry output, event logging, fuel update behavior, and LED state output.

### LED Behavior

```text
Red LED    = ABORT, FAULT, or unsafe sensor condition
Yellow LED = active refueling sequence state
Green LED  = safe idle, safe complete, or normal non-active state
```

The red LED has the highest priority. If alignment or pressure is outside the safe range, the red LED turns on even if the controller is in an active state such as APPROACH.

---

## Build and Run Instructions

### Software-in-the-Loop Version on Windows

Compile the C controller:

```powershell
gcc .\controller\controller.c -o .\controller.exe
```

Run the Python supervisor:

```powershell
python .\tools\safety_supervisor.py
```

### Software-in-the-Loop Version on macOS / Linux

Compile the C controller:

```bash
gcc ./controller/controller.c -o ./controller
```

Run the Python supervisor:

```bash
python3 ./tools/safety_supervisor.py
```

### Teensy Hardware-in-the-Loop Version

1. Open `hardware_teensy/TeensyRefuelingController.ino` in Arduino IDE.
2. Select the Teensy 4.1 board.
3. Install the `CapacitiveSensor` library if needed.
4. Upload the firmware to the Teensy.
5. Close Arduino Serial Monitor before running the Python serial supervisor.
6. Install Python serial support:

```powershell
python -m pip install pyserial
```

7. Confirm the Teensy COM port and update `PORT` in:

```text
tools/teensy_serial_supervisor.py
```

8. Run the Teensy serial supervisor:

```powershell
python .\tools\teensy_serial_supervisor.py
```

During the run, keep the foil sensor aligned/touched during alignment-related checks and keep the simulated pressure input inside the safe range. After the controller reaches REFUELING, create an unsafe condition to show the Python supervisor automatically sending an abort command.

---

## Project Structure

```text
spacecraft-refueling-safety-sim/
├── controller/
│   └── controller.c
├── hardware_teensy/
│   ├── TeensyRefuelingHardwareTest.ino
│   └── TeensyRefuelingController.ino
├── tools/
│   ├── safety_supervisor.py
│   └── teensy_serial_supervisor.py
├── logs/
│   ├── telemetry_log.csv
│   └── teensy_telemetry_log.csv
├── .gitignore
└── README.md
```

---

## Example Demo Output

### Software-in-the-Loop Demo

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

### Teensy Hardware-in-the-Loop Demo

```text
--- Running nominal refueling sequence on Teensy ---
TX: RESET
RX: ACK,RESET_TO_SAFE
RX: TLM,STATE=SAFE,ALIGN=100,PRESSURE=29,FUEL=0,DOCK=0,GATE=CLOSED,FAULT=NONE

TX: START_APPROACH
RX: ACK,APPROACH_STARTED
RX: TLM,STATE=APPROACH,ALIGN=100,PRESSURE=29,FUEL=0,DOCK=0,GATE=CLOSED,FAULT=NONE

TX: CHECK_ALIGNMENT
RX: ACK,ALIGNMENT_OK
RX: TLM,STATE=ALIGNMENT_CHECK,ALIGN=100,PRESSURE=29,FUEL=0,DOCK=0,GATE=CLOSED,FAULT=NONE

TX: START_REFUEL
RX: ACK,REFUELING_STARTED
RX: TLM,STATE=REFUELING,ALIGN=100,PRESSURE=29,FUEL=0,DOCK=1,GATE=OPEN,FAULT=NONE

SUPERVISOR: Unsafe telemetry detected: ALIGNMENT_LOST
TX: ABORT ALIGNMENT_LOST
RX: ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=ALIGNMENT_LOST
RX: TLM,STATE=ABORT,ALIGN=30,PRESSURE=29,FUEL=20,DOCK=1,GATE=CLOSED,FAULT=ALIGNMENT_LOST
PASS: Teensy entered ABORT state.
```

---

## Event Log Example

```text
LOG,BEGIN
LOG,1,SYSTEM_BOOT
LOG,2,RESET_TO_SAFE
LOG,3,START_APPROACH
LOG,4,ALIGNMENT_OK
LOG,5,DOCK_LOCKED
LOG,6,GATE_OPEN
LOG,7,PRESSURE_OK
LOG,8,REFUELING_STARTED
LOG,9,ABORT_ALIGNMENT_LOST
LOG,END
```

---

## Engineering Challenge

One engineering challenge was deciding how to separate responsibilities between the embedded controller and the supervisor.

The controller is responsible for command parsing, state-machine control, telemetry reporting, event logging, LED output, and executing abort behavior. The supervisor is responsible for monitoring telemetry, detecting unsafe conditions during refueling, and commanding aborts when safety limits are violated.

This separation makes the project closer to a software-in-the-loop and hardware-in-the-loop validation setup, where an embedded controller can be tested by an external diagnostic or ground-station tool.

Another challenge was connecting real hardware inputs to the state machine. The foil capacitive sensor had to be calibrated before use, and LED polarity and wiring had to be verified during hardware debugging. The hardware version also required aligning the Teensy firmware safety thresholds with the Python serial supervisor thresholds.

---

## AI Usage

I used AI to help brainstorm the project structure, review possible edge cases, compare implementation options, and improve the explanation of the architecture.

I customized the project around spacecraft servicing and refueling, and I implemented the controller and supervisor logic with a focus on embedded-style state-machine control, telemetry monitoring, fault detection, hardware-in-the-loop testing, and automated validation.

AI was also used as a development assistant while debugging serial communication, hardware wiring, README wording, and the Python supervisor workflow. I reviewed and tested the resulting code on my local machine and Teensy hardware.

---

## Limitations

This project is not flight-qualified hardware or flight software.

It is a prototype designed to demonstrate embedded control concepts. The Teensy hardware setup uses simplified sensors to simulate spacecraft subsystem inputs. In a real spacecraft system, the controller would require formal verification, hardware-in-the-loop testing with representative sensors and actuators, fault tolerance analysis, timing validation, redundancy, watchdog behavior, communication integrity checks, and environmental qualification.

---

## Future Improvements

- Add UART or CAN-style packet framing
- Add checksum or CRC validation
- Add watchdog timeout behavior
- Add automatic sensor calibration persistence
- Add unit tests for the command parser
- Add a graphical ground-station dashboard
- Add more detailed fault recovery modes
- Add retry logic and timeout handling in the Python serial supervisor
- Add more realistic pressure and alignment sensor models
