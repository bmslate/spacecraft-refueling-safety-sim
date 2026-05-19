import csv
import os
import time
from datetime import datetime

import serial


# ======================================================
# Serial Configuration
# ======================================================

PORT = "COM9"          # Change this to your Teensy COM port
BAUD_RATE = 115200
TIMEOUT_SECONDS = 1


# ======================================================
# Safety Thresholds
# Must match TeensyRefuelingController.ino
# ======================================================

ALIGNMENT_ABORT_THRESHOLD = 80
PRESSURE_MIN_SAFE = 25
PRESSURE_MAX_SAFE = 32


# ======================================================
# CSV Log Path
# ======================================================

CSV_PATH = os.path.join("logs", "teensy_telemetry_log.csv")


def open_teensy_serial():
    """
    Open the Teensy serial port.
    Make sure Arduino Serial Monitor is closed before running this script.
    """
    print(f"Opening Teensy serial port: {PORT} at {BAUD_RATE} baud")

    ser = serial.Serial(
        port=PORT,
        baudrate=BAUD_RATE,
        timeout=TIMEOUT_SECONDS
    )

    # Give Teensy/USB serial a moment to settle
    time.sleep(2)

    print("Serial connection opened.")
    return ser


def parse_telemetry(line):
    """
    Example:
    TLM,STATE=REFUELING,ALIGN=85,PRESSURE=27,FUEL=20,DOCK=1,GATE=OPEN,FAULT=NONE
    """
    if not line.startswith("TLM,"):
        return None

    result = {}
    parts = line.split(",")[1:]

    for part in parts:
        if "=" in part:
            key, value = part.split("=", 1)
            result[key.strip()] = value.strip()

    return result


def send_command(ser, command):
    """
    Send one command line to Teensy.
    """
    print(f"TX: {command}")
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()


def read_line(ser):
    """
    Read one line from Teensy serial output.
    """
    raw = ser.readline()

    if not raw:
        return None

    line = raw.decode("utf-8", errors="replace").strip()

    if line:
        print(f"RX: {line}")

    return line


def should_abort(telemetry):
    """
    Decide whether supervisor should send ABORT based on telemetry.
    """
    state = telemetry.get("STATE", "")
    fault = telemetry.get("FAULT", "NONE")

    try:
        alignment = int(telemetry.get("ALIGN", "100"))
        pressure = int(telemetry.get("PRESSURE", "27"))
    except ValueError:
        return True, "INVALID_TELEMETRY_VALUE"

    # If Teensy is already in fault/abort, do not repeatedly send abort
    if fault != "NONE":
        return False, ""

    if state == "REFUELING" and alignment < ALIGNMENT_ABORT_THRESHOLD:
        return True, "ALIGNMENT_LOST"

    if state == "REFUELING" and not (PRESSURE_MIN_SAFE <= pressure <= PRESSURE_MAX_SAFE):
        return True, "PRESSURE_OUT_OF_RANGE"

    return False, ""


def write_csv_header(writer):
    writer.writerow([
        "timestamp",
        "state",
        "alignment",
        "pressure",
        "fuel",
        "dock",
        "gate",
        "fault"
    ])


def write_telemetry_row(writer, telemetry):
    writer.writerow([
        datetime.now().isoformat(timespec="seconds"),
        telemetry.get("STATE", ""),
        telemetry.get("ALIGN", ""),
        telemetry.get("PRESSURE", ""),
        telemetry.get("FUEL", ""),
        telemetry.get("DOCK", ""),
        telemetry.get("GATE", ""),
        telemetry.get("FAULT", "")
    ])


def read_until_ack_error_fault_or_telemetry(ser):
    """
    Read lines until we get ACK, ERR, FAULT, or TLM.
    This is useful after sending a command.
    """
    while True:
        line = read_line(ser)

        if line is None:
            return None

        if (
            line.startswith("ACK,")
            or line.startswith("ERR,")
            or line.startswith("FAULT,")
            or line.startswith("TLM,")
        ):
            return line


# def run_nominal_sequence(ser):
#     """
#     Send commands to move Teensy controller into REFUELING.
#     During CHECK_ALIGNMENT, keep foil close/touched so ALIGN >= 80.
#     During CHECK_PRESSURE, keep pressure between 25 and 30.
#     """
#     print("\n--- Running nominal refueling sequence on Teensy ---")
#     print("IMPORTANT: Keep foil aligned/touched during CHECK_ALIGNMENT and START_REFUEL.")
#     print("IMPORTANT: Keep pressure in safe range 25-30 during CHECK_PRESSURE.")

#     commands = [
#         "RESET",
#         "START_APPROACH",
#         "CHECK_ALIGNMENT",
#         "LOCK_DOCK",
#         "OPEN_GATE",
#         "CHECK_PRESSURE",
#         "START_REFUEL"
#     ]

#     for command in commands:
#         send_command(ser, command)

