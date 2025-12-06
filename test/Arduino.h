// Mock Arduino.h for unit testing
#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <string>
#include <cstring>

// Arduino timing
extern unsigned long millis();

/**
     * Lightweight mock of Arduino's String class backed by std::string.
     *
     * Provides constructors to create String instances from C-strings, std::string,
     * integers, unsigned long, and floats with configurable decimal places. Also
     * exposes the underlying std::string as the public member `_str`.
     *
     * @note The float constructor formats the value using snprintf with the given
     *       number of decimal places.
     *
     * @param s C-string to initialize from; null is treated as an empty string.
     * @param s std::string to initialize from.
     * @param n Integer value to convert to its decimal string representation.
     * @param n Unsigned long value to convert to its decimal string representation.
     * @param n Float value to convert to a string.
     * @param decimals Number of decimal places to include when formatting the float (default 2).
     */
class String {
public:
    std::string _str;
    String() : _str() {}
    /**
     * Constructs a String from a C-style string.
     * If `s` is null, constructs an empty String.
     * @param s C-string to initialize from; may be null.
     */
    
    /**
     * Constructs a String from an `std::string`.
     * @param s `std::string` whose contents are copied into the String.
     */
    String(const char* s) : _str(s ? s : "") {}
    String(const std::string& s) : _str(s) {}
    /**
 * Construct a String containing the decimal representation of an integer.
 * @param n Integer value to convert to its decimal string form.
 */
String(int n) : _str(std::to_string(n)) {}
    /**
 * Construct a String from an unsigned long by converting the value to its decimal representation.
 * @param n Unsigned long value to convert to a decimal string. 
 */
String(unsigned long n) : _str(std::to_string(n)) {}
    String(float n, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, n);
        _str = buf;
    }

    /**
 * Retrieve a pointer to the internal null-terminated C string representation.
 *
 * @returns Pointer to the internal null-terminated character array. The pointer remains valid until the String is modified or destroyed.
 */
const char* c_str() const { return _str.c_str(); }
    size_t length() const { return _str.length(); }
    bool isEmpty() const { return _str.empty(); }

    String& operator+=(const String& s) { _str += s._str; return *this; }
    String& operator+=(const char* s) { if(s) _str += s; return *this; }
    String operator+(const String& s) const { return String(_str + s._str); }
    String operator+(const char* s) const { return String(_str + (s ? s : "")); }

    /**
 * Compare this String with another for content equality.
 * @returns `true` if the string contents are equal, `false` otherwise.
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