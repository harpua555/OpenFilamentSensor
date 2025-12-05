#include <Arduino.h>

#include "ElegooCC.h"
#include "LittleFS.h"
#include "Logger.h"
#include "SettingsManager.h"
#include "SystemServices.h"
#include "WebServer.h"

#define SPIFFS LittleFS

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Handle the case where environment variables are empty strings
#ifndef FIRMWARE_VERSION_RAW
#define FIRMWARE_VERSION_RAW "alpha"
#endif

// Build scripts now guarantee that CHIP_FAMILY_RAW is always set with valid values
#ifndef CHIP_FAMILY_RAW
#define CHIP_FAMILY_RAW "ESP32"
#endif

static const char* firmwareVersionRaw = FIRMWARE_VERSION_RAW;
const char* firmwareVersion = (firmwareVersionRaw[0] != '\0') ? firmwareVersionRaw : "alpha";

// Use the CHIP_FAMILY_RAW if it expands to a non-empty string literal, otherwise fall back to the default.
static const char* chipFamilyRaw = CHIP_FAMILY_RAW;
const char* chipFamily = (chipFamilyRaw[0] != '\0') ? chipFamilyRaw : "ESP32";


// Use BUILD_DATE and BUILD_TIME if available (set by build script), otherwise fall back to __DATE__ and __TIME__
#ifdef BUILD_DATE
const char* buildTimestamp  = BUILD_DATE " " BUILD_TIME;
#else
const char* buildTimestamp  = __DATE__ " " __TIME__;
#endif

WebServer webServer(80);

// These things get setup in the loop, not setup, so we need to track if they've happened
bool isElegooSetup    = false;
bool isWebServerSetup = false;

void setup()
{
    // put your setup code here, to run once:
    pinMode(FILAMENT_RUNOUT_PIN, INPUT_PULLUP);
    pinMode(MOVEMENT_SENSOR_PIN, INPUT_PULLUP);
    Serial.begin(115200);

    // Initialize logging system
    logger.log("ESP SFS System starting up...");
    logger.logf("Firmware version: %s", firmwareVersion);
    logger.logf("Chip family: %s", chipFamily);
    logger.logf("Build timestamp (UTC compile time): %s", buildTimestamp);

    SPIFFS.begin();  // note: this must be done before wifi/server setup
    logger.log("Filesystem initialized");
    logger.logf("Filesystem usage: total=%u bytes, used=%u bytes",
                SPIFFS.totalBytes(), SPIFFS.usedBytes());

    // Load settings early
    settingsManager.load();
    logger.log("Settings Manager Loaded");
    String settingsJson = settingsManager.toJson(false);
    logger.logf("Settings snapshot: %s", settingsJson.c_str());

    systemServices.begin();
}

/**
 * @brief Main program loop that drives periodic system tasks and conditional subsystem startup.
 *
 * Runs recurring service processing, defers further work while setup is required, starts the web
 * server once a Wi‑Fi setup attempt has occurred, initializes and processes the Elegoo subsystem
 * when Wi‑Fi is ready and an Elegoo IP is configured, and services the web server if started.
 *
 * @note The loop yields to the FreeRTOS scheduler with a 1 ms delay to reduce CPU usage while
 *       preserving sensor and polling timing requirements.
 */
void loop()
{
    systemServices.loop();

    if (systemServices.shouldYieldForSetup())
    {
        return;
    }

    if (!isWebServerSetup && systemServices.hasAttemptedWifiSetup())
    {
        webServer.begin();
        isWebServerSetup = true;
        logger.log("Webserver setup complete");
        return;
    }

    if (systemServices.wifiReady())
    {
        if (!isElegooSetup && settingsManager.getElegooIP().length() > 0)
        {
            elegooCC.setup();
            logger.log("Elegoo setup complete");
            isElegooSetup = true;
        }

        if (isElegooSetup)
        {
            elegooCC.loop();
        }
    }

    if (isWebServerSetup)
    {
        webServer.loop();
    }

    // Strategic 1ms delay to reduce CPU usage while maintaining detection accuracy.
    // This yields to the FreeRTOS scheduler, reducing CPU from 100% spin to ~10-20%.
    // 1ms is well below all critical timing thresholds:
    // - Motion sensor: ~60ms between pulses at typical speeds
    // - Jam detector: 250ms update interval
    // - Printer polling: 250ms status interval
    vTaskDelay(pdMS_TO_TICKS(1));
}