"""Constants for the DSHA integration."""

from __future__ import annotations

DOMAIN = "dsha"
VERSION = "1.2.0"

STORAGE_KEY = "dsha.layout"
STORAGE_VERSION = 1

PANEL_URL_PATH = "dsha"
PANEL_TITLE = "DS Dashboard"
PANEL_ICON = "mdi:nintendo-game-boy"
PANEL_COMPONENT = "dsha-panel"
STATIC_URL = "/dsha_static"

# The DS shows 2 x 4 tiles per page
TILES_PER_PAGE = 8
MAX_PAGES = 16
MAX_TITLE = 31
MAX_PAGE_NAME = 20
MAX_TILE_NAME = 40

TILE_ACTIONS = ("default", "trigger", "none")

# Attributes the DS uses. Everything else is dropped to keep the responses
# small (the DS has 4 MB of RAM and a slow WiFi chip).
DS_ATTRIBUTES = (
    "friendly_name",
    "unit_of_measurement",
    "device_class",
    "brightness",
    "supported_color_modes",
    "current_temperature",
    "temperature",
    "target_temp_step",
    "min_temp",
    "max_temp",
    "min",
    "max",
    "step",
    "volume_level",
    "current_position",
    "media_title",
    "hvac_action",
    "temperature_unit",
)

EVENT_LAYOUT_UPDATED = "dsha_layout_updated"
