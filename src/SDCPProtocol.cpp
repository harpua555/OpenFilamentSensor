#include "SDCPProtocol.h"
#include "ElegooCC.h"

bool SDCPProtocol::buildCommandMessage(
    JsonDocument& doc,
    int command,
    const String& requestId,
    const String& mainboardId,
    unsigned long timestamp,
    int printStatus,
    uint8_t machineStatusMask
) {
    // Build the SDCP command JSON structure
    doc["Id"] = requestId;
    JsonObject data = doc.createNestedObject("Data");
    data["Cmd"] = command;
    data["RequestID"] = requestId;
    data["MainboardID"] = mainboardId;
    data["TimeStamp"] = timestamp;

    // Match the Home Assistant integration's client identity for SDCP commands.
    // From = 0 is used there and is known to work reliably for pause/stop.
    data["From"] = 0;

    // Explicit empty Data object (matches existing payload structure)
    data.createNestedObject("Data");

    // Include current SDCP print and machine status, mirroring the status payload fields.
    data["PrintStatus"] = printStatus;
    JsonArray currentStatus = data.createNestedArray("CurrentStatus");
    for (int s = 0; s <= 4; ++s) {
        if ((machineStatusMask & (1 << s)) != 0) {
            currentStatus.add(s);
        }
    }

    // When we know the MainboardID, include a Topic field that matches the
    // "sdcp/request/<MainboardID>" pattern used by the Elegoo HA integration.
    if (!mainboardId.isEmpty()) {
        String topic = "sdcp/request/";
        topic += mainboardId;
        doc["Topic"] = topic;
    }

    return true;
}

bool SDCPProtocol::tryReadExtrusionValue(
    JsonObject& printInfo,
    const char* key,
    const char* hexKey,
    float& output
) {
    if (printInfo.containsKey(key) && !printInfo[key].isNull()) {
        output = printInfo[key].as<float>();
        return true;
    }

    if (hexKey != nullptr && printInfo.containsKey(hexKey) && !printInfo[hexKey].isNull()) {
        output = printInfo[hexKey].as<float>();
        return true;
    }

    return false;
}
