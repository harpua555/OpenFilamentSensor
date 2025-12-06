// Mock ArduinoJson.h for unit testing
#ifndef ARDUINOJSON_H
#define ARDUINOJSON_H

#include <string>
#include <cstring>

// Minimal mock of ArduinoJson types for compilation
/**
 * Clears the document's contents.
 *
 * In this test stub implementation the operation is a no-op.
 */

namespace ArduinoJson {
    /**
     * Remove all contents from the JSON document.
     */
    class JsonDocument {
    public:
        void clear() {}
    };
}

/**
 * Mock implementation of a DynamicJsonDocument for unit tests; methods are no-ops
 * and the document always reports null.
 */

/**
 * Construct a mock DynamicJsonDocument.
 * @param size Size in bytes of the allocation pool (ignored by this mock).
 */

/**
 * Clear the document's contents.
 *
 * This mock implementation performs no action.
 */

/**
 * Indicate whether the document represents a JSON null value.
 * @returns `true` if the document represents null, `false` otherwise.
 *          This mock always returns `true`.
 */
class DynamicJsonDocument {
public:
    DynamicJsonDocument(size_t) {}
    /**
 * Clears the document, removing any stored content and resetting it to an empty state.
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
 * Reports whether this JsonObject represents a null JSON value.
 *
 * @returns `true` if the object represents a null or absent JSON value, `false` otherwise.
 */
class JsonObject {
public:
    bool isNull() const { return true; }
};

/**
 * Indicates whether the JsonArray represents a null JSON value.
 *
 * @returns `true` if the array represents a null JSON value, `false` otherwise.
 */
class JsonArray {
public:
    bool isNull() const { return true; }
};

#endif // ARDUINOJSON_H