/**
 * StatusDisplay - Optional OLED display for visual status indication
 * 
 * Compile with -D ENABLE_OLED_DISPLAY=1 to enable.
 * 
 * ============================================================================
 * HARDWARE NOTES - ESP32-C3 SuperMini with Built-in OLED
 * ============================================================================
 * 
 * The ESP32-C3 SuperMini boards from Amazon typically have a 0.42" OLED with:
 *   - Visible display area: 72x40 pixels
 *   - Controller: SSD1306 with 128x64 (or 132x64) internal buffer
 *   - The visible 72x40 area is CENTERED in the buffer
 * 
 * Buffer Layout:
 *   +----------------------------------+ (0,0) buffer origin
 *   |          (28 pixels)             |
 *   |    +--------------------+        |
 *   | 24 |                    |        |
 *   | px |   VISIBLE AREA     | 40px   |
 *   |    |     72 x 40        |        |
 *   |    +--------------------+        |
 *   |                                  |
 *   +----------------------------------+ (127,63) buffer end
 * 
 * Therefore, to draw in the visible area, you must offset all coordinates:
 *   - X offset: 28 pixels (from left edge of buffer)
 *   - Y offset: 24 pixels (from top edge of buffer)
 * 
 * Default I2C pins for ESP32-C3 SuperMini OLED:
 *   - SDA: GPIO 5
 *   - SCL: GPIO 6
 * 
 * Override via build flags: -D OLED_SDA_PIN=X -D OLED_SCL_PIN=Y
 * ============================================================================
 */

#include "StatusDisplay.h"

#ifdef ENABLE_OLED_DISPLAY

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "ElegooCC.h"
#include "SettingsManager.h"

// ============================================================================
// Display Mode Configuration
// ============================================================================
// Set OLED_DISPLAY_MODE via build flags: -D OLED_DISPLAY_MODE=X
//   Mode 1: IP last octet only (default) - shows "IP: 104"
//   Mode 2: Full IP address - shows "192..104" (first..last octet)
//   Mode 3: Both IPs - shows ESP32 IP and Elegoo printer IP (abbreviated)
//   Mode 4: Both IPs + connection status (abbreviated)
//   Mode 5: Uptime display - shows time since boot in adaptive D:H:M:S format
#ifndef OLED_DISPLAY_MODE
#define OLED_DISPLAY_MODE 1
#endif

// ============================================================================
// Display Configuration
// ============================================================================

// SSD1306 buffer dimensions (what the controller thinks the display is)
#define BUFFER_WIDTH  128
#define BUFFER_HEIGHT 64

// Actual visible display dimensions (the physical OLED panel)
#define VISIBLE_WIDTH  72
#define VISIBLE_HEIGHT 40

// Offset from buffer origin to visible area origin
// These values center the 72x40 visible area in the 128x64 buffer
// Calculation: X_OFFSET = (128 - 72) / 2 = 28, but some displays use (132-72)/2 = 30
// Y_OFFSET = (64 - 40) / 2 = 12, but many boards report needing 24
// The values 28,24 work for most Amazon ESP32-C3 SuperMini boards per user reports
#define X_OFFSET 28
#define Y_OFFSET 24

// Convenience macros to convert visible coordinates to buffer coordinates
#define VIS_X(x) ((x) + X_OFFSET)
#define VIS_Y(y) ((y) + Y_OFFSET)

// I2C address (0x3C is most common for SSD1306)
#define OLED_I2C_ADDRESS 0x3C

// Default I2C pins for ESP32-C3 SuperMini with built-in OLED
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 5
#endif

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 6
#endif

// Update throttle (100ms = 10 FPS max)
static constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 100;

// Display instance (uses buffer dimensions, -1 = no reset pin)
static Adafruit_SSD1306 display(BUFFER_WIDTH, BUFFER_HEIGHT, &Wire, -1);

// State tracking
static DisplayStatus currentStatus = DisplayStatus::NORMAL;
static DisplayStatus lastDrawnStatus = DisplayStatus::NORMAL;
static unsigned long lastUpdateMs = 0;
static bool displayInitialized = false;
static uint8_t lastDisplayedIpOctet = 0;  // Track IP to redraw when WiFi connects
static bool lastConnectionStatus = false; // Track connection state for mode 4
static String lastPrinterIp = "";          // Track printer IP for modes 3/4
static unsigned long lastDisplayedUptime = 0;  // Track uptime for mode 5 refresh

// Forward declarations
static void drawStatus(DisplayStatus status);

void statusDisplayBegin()
{
    // Initialize I2C with custom pins
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    
    // Initialize display
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS))
    {
        displayInitialized = true;
        display.clearDisplay();
        display.display();
        
        // Draw initial state
        drawStatus(DisplayStatus::NORMAL);
        lastDrawnStatus = DisplayStatus::NORMAL;
    }
}

void statusDisplayUpdate(DisplayStatus status)
{
    currentStatus = status;
}

