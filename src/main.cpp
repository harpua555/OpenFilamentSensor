#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "ElegooCC.h"
#include "LittleFS.h"
#include "Logger.h"
#include "SettingsManager.h"
#include "WebServer.h"
#include "time.h"

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

#define WIFI_CHECK_INTERVAL 30000     // Check WiFi every 30 seconds
#define WIFI_RECONNECT_TIMEOUT 10000  // Wait 10 seconds for reconnection
#define NTP_SYNC_INTERVAL 3600000     // Re-sync with NTP every hour (3600000 ms)

// NTP server to request epoch time
const char* ntpServer = "pool.ntp.org";

WebServer webServer(80);

// Variables to track WiFi connection monitoring
unsigned long lastWifiCheck      = 0;
unsigned long wifiReconnectStart = 0;
bool          isReconnecting     = false;

// These things get setup in the loop, not setup, so we need to track if they've happened
bool isWifiSetup      = false;
bool isElegooSetup    = false;
bool isWebServerSetup = false;
bool isNtpSetup       = false;

// Variables to track NTP synchronization
unsigned long lastNTPSyncAttempt = 0;

// If wifi fails, revert to AP mode (only if never connected before)
void failWifi()
{
    // Only revert to AP mode if WiFi has never successfully connected
    if (!settingsManager.getHasConnected())
    {
        settingsManager.setAPMode(true);
        if (settingsManager.save())
        {
            logger.log("Failed to connect to wifi, reverted to AP mode (first connection attempt)");
            logger.log("Please manually restart device to enter AP mode.");
        }
        else
        {
            logger.log("Failed to update settings");
        }

        // Don't restart - let system continue in current state
        // User can manually restart to configure WiFi
    }
    else
    {
        logger.log("WiFi connection failed, retrying in 30 seconds");
        // Don't restart, just continue trying to reconnect in checkWifiConnection()
    }
}

void startAPMode()
{
    logger.log("Starting AP mode");
    WiFi.softAP("CentauriFilament.local");
    logger.logf("AP IP Address: %s", WiFi.softAPIP().toString().c_str());
    // Start mDNS for AP mode
    if (!MDNS.begin("centaurifilament"))
    {
        logger.log("Error setting up MDNS responder in AP mode!");
    }
}

void handleSuccessfulWifiConnection()
{
    logger.log("WiFi Connected");
    logger.logf("IP Address: %s", WiFi.localIP().toString().c_str());

    // Mark that WiFi has successfully connected at least once
    if (!settingsManager.getHasConnected())
    {
        settingsManager.setHasConnected(true);
        settingsManager.save();
        logger.log("First successful WiFi connection recorded");
    }

    // Reset any reconnection state
    isReconnecting = false;

    // Start/restart mDNS for station mode
    MDNS.end();
    if (!MDNS.begin("centaurifilament"))
    {
        logger.log("Error setting up MDNS responder!");
    }
}

bool connectToWifiStation(bool isReconnect = false)
{
    const char* action = isReconnect ? "Reconnecting to" : "Connecting to";
    logger.logf("%s WiFi: %s", action, settingsManager.getSSID().c_str());

    WiFi.begin(settingsManager.getSSID().c_str(), settingsManager.getPassword().c_str());

    // Use time-based timeout instead of attempt count (reduced from 30s to 15s)
    unsigned long startTime = millis();
    const unsigned long CONNECT_TIMEOUT_MS = 15000;  // 15 seconds

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < CONNECT_TIMEOUT_MS)
    {
        Serial.print('.');

        // Use vTaskDelay instead of delay for better multi-tasking
        vTaskDelay(pdMS_TO_TICKS(500));  // 500ms, allows other tasks to run

        // Allow WiFi stack to process
        yield();
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        handleSuccessfulWifiConnection();
        return true;
    }
    else
    {
        if (isReconnect)
        {
            logger.log("Failed to connect with new WiFi credentials");
        }
        else
        {
            failWifi();
        }
        return false;
    }
}

void cleanupWifiConnections()
{
    // Stop AP mode if it was running
    WiFi.softAPdisconnect(true);
    // Disconnect from any existing station connection
    WiFi.disconnect(true);
    delay(1000);
}

bool wifiSetup()
{
    if (settingsManager.isAPMode())
    {
        startAPMode();
        logger.log("Wifi setup in AP mode");
        return false;
    }
    else
    {
        return connectToWifiStation(false);
    }
}