#         # Teensy normally prints ACK/ERR/FAULT and then TLM
#         response = read_until_ack_error_fault_or_telemetry(ser)

#         if response is None:
#             print("FAIL: No response from Teensy.")
#             return False

#         if response.startswith("ERR,") or response.startswith("FAULT,"):
#             print(f"FAIL: Teensy rejected command: {response}")
#             return False

#         # After ACK, Teensy code sends telemetry.
#         # If the first line was ACK, read until next TLM.
#         if response.startswith("ACK,"):
#             while True:
#                 line = read_line(ser)

#                 if line is None:
#                     break

#                 telemetry = parse_telemetry(line)

#                 if telemetry:
#                     print(f"STATE AFTER COMMAND: {telemetry.get('STATE')}")
#                     break

#         time.sleep(0.3)

#     print("PASS: Teensy controller reached REFUELING sequence.")
#     return True

def run_nominal_sequence(ser):
    print("\n--- Running nominal refueling sequence on Teensy ---")
    print("IMPORTANT: Keep foil aligned/touched during CHECK_ALIGNMENT and START_REFUEL.")
    print("IMPORTANT: Keep pressure in safe range 25-32 during CHECK_PRESSURE.")

    commands = [
        "RESET",
        "START_APPROACH",
        "CHECK_ALIGNMENT",
        "LOCK_DOCK",
        "OPEN_GATE",
        "CHECK_PRESSURE",
        "START_REFUEL"
    ]

    for command in commands:
        if command in ["CHECK_ALIGNMENT", "LOCK_DOCK", "CHECK_PRESSURE", "START_REFUEL"]:
            print("\nPrepare hardware input now:")
            print("- Hold/touch the foil so ALIGN >= 80")
            print("- Keep pressure in safe range 25-32")
            print(f"Next command: {command}")
            print("Continuing in 10 seconds...")
            time.sleep(10)

        send_command(ser, command)

        response = read_until_ack_error_fault_or_telemetry(ser)

        if response is None:
            print("FAIL: No response from Teensy.")
            return False

        if response.startswith("ERR,") or response.startswith("FAULT,"):
            print(f"FAIL: Teensy rejected command: {response}")
            return False

        if response.startswith("ACK,"):
            while True:
                line = read_line(ser)

                if line is None:
                    break

                telemetry = parse_telemetry(line)

                if telemetry:
                    print(f"STATE AFTER COMMAND: {telemetry.get('STATE')}")
                    break

        time.sleep(0.3)

    print("PASS: Teensy controller reached REFUELING sequence.")
    return True

def supervise_refueling(ser, writer):
    """
    Continuously monitor telemetry during REFUELING.
    If unsafe condition is detected, send ABORT with cause.
    """
    print("\n--- Supervising Teensy telemetry ---")
    print("Now create an unsafe condition during REFUELING:")
    print("- Move hand away from foil to reduce ALIGN, or")
    print("- Change pressure outside 25-32")
    print("Supervisor will send ABORT automatically.\n")

    while True:
        line = read_line(ser)

        if line is None:
            continue

        telemetry = parse_telemetry(line)

        if not telemetry:
            continue

        write_telemetry_row(writer, telemetry)

        state = telemetry.get("STATE", "")
        should_send_abort, reason = should_abort(telemetry)

        if should_send_abort:
            print(f"SUPERVISOR: Unsafe telemetry detected: {reason}")
            print("SUPERVISOR: Sending ABORT command.")
            send_command(ser, f"ABORT {reason}")

            # Read ABORT response and post-abort telemetry
            for _ in range(10):
                response_line = read_line(ser)

                if response_line is None:
                    continue

                post_abort_telemetry = parse_telemetry(response_line)

                if post_abort_telemetry:
                    write_telemetry_row(writer, post_abort_telemetry)

                    if post_abort_telemetry.get("STATE") == "ABORT":
                        print("PASS: Teensy entered ABORT state.")
                        return

        if state == "COMPLETE":
            print("INFO: Refueling completed before unsafe condition was detected.")
            return

        if state == "ABORT" or state == "FAULT":
            print(f"INFO: Teensy already entered {state}.")
            return


def get_event_log(ser):
    """
    Request and display Teensy event log.
    """
    print("\n--- Controller Event Log ---")
    send_command(ser, "GET_LOG")

    while True:
        line = read_line(ser)

        if line is None:
            break

        if line == "LOG,END":
            break


def main():
    os.makedirs(os.path.dirname(CSV_PATH), exist_ok=True)

    ser = open_teensy_serial()

    try:
        with open(CSV_PATH, "w", newline="") as file:
            writer = csv.writer(file)
            write_csv_header(writer)

            # Clear old state and move to REFUELING
            ok = run_nominal_sequence(ser)

            if ok:
                supervise_refueling(ser, writer)

            get_event_log(ser)

        print(f"\nTelemetry log saved to: {CSV_PATH}")

    finally:
        ser.close()
        print("Serial connection closed.")


if __name__ == "__main__":
    main()