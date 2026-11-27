# =============================================================================
# paradox — Daisy-bootloader firmware for Hermetic Modular Alchemy Lab (V2)
#
# Standard Daisy workflow (libDaisy core Makefile underneath):
#   make libdaisy       — build lib/libDaisy once after cloning
#   make                — build firmware
#   make program-dfu    — flash over USB (module in DFU mode first; see README)
#   make clean          — remove the build tree
# =============================================================================

TARGET = paradox

ALCHEMY_DIR  = lib/alchemy-sdk
LIBDAISY_DIR = lib/libDaisy

# ── App sources — yours to edit ─────────────────────────────────────────────
CPP_SOURCES = \
    src/paradox.cpp \
    src/paradox_dsp.cpp

# ── Alchemy SDK, compiled straight from the submodule ───────────────────────
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/v2/src/*.cpp))

C_INCLUDES += \
    -Isrc \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/v2/include

C_DEFS += -DALCHEMY_BOARD_V2

# ── Daisy bootloader build (BOOT_SRAM) ──────────────────────────────────────
APP_TYPE = BOOT_SRAM
LDSCRIPT = $(ALCHEMY_DIR)/cmake/linkers/alchemy_stm32h750ib_sram.lds

# The Alchemy SDK requires C++17 (libDaisy's default is gnu++14).
CPP_STANDARD = -std=gnu++17

# ── libDaisy core Makefile does the rest ────────────────────────────────────
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: libdaisy program-live
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

# ── Flash without touching the module ───────────────────────────────────────
USBPID ?= df11

program-live: all
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs reboot bootloader
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)
