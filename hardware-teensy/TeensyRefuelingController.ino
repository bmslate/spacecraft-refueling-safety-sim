#include <CapacitiveSensor.h>
#include <string.h>
#include <ctype.h>

// ======================================================
// Hardware Pin Mapping
// ======================================================

// Foil capacitive sensor: simulated docking alignment
#define SEND_PIN 17
#define RECEIVE_PIN 8

// Temperature sensor used as simulated pressure input
#define TEMP_PIN A7

// LEDs
#define RED_LED 16      // ABORT / FAULT
#define GREEN_LED 15    // SAFE / NORMAL
#define YELLOW_LED 14   // WARNING / ACTIVE / CHECKING

// ======================================================
// Controller Constants
// ======================================================

#define MAX_LOGS 12
#define MAX_LINE 128

#define ALIGNMENT_MIN_SAFE 80
#define PRESSURE_MIN_SAFE 25
#define PRESSURE_MAX_SAFE 32

#define REFUEL_UPDATE_INTERVAL_MS 700

// ======================================================
// Capacitive Sensor
// ======================================================

CapacitiveSensor capSensor = CapacitiveSensor(SEND_PIN, RECEIVE_PIN);

long sensorMin = 999999;
long sensorMax = 0;

// ======================================================
// State Machine
// ======================================================

typedef enum {
  STATE_IDLE,
  STATE_APPROACH,
  STATE_ALIGNMENT_CHECK,
  STATE_DOCK_LOCKED,
  STATE_GATE_OPEN,
  STATE_PRESSURE_CHECK,
  STATE_REFUELING,
  STATE_COMPLETE,
  STATE_ABORT,
  STATE_SAFE,
  STATE_FAULT
} SystemState;

typedef struct {
  SystemState state;
  int alignment;
  int pressure;
  int fuel;
  int dockLocked;
  int gateOpen;
  char faultCause[64];

  char eventLog[MAX_LOGS][96];
  int logCount;

  unsigned long lastFuelUpdate;
} RefuelingController;

RefuelingController controller;
char inputBuffer[MAX_LINE];
int inputIndex = 0;

// ======================================================
// Helper Functions
// ======================================================

const char* stateToString(SystemState state) {
  switch (state) {
    case STATE_IDLE: return "IDLE";
    case STATE_APPROACH: return "APPROACH";
    case STATE_ALIGNMENT_CHECK: return "ALIGNMENT_CHECK";
    case STATE_DOCK_LOCKED: return "DOCK_LOCKED";
    case STATE_GATE_OPEN: return "GATE_OPEN";
    case STATE_PRESSURE_CHECK: return "PRESSURE_CHECK";
    case STATE_REFUELING: return "REFUELING";
    case STATE_COMPLETE: return "COMPLETE";
    case STATE_ABORT: return "ABORT";
    case STATE_SAFE: return "SAFE";
    case STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
  }
}

void toUpperCase(char* text) {
  for (int i = 0; text[i] != '\0'; i++) {
    text[i] = (char)toupper((unsigned char)text[i]);
  }
}

void trimNewline(char* text) {
  size_t len = strlen(text);

  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
    text[len - 1] = '\0';
    len--;
  }
}

void addEvent(RefuelingController* controller, const char* eventText) {
  if (controller->logCount < MAX_LOGS) {
    snprintf(controller->eventLog[controller->logCount], 96, "%s", eventText);
    controller->logCount++;
  } else {
    for (int i = 1; i < MAX_LOGS; i++) {
      snprintf(controller->eventLog[i - 1], 96, "%s", controller->eventLog[i]);
    }

    snprintf(controller->eventLog[MAX_LOGS - 1], 96, "%s", eventText);
  }
}

void splitCommand(char* input, char* command, char* argument) {
  command[0] = '\0';
  argument[0] = '\0';

  char* token = strtok(input, " ");

  if (token != NULL) {
    snprintf(command, 64, "%s", token);
  }

  token = strtok(NULL, " ");

  if (token != NULL) {
    snprintf(argument, 64, "%s", token);
  }
}

