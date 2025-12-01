#pragma once

#include <Arduino.h>

class SystemServices
{
  public:
    void begin();
    void loop();

    bool wifiReady() const;
    bool runningInAPMode() const;
    bool hasAttemptedWifiSetup() const;
    bool shouldYieldForSetup() const;

    unsigned long currentEpoch() const;

  private:
    void failWifi();
    void startAPMode();
    void handleSuccessfulWifiConnection();
    bool connectToWifiStation(bool isReconnect);
    void cleanupWifiConnections();
    bool wifiSetup();
    bool reconnectWifiWithNewCredentials();
    void checkWifiConnection();
    void syncTimeWithNTP(unsigned long currentTime);
    void monitorHeap(unsigned long currentTime);
    void handleWifiReconnectRequest();

    bool          wifiSetupAttempted        = false;
    bool          wifiSetupAttemptedThisLoop = false;
    bool          stationConnected          = false;
    bool          isReconnecting            = false;
    bool          ntpConfigured             = false;
    unsigned long lastWifiCheck             = 0;
    unsigned long wifiReconnectStart        = 0;
    unsigned long lastNTPSyncAttempt        = 0;
    unsigned long lastHeapCheck             = 0;
};

extern SystemServices systemServices;

// Global helper for existing modules that depend on this symbol.
unsigned long getTime();
