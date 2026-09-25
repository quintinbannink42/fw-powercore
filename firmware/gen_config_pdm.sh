#!/usr/bin/env bash
# rusEFI CUSTOM_GEN_CONFIG for PowerCore.
# Gates engine-only TunerStudio lines the shared template does not flag,
# regenerates the INI, then restores tunerstudio.template.ini so the
# rusEFI submodule stays at the pinned commit.

set -euo pipefail

BOARD_ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUSEFI_FW="$BOARD_ROOT/ext/rusefi/firmware"
TEMPLATE="$RUSEFI_FW/tunerstudio/tunerstudio.template.ini"
SHORT_BOARD_NAME=${SHORT_BOARD_NAME:-powercore}

if [ ! -f "$TEMPLATE" ]; then
	echo "gen_config_pdm.sh: missing $TEMPLATE (git submodule update --init ext/rusefi)" >&2
	exit 1
fi

if [ -z "${META_OUTPUT_ROOT_FOLDER:-}" ]; then
	export META_OUTPUT_ROOT_FOLDER
	META_OUTPUT_ROOT_FOLDER=$(realpath --relative-to="$RUSEFI_FW" "$BOARD_ROOT/generated")/
fi

if [ -z "${AUTOMATION_REF:-}" ]; then
	export AUTOMATION_REF
	AUTOMATION_REF=$(git -C "$BOARD_ROOT" branch --show-current || true)
fi

# Start from the pinned template even if a previous run died mid-patch.
git -C "$BOARD_ROOT/ext/rusefi" checkout -- firmware/tunerstudio/tunerstudio.template.ini

backup=$(mktemp)
cp -a "$TEMPLATE" "$backup"
restore_template() {
	cp -a "$backup" "$TEMPLATE"
	rm -f "$backup"
}
trap restore_template EXIT

python3 "$BOARD_ROOT/firmware/gate_pdm_ts_menus.py" "$TEMPLATE"

# Same order as rusefi_config.mk: signature, then config_definition.
bash "$RUSEFI_FW/gen_signature.sh" "$SHORT_BOARD_NAME"
bash "$RUSEFI_FW/gen_config_board.sh" "$BOARD_ROOT" "$SHORT_BOARD_NAME"

# gen_config rewrites Java sources inside the rusEFI tree and drops a
# self-test XML next to the firmware makefile. The template patch is already
# consumed; put the submodule back to the pinned commit.
git -C "$BOARD_ROOT/ext/rusefi" checkout -- .
rm -f "$RUSEFI_FW/quick-self-test.xml"
