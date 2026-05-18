#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define MAX_LOGS 12
#define MAX_LINE 128

#define ALIGNMENT_MIN_SAFE 60
#define PRESSURE_MIN_SAFE 20
#define PRESSURE_MAX_SAFE 80

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

    clock_t lastFuelUpdate;
} RefuelingController;

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

void resetToSafe(RefuelingController* controller) {
    controller->state = STATE_SAFE;
    controller->alignment = 85;
    controller->pressure = 40;
    controller->fuel = 0;
    controller->dockLocked = 0;
    controller->gateOpen = 0;
    snprintf(controller->faultCause, sizeof(controller->faultCause), "NONE");
    controller->lastFuelUpdate = clock();

    addEvent(controller, "RESET_TO_SAFE");
}

void initializeController(RefuelingController* controller) {
    controller->state = STATE_IDLE;
    controller->alignment = 85;
    controller->pressure = 40;
    controller->fuel = 0;
    controller->dockLocked = 0;
    controller->gateOpen = 0;
    controller->logCount = 0;
    snprintf(controller->faultCause, sizeof(controller->faultCause), "NONE");
    controller->lastFuelUpdate = clock();

    addEvent(controller, "SYSTEM_BOOT");
}

void sendTelemetry(const RefuelingController* controller) {
    printf(
        "TLM,STATE=%s,ALIGN=%d,PRESSURE=%d,FUEL=%d,DOCK=%d,GATE=%s,FAULT=%s\n",
        stateToString(controller->state),
        controller->alignment,
        controller->pressure,
        controller->fuel,
        controller->dockLocked,
        controller->gateOpen ? "OPEN" : "CLOSED",
        controller->faultCause
    );

    fflush(stdout);
}

void printLog(const RefuelingController* controller) {
    printf("LOG,BEGIN\n");

    for (int i = 0; i < controller->logCount; i++) {
        printf("LOG,%d,%s\n", i + 1, controller->eventLog[i]);
    }

    printf("LOG,END\n");
    fflush(stdout);
}

void enterAbort(RefuelingController* controller, const char* cause) {
    controller->state = STATE_ABORT;
    controller->gateOpen = 0;
    snprintf(controller->faultCause, sizeof(controller->faultCause), "%s", cause);

    char logMessage[96];
    snprintf(logMessage, sizeof(logMessage), "ABORT_%s", cause);
    addEvent(controller, logMessage);

    printf("ACK,ABORT_ENTERING_SAFE_MODE,CAUSE=%s\n", cause);
    fflush(stdout);
}

void enterFault(RefuelingController* controller, const char* cause) {
    controller->state = STATE_FAULT;
    controller->gateOpen = 0;
    snprintf(controller->faultCause, sizeof(controller->faultCause), "%s", cause);

    char logMessage[96];
    snprintf(logMessage, sizeof(logMessage), "FAULT_%s", cause);
    addEvent(controller, logMessage);

    printf("FAULT,%s\n", cause);
    fflush(stdout);
}

void updateRefueling(RefuelingController* controller) {
    if (controller->state != STATE_REFUELING) {
        return;
    }

    clock_t now = clock();
    double elapsedSeconds = (double)(now - controller->lastFuelUpdate) / CLOCKS_PER_SEC;

    if (elapsedSeconds >= 0.7) {
        controller->lastFuelUpdate = now;

        if (controller->fuel < 100) {
            controller->fuel += 5;
        }

        if (controller->fuel >= 100) {
            controller->fuel = 100;
            controller->gateOpen = 0;
            controller->state = STATE_COMPLETE;
            addEvent(controller, "REFUELING_COMPLETE");
            printf("ACK,REFUELING_COMPLETE\n");
            fflush(stdout);
        }
    }
}

