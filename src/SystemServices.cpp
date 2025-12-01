#include "SystemServices.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "Logger.h"
#include "SettingsManager.h"

namespace
{
constexpr unsigned long WIFI_CHECK_INTERVAL_MS    = 30000;
constexpr unsigned long WIFI_RECONNECT_TIMEOUT_MS = 10000;
constexpr unsigned long NTP_SYNC_INTERVAL_MS      = 3600000;
const char*             NTP_SERVER                = "pool.ntp.org";
}  // namespace

SystemServices systemServices;

void SystemServices::begin()
{
    wifiSetupAttempted         = false;
    wifiSetupAttemptedThisLoop = false;
    stationConnected           = false;
    isReconnecting             = false;
    ntpConfigured              = false;
    lastWifiCheck              = 0;
    wifiReconnectStart         = 0;
    lastNTPSyncAttempt         = 0;
    lastHeapCheck              = 0;
}

void SystemServices::loop()
{
    unsigned long currentTime = millis();
    wifiSetupAttemptedThisLoop = false;

    if (!wifiSetupAttempted)
    {
        wifiSetupAttempted         = true;
        wifiSetupAttemptedThisLoop = true;
        bool success = wifiSetup();

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

    handleWifiReconnectRequest();

    stationConnected = (!settingsManager.isAPMode() && WiFi.status() == WL_CONNECTED);

    if (stationConnected)
    {
        if (!ntpConfigured)
        {
            configTime(0, 0, NTP_SERVER);
            syncTimeWithNTP(currentTime);
            logger.log("NTP setup complete");
            ntpConfigured = true;
        }
        else if (currentTime - lastNTPSyncAttempt >= NTP_SYNC_INTERVAL_MS)
        {
            syncTimeWithNTP(currentTime);
        }
    }
    else if (!settingsManager.isAPMode() && currentTime - lastWifiCheck >= WIFI_CHECK_INTERVAL_MS)
    {
        lastWifiCheck = currentTime;
        checkWifiConnection();
    }

    monitorHeap(currentTime);
}

bool SystemServices::wifiReady() const
{
    return stationConnected;
}

bool SystemServices::runningInAPMode() const
{
    return settingsManager.isAPMode();
}

bool SystemServices::hasAttemptedWifiSetup() const
{
    return wifiSetupAttempted;
}

bool SystemServices::shouldYieldForSetup() const
{
    return wifiSetupAttemptedThisLoop;
}

unsigned long SystemServices::currentEpoch() const
{
    time_t now;
    time(&now);
    return now;
}

void SystemServices::failWifi()
{
    stationConnected = false;

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
    }
    else
    {
        logger.log("WiFi connection failed, retrying in 30 seconds");
    }
}

void SystemServices::startAPMode()
{
    stationConnected = false;
    logger.log("Starting AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("CentauriFilament.local");
    logger.logf("AP IP Address: %s", WiFi.softAPIP().toString().c_str());

    if (!MDNS.begin("centaurifilament"))
    {
        logger.log("Error setting up MDNS responder in AP mode!");
    }
}

void SystemServices::handleSuccessfulWifiConnection()
{
    stationConnected = true;
    logger.log("WiFi Connected");
    logger.logf("IP Address: %s", WiFi.localIP().toString().c_str());

    if (!settingsManager.getHasConnected())
    {
        settingsManager.setHasConnected(true);
        settingsManager.save();
        logger.log("First successful WiFi connection recorded");
    }

    isReconnecting = false;

    MDNS.end();
    if (!MDNS.begin("centaurifilament"))
    {
        logger.log("Error setting up MDNS responder!");
    }
}

bool SystemServices::connectToWifiStation(bool isReconnect)
{
    WiFi.mode(WIFI_STA);
    const char* action = isReconnect ? "Reconnecting to" : "Connecting to";
    logger.logf("%s WiFi: %s", action, settingsManager.getSSID().c_str());

    WiFi.begin(settingsManager.getSSID().c_str(), settingsManager.getPassword().c_str());

    const unsigned long CONNECT_TIMEOUT_MS = 15000;
    unsigned long       startTime          = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < CONNECT_TIMEOUT_MS)
    {
        Serial.print('.');
        vTaskDelay(pdMS_TO_TICKS(500));
        yield();
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        handleSuccessfulWifiConnection();
        return true;
    }

    if (isReconnect)
    {
        logger.log("Failed to connect with new WiFi credentials");
    }
    else
    {
        failWifi();
    }

    stationConnected = false;
    return false;
}

void SystemServices::cleanupWifiConnections()
{
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(1000);
}

bool SystemServices::wifiSetup()
{
    if (settingsManager.isAPMode())
    {
        startAPMode();
        return false;
    }

    return connectToWifiStation(false);
}

bool SystemServices::reconnectWifiWithNewCredentials()
{
    logger.log("Applying new WiFi credentials...");
    cleanupWifiConnections();

    if (settingsManager.isAPMode())
    {
        logger.log("Switching to AP mode");
        startAPMode();
        return false;
    }

    logger.log("Connecting to WiFi station mode with new credentials...");
    return connectToWifiStation(true);
}

void SystemServices::checkWifiConnection()
{
    if (settingsManager.isAPMode())
    {
        stationConnected = false;
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        stationConnected = false;

        if (!isReconnecting)
        {
            logger.log("WiFi disconnected, attempting to reconnect...");
            WiFi.begin(settingsManager.getSSID().c_str(), settingsManager.getPassword().c_str());
            wifiReconnectStart = millis();
            isReconnecting     = true;
        }
        else if (millis() - wifiReconnectStart >= WIFI_RECONNECT_TIMEOUT_MS)
        {
            failWifi();
        }
    }
    else if (isReconnecting)
    {
        logger.log("WiFi reconnected successfully");
        isReconnecting = false;

        if (!settingsManager.getHasConnected())
        {
            settingsManager.setHasConnected(true);
            settingsManager.save();
        }
    }
}

void SystemServices::syncTimeWithNTP(unsigned long currentTime)
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

void SystemServices::monitorHeap(unsigned long currentTime)
{
    if (currentTime - lastHeapCheck <= 60000)
    {
        return;
    }

    lastHeapCheck = currentTime;

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minHeap  = ESP.getMinFreeHeap();
    uint32_t maxAlloc = ESP.getMaxAllocHeap();

    float fragmentation = 100.0f * (1.0f - ((float)maxAlloc / (float)freeHeap));

    logger.logf(LOG_VERBOSE, "Heap: free=%lu min=%lu maxAlloc=%lu frag=%.1f%%",
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

void SystemServices::handleWifiReconnectRequest()
{
    if (!settingsManager.requestWifiReconnect)
    {
        return;
    }

    settingsManager.requestWifiReconnect = false;
    reconnectWifiWithNewCredentials();
}

unsigned long getTime()
{
    // Return epoch time for now - Logger will format it as local time
    return systemServices.currentEpoch();
}
