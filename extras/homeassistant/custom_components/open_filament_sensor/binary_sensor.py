"""Binary sensor platform for Open Filament Sensor integration."""
from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import OFSDataUpdateCoordinator
from .const import DOMAIN, BINARY_SENSORS


# Map string device class names to actual classes
DEVICE_CLASS_MAP = {
    "problem": BinarySensorDeviceClass.PROBLEM,
    "connectivity": BinarySensorDeviceClass.CONNECTIVITY,
}


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up OFS binary sensors based on a config entry."""
    coordinator: OFSDataUpdateCoordinator = hass.data[DOMAIN][entry.entry_id]

    entities = []
    for sensor_def in BINARY_SENSORS:
        key, name, icon_on, icon_off, device_class_str, json_path = sensor_def
        device_class = DEVICE_CLASS_MAP.get(device_class_str) if device_class_str else None
        entities.append(
            OFSBinarySensor(
                coordinator=coordinator,
                key=key,
                name=name,
                icon_on=icon_on,
                icon_off=icon_off,
                device_class=device_class,
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


class OFSBinarySensor(CoordinatorEntity[OFSDataUpdateCoordinator], BinarySensorEntity):
    """Representation of an OFS binary sensor."""

    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: OFSDataUpdateCoordinator,
        key: str,
        name: str,
        icon_on: str,
        icon_off: str,
        device_class: BinarySensorDeviceClass | None,
        json_path: str,
    ) -> None:
        """Initialize the binary sensor."""
        super().__init__(coordinator)
        self._key = key
        self._json_path = json_path
        self._icon_on = icon_on
        self._icon_off = icon_off
        self._attr_name = name
        self._attr_device_class = device_class

        # Create unique ID from MAC + sensor key
        mac = coordinator.data.get("mac", "unknown") if coordinator.data else "unknown"
        self._attr_unique_id = f"{mac}_{key}"

    @property
    def device_info(self):
        """Return device info."""
        return self.coordinator.device_info

    @property
    def is_on(self) -> bool | None:
        """Return true if the binary sensor is on."""
        value = get_nested_value(self.coordinator.data, self._json_path)
        if value is None:
            return None
        return bool(value)

    @property
    def icon(self) -> str:
        """Return the icon based on state."""
        if self.is_on:
            return self._icon_on
        return self._icon_off
