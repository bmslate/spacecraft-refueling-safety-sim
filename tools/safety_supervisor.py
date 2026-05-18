import subprocess
import time
import csv
import os
import sys
from datetime import datetime


ALIGNMENT_ABORT_THRESHOLD = 60
PRESSURE_MIN_SAFE = 20
PRESSURE_MAX_SAFE = 80


def get_controller_path():
    if os.name == "nt":
        return os.path.join(".", "controller.exe")
    return os.path.join(".", "controller")


def parse_telemetry(line):
    """
    Example:
    TLM,STATE=REFUELING,ALIGN=85,PRESSURE=40,FUEL=20,DOCK=1,GATE=OPEN,FAULT=NONE
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


def send_command(process, command):
    print(f"TX: {command}")
    process.stdin.write(command + "\n")
    process.stdin.flush()


def read_line(process):
    line = process.stdout.readline()

    if not line:
        return None

    line = line.strip()

    if line:
        print(f"RX: {line}")

    return line


def read_until_ack_error_or_fault(process):
    while True:
        line = read_line(process)

        if line is None:
            return None

        if line.startswith("ACK,") or line.startswith("ERR,") or line.startswith("FAULT,"):
            return line


def read_next_telemetry(process):
    while True:
        line = read_line(process)

        if line is None:
            return None

        telemetry = parse_telemetry(line)

        if telemetry is not None:
            return telemetry


def should_abort(telemetry):
    state = telemetry.get("STATE", "")
    fault = telemetry.get("FAULT", "NONE")

    try:
        alignment = int(telemetry.get("ALIGN", "100"))
        pressure = int(telemetry.get("PRESSURE", "40"))
    except ValueError:
        return True, "INVALID_TELEMETRY_VALUE"

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


def send_and_expect_success(process, command):
    send_command(process, command)
    response = read_until_ack_error_or_fault(process)

    if response is None:
        print("FAIL: No response from controller.")
        return False

    if response.startswith("ERR,") or response.startswith("FAULT,"):
        print(f"FAIL: Controller rejected command: {response}")
        return False

    telemetry = read_next_telemetry(process)

    if telemetry:
        print(f"STATE AFTER COMMAND: {telemetry.get('STATE')}")

    time.sleep(0.2)
    return True


def test_invalid_transition(process):
    print("\n--- Test 1: Invalid state transition ---")

    send_command(process, "RESET")
    read_until_ack_error_or_fault(process)
    read_next_telemetry(process)

    send_command(process, "START_REFUEL")
    response = read_until_ack_error_or_fault(process)

    if response and response.startswith("ERR,"):
        print("PASS: START_REFUEL was blocked before docking, gate opening, and pressure check.")
    else:
        print("FAIL: START_REFUEL was not blocked.")

    read_next_telemetry(process)


def run_nominal_sequence(process):
    print("\n--- Test 2: Nominal refueling sequence ---")

    commands = [
        "RESET",
        "SIM_ALIGN 85",
        "SIM_PRESSURE 40",
        "START_APPROACH",
        "CHECK_ALIGNMENT",
        "LOCK_DOCK",
        "OPEN_GATE",
        "CHECK_PRESSURE",
        "START_REFUEL"
    ]

    for command in commands:
        ok = send_and_expect_success(process, command)

        if not ok:
            return False

    print("PASS: Controller reached REFUELING state.")
    return True


def inject_fault_and_supervise(process, csv_path):
    print("\n--- Test 3: Telemetry-based fault detection and automatic abort ---")

    os.makedirs(os.path.dirname(csv_path), exist_ok=True)

    with open(csv_path, "w", newline="") as file:
        writer = csv.writer(file)
        write_csv_header(writer)

        print("Injecting alignment loss: SIM_ALIGN 30")
        send_command(process, "SIM_ALIGN 30")

        response = read_until_ack_error_or_fault(process)

        if response:
            print(f"Injection response: {response}")

        telemetry = read_next_telemetry(process)

        if telemetry:
            write_telemetry_row(writer, telemetry)

            should_send_abort, reason = should_abort(telemetry)

            if should_send_abort:
                print(f"SUPERVISOR: Unsafe telemetry detected: {reason}")
                print("SUPERVISOR: Sending ABORT command.")
                # send_command(process, "ABORT")
                send_command(process, f"ABORT {reason}")

                abort_response = read_until_ack_error_or_fault(process)

                if abort_response:
                    print(f"ABORT RESPONSE: {abort_response}")

                abort_telemetry = read_next_telemetry(process)

                if abort_telemetry:
                    write_telemetry_row(writer, abort_telemetry)

                    if abort_telemetry.get("STATE") == "ABORT":
                        print("PASS: Controller entered ABORT state.")
                    else:
                        print("WARN: Controller did not enter ABORT state.")
            else:
                print("WARN: Supervisor did not detect an unsafe condition.")

    print(f"Telemetry log saved to: {csv_path}")


def request_event_log(process):
    print("\n--- Controller Event Log ---")

    send_command(process, "GET_LOG")

    while True:
        line = read_line(process)

        if line is None:
            break

        if line == "LOG,END":
            break


def main():
    controller_path = get_controller_path()

    if not os.path.exists(controller_path):
        print("ERROR: controller executable not found.")
        print("Please compile it first:")
        print("  gcc .\\controller\\controller.c -o .\\controller.exe")
        sys.exit(1)

    process = subprocess.Popen(
        [controller_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    # Read boot lines
    read_line(process)
    read_line(process)

    test_invalid_transition(process)

    ok = run_nominal_sequence(process)

    if ok:
        inject_fault_and_supervise(
            process,
            csv_path=os.path.join("logs", "telemetry_log.csv")
        )

    request_event_log(process)

    process.kill()

    print("\n--- Demo complete ---")


if __name__ == "__main__":
    main()