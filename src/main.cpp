#include <Arduino.h>

SET_LOOP_TASK_STACK_SIZE(24 * 1024);

#include "app/App.h"

App app;

void setup() {
    app.begin();
}

void loop() {
    app.update();
}