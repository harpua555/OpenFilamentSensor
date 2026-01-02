"""Constants for Open Filament Sensor integration."""
from homeassistant.components.sensor import SensorDeviceClass, SensorStateClass
from homeassistant.const import (
    PERCENTAGE,
    UnitOfLength,
    UnitOfTime,
)

DOMAIN = "open_filament_sensor"
SCAN_INTERVAL = 5  # seconds

# Sensor definitions: (key, name, unit, device_class, state_class, icon, json_path)
# json_path is dot-notation: "elegoo.hardJamPercent" means data["elegoo"]["hardJamPercent"]
SENSORS = [
    # Jam Detection
    ("hard_jam_percent", "Hard Jam", PERCENTAGE, None, SensorStateClass.MEASUREMENT, "mdi:alert-circle", "elegoo.hardJamPercent"),
    ("soft_jam_percent", "Soft Jam", PERCENTAGE, None, SensorStateClass.MEASUREMENT, "mdi:alert", "elegoo.softJamPercent"),
    ("grace_state", "Grace State", None, None, None, "mdi:timer-sand", "elegoo.graceState"),

    # Filament Tracking
    ("expected_filament", "Expected Filament", UnitOfLength.MILLIMETERS, SensorDeviceClass.DISTANCE, SensorStateClass.TOTAL_INCREASING, "mdi:printer-3d-nozzle", "elegoo.expectedFilament"),
    ("actual_filament", "Actual Filament", UnitOfLength.MILLIMETERS, SensorDeviceClass.DISTANCE, SensorStateClass.TOTAL_INCREASING, "mdi:printer-3d-nozzle-outline", "elegoo.actualFilament"),
    ("current_deficit", "Current Deficit", UnitOfLength.MILLIMETERS, SensorDeviceClass.DISTANCE, SensorStateClass.MEASUREMENT, "mdi:delta", "elegoo.currentDeficitMm"),
    ("deficit_ratio", "Deficit Ratio", None, None, SensorStateClass.MEASUREMENT, "mdi:percent", "elegoo.deficitRatio"),
    ("pass_ratio", "Pass Ratio", None, None, SensorStateClass.MEASUREMENT, "mdi:check-circle", "elegoo.passRatio"),
    ("expected_rate", "Expected Rate", "mm/s", None, SensorStateClass.MEASUREMENT, "mdi:speedometer", "elegoo.expectedRateMmPerSec"),
    ("actual_rate", "Actual Rate", "mm/s", None, SensorStateClass.MEASUREMENT, "mdi:speedometer-slow", "elegoo.actualRateMmPerSec"),

    # Print Progress
    ("print_status", "Print Status", None, None, None, "mdi:printer-3d", "elegoo.printStatus"),
    ("current_layer", "Current Layer", None, None, SensorStateClass.MEASUREMENT, "mdi:layers", "elegoo.currentLayer"),
    ("total_layers", "Total Layers", None, None, None, "mdi:layers-triple", "elegoo.totalLayer"),
    ("progress", "Progress", PERCENTAGE, None, SensorStateClass.MEASUREMENT, "mdi:progress-check", "elegoo.progress"),
    ("current_z", "Z Height", UnitOfLength.MILLIMETERS, SensorDeviceClass.DISTANCE, SensorStateClass.MEASUREMENT, "mdi:axis-z-arrow", "elegoo.currentZ"),
    ("print_speed", "Print Speed", PERCENTAGE, None, SensorStateClass.MEASUREMENT, "mdi:speedometer", "elegoo.PrintSpeedPct"),

    # Physical Sensors
    ("movement_pulses", "Movement Pulses", None, None, SensorStateClass.TOTAL_INCREASING, "mdi:pulse", "elegoo.movementPulses"),

    # Device Info
    ("uptime", "Uptime", UnitOfTime.SECONDS, SensorDeviceClass.DURATION, SensorStateClass.TOTAL_INCREASING, "mdi:timer-outline", "uptimeSec"),
    ("mainboard_id", "Mainboard ID", None, None, None, "mdi:identifier", "elegoo.mainboardID"),
]

# Binary sensor definitions: (key, name, icon_on, icon_off, device_class, json_path)
BINARY_SENSORS = [
    ("filament_stopped", "Filament Stopped", "mdi:alert-octagon", "mdi:check-circle", "problem", "stopped"),
    ("filament_runout", "Filament Runout", "mdi:alert", "mdi:check-circle", "problem", "filamentRunout"),
    ("printer_connected", "Printer Connected", "mdi:lan-connect", "mdi:lan-disconnect", "connectivity", "elegoo.isWebsocketConnected"),
    ("is_printing", "Printing", "mdi:printer-3d-nozzle", "mdi:printer-3d-nozzle-off", None, "elegoo.isPrinting"),
    ("grace_active", "Grace Active", "mdi:timer-sand", "mdi:timer-sand-empty", None, "elegoo.graceActive"),
    ("telemetry_available", "Telemetry Available", "mdi:satellite-uplink", "mdi:satellite-variant", "connectivity", "elegoo.telemetryAvailable"),
]

# Print status mapping
PRINT_STATUS_MAP = {
    0: "Idle",
    1: "Homing",
    2: "Descending",
    3: "Exposing",
    4: "Lifting",
    5: "Pausing",
    6: "Paused",
    7: "Stopping",
    8: "Stopped",
    9: "Complete",
    10: "File Checking",
    13: "Printing",
    16: "Heating",
    20: "Bed Leveling",
}

# Grace state mapping
GRACE_STATE_MAP = {
    0: "Idle",
    1: "Start Grace",
    2: "Resume Grace",
    3: "Active",
    4: "Jammed",
}
