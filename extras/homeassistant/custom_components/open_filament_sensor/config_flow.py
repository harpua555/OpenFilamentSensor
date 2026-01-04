"""Config flow for Open Filament Sensor integration."""
from __future__ import annotations

import asyncio
import logging
from typing import Any

import aiohttp
import voluptuous as vol
from homeassistant.components.zeroconf import ZeroconfServiceInfo
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import CONF_MAC, DOMAIN

_LOGGER = logging.getLogger(__name__)

STEP_USER_DATA_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_HOST): str,
    }
)


async def async_get_device_info(hass, host: str) -> dict[str, Any]:
    """Fetch and validate device info from the given host."""
    session = async_get_clientsession(hass)
    url = f"http://{host}/sensor_status"

    try:
        async with asyncio.timeout(10):
            async with session.get(url) as response:
                if response.status != 200:
                    raise CannotConnect(f"HTTP {response.status}")
                json_data = await response.json()
                # Get MAC for unique ID
                mac = json_data.get("mac")
                if not mac:
                    raise CannotConnect("Missing MAC address")
                return {"title": f"Open Filament Sensor ({host})", "mac": mac, "host": host}
    except aiohttp.ClientError as err:
        raise CannotConnect(str(err)) from err
    except asyncio.TimeoutError as err:
        raise CannotConnect("Connection timeout") from err


class OFSConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle a config flow for Open Filament Sensor."""

    VERSION = 1
    _discovered_host: str | None = None
    _discovered_mac: str | None = None
    _discovered_title: str | None = None

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Handle the initial step."""
        errors: dict[str, str] = {}

        if user_input is not None:
            try:
                info = await async_get_device_info(self.hass, user_input[CONF_HOST])
            except CannotConnect:
                errors["base"] = "cannot_connect"
            except Exception:
                _LOGGER.exception("Unexpected exception")
                errors["base"] = "unknown"
            else:
                # Use MAC as unique ID to prevent duplicate entries
                await self.async_set_unique_id(info["mac"])
                self._abort_if_unique_id_configured(
                    updates={CONF_HOST: info["host"], CONF_MAC: info["mac"]}
                )

                return self.async_create_entry(
                    title=info["title"],
                    data={CONF_HOST: info["host"], CONF_MAC: info["mac"]},
                )

        return self.async_show_form(
            step_id="user",
            data_schema=STEP_USER_DATA_SCHEMA,
            errors=errors,
        )

    async def async_step_zeroconf(
        self, discovery_info: ZeroconfServiceInfo
    ) -> ConfigFlowResult:
        """Handle a zeroconf discovery."""
        host = discovery_info.host or discovery_info.ip_address
        if not host:
            return self.async_abort(reason="cannot_connect")
        host = str(host)

        try:
            info = await async_get_device_info(self.hass, host)
        except CannotConnect:
            return self.async_abort(reason="cannot_connect")

        await self.async_set_unique_id(info["mac"])
        self._abort_if_unique_id_configured(
            updates={CONF_HOST: info["host"], CONF_MAC: info["mac"]}
        )

        self._discovered_host = info["host"]
        self._discovered_mac = info["mac"]
        self._discovered_title = info["title"]

        return await self.async_step_confirm()

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Confirm zeroconf discovery."""
        if user_input is not None and self._discovered_host and self._discovered_mac:
            return self.async_create_entry(
                title=self._discovered_title or "Open Filament Sensor",
                data={
                    CONF_HOST: self._discovered_host,
                    CONF_MAC: self._discovered_mac,
                },
            )

        return self.async_show_form(
            step_id="confirm",
            data_schema=vol.Schema({}),
            description_placeholders={
                "host": self._discovered_host or "unknown",
            },
        )


class CannotConnect(Exception):
    """Error to indicate we cannot connect."""
