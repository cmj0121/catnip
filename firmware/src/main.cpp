/*
 * main.cpp - Catnip firmware entry for the MeowKit (ESP32-S3).
 *
 * Issue #7 scope: bring the Lua runtime up on the device and prove it runs a
 * script, with output on the serial log. The cooperative scheduler that drives
 * scripts from loop() arrives in issue #9; for now loop() is idle.
 *
 * This file only compiles under the Arduino/ESP32 toolchain (PlatformIO). The
 * host test build (`make test`) compiles src/*.c and never this file.
 */
#include <Arduino.h>

#include "catnip_runtime.h"

static catnip_rt *g_rt;

static void serial_log(void *ud, const char *msg, size_t len)
{
    (void)ud;
    Serial.write(reinterpret_cast<const uint8_t *>(msg), len);
    Serial.write('\n');
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("[catnip] booting Lua runtime");

    g_rt = catnip_rt_new_tracked(); /* Lua heap lives in PSRAM (#8) */
    if (!g_rt) {
        Serial.println("[catnip] FATAL: runtime allocation failed");
        return;
    }
    catnip_rt_set_log(g_rt, serial_log, nullptr);
    catnip_rt_dostring(g_rt, "print('meow from MeowKit')", "=boot");

    size_t in_use = 0, peak = 0;
    if (catnip_rt_mem(g_rt, &in_use, &peak)) {
        Serial.printf("[catnip] Lua heap: %u B in use, %u B peak (PSRAM)\n",
                      (unsigned)in_use, (unsigned)peak);
    }
}

void loop()
{
    delay(1000);
}
