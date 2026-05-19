#include <CapacitiveSensor.h>

// Foil capacitive sensor
#define SEND_PIN 17
#define RECEIVE_PIN 8

// Temperature sensor used as simulated pressure input
#define TEMP_PIN A7

// LEDs
#define RED_LED 16     // ABORT / FAULT
#define GREEN_LED 15   // SAFE / NORMAL
#define YELLOW_LED 14  // WARNING

CapacitiveSensor capSensor = CapacitiveSensor(SEND_PIN, RECEIVE_PIN);

// Calibration values for foil sensor
long sensorMin = 999999;
long sensorMax = 0;

// Safety thresholds
#define ALIGNMENT_MIN_SAFE 80
#define PRESSURE_MIN_SAFE 20
#define PRESSURE_MAX_SAFE 80

void setup() {
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);

  while (!Serial && millis() < 3000) {
    // wait for Serial Monitor
  }

  Serial.println("BOOT,TEENSY_SENSOR_LED_TEST");
  Serial.println("INFO,Testing foil alignment sensor, pressure sensor, and LEDs");

  // LED self test
  testLEDs();

  Serial.println("=== Foil sensor calibration started ===");

  Serial.println("Step 1: Do NOT touch the foil...");
  delay(2000);
  calibrateSensorMin();

  Serial.println("Step 2: Touch and hold the foil...");
  delay(2000);
  calibrateSensorMax();

  if (sensorMax <= sensorMin) {
    Serial.println("WARN,Foil calibration range invalid. Using fallback range.");
    sensorMin = 0;
    sensorMax = 1000;
  }

  Serial.println("=== Calibration complete ===");
  Serial.print("sensorMin=");
  Serial.println(sensorMin);
  Serial.print("sensorMax=");
  Serial.println(sensorMax);
  Serial.println("INFO,Starting telemetry output...");
}

void loop() {
  int alignment = readAlignment();
  int pressure = readPressure();

  updateLEDs(alignment, pressure);

  Serial.print("TLM,STATE=HARDWARE_TEST");
  Serial.print(",ALIGN=");
  Serial.print(alignment);
  Serial.print(",PRESSURE=");
  Serial.print(pressure);
  Serial.print(",FUEL=0");
  Serial.print(",DOCK=0");
  Serial.print(",GATE=CLOSED");

  if (alignment < ALIGNMENT_MIN_SAFE) {
    Serial.println(",FAULT=ALIGNMENT_LOST");
  } else if (pressure < PRESSURE_MIN_SAFE || pressure > PRESSURE_MAX_SAFE) {
    Serial.println(",FAULT=PRESSURE_OUT_OF_RANGE");
  } else {
    Serial.println(",FAULT=NONE");
  }

  delay(500);
}

void testLEDs() {
  Serial.println("INFO,LED self test started");

  digitalWrite(RED_LED, HIGH);
  delay(500);
  digitalWrite(RED_LED, LOW);

  digitalWrite(GREEN_LED, HIGH);
  delay(500);
  digitalWrite(GREEN_LED, LOW);

  digitalWrite(YELLOW_LED, HIGH);
  delay(500);
  digitalWrite(YELLOW_LED, LOW);

  Serial.println("INFO,LED self test complete");
}

void calibrateSensorMin() {
  long startTime = millis();

  while (millis() - startTime < 3000) {
    long value = capSensor.capacitiveSensor(50);

    if (value > 0 && value < sensorMin) {
      sensorMin = value;
    }

    delay(50);
  }

  Serial.print("sensorMin=");
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

  Serial.print("sensorMax=");
  Serial.println(sensorMax);
}

int readAlignment() {
  long raw = capSensor.capacitiveSensor(50);

  long limitedRaw = constrain(raw, sensorMin, sensorMax);

  // Untouched / far = lower alignment
  // Touched / close = higher alignment
  int alignment = map(limitedRaw, sensorMin, sensorMax, 30, 100);
  alignment = constrain(alignment, 0, 100);

  return alignment;
}

int readPressure() {
  float code = analogRead(TEMP_PIN);

  // Your original conversion formula
  float celsius = 25 + (code - 512) / 11.3;

  // In this prototype, temperature reading is used as simulated pressure.
  int pressure = (int)celsius;
  pressure = constrain(pressure, 0, 100);

  return pressure;
}

void updateLEDs(int alignment, int pressure) {
  bool alignmentLost = alignment < ALIGNMENT_MIN_SAFE;
  bool pressureBad = pressure < PRESSURE_MIN_SAFE || pressure > PRESSURE_MAX_SAFE;

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);

  if (alignmentLost || pressureBad) {
    // Dangerous condition
    digitalWrite(RED_LED, HIGH);
  } else if (alignment < 75 || pressure < 30 || pressure > 70) {
    // Warning condition
    digitalWrite(YELLOW_LED, HIGH);
  } else {
    // Normal condition
    digitalWrite(GREEN_LED, HIGH);
  }
}

//void updateLEDs(int alignment, int pressure) {
//  bool alignmentOk = alignment >= 95;
//  bool pressureOk = pressure >= PRESSURE_MIN_SAFE && pressure <= PRESSURE_MAX_SAFE;
//
//  digitalWrite(RED_LED, LOW);
//  digitalWrite(GREEN_LED, LOW);
//  digitalWrite(YELLOW_LED, LOW);
//
//  if (!alignmentOk || !pressureOk) {
//    // Dangerous condition: not fully aligned or pressure unsafe
//    digitalWrite(RED_LED, HIGH);
//  } else {
//    // Fully aligned and pressure safe
//    digitalWrite(GREEN_LED, HIGH);
//  }
//}