// ======================================================
// Sensor Reading
// ======================================================

void calibrateSensorMin() {
  long startTime = millis();

  while (millis() - startTime < 3000) {
    long value = capSensor.capacitiveSensor(50);

    if (value > 0 && value < sensorMin) {
      sensorMin = value;
    }

    delay(50);
  }

  Serial.print("INFO,SENSOR_MIN=");
  Serial.println(sensorMin);
}

void calibrateSensorMax() {
  long startTime = millis();

  while (millis() - startTime < 3000) {
    long value = capSensor.capacitiveSensor(50);

    if (value > sensorMax) {
      sensorMax = value;
    }

    delay(50);
  }

  Serial.print("INFO,SENSOR_MAX=");
  Serial.println(sensorMax);
}

void calibrateFoilSensor() {
  Serial.println("INFO,FOIL_CALIBRATION_START");
  Serial.println("INFO,STEP_1_DO_NOT_TOUCH_FOIL");
  delay(2000);
  calibrateSensorMin();

  Serial.println("INFO,STEP_2_TOUCH_AND_HOLD_FOIL");
  delay(2000);
  calibrateSensorMax();

  if (sensorMax <= sensorMin) {
    Serial.println("WARN,FOIL_CALIBRATION_INVALID_USING_FALLBACK");
    sensorMin = 0;
    sensorMax = 1000;
  }

  Serial.println("INFO,FOIL_CALIBRATION_COMPLETE");
  Serial.print("INFO,FINAL_SENSOR_MIN=");
  Serial.println(sensorMin);
  Serial.print("INFO,FINAL_SENSOR_MAX=");
  Serial.println(sensorMax);
}

int readAlignmentSensor() {
  long raw = capSensor.capacitiveSensor(50);
  long limitedRaw = constrain(raw, sensorMin, sensorMax);

  // Far / not touching = lower alignment
  // Close / touching = higher alignment
  int alignment = map(limitedRaw, sensorMin, sensorMax, 30, 100);
  alignment = constrain(alignment, 0, 100);

  return alignment;
}

int readPressureSensor() {
  float code = analogRead(TEMP_PIN);

  // Original temperature conversion formula.
  // In this prototype, the value is used as simulated pressure.
  float celsius = 25 + (code - 512) / 11.3;

  int pressure = (int)celsius;
  pressure = constrain(pressure, 0, 100);

  return pressure;
}

void updateSensorInputs(RefuelingController* controller) {
  controller->alignment = readAlignmentSensor();
  controller->pressure = readPressureSensor();
}

// ======================================================
// LEDs
// ======================================================

void turnOffAllLEDs() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
}

void updateLEDs(const RefuelingController* controller) {
  turnOffAllLEDs();

  if (controller->state == STATE_ABORT || controller->state == STATE_FAULT) {
    digitalWrite(RED_LED, HIGH);
    return;
  }

  if (controller->alignment < ALIGNMENT_MIN_SAFE ||
      controller->pressure < PRESSURE_MIN_SAFE ||
      controller->pressure > PRESSURE_MAX_SAFE) {
    digitalWrite(RED_LED, HIGH);
    return;
  }

  if (controller->state == STATE_REFUELING ||
      controller->state == STATE_APPROACH ||
      controller->state == STATE_ALIGNMENT_CHECK ||
      controller->state == STATE_DOCK_LOCKED ||
      controller->state == STATE_GATE_OPEN ||
      controller->state == STATE_PRESSURE_CHECK) {
    digitalWrite(YELLOW_LED, HIGH);
    return;
  }

  digitalWrite(GREEN_LED, HIGH);
}

