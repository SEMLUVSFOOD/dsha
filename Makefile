# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 DSHA contributors
#
# Builds dsha.nds with BlocksDS (https://blocksds.skylyrac.net). If you don't
# have BlocksDS installed, use "make docker" (see docs/building.md).

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
LIBS		:= -ldswifi9 -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi \
		   $(BLOCKSDS)/libs/libnds

# Build inside the official BlocksDS docker image
DOCKER_IMAGE	?= skylyrac/blocksds:slim-v1.25.0

.DEFAULT_GOAL := all

.PHONY: docker test screenshots

docker:
	docker run --rm -v "$(CURDIR)":/work -w /work -u $$(id -u):$$(id -g) \
		$(DOCKER_IMAGE) make

test:
	$(MAKE) -C tests test

screenshots:
	$(MAKE) -C tests screenshots

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