void statusDisplayLoop()
{
    if (!displayInitialized)
    {
        return;
    }
    
    unsigned long now = millis();
    if (now - lastUpdateMs < DISPLAY_UPDATE_INTERVAL_MS)
    {
        return;
    }
    lastUpdateMs = now;
    
    // Query current state from ElegooCC
    DisplayStatus newStatus = DisplayStatus::NORMAL;
    
    if (elegooCC.isFilamentRunout())
    {
        newStatus = DisplayStatus::RUNOUT;
    }
    else if (elegooCC.isJammed())
    {
        newStatus = DisplayStatus::JAM;
    }
    
    currentStatus = newStatus;
    
    // Check if IP changed (e.g., WiFi just connected)
    // This forces a redraw when the IP becomes available after boot
    uint8_t currentIpOctet = WiFi.localIP()[3];
    bool ipChanged = (currentIpOctet != lastDisplayedIpOctet);
    
    // Check if printer IP or connection status changed (for modes 3/4)
    String currentPrinterIp = settingsManager.getElegooIP();
    printer_info_t info = elegooCC.getCurrentInformation();
    bool printerIpChanged = (currentPrinterIp != lastPrinterIp);
    bool connectionChanged = (info.isWebsocketConnected != lastConnectionStatus);

    // Check if uptime changed (for mode 5 - refresh every second)
    unsigned long currentUptimeSec = millis() / 1000;
    bool uptimeChanged = (currentUptimeSec != lastDisplayedUptime);

    // Redraw if any relevant state changed
    bool needsRedraw = (currentStatus != lastDrawnStatus) ||
                       (ipChanged && currentStatus == DisplayStatus::NORMAL) ||
                       (printerIpChanged && currentStatus == DisplayStatus::NORMAL) ||
                       (connectionChanged && currentStatus == DisplayStatus::NORMAL) ||
                       (uptimeChanged && currentStatus == DisplayStatus::NORMAL);
    
    if (needsRedraw)
    {
        drawStatus(currentStatus);
        lastDrawnStatus = currentStatus;
        lastDisplayedIpOctet = currentIpOctet;
        lastPrinterIp = currentPrinterIp;
        lastConnectionStatus = info.isWebsocketConnected;
        lastDisplayedUptime = currentUptimeSec;
    }
}

/**
 * Draw status indicator on the OLED.
 * 
 * All coordinates use VIS_X() and VIS_Y() macros to offset into the
 * visible 72x40 area of the display buffer.
 * 
 * Display states:
 *   - NORMAL:  Shows "IP:" and the last octet of the device's IP address
 *   - JAM:     Inverted (white background) with "JAM" text
 *   - RUNOUT:  Striped pattern with "OUT" text
 */