void testLEDs() {
  Serial.println("INFO,LED_SELF_TEST_START");

  digitalWrite(RED_LED, HIGH);
  delay(400);
  digitalWrite(RED_LED, LOW);

  digitalWrite(GREEN_LED, HIGH);
  delay(400);
  digitalWrite(GREEN_LED, LOW);

  digitalWrite(YELLOW_LED, HIGH);
  delay(400);
  digitalWrite(YELLOW_LED, LOW);

  Serial.println("INFO,LED_SELF_TEST_COMPLETE");
}

// ======================================================
// Controller State Functions
// ======================================================

void initializeController(RefuelingController* controller) {
  controller->state = STATE_IDLE;
  controller->alignment = 85;
  controller->pressure = 40;
  controller->fuel = 0;
  controller->dockLocked = 0;
  controller->gateOpen = 0;
  controller->logCount = 0;
  snprintf(controller->faultCause, sizeof(controller->faultCause), "NONE");
  controller->lastFuelUpdate = millis();

  addEvent(controller, "SYSTEM_BOOT");
}

void resetToSafe(RefuelingController* controller) {
  controller->state = STATE_SAFE;
  controller->fuel = 0;
  controller->dockLocked = 0;
  controller->gateOpen = 0;
  snprintf(controller->faultCause, sizeof(controller->faultCause), "NONE");
  controller->lastFuelUpdate = millis();

  addEvent(controller, "RESET_TO_SAFE");
}

void sendTelemetry(const RefuelingController* controller) {
  Serial.print("TLM,STATE=");
  Serial.print(stateToString(controller->state));

  Serial.print(",ALIGN=");
  Serial.print(controller->alignment);

  Serial.print(",PRESSURE=");
  Serial.print(controller->pressure);

  Serial.print(",FUEL=");
  Serial.print(controller->fuel);

  Serial.print(",DOCK=");
  Serial.print(controller->dockLocked);

  Serial.print(",GATE=");
  Serial.print(controller->gateOpen ? "OPEN" : "CLOSED");

  Serial.print(",FAULT=");
  Serial.println(controller->faultCause);
}

void printLog(const RefuelingController* controller) {
  Serial.println("LOG,BEGIN");

  for (int i = 0; i < controller->logCount; i++) {
    Serial.print("LOG,");
    Serial.print(i + 1);
    Serial.print(",");
    Serial.println(controller->eventLog[i]);
  }

  Serial.println("LOG,END");
}

void enterAbort(RefuelingController* controller, const char* cause) {
  controller->state = STATE_ABORT;
  controller->gateOpen = 0;
  snprintf(controller->faultCause, sizeof(controller->faultCause), "%s", cause);

  char logMessage[96];
  snprintf(logMessage, sizeof(logMessage), "ABORT_%s", cause);
  addEvent(controller, logMessage);

  Serial.print("ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=");
  Serial.println(cause);
}

void enterFault(RefuelingController* controller, const char* cause) {
  controller->state = STATE_FAULT;
  controller->gateOpen = 0;
  snprintf(controller->faultCause, sizeof(controller->faultCause), "%s", cause);

  char logMessage[96];
  snprintf(logMessage, sizeof(logMessage), "FAULT_%s", cause);
  addEvent(controller, logMessage);

  Serial.print("FAULT,");
  Serial.println(cause);
}

void updateRefueling(RefuelingController* controller) {
  if (controller->state != STATE_REFUELING) {
    return;
  }

  unsigned long now = millis();

  if (now - controller->lastFuelUpdate >= REFUEL_UPDATE_INTERVAL_MS) {
    controller->lastFuelUpdate = now;

    if (controller->fuel < 100) {
      controller->fuel += 5;
    }

    if (controller->fuel >= 100) {
      controller->fuel = 100;
      controller->gateOpen = 0;
      controller->state = STATE_COMPLETE;
      addEvent(controller, "REFUELING_COMPLETE");
      Serial.println("ACK,REFUELING_COMPLETE");
    }
  }
}

// ======================================================
// Command Handling
// ======================================================

