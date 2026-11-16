"""Layout of the DS dashboard: validation and storage."""

from __future__ import annotations

import re
from typing import Any

import voluptuous as vol

from homeassistant.core import HomeAssistant, valid_entity_id
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


_ENTITY_ID_RE = re.compile(r"[a-z0-9_]+\.[a-z0-9_]+")


def _entity_id(value: Any) -> str:
    """A syntactically valid entity id (the entity may not exist yet).

    Uses the rules of Home Assistant itself. The DS puts entity ids in request
    paths and JSON, so nothing else may get through.
    """
    if not isinstance(value, str):
        raise vol.Invalid("invalid entity id")
    value = value.lower()
    # fullmatch: valid_entity_id() alone accepts a trailing newline
    if (
        len(value) > 63
        or not _ENTITY_ID_RE.fullmatch(value)
        or not valid_entity_id(value)
    ):
        raise vol.Invalid("invalid entity id")
    return value


def _text(max_len: int, min_len: int = 0):
    """A single line of text: control characters are removed."""

    def validate(value: Any) -> str:
        if not isinstance(value, str):
            raise vol.Invalid("expected a string")
        value = "".join(c for c in value if c.isprintable()).strip()
        if not min_len <= len(value) <= max_len:
            raise vol.Invalid(f"length must be between {min_len} and {max_len}")
        return value

    return validate


TILE_SCHEMA = vol.Any(
    None,
    vol.Schema(
        {
            vol.Required("entity_id"): _entity_id,
            vol.Optional("name", default=""): _text(MAX_TILE_NAME),
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
        vol.Required("name"): _text(MAX_PAGE_NAME, 1),
        vol.Required("tiles"): _tiles,
    }
)

LAYOUT_SCHEMA = vol.Schema(
    {
        vol.Optional("title", default=""): _text(MAX_TITLE),
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
