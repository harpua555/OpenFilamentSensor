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
#define FIRMWARE_VERSION_RAW dev
#endif
#ifndef CHIP_FAMILY_RAW
#define CHIP_FAMILY_RAW Unknown
#endif

// Create a macro that checks if the stringified value is empty and uses fallback
#define GET_VERSION_STRING(x, fallback) (strlen(TOSTRING(x)) == 0 ? fallback : TOSTRING(x))

const char* firmwareVersion = GET_VERSION_STRING(FIRMWARE_VERSION_RAW, "dev");
const char* chipFamily      = GET_VERSION_STRING(CHIP_FAMILY_RAW, "Unknown");

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
}