void handleCommand(RefuelingController* controller, char* input) {
  trimNewline(input);
  toUpperCase(input);

  char command[64];
  char argument[64];

  splitCommand(input, command, argument);

  if (strlen(command) == 0) {
    Serial.println("ERR,EMPTY_COMMAND");
    return;
  }

  if (strcmp(command, "PING") == 0) {
    Serial.println("ACK,PING");
    return;
  }

  if (strcmp(command, "GET_STATUS") == 0) {
    sendTelemetry(controller);
    return;
  }

  if (strcmp(command, "GET_LOG") == 0) {
    printLog(controller);
    return;
  }

  if (strcmp(command, "RESET") == 0) {
    resetToSafe(controller);
    Serial.println("ACK,RESET_TO_SAFE");
    return;
  }

  if (strcmp(command, "ABORT") == 0) {
    if (strlen(argument) > 0) {
      enterAbort(controller, argument);
    } else {
      enterAbort(controller, "SUPERVISOR_COMMAND");
    }
    return;
  }

  if (strcmp(command, "INJECT_FAULT") == 0) {
    enterFault(controller, "MANUAL_INJECTION");
    return;
  }

  if ((controller->state == STATE_ABORT || controller->state == STATE_FAULT) &&
      strcmp(command, "RESET") != 0 &&
      strcmp(command, "GET_STATUS") != 0 &&
      strcmp(command, "GET_LOG") != 0 &&
      strcmp(command, "PING") != 0) {
    Serial.println("ERR,SYSTEM_IN_FAULT_OR_ABORT_STATE");
    return;
  }

  if (strcmp(command, "START_APPROACH") == 0) {
    if (controller->state != STATE_IDLE && controller->state != STATE_SAFE) {
      Serial.println("ERR,INVALID_STATE_TRANSITION");
      return;
    }

    controller->state = STATE_APPROACH;
    addEvent(controller, "START_APPROACH");
    Serial.println("ACK,APPROACH_STARTED");
    return;
  }

  if (strcmp(command, "CHECK_ALIGNMENT") == 0) {
    if (controller->state != STATE_APPROACH) {
      Serial.println("ERR,ALIGNMENT_CHECK_REQUIRES_APPROACH");
      return;
    }

    controller->state = STATE_ALIGNMENT_CHECK;

    if (controller->alignment < ALIGNMENT_MIN_SAFE) {
      enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
      return;
    }

    addEvent(controller, "ALIGNMENT_OK");
    Serial.println("ACK,ALIGNMENT_OK");
    return;
  }

  if (strcmp(command, "LOCK_DOCK") == 0) {
    if (controller->state != STATE_ALIGNMENT_CHECK) {
      Serial.println("ERR,DOCK_LOCK_REQUIRES_ALIGNMENT_CHECK");
      return;
    }

    if (controller->alignment < ALIGNMENT_MIN_SAFE) {
      enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
      return;
    }

    controller->dockLocked = 1;
    controller->state = STATE_DOCK_LOCKED;
    addEvent(controller, "DOCK_LOCKED");
    Serial.println("ACK,DOCK_LOCKED");
    return;
  }

  if (strcmp(command, "OPEN_GATE") == 0) {
    if (controller->state != STATE_DOCK_LOCKED) {
      Serial.println("ERR,GATE_BLOCKED_DOCK_NOT_LOCKED");
      return;
    }

    controller->gateOpen = 1;
    controller->state = STATE_GATE_OPEN;
    addEvent(controller, "GATE_OPEN");
    Serial.println("ACK,GATE_OPEN");
    return;
  }

  if (strcmp(command, "CHECK_PRESSURE") == 0) {
    if (controller->state != STATE_GATE_OPEN) {
      Serial.println("ERR,PRESSURE_CHECK_REQUIRES_GATE_OPEN");
      return;
    }

    if (controller->pressure < PRESSURE_MIN_SAFE ||
        controller->pressure > PRESSURE_MAX_SAFE) {
      enterFault(controller, "PRESSURE_OUT_OF_RANGE");
      return;
    }

    controller->state = STATE_PRESSURE_CHECK;
    addEvent(controller, "PRESSURE_OK");
    Serial.println("ACK,PRESSURE_OK");
    return;
  }

  if (strcmp(command, "START_REFUEL") == 0) {
    if (controller->state != STATE_PRESSURE_CHECK) {
      Serial.println("ERR,REFUEL_BLOCKED_PRESSURE_CHECK_REQUIRED");
      return;
    }

    if (!controller->dockLocked) {
      Serial.println("ERR,REFUEL_BLOCKED_DOCK_NOT_LOCKED");
      return;
    }

    if (!controller->gateOpen) {
      Serial.println("ERR,REFUEL_BLOCKED_GATE_NOT_OPEN");
      return;
    }

    if (controller->alignment < ALIGNMENT_MIN_SAFE) {
      enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
      return;
    }

    if (controller->pressure < PRESSURE_MIN_SAFE ||
        controller->pressure > PRESSURE_MAX_SAFE) {
      enterFault(controller, "PRESSURE_OUT_OF_RANGE");
      return;
    }

    controller->state = STATE_REFUELING;
    controller->fuel = 0;
    controller->lastFuelUpdate = millis();
    addEvent(controller, "REFUELING_STARTED");
    Serial.println("ACK,REFUELING_STARTED");
    return;
  }

  if (strcmp(command, "STOP_REFUEL") == 0) {
    if (controller->state != STATE_REFUELING) {
      Serial.println("ERR,NOT_REFUELING");
      return;
    }

    controller->state = STATE_SAFE;
    controller->gateOpen = 0;
    addEvent(controller, "REFUELING_STOPPED_BY_COMMAND");
    Serial.println("ACK,REFUELING_STOPPED_ENTERING_SAFE");
    return;
  }

  Serial.println("ERR,INVALID_COMMAND");
}

