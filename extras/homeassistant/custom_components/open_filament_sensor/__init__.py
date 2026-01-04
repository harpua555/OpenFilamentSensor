"""Open Filament Sensor integration for Home Assistant."""
from __future__ import annotations

import asyncio
import ipaddress
import logging
from datetime import timedelta

import aiohttp
from homeassistant.components.frontend import async_register_built_in_panel, async_remove_panel
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, Platform
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .const import CONF_MAC, DOMAIN, PANEL_ICON, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR]


def _panel_id(entry_id: str) -> str:
    return f"{DOMAIN}_{entry_id}"


def _register_panel(hass: HomeAssistant, entry: ConfigEntry, host: str) -> None:
    """Register an iframe panel for the device's web UI."""
    panel_id = _panel_id(entry.entry_id)
    try:
        async_remove_panel(hass, panel_id)
    except ValueError:
        pass
    try:
        async_register_built_in_panel(
            hass,
            component_name="iframe",
            sidebar_title=entry.title,
            sidebar_icon=PANEL_ICON,
            frontend_url_path=panel_id,
            config={"url": f"http://{host}"},
            require_admin=False,
        )
    except Exception as err:
        _LOGGER.warning("Failed to register panel: %s", err)


def _unregister_panel(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Unregister the iframe panel."""
    try:
        async_remove_panel(hass, _panel_id(entry.entry_id))
    except ValueError:
        pass


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    coordinator: OFSDataUpdateCoordinator = hass.data[DOMAIN][entry.entry_id]
    coordinator.set_host(entry.data[CONF_HOST])
    _register_panel(hass, entry, entry.data[CONF_HOST])


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Open Filament Sensor from a config entry."""
    host = entry.data[CONF_HOST]

    coordinator = OFSDataUpdateCoordinator(hass, entry, host)
    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = coordinator

    _register_panel(hass, entry, host)
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        _unregister_panel(hass, entry)
        hass.data[DOMAIN].pop(entry.entry_id)

    return unload_ok


class OFSDataUpdateCoordinator(DataUpdateCoordinator):
    """Class to manage fetching OFS data."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry, host: str) -> None:
        """Initialize the coordinator."""
        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            update_interval=timedelta(seconds=SCAN_INTERVAL),
        )
        self.entry = entry
        self.host = host
        self.session = async_get_clientsession(hass)
        self._url = f"http://{host}/sensor_status"

    def set_host(self, host: str) -> None:
        """Update host details for the coordinator."""
        self.host = host
        self._url = f"http://{host}/sensor_status"

    async def _async_update_data(self) -> dict:
        """Fetch data from OFS device."""
        try:
            async with asyncio.timeout(10):
                async with self.session.get(self._url) as response:
                    if response.status != 200:
                        raise UpdateFailed(f"HTTP error {response.status}")
                    data = await response.json()
                    self._maybe_update_host(data)
                    return data
        except aiohttp.ClientError as err:
            raise UpdateFailed(f"Error communicating with OFS: {err}") from err
        except asyncio.TimeoutError as err:
            raise UpdateFailed(f"Timeout communicating with OFS") from err

    def _maybe_update_host(self, data: dict) -> None:
        """Refresh stored host if the device reports a new IP."""
        reported_ip = data.get("ip")
        if not reported_ip or reported_ip == self.host:
            return

        if not _is_ip_address(self.host):
            return

        if self.entry.data.get(CONF_HOST) == reported_ip:
            return

        self.hass.config_entries.async_update_entry(
            self.entry,
            data={**self.entry.data, CONF_HOST: reported_ip},
        )
        self.set_host(reported_ip)

    @property
    def device_info(self) -> dict:
        """Return device info for this OFS device."""
        mac = (
            self.data.get("mac")
            if self.data
            else self.entry.unique_id or self.entry.data.get(CONF_MAC, "unknown")
        )
        return {
            "identifiers": {(DOMAIN, mac)},
            "name": f"Open Filament Sensor ({self.host})",
            "manufacturer": "OpenFilamentSensor",
            "model": "ESP32 Filament Sensor",
            "sw_version": "1.0",
            "configuration_url": f"http://{self.host}",
        }


def _is_ip_address(value: str) -> bool:
    try:
        ipaddress.ip_address(value)
    except ValueError:
        return False
    return True
