#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H

#include <Arduino.h>

/**
 * StatusDisplay - Optional OLED display for visual status indication
 * 
 * Enabled via -D ENABLE_OLED_DISPLAY=1 build flag.
 * When disabled, all functions are no-ops for zero overhead.
 * 
 * Display colors:
 *   - Green:  Normal operation (no problems)
 *   - Red:    Jam detected
 *   - Purple: Filament runout
 */

enum class DisplayStatus : uint8_t
{
    NORMAL = 0,  // Green - all good
    JAM    = 1,  // Red - jam detected
    RUNOUT = 2   // Purple - filament runout
};

/**
 * Initialize the OLED display.
 * Call once in setup().
 */
void statusDisplayBegin();

/**
 * Update the display to show a specific status.
 * Only redraws if status has changed.
 */
void statusDisplayUpdate(DisplayStatus status);

/**
 * Process display updates (call in main loop).
 * Automatically queries ElegooCC state and updates display.
 * Throttled internally to avoid excessive redraws.
 */
void statusDisplayLoop();

#endif // STATUS_DISPLAY_H