bool reconnectWifiWithNewCredentials()
{
    logger.log("Applying new WiFi credentials...");

    // Clean up any existing connections first
    cleanupWifiConnections();

    // Check if we're switching to AP mode
    if (settingsManager.isAPMode())
    {
        logger.log("Switching to AP mode");
        startAPMode();
        return false;
    }

    // We're switching to or staying in station mode
    logger.log("Connecting to WiFi station mode with new credentials...");
    return connectToWifiStation(true);
}

void checkWifiConnection()
{
    // Skip check if already in AP mode
    if (settingsManager.isAPMode())
    {
        return;
    }

    // Check if WiFi is connected
    if (WiFi.status() != WL_CONNECTED)
    {
        if (!isReconnecting)
        {
            logger.log("WiFi disconnected, attempting to reconnect...");
            WiFi.begin(settingsManager.getSSID().c_str(), settingsManager.getPassword().c_str());
            wifiReconnectStart = millis();
            isReconnecting     = true;
        }
        else
        {
            // Check if reconnection timeout has elapsed
            if (millis() - wifiReconnectStart >= WIFI_RECONNECT_TIMEOUT)
            {
                failWifi();
            }
        }
    }
    else
    {
        // WiFi is connected, reset reconnection state
        if (isReconnecting)
        {
            logger.log("WiFi reconnected successfully");
            isReconnecting = false;

            // Mark that WiFi has successfully connected at least once
            if (!settingsManager.getHasConnected())
            {
                settingsManager.setHasConnected(true);
                settingsManager.save();
            }
        }
    }
}

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
}

void syncTimeWithNTP(unsigned long currentTime)
{
    struct tm timeinfo;
    lastNTPSyncAttempt = currentTime;
    if (getLocalTime(&timeinfo))
    {
        logger.log("NTP time synchronization successful");
    }
    else
    {
        logger.log("NTP time synchronization failed");
    }
}

unsigned long getTime()
{
    time_t now;
    time(&now);
    return now;
}

void loop()
{
    unsigned long currentTime = millis();
    bool          isWifiConnected = !settingsManager.isAPMode() && WiFi.status() == WL_CONNECTED;

    if (!isWifiSetup)
    {
        bool success = wifiSetup();
        isWifiSetup = true;  // Mark as attempted (don't retry setup every loop)

        if (success)
        {
            logger.log(F("WiFi connected successfully"));
            logger.logf("IP Address: %s", WiFi.localIP().toString().c_str());
        }
        else
        {
            if (settingsManager.isAPMode())
            {
                logger.log(F("WiFi setup complete - running in AP mode"));
                logger.logf("AP IP Address: %s", WiFi.softAPIP().toString().c_str());
            }
            else
            {
                logger.log(F("WiFi setup attempted - will retry connection in background"));
            }
        }
        return;
    }
    if (!isWebServerSetup)
    {
        webServer.begin();
        isWebServerSetup = true;
        logger.log("Webserver setup complete");
        return;  //
    }

    // Check if WiFi reconnection is requested

    if (settingsManager.requestWifiReconnect)
    {
        settingsManager.requestWifiReconnect = false;
        reconnectWifiWithNewCredentials();
    }

    if (isWifiConnected)
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

        if (!isNtpSetup)
        {
            configTime(0, 0, ntpServer);
            syncTimeWithNTP(currentTime);
            logger.log("NTP setup complete");
            isNtpSetup = true;
        }
        else if (currentTime - lastNTPSyncAttempt >= NTP_SYNC_INTERVAL)
        {
            syncTimeWithNTP(currentTime);
        }
    }
    else if (currentTime - lastWifiCheck >= WIFI_CHECK_INTERVAL)
    {
        lastWifiCheck = currentTime;
        checkWifiConnection();
    }

    // Heap fragmentation monitoring (every 60 seconds)
    static unsigned long lastHeapCheck = 0;
    if (currentTime - lastHeapCheck > 60000)
    {
        lastHeapCheck = currentTime;

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();

        // Calculate fragmentation: if maxAlloc << freeHeap, heap is fragmented
        float fragmentation = 100.0f * (1.0f - ((float)maxAlloc / (float)freeHeap));

        logger.logf(LOG_DEBUG, "Heap: free=%lu min=%lu maxAlloc=%lu frag=%.1f%%",
                   freeHeap, minHeap, maxAlloc, fragmentation);

        if (fragmentation > 30.0f)
        {
            logger.log(F("WARNING: Heap fragmentation high!"), LOG_NORMAL);
        }

        if (minHeap < 20000)
        {
            logger.logf(LOG_NORMAL, "CRITICAL: Low memory! Min heap: %lu", minHeap);
        }
    }

    webServer.loop();
}
