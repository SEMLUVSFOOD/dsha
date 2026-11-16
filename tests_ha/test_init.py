"""Tests for the DSHA integration (run inside a real Home Assistant core)."""

from http import HTTPStatus

import pytest

from homeassistant import config_entries
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.setup import async_setup_component

from custom_components.dsha.const import DOMAIN, STORAGE_KEY

from pytest_homeassistant_custom_component.common import MockConfigEntry

LAYOUT = {
    "title": "My DS",
    "pages": [
        {
            "name": "Living room",
            "tiles": [
                {"entity_id": "light.living_room", "name": "Ceiling"},
                None,
                {"entity_id": "automation.night", "action": "trigger"},
                {"entity_id": "sensor.missing"},
            ],
        },
        {"name": "Climate", "tiles": [{"entity_id": "climate.thermostat"}]},
    ],
}


async def setup(hass: HomeAssistant) -> MockConfigEntry:
    hass.states.async_set(
        "light.living_room",
        "on",
        {
            "friendly_name": "Living room",
            "brightness": 128,
            "supported_color_modes": ["brightness"],
            "rgb_color": [1, 2, 3],
            "entity_picture": "/big/picture.png",
        },
    )
    hass.states.async_set("automation.night", "on", {"friendly_name": "Night"})
    hass.states.async_set(
        "climate.thermostat", "heat", {"temperature": 21, "hvac_modes": ["heat"]}
    )
    entry = MockConfigEntry(domain=DOMAIN, title="DS Dashboard")
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    return entry


async def test_config_flow(hass: HomeAssistant) -> None:
    """The integration is added with one click, only once."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    assert result["type"] is FlowResultType.FORM
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == "DS Dashboard"

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    assert result["type"] is FlowResultType.ABORT


async def test_panel(hass: HomeAssistant, hass_client) -> None:
    """The panel is in the sidebar and its script is served."""
    entry = await setup(hass)
    panels = hass.data["frontend_panels"]
    assert "dsha" in panels
    panel = panels["dsha"]
    assert panel.sidebar_title == "DS Dashboard"
    assert panel.require_admin

    client = await hass_client()
    resp = await client.get("/dsha_static/dsha-panel.js")
    assert resp.status == HTTPStatus.OK

    # Unloading removes the panel, reloading adds it back
    assert await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()
    assert "dsha" not in hass.data["frontend_panels"]
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert "dsha" in hass.data["frontend_panels"]


async def test_websocket(hass: HomeAssistant, hass_ws_client, hass_storage) -> None:
    """The panel loads and saves the layout through the websocket API."""
    await setup(hass)
    ws = await hass_ws_client(hass)

    await ws.send_json({"id": 1, "type": "dsha/layout/get"})
    msg = await ws.receive_json()
    assert msg["success"]
    assert msg["result"]["pages"][0]["name"] == "Home"
    assert msg["result"]["pages"][0]["tiles"] == [None] * 8

    await ws.send_json({"id": 2, "type": "dsha/layout/save", "layout": LAYOUT})
    msg = await ws.receive_json()
    assert msg["success"], msg
    saved = msg["result"]
    assert saved["revision"] == 1
    tiles = saved["pages"][0]["tiles"]
    assert len(tiles) == 8  # Padded
    assert tiles[0] == {"entity_id": "light.living_room", "name": "Ceiling",
                        "action": "default"}
    assert tiles[1] is None
    assert tiles[2]["action"] == "trigger"

    # Stored in .storage
    await hass.async_block_till_done()
    assert hass_storage[STORAGE_KEY]["data"]["pages"][1]["name"] == "Climate"

    # Invalid layouts are rejected and don't replace the stored one
    bad = {"pages": [{"name": "x", "tiles": [{"entity_id": "nope"}]}]}
    await ws.send_json({"id": 3, "type": "dsha/layout/save", "layout": bad})
    msg = await ws.receive_json()
    assert not msg["success"]
    assert msg["error"]["code"] == "invalid_format"

    too_many = {"pages": [{"name": "x", "tiles": [None] * 9}]}
    await ws.send_json({"id": 4, "type": "dsha/layout/save", "layout": too_many})
    msg = await ws.receive_json()
    assert not msg["success"]

    await ws.send_json({"id": 5, "type": "dsha/layout/get"})
    msg = await ws.receive_json()
    assert msg["result"]["revision"] == 1


async def test_save_needs_admin(
    hass: HomeAssistant, hass_ws_client, hass_read_only_access_token
) -> None:
    await setup(hass)
    ws = await hass_ws_client(hass, hass_read_only_access_token)
    await ws.send_json({"id": 1, "type": "dsha/layout/save", "layout": LAYOUT})
    msg = await ws.receive_json()
    assert not msg["success"]
    assert msg["error"]["code"] == "unauthorized"


async def test_rest_api(hass: HomeAssistant, hass_client, hass_ws_client) -> None:
    """The DS reads the layout and compact states over plain HTTP."""
    await setup(hass)
    ws = await hass_ws_client(hass)
    await ws.send_json({"id": 1, "type": "dsha/layout/save", "layout": LAYOUT})
    assert (await ws.receive_json())["success"]

    client = await hass_client()
    resp = await client.get("/api/dsha/layout")
    assert resp.status == HTTPStatus.OK
    data = await resp.json()
    assert data["version"] == 1
    assert data["title"] == "My DS"
    assert data["revision"] == 1
    assert [p["name"] for p in data["pages"]] == ["Living room", "Climate"]

    resp = await client.get("/api/dsha/states?page=0")
    states = await resp.json()
    assert [s["entity_id"] for s in states] == [
        "light.living_room",
        "automation.night",
        "sensor.missing",
    ]
    light = states[0]
    assert light["state"] == "on"
    # Only the attributes the DS needs
    assert light["attributes"] == {
        "friendly_name": "Living room",
        "brightness": 128,
        "supported_color_modes": ["brightness"],
    }
    assert states[2] == {"entity_id": "sensor.missing", "state": "unavailable",
                         "attributes": {}}

    resp = await client.get("/api/dsha/states")
    assert len(await resp.json()) == 4

    resp = await client.get("/api/dsha/states?page=7")
    assert resp.status == HTTPStatus.BAD_REQUEST


async def test_rest_api_auth(hass: HomeAssistant, hass_client_no_auth) -> None:
    await setup(hass)
    client = await hass_client_no_auth()
    resp = await client.get("/api/dsha/layout")
    assert resp.status == HTTPStatus.UNAUTHORIZED


async def test_rest_api_read_only_user(
    hass: HomeAssistant, hass_client, hass_read_only_access_token
) -> None:
    """A non-admin account (recommended for the DS) can read the layout."""
    await setup(hass)
    client = await hass_client(hass_read_only_access_token)
    resp = await client.get("/api/dsha/layout")
    assert resp.status == HTTPStatus.OK


async def test_layout_survives_restart(hass: HomeAssistant, hass_storage) -> None:
    hass_storage[STORAGE_KEY] = {
        "version": 1,
        "key": STORAGE_KEY,
        "data": {**LAYOUT, "revision": 5},
    }
    await setup(hass)
    layout = hass.data[DOMAIN]["store"].layout
    assert layout["revision"] == 5
    assert layout["title"] == "My DS"
    assert len(layout["pages"][0]["tiles"]) == 8


async def test_not_set_up(hass: HomeAssistant, hass_client) -> None:
    """Without a config entry, the DS gets a 404 and uses its normal mode."""
    assert await async_setup_component(hass, DOMAIN, {})
    client = await hass_client()
    resp = await client.get("/api/dsha/layout")
    assert resp.status == HTTPStatus.NOT_FOUND
