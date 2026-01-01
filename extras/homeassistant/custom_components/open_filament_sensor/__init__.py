"""Open Filament Sensor integration for Home Assistant."""
from __future__ import annotations

import asyncio
import logging
from datetime import timedelta

import aiohttp
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, Platform
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .const import DOMAIN, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Open Filament Sensor from a config entry."""
    host = entry.data[CONF_HOST]

    coordinator = OFSDataUpdateCoordinator(hass, host)
    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = coordinator

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        hass.data[DOMAIN].pop(entry.entry_id)

    return unload_ok


class OFSDataUpdateCoordinator(DataUpdateCoordinator):
    """Class to manage fetching OFS data."""

    def __init__(self, hass: HomeAssistant, host: str) -> None:
        """Initialize the coordinator."""
        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            update_interval=timedelta(seconds=SCAN_INTERVAL),
        )
        self.host = host
        self.session = async_get_clientsession(hass)
        self._url = f"http://{host}/sensor_status"

    async def _async_update_data(self) -> dict:
        """Fetch data from OFS device."""
        try:
            async with asyncio.timeout(10):
                async with self.session.get(self._url) as response:
                    if response.status != 200:
                        raise UpdateFailed(f"HTTP error {response.status}")
                    data = await response.json()
                    return data
        except aiohttp.ClientError as err:
            raise UpdateFailed(f"Error communicating with OFS: {err}") from err
        except asyncio.TimeoutError as err:
            raise UpdateFailed(f"Timeout communicating with OFS") from err

    @property
    def device_info(self) -> dict:
        """Return device info for this OFS device."""
        mac = self.data.get("mac", "unknown") if self.data else "unknown"
        return {
            "identifiers": {(DOMAIN, mac)},
            "name": f"Open Filament Sensor ({self.host})",
            "manufacturer": "OpenFilamentSensor",
            "model": "ESP32 Filament Sensor",
            "sw_version": "1.0",
            "configuration_url": f"http://{self.host}",
        }
