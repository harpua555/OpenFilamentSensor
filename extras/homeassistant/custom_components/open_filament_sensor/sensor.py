"""Sensor platform for Open Filament Sensor integration."""
from __future__ import annotations

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import OFSDataUpdateCoordinator
from .const import DOMAIN, SENSORS, PRINT_STATUS_MAP, GRACE_STATE_MAP


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up OFS sensors based on a config entry."""
    coordinator: OFSDataUpdateCoordinator = hass.data[DOMAIN][entry.entry_id]

    entities = []
    for sensor_def in SENSORS:
        key, name, unit, device_class, state_class, icon, json_path = sensor_def
        entities.append(
            OFSSensor(
                coordinator=coordinator,
                key=key,
                name=name,
                unit=unit,
                device_class=device_class,
                state_class=state_class,
                icon=icon,
                json_path=json_path,
            )
        )

    async_add_entities(entities)


def get_nested_value(data: dict, path: str):
    """Get a value from nested dict using dot notation."""
    if not data:
        return None
    keys = path.split(".")
    value = data
    for key in keys:
        if isinstance(value, dict):
            value = value.get(key)
        else:
            return None
    return value


class OFSSensor(CoordinatorEntity[OFSDataUpdateCoordinator], SensorEntity):
    """Representation of an OFS sensor."""

    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: OFSDataUpdateCoordinator,
        key: str,
        name: str,
        unit: str | None,
        device_class: str | None,
        state_class: str | None,
        icon: str,
        json_path: str,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._key = key
        self._json_path = json_path
        self._attr_name = name
        self._attr_native_unit_of_measurement = unit
        self._attr_device_class = device_class
        self._attr_state_class = state_class
        self._attr_icon = icon

        # Create unique ID from MAC + sensor key
        mac = coordinator.data.get("mac", "unknown") if coordinator.data else "unknown"
        self._attr_unique_id = f"{mac}_{key}"

    @property
    def device_info(self):
        """Return device info."""
        return self.coordinator.device_info

    @property
    def native_value(self):
        """Return the state of the sensor."""
        value = get_nested_value(self.coordinator.data, self._json_path)

        # Map numeric status values to human-readable strings
        if self._key == "print_status" and value is not None:
            return PRINT_STATUS_MAP.get(value, f"Unknown ({value})")
        if self._key == "grace_state" and value is not None:
            return GRACE_STATE_MAP.get(value, f"Unknown ({value})")

        # Round floating point values for cleaner display
        if isinstance(value, float):
            if self._key in ("hard_jam_percent", "soft_jam_percent", "progress", "print_speed"):
                return round(value, 1)
            if self._key in ("expected_filament", "actual_filament", "current_deficit"):
                return round(value, 2)
            if self._key in ("deficit_ratio", "pass_ratio"):
                return round(value, 3)
            if self._key in ("expected_rate", "actual_rate"):
                return round(value, 2)

        return value
