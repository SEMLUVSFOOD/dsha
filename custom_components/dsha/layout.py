"""Layout of the DS dashboard: validation and storage."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store

from .const import (
    MAX_PAGE_NAME,
    MAX_PAGES,
    MAX_TILE_NAME,
    MAX_TITLE,
    STORAGE_KEY,
    STORAGE_VERSION,
    TILE_ACTIONS,
    TILES_PER_PAGE,
)


def _entity_id(value: Any) -> str:
    """Accept anything that looks like an entity id (it may not exist yet)."""
    if not isinstance(value, str) or value.count(".") != 1:
        raise vol.Invalid("invalid entity id")
    domain, object_id = value.split(".")
    if not domain or not object_id or len(value) > 63:
        raise vol.Invalid("invalid entity id")
    return value.lower()


TILE_SCHEMA = vol.Any(
    None,
    vol.Schema(
        {
            vol.Required("entity_id"): _entity_id,
            vol.Optional("name", default=""): vol.All(
                str, vol.Length(max=MAX_TILE_NAME)
            ),
            vol.Optional("action", default="default"): vol.In(TILE_ACTIONS),
        }
    ),
)


def _tiles(value: Any) -> list:
    """Exactly TILES_PER_PAGE slots, padded with empty ones."""
    if not isinstance(value, list) or len(value) > TILES_PER_PAGE:
        raise vol.Invalid(f"a page has at most {TILES_PER_PAGE} tiles")
    tiles = [TILE_SCHEMA(tile) for tile in value]
    return tiles + [None] * (TILES_PER_PAGE - len(tiles))


PAGE_SCHEMA = vol.Schema(
    {
        vol.Required("name"): vol.All(str, vol.Length(min=1, max=MAX_PAGE_NAME)),
        vol.Required("tiles"): _tiles,
    }
)

LAYOUT_SCHEMA = vol.Schema(
    {
        vol.Optional("title", default=""): vol.All(str, vol.Length(max=MAX_TITLE)),
        vol.Required("pages"): vol.All(
            [PAGE_SCHEMA], vol.Length(max=MAX_PAGES)
        ),
    }
)


def empty_layout() -> dict[str, Any]:
    """Layout of a fresh installation: one empty page."""
    return {
        "title": "",
        "pages": [{"name": "Home", "tiles": [None] * TILES_PER_PAGE}],
        "revision": 0,
    }


def layout_entities(layout: dict[str, Any], page: int | None = None) -> list[str]:
    """Entity ids in the layout (or in one page), without duplicates."""
    pages = layout["pages"]
    if page is not None:
        pages = pages[page : page + 1]
    seen: list[str] = []
    for p in pages:
        for tile in p["tiles"]:
            if tile and tile["entity_id"] not in seen:
                seen.append(tile["entity_id"])
    return seen


class LayoutStore:
    """Keeps the layout in .storage/dsha.layout."""

    def __init__(self, hass: HomeAssistant) -> None:
        self._store: Store[dict[str, Any]] = Store(
            hass, STORAGE_VERSION, STORAGE_KEY
        )
        self.layout: dict[str, Any] = empty_layout()

    async def async_load(self) -> None:
        data = await self._store.async_load()
        if data is None:
            return
        try:
            layout = LAYOUT_SCHEMA({k: v for k, v in data.items() if k != "revision"})
        except vol.Invalid:
            # Never lose the whole dashboard because of a bad entry
            return
        layout["revision"] = int(data.get("revision", 0))
        self.layout = layout

    async def async_save(self, layout: dict[str, Any]) -> dict[str, Any]:
        """Validate and store a new layout. Raises vol.Invalid."""
        new = LAYOUT_SCHEMA(layout)
        if not new["pages"]:
            new["pages"] = empty_layout()["pages"]
        new["revision"] = self.layout.get("revision", 0) + 1
        self.layout = new
        await self._store.async_save(new)
        return new
