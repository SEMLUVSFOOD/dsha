# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 DSHA contributors
#
# Builds dsha.nds with BlocksDS (https://blocksds.skylyrac.net) and its Mbed
# TLS package (wf-pacman -S blocksds-mbedtls). If you don't have BlocksDS
# installed, use "make docker" (see docs/building.md).

BLOCKSDS	?= /opt/blocksds/core
BLOCKSDSEXT	?= /opt/blocksds/external

NAME		:= dsha

GAME_TITLE	:= DSHA
GAME_SUBTITLE	:= Home Assistant for DS
GAME_AUTHOR	:= github.com/semluvsfood/dsha
GAME_ICON	:= assets/icon.gif

SOURCEDIRS	:= source
INCLUDEDIRS	:= source/core

# A config file can be embedded in the ROM (optional, see README.md)
ifneq ($(wildcard nitrofs/config.ini),)
    NITROFSDIR	:= nitrofs
endif

# Truncating long strings to fit the screen is intended
CFLAGS		:= -Wno-format-truncation

ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
LIBS		:= -lmbedtls -lmbedx509 -lmbedcrypto -ldswifi9 -lnds9
LIBDIRS		:= $(BLOCKSDSEXT)/mbedtls \
		   $(BLOCKSDS)/libs/dswifi \
		   $(BLOCKSDS)/libs/libnds

# Build inside the official BlocksDS docker image
DOCKER_IMAGE	?= skylyrac/blocksds:slim-v1.25.0

.DEFAULT_GOAL := all

.PHONY: docker test screenshots test-integration test-panel

# The image doesn't include Mbed TLS: install it (as root), build, and give
# the files back to the user.
docker:
	docker run --rm -v "$(CURDIR)":/work -w /work $(DOCKER_IMAGE) sh -c \
		'wf-pacman -Sy --noconfirm blocksds-mbedtls >/dev/null && make; \
		 rc=$$?; chown -R $(shell id -u):$(shell id -g) build $(NAME).nds \
		 2>/dev/null; exit $$rc'

test:
	$(MAKE) -C tests test

screenshots:
	$(MAKE) -C tests screenshots

# Home Assistant integration (needs pytest-homeassistant-custom-component)
PYTHON		?= python3
test-integration:
	$(PYTHON) -m pytest -q -p no:cacheprovider -c tests_ha/pytest.ini tests_ha

# DS Dashboard panel in Chromium (needs node and playwright)
test-panel:
	node tests/panel/test_panel.js

# The DS build needs BlocksDS; the other targets (tests) work without it
ifneq ($(wildcard $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile),)
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
else
all:
	@echo "BlocksDS wasn't found in $(BLOCKSDS)."
	@echo "Install it, or build with Docker: make docker"
	@false
endif
