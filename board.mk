# PowerCore — standalone PDM on Hellen mega-mcu144 (STM32F767)
# Former FIRMWARE_ID / short name: hellen-pdm-razor
#
# Layout mirrors rusefi/fw-custom-example + Hellen 144 common makefile.

include $(BOARD_DIR)/firmware/firmware.mk

BOARDINC += $(BOARD_DIR)/generated/controllers/generated

include $(BOARD_DIR)/meta-info.env

BOARDCPPSRC = $(BOARD_DIR)/board_configuration.cpp

# Hellen mega-mcu144 shared flags (critical LED, HELLEN_BOARD_MM144, board ID)
BOARDS_DIR = $(BOARD_DIR)/ext/rusefi/firmware/config/boards
include $(BOARDS_DIR)/hellen/hellen-common144.mk

DDEFS += -DFIRMWARE_ID=\"powercore\"
DDEFS += -DDEFAULT_ENGINE_TYPE=engine_type_e::MINIMAL_PINS

# Skip unused engine weight where safe for a PDM
DDEFS += -DEFI_WIDEBAND_FIRMWARE_UPDATE=FALSE
DDEFS += -DEFI_MAIN_RELAY_CONTROL=FALSE
