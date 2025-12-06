// Mock ArduinoJson.h for unit testing
#ifndef ARDUINOJSON_H
#define ARDUINOJSON_H

#include <string>
#include <cstring>

// Minimal mock of ArduinoJson types for compilation
/**
 * Clear the contents of the JSON document.
 *
 * This mock implementation performs no action and exists only for unit tests.
 */

namespace ArduinoJson {
    /**
     * Clear the JSON document.
     *
     * In this mock implementation the method is a no-op and leaves the document unchanged.
     */
    class JsonDocument {
    public:
        void clear() {}
    };
}

/**
 * Minimal mock of a dynamic ArduinoJson document used for unit tests.
 *
 * Represents a document object with no real JSON storage; all operations are stubs.
 */

/**
 * Constructs a DynamicJsonDocument with the given capacity.
 * @param capacity Intended memory capacity in bytes (unused by this mock).
 */

/**
 * Clears the document.
 *
 * No action is performed in this mock implementation.
 */

/**
 * Reports whether the document is null.
 * @returns `true` indicating this mock represents an always-null document, `false` otherwise.
 */
class DynamicJsonDocument {
public:
    DynamicJsonDocument(size_t) {}
    /**
 * Clear the document and reset it to an empty state.
 *
 * In this mock implementation the operation is a no-op.
 */
void clear() {}
    bool isNull() const { return true; }
};

// Mock StaticJsonDocument
template<size_t N>
class StaticJsonDocument {
public:
    void clear() {}
    bool isNull() const { return true; }
};

/**
 * Report whether the JSON object represents a null value.
 *
 * @returns `true` if the object represents a null or absent JSON object, `false` otherwise.
 */
class JsonObject {
public:
    bool isNull() const { return true; }
};

/**
 * Reports whether the JsonArray represents a null/empty value.
 *
 * @returns `true` if the array is null, `false` otherwise.
 */
class JsonArray {
public:
    bool isNull() const { return true; }
};

#endif // ARDUINOJSON_H