void automaticSafetyCheck(RefuelingController* controller) {
    if (controller->state != STATE_REFUELING) {
        return;
    }

    if (controller->alignment < ALIGNMENT_MIN_SAFE) {
        enterAbort(controller, "ALIGNMENT_LOST");
        return;
    }

    if (controller->pressure < PRESSURE_MIN_SAFE || controller->pressure > PRESSURE_MAX_SAFE) {
        enterAbort(controller, "PRESSURE_OUT_OF_RANGE");
        return;
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

void handleCommand(RefuelingController* controller, char* input) {
    trimNewline(input);
    toUpperCase(input);

    char command[64];
    char argument[64];

    splitCommand(input, command, argument);

    if (strlen(command) == 0) {
        printf("ERR,EMPTY_COMMAND\n");
        return;
    }

    if (strcmp(command, "PING") == 0) {
        printf("ACK,PING\n");
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
        printf("ACK,RESET_TO_SAFE\n");
        return;
    }

    if (strcmp(command, "SIM_ALIGN") == 0) {
        if (strlen(argument) == 0) {
            printf("ERR,SIM_ALIGN_REQUIRES_VALUE\n");
            return;
        }

        int value = atoi(argument);

        if (value < 0 || value > 100) {
            printf("ERR,ALIGN_OUT_OF_RANGE\n");
            return;
        }

        controller->alignment = value;
        printf("ACK,SIM_ALIGN=%d\n", controller->alignment);
        return;
    }

    if (strcmp(command, "SIM_PRESSURE") == 0) {
        if (strlen(argument) == 0) {
            printf("ERR,SIM_PRESSURE_REQUIRES_VALUE\n");
            return;
        }

        int value = atoi(argument);

        if (value < 0 || value > 100) {
            printf("ERR,PRESSURE_OUT_OF_RANGE\n");
            return;
        }

        controller->pressure = value;
        printf("ACK,SIM_PRESSURE=%d\n", controller->pressure);
        return;
    }

    // if (strcmp(command, "ABORT") == 0) {
    //     enterAbort(controller, "SUPERVISOR_COMMAND");
    //     return;
    // }
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
        printf("ERR,SYSTEM_IN_FAULT_OR_ABORT_STATE\n");
        return;
    }

    if (strcmp(command, "START_APPROACH") == 0) {
        if (controller->state != STATE_IDLE && controller->state != STATE_SAFE) {
            printf("ERR,INVALID_STATE_TRANSITION\n");
            return;
        }

        controller->state = STATE_APPROACH;
        addEvent(controller, "START_APPROACH");
        printf("ACK,APPROACH_STARTED\n");
        return;
    }

    if (strcmp(command, "CHECK_ALIGNMENT") == 0) {
        if (controller->state != STATE_APPROACH) {
            printf("ERR,ALIGNMENT_CHECK_REQUIRES_APPROACH\n");
            return;
        }

        controller->state = STATE_ALIGNMENT_CHECK;

        if (controller->alignment < ALIGNMENT_MIN_SAFE) {
            enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
            return;
        }

        addEvent(controller, "ALIGNMENT_OK");
        printf("ACK,ALIGNMENT_OK\n");
        return;
    }

    if (strcmp(command, "LOCK_DOCK") == 0) {
        if (controller->state != STATE_ALIGNMENT_CHECK) {
            printf("ERR,DOCK_LOCK_REQUIRES_ALIGNMENT_CHECK\n");
            return;
        }

        if (controller->alignment < ALIGNMENT_MIN_SAFE) {
            enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
            return;
        }

        controller->dockLocked = 1;
        controller->state = STATE_DOCK_LOCKED;
        addEvent(controller, "DOCK_LOCKED");
        printf("ACK,DOCK_LOCKED\n");
        return;
    }

    if (strcmp(command, "OPEN_GATE") == 0) {
        if (controller->state != STATE_DOCK_LOCKED) {
            printf("ERR,GATE_BLOCKED_DOCK_NOT_LOCKED\n");
            return;
        }

        controller->gateOpen = 1;
        controller->state = STATE_GATE_OPEN;
        addEvent(controller, "GATE_OPEN");
        printf("ACK,GATE_OPEN\n");
        return;
    }

    if (strcmp(command, "CHECK_PRESSURE") == 0) {
        if (controller->state != STATE_GATE_OPEN) {
            printf("ERR,PRESSURE_CHECK_REQUIRES_GATE_OPEN\n");
            return;
        }

        if (controller->pressure < PRESSURE_MIN_SAFE || controller->pressure > PRESSURE_MAX_SAFE) {
            enterFault(controller, "PRESSURE_OUT_OF_RANGE");
            return;
        }

        controller->state = STATE_PRESSURE_CHECK;
        addEvent(controller, "PRESSURE_OK");
        printf("ACK,PRESSURE_OK\n");
        return;
    }

    if (strcmp(command, "START_REFUEL") == 0) {
        if (controller->state != STATE_PRESSURE_CHECK) {
            printf("ERR,REFUEL_BLOCKED_PRESSURE_CHECK_REQUIRED\n");
            return;
        }

        if (!controller->dockLocked) {
            printf("ERR,REFUEL_BLOCKED_DOCK_NOT_LOCKED\n");
            return;
        }

        if (!controller->gateOpen) {
            printf("ERR,REFUEL_BLOCKED_GATE_NOT_OPEN\n");
            return;
        }

        if (controller->alignment < ALIGNMENT_MIN_SAFE) {
            enterFault(controller, "ALIGNMENT_OUT_OF_RANGE");
            return;
        }

        controller->state = STATE_REFUELING;
        controller->fuel = 0;
        controller->lastFuelUpdate = clock();
        addEvent(controller, "REFUELING_STARTED");
        printf("ACK,REFUELING_STARTED\n");
        return;
    }

    if (strcmp(command, "STOP_REFUEL") == 0) {
        if (controller->state != STATE_REFUELING) {
            printf("ERR,NOT_REFUELING\n");
            return;
        }

        controller->state = STATE_SAFE;
        controller->gateOpen = 0;
        addEvent(controller, "REFUELING_STOPPED_BY_COMMAND");
        printf("ACK,REFUELING_STOPPED_ENTERING_SAFE\n");
        return;
    }

    printf("ERR,INVALID_COMMAND\n");
}

int main(void) {
    RefuelingController controller;
    char input[MAX_LINE];

    initializeController(&controller);

    printf("BOOT,SOFTWARE_IN_THE_LOOP_REFUELING_CONTROLLER\n");
    printf("INFO,READY_FOR_COMMANDS\n");
    fflush(stdout);

    // while (fgets(input, sizeof(input), stdin) != NULL) {
    //     updateRefueling(&controller);
    //     automaticSafetyCheck(&controller);

    //     handleCommand(&controller, input);

    //     updateRefueling(&controller);
    //     automaticSafetyCheck(&controller);

    //     sendTelemetry(&controller);
    // }

    while (fgets(input, sizeof(input), stdin) != NULL) {
    updateRefueling(&controller);

    handleCommand(&controller, input);

    updateRefueling(&controller);

    sendTelemetry(&controller);
    }

    return 0;
}