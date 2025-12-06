// Mock Arduino.h for unit testing
#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <string>
#include <cstring>

// Arduino timing
extern unsigned long millis();

/**
     * Minimal mock of Arduino's `String` that wraps a `std::string` for unit tests.
     *
     * Provides constructors to create a String from a C string, `std::string`, `int`,
     * `unsigned long`, or `float` with a configurable number of decimal places.
     *
     * @param decimals Number of fractional digits when constructing from a `float`.
     */
class String {
public:
    std::string _str;
    String() : _str() {}
    /**
     * Construct a String from a C-style string.
     * @param s C-string to initialize from; if `s` is `nullptr`, the String is initialized as empty.
     */
    
    /**
     * Construct a String from an std::string.
     * @param s std::string whose contents are copied into the new String.
     */
    String(const char* s) : _str(s ? s : "") {}
    String(const std::string& s) : _str(s) {}
    /**
 * Construct a String from an integer.
 * @param n Integer value to convert to its decimal string representation.
 */
String(int n) : _str(std::to_string(n)) {}
    /**
 * Construct a String from an unsigned long integer.
 * @param n Unsigned long value to convert into the String's contents.
 */
String(unsigned long n) : _str(std::to_string(n)) {}
    String(float n, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, n);
        _str = buf;
    }

    /**
 * Return the stored string as a null-terminated C string.
 * @returns Pointer to the internal null-terminated character array representing the String.
 *          The pointer remains valid until the String is modified or destroyed.
 */
const char* c_str() const { return _str.c_str(); }
    size_t length() const { return _str.length(); }
    bool isEmpty() const { return _str.empty(); }

    String& operator+=(const String& s) { _str += s._str; return *this; }
    String& operator+=(const char* s) { if(s) _str += s; return *this; }
    String operator+(const String& s) const { return String(_str + s._str); }
    String operator+(const char* s) const { return String(_str + (s ? s : "")); }

    /**
 * Compare this String with another for character-wise equality.
 *
 * @param s The other String to compare.
 * @returns `true` if both Strings contain identical characters, `false` otherwise.
 */
bool operator==(const String& s) const { return _str == s._str; }
    bool operator==(const char* s) const { return _str == (s ? s : ""); }
};

// Mock __FlashStringHelper (used for PROGMEM strings on AVR)
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))

// Common Arduino macros
#define PROGMEM

#endif // ARDUINO_H