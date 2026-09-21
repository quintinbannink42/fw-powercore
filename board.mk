# PowerCore — standalone PDM on Hellen mega-mcu144 (STM32F767)
# Former FIRMWARE_ID / short name: hellen-pdm-razor
#
# Layout mirrors rusefi/fw-custom-example + Hellen 144 common makefile.

include $(BOARD_DIR)/firmware/firmware.mk

BOARDINC += $(BOARD_DIR)/generated/controllers/generated
BOARDINC += $(BOARD_DIR)/firmware

include $(BOARD_DIR)/meta-info.env

BOARDCPPSRC = $(BOARD_DIR)/board_configuration.cpp \
	$(BOARD_DIR)/firmware/pdm_efuse.cpp

# Hellen mega-mcu144 shared flags (critical LED, HELLEN_BOARD_MM144, board ID)
BOARDS_DIR = $(BOARD_DIR)/ext/rusefi/firmware/config/boards
include $(BOARDS_DIR)/hellen/hellen-common144.mk

DDEFS += -DFIRMWARE_ID=\"powercore\"
DDEFS += -DDEFAULT_ENGINE_TYPE=engine_type_e::MINIMAL_PINS

# Reserve a gpiochip slot for the PDM e-fuse bank (PROTECTED_PIN_0..11).
# TLE9104_COUNT increments BOARD_EXT_GPIOCHIPS; hellen does not auto-add the IC
# (see initSmartGpio: "No official boards have this IC").
DDEFS += -DBOARD_TLE9104_COUNT=1

# Skip unused engine weight where safe for a PDM
DDEFS += -DEFI_WIDEBAND_FIRMWARE_UPDATE=FALSE
DDEFS += -DEFI_MAIN_RELAY_CONTROL=FALSE
# EFI_LTFT_CONTROL FALSE is declared in prepend.txt and lifted into DDEFS by
# firmware/Makefile (TS page guard). Do not set it here — tunerstudio.cpp
# static_asserts it matches generated LTFT_PAGE_ENABLED.