static void drawStatus(DisplayStatus status)
{
    display.clearDisplay();
    
    switch (status)
    {
        case DisplayStatus::NORMAL:
        {
            display.setTextColor(SSD1306_WHITE);
            
#if OLED_DISPLAY_MODE == 1
            // Mode 1: Show IP last octet only (large, easy to read)
            IPAddress ip = WiFi.localIP();
            uint8_t lastOctet = ip[3];
            
            // "IP:" label - small text at top
            display.setTextSize(1);
            display.setCursor(VIS_X(24), VIS_Y(2));
            display.print("IP:");
            
            // Large last octet number - centered
            display.setTextSize(3);
            int numWidth = (lastOctet < 10) ? 18 : (lastOctet < 100) ? 36 : 54;
            int xPos = (VISIBLE_WIDTH - numWidth) / 2;
            display.setCursor(VIS_X(xPos), VIS_Y(14));
            display.print(lastOctet);
            
#elif OLED_DISPLAY_MODE == 2
            // Mode 2: First..Last octet format (fits 72px width)
            {
                IPAddress ip = WiFi.localIP();

                display.setTextSize(1);
                display.setCursor(VIS_X(18), VIS_Y(2));
                display.print("My IP");

                display.setTextSize(2);
                char buf[12];
                snprintf(buf, sizeof(buf), "%d..%d", ip[0], ip[3]);
                int len = strlen(buf);
                int xPos = (VISIBLE_WIDTH - len * 12) / 2;
                display.setCursor(VIS_X(xPos), VIS_Y(14));
                display.print(buf);
            }
            
#elif OLED_DISPLAY_MODE == 3
            // Mode 3: Both IPs - first..last format (fits 72px width)
            {
                IPAddress myIp = WiFi.localIP();
                String printerIp = settingsManager.getElegooIP();

                display.setTextSize(1);

                // ME:NNN..NNN (12 chars max)
                char buf[14];
                snprintf(buf, sizeof(buf), "ME:%d..%d", myIp[0], myIp[3]);
                display.setCursor(VIS_X(0), VIS_Y(8));
                display.print(buf);

                // PR:NNN..NNN or PR:--
                display.setCursor(VIS_X(0), VIS_Y(22));
                if (printerIp.length() > 0) {
                    int firstDot = printerIp.indexOf('.');
                    int lastDot = printerIp.lastIndexOf('.');
                    String first = printerIp.substring(0, firstDot);
                    String last = printerIp.substring(lastDot + 1);
                    snprintf(buf, sizeof(buf), "PR:%s..%s", first.c_str(), last.c_str());
                    display.print(buf);
                } else {
                    display.print("PR:--");
                }
            }
            
#elif OLED_DISPLAY_MODE == 4
            // Mode 4: Both IPs + connection status (abbreviated to fit)
            {
                IPAddress myIp = WiFi.localIP();
                String printerIp = settingsManager.getElegooIP();
                printer_info_t info = elegooCC.getCurrentInformation();

                display.setTextSize(1);

                // Line 1: ME:NNN..NNN
                char buf[14];
                snprintf(buf, sizeof(buf), "ME:%d..%d", myIp[0], myIp[3]);
                display.setCursor(VIS_X(0), VIS_Y(0));
                display.print(buf);

                // Line 2: PR:NNN..NNN or PR:--
                display.setCursor(VIS_X(0), VIS_Y(10));
                if (printerIp.length() > 0) {
                    int firstDot = printerIp.indexOf('.');
                    int lastDot = printerIp.lastIndexOf('.');
                    String first = printerIp.substring(0, firstDot);
                    String last = printerIp.substring(lastDot + 1);
                    snprintf(buf, sizeof(buf), "PR:%s..%s", first.c_str(), last.c_str());
                    display.print(buf);
                } else {
                    display.print("PR:--");
                }

                // Line 3: Connection status
                display.setCursor(VIS_X(0), VIS_Y(22));
                if (info.isWebsocketConnected) {
                    display.print("*CONNECTED*");
                } else {
                    display.print("DISCONNECTED");
                }

                // Line 4: Print status if connected
                display.setCursor(VIS_X(0), VIS_Y(32));
                if (info.isWebsocketConnected && info.isPrinting) {
                    display.print("PRINTING");
                }
            }

#elif OLED_DISPLAY_MODE == 5
            // Mode 5: Uptime display - adaptive format
            {
                unsigned long uptimeSec = millis() / 1000;
                unsigned long secs = uptimeSec % 60;
                unsigned long mins = (uptimeSec / 60) % 60;
                unsigned long hours = (uptimeSec / 3600) % 24;
                unsigned long days = uptimeSec / 86400;

                display.setTextSize(1);
                display.setCursor(VIS_X(12), VIS_Y(2));
                display.print("UPTIME");

                display.setTextSize(2);
                char buf[12];

                if (days > 0) {
                    // D:HH:MMd (drop seconds)
                    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lud", days, hours, mins);
                } else if (hours > 0) {
                    // H:MM:SSh
                    snprintf(buf, sizeof(buf), "%lu:%02lu:%02luh", hours, mins, secs);
                } else if (mins > 0) {
                    // M:SSm
                    snprintf(buf, sizeof(buf), "%lu:%02lum", mins, secs);
                } else {
                    // SSs
                    snprintf(buf, sizeof(buf), "%lus", secs);
                }

                // Center the text
                int len = strlen(buf);
                int width = len * 12;  // TextSize 2 = 12px per char
                int xPos = (VISIBLE_WIDTH - width) / 2;
                display.setCursor(VIS_X(xPos), VIS_Y(14));
                display.print(buf);
            }
#endif
            break;
        }
            
        case DisplayStatus::JAM:
        {
            // Filled background (inverted - represents danger/red)
            display.fillRect(VIS_X(0), VIS_Y(0), VISIBLE_WIDTH, VISIBLE_HEIGHT, SSD1306_WHITE);
            
            // "JAM" text - centered, black on white
            // TextSize 2 = 12x16 pixels per character, "JAM" = 36px wide
            display.setTextSize(2);
            display.setTextColor(SSD1306_BLACK);
            display.setCursor(VIS_X(18), VIS_Y(12));  // (72-36)/2 = 18
            display.print("JAM");
            break;
        }
            
        case DisplayStatus::RUNOUT:
        {
            // Striped pattern (represents warning/purple)
            for (int y = 0; y < VISIBLE_HEIGHT; y += 4)
            {
                display.fillRect(VIS_X(0), VIS_Y(y), VISIBLE_WIDTH, 2, SSD1306_WHITE);
            }
            
            // "OUT" text - centered
            // TextSize 2 = 12x16 pixels per character, "OUT" = 36px wide
            display.setTextSize(2);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(VIS_X(18), VIS_Y(12));  // (72-36)/2 = 18
            display.print("OUT");
            break;
        }
    }
    
    display.display();
}

#else // ENABLE_OLED_DISPLAY not defined

// No-op stubs when OLED is disabled
void statusDisplayBegin() {}
void statusDisplayUpdate(DisplayStatus) {}
void statusDisplayLoop() {}

#endif // ENABLE_OLED_DISPLAY