// ======================================================
// Serial Input
// ======================================================

void processSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputIndex > 0) {
        inputBuffer[inputIndex] = '\0';

        updateSensorInputs(&controller);
        updateRefueling(&controller);

        handleCommand(&controller, inputBuffer);

        updateSensorInputs(&controller);
        updateRefueling(&controller);

        sendTelemetry(&controller);

        inputIndex = 0;
      }
    } else {
      if (inputIndex < MAX_LINE - 1) {
        inputBuffer[inputIndex++] = c;
      }
    }
  }
}

// ======================================================
// Arduino Setup and Loop
// ======================================================

void setup() {
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);

  turnOffAllLEDs();

  while (!Serial && millis() < 3000) {
    // wait for Serial Monitor or Python serial connection
  }

  Serial.println("BOOT,TEENSY_REFUELING_CONTROLLER");
  Serial.println("INFO,READY_FOR_COMMANDS");

  testLEDs();
  calibrateFoilSensor();

  initializeController(&controller);
  updateSensorInputs(&controller);
  updateLEDs(&controller);
  sendTelemetry(&controller);
}

//void loop() {
//  updateSensorInputs(&controller);
//  updateRefueling(&controller);
//  updateLEDs(&controller);
//
//  processSerialInput();
//
//  // Optional periodic telemetry during refueling
//  static unsigned long lastPeriodicTelemetry = 0;
//
//  if (millis() - lastPeriodicTelemetry >= 1000) {
//    lastPeriodicTelemetry = millis();
//
//    if (controller.state == STATE_REFUELING) {
//      sendTelemetry(&controller);
//    }
//  }
//}

void loop() {
  updateSensorInputs(&controller);
  updateRefueling(&controller);
  updateLEDs(&controller);

  processSerialInput();

  // Periodic telemetry every 10 seconds in all states
  static unsigned long lastPeriodicTelemetry = 0;

  if (millis() - lastPeriodicTelemetry >= 10000) {
    lastPeriodicTelemetry = millis();
    sendTelemetry(&controller);
  }
}