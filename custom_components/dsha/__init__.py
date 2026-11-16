"""DSHA: dashboard editor for the Nintendo DS Home Assistant app.

Adds a "DS Dashboard" panel to the sidebar, where the pages and tiles shown by
the DS app are arranged with drag and drop. The DS reads the result from
/api/dsha/layout.

https://github.com/semluvsfood/dsha
"""

from __future__ import annotations

from http import HTTPStatus
from pathlib import Path
from typing import Any

from aiohttp import web
import voluptuous as vol

from homeassistant.components import frontend, panel_custom, websocket_api
from homeassistant.components.http import HomeAssistantView, StaticPathConfig
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EVENT_HOMEASSISTANT_START
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import Unauthorized
from homeassistant.auth.permissions.const import POLICY_READ
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.typing import ConfigType

from .const import (
    DOMAIN,
    DS_ATTRIBUTES,
    EVENT_LAYOUT_UPDATED,
    PANEL_COMPONENT,
    PANEL_ICON,
    PANEL_TITLE,
    PANEL_URL_PATH,
    STATIC_URL,
    VERSION,
)
from .layout import LayoutStore, layout_entities

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)

FRONTEND_DIR = Path(__file__).parent / "frontend"


def _store(hass: HomeAssistant) -> LayoutStore:
    return hass.data[DOMAIN]["store"]


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Things that must only be registered once, even across reloads."""
    store = LayoutStore(hass)
    await store.async_load()
    hass.data[DOMAIN] = {"store": store, "loaded": False}

    await hass.http.async_register_static_paths(
        [StaticPathConfig(STATIC_URL, str(FRONTEND_DIR), False)]
    )
    hass.http.register_view(DshaLayoutView())
    hass.http.register_view(DshaStatesView())
    websocket_api.async_register_command(hass, ws_get_layout)
    websocket_api.async_register_command(hass, ws_save_layout)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Show the panel in the sidebar."""
    await panel_custom.async_register_panel(
        hass,
        webcomponent_name=PANEL_COMPONENT,
        frontend_url_path=PANEL_URL_PATH,
        sidebar_title=PANEL_TITLE,
        sidebar_icon=PANEL_ICON,
        module_url=f"{STATIC_URL}/dsha-panel.js?v={VERSION}",
        require_admin=True,
        config={"version": VERSION},
    )
    hass.data[DOMAIN]["loaded"] = True
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Remove the panel. The layout is kept in case the entry is re-added."""
    frontend.async_remove_panel(hass, PANEL_URL_PATH)
    hass.data[DOMAIN]["loaded"] = False
    return True


# Websocket API, used by the panel
# --------------------------------


@websocket_api.websocket_command({vol.Required("type"): "dsha/layout/get"})
@callback
def ws_get_layout(
    hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict
) -> None:
    connection.send_result(msg["id"], _store(hass).layout)


@websocket_api.websocket_command(
    {
        vol.Required("type"): "dsha/layout/save",
        vol.Required("layout"): dict,
    }
)
@websocket_api.require_admin
@websocket_api.async_response
async def ws_save_layout(
    hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict
) -> None:
    try:
        layout = await _store(hass).async_save(msg["layout"])
    except vol.Invalid as err:
        connection.send_error(msg["id"], "invalid_format", str(err))
        return
    hass.bus.async_fire(EVENT_LAYOUT_UPDATED, {"revision": layout["revision"]})
    connection.send_result(msg["id"], layout)


# REST API, used by the DS
# ------------------------


class DshaLayoutView(HomeAssistantView):
    """GET /api/dsha/layout: the pages and tiles to show."""

    url = "/api/dsha/layout"
    name = "api:dsha:layout"

    async def get(self, request: web.Request) -> web.Response:
        hass: HomeAssistant = request.app["hass"]
        if not hass.data[DOMAIN]["loaded"]:
            return self.json_message("DSHA is not set up", HTTPStatus.NOT_FOUND)
        layout = _store(hass).layout
        return self.json(
            {
                "version": 1,
                "revision": layout.get("revision", 0),
                "title": layout["title"],
                "pages": layout["pages"],
            }
        )


class DshaStatesView(HomeAssistantView):
    """GET /api/dsha/states[?page=N]: compact states of the layout entities.

    Same format as /api/states, with only the attributes the DS uses, so one
    small request refreshes a whole page.
    """

    url = "/api/dsha/states"
    name = "api:dsha:states"

    async def get(self, request: web.Request) -> web.Response:
        hass: HomeAssistant = request.app["hass"]
        if not hass.data[DOMAIN]["loaded"]:
            return self.json_message("DSHA is not set up", HTTPStatus.NOT_FOUND)

        layout = _store(hass).layout
        page: int | None = None
        if "page" in request.query:
            try:
                page = int(request.query["page"])
            except ValueError:
                return self.json_message("Invalid page", HTTPStatus.BAD_REQUEST)
            if not 0 <= page < len(layout["pages"]):
                return self.json_message("Invalid page", HTTPStatus.BAD_REQUEST)

        user = request["hass_user"]
        result: list[dict[str, Any]] = []
        for entity_id in layout_entities(layout, page):
            if not user.permissions.check_entity(entity_id, POLICY_READ):
                continue
            state = hass.states.get(entity_id)
            if state is None:
                result.append(
                    {"entity_id": entity_id, "state": "unavailable", "attributes": {}}
                )
                continue
            result.append(
                {
                    "entity_id": entity_id,
                    "state": state.state,
                    "attributes": {
                        k: v for k, v in state.attributes.items() if k in DS_ATTRIBUTES
                    },
                }
            )
        return self.json(result)
