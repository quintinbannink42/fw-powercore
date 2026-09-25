#!/usr/bin/env python3
"""Add TunerStudio hide-gates the shared rusEFI template does not ship.

PowerCore is a PDM. Menu lines for limits, trigger, check-engine, the empty
Setup Outputs page, CAN VSS/EGT/MS IO-Box, traction timing, idle-flow, and
the engine live-data View list are unconditional in tunerstudio.template.ini.
This script appends the existing @@if_ tokens (prepend.txt already sets them
false) and restores nothing itself — gen_config_pdm.sh puts the template back.

The script is idempotent: a string that is already gated is left alone.
"""

from __future__ import annotations

import sys
from pathlib import Path

ENGINE = "@@if_ts_show_engine_control"
SD = "@@if_ts_show_sd_card"


def once(text: str, old: str, new: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise SystemExit(f"gate_pdm_ts_menus: pattern not found:\n{old}")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"gate_pdm_ts_menus: expected 1 match, found {count} for:\n{old}")
    return text.replace(old, new, 1)


def gate_line(text: str, line: str, token: str) -> str:
    gated = line + token
    # The bare line is a prefix of the gated line, so a second run must not
    # append the token again.
    if gated in text and text.count(line) == text.count(gated):
        return text
    return once(text, line, gated)


def main() -> None:
    path = Path(sys.argv[1])
    text = path.read_text()

    # Setup: limits, trigger, check engine, empty Outputs. Keep vehicle info + status LEDs.
    setup_old = """\
\t\tgroupMenu = "Limits and protection"
\t\t\tgroupChildMenu = limitsAndFallback,\t\t\t"Limits and fallbacks"
\t\t\tgroupChildMenu = lowOilPressureProtection,\t"Low oil pressure protection"
\t\t\tgroupChildMenu = highOilPressureProtection,\t"High oil pressure protection"
\t\t\tgroupChildMenu = lambdaProtection,\t\t\t"Lambda protection", 0, { isInjectionEnabled }
\t\t\tgroupChildMenu = misfireDetectionDialog,\t"Misfire Detection"
"""
    setup_new = "\n".join(line + ENGINE for line in setup_old.splitlines()) + "\n"
    text = once(text, setup_old, setup_new)

    text = gate_line(text, '\t\tsubMenu = triggerConfiguration,\t\t"Trigger"', ENGINE)
    text = gate_line(text, '\t\tsubMenu = malfunctionDialog, "Check Engine Settings"', ENGINE)
    # Separator that only existed to split check-engine from the duplicate Outputs page.
    # Include the trailing newline so a second run does not treat the bare line as a prefix.
    text = once(
        text,
        '\t\tsubMenu = ignitionInputDialog,\t\t"Ignition key input Settings"@@if_ts_show_ign_key_menu\n\t\tsubMenu = std_separator\n',
        '\t\tsubMenu = ignitionInputDialog,\t\t"Ignition key input Settings"@@if_ts_show_ign_key_menu\n\t\tsubMenu = std_separator' + ENGINE + '\n',
    )
    text = gate_line(text, '\t\tsubMenu = outputsDialog,\t\t\t"Outputs"', ENGINE)

    # CAN settings + sniffer stay. VSS / EGT / MS IO-Box share the parent CAN flag today.
    text = once(
        text,
        '\t\tsubMenu = std_separator@@if_ts_show_top_level_can_menu\n\n\t\tsubMenu = speedSensorCan,\t\t\t"CAN Vehicle speed sensor"@@if_ts_show_top_level_can_menu\n\t\tsubMenu = uegoCan,\t\t\t\t\t"CAN O2 sensors"@@if_ts_show_wbo_can_menu\n\t\tsubMenu = egtInputsCan,\t\t\t\t"CAN EGT sensors"@@if_ts_show_top_level_can_menu\n\t\tsubMenu = msIoBox,\t\t\t\t\t"CAN MS IO-Box Settings"@@if_ts_show_top_level_can_menu',
        '\t\tsubMenu = std_separator' + ENGINE + '\n\n\t\tsubMenu = speedSensorCan,\t\t\t"CAN Vehicle speed sensor"' + ENGINE + '\n\t\tsubMenu = uegoCan,\t\t\t\t\t"CAN O2 sensors"@@if_ts_show_wbo_can_menu\n\t\tsubMenu = egtInputsCan,\t\t\t\t"CAN EGT sensors"' + ENGINE + '\n\t\tsubMenu = msIoBox,\t\t\t\t\t"CAN MS IO-Box Settings"' + ENGINE,
    )

    text = gate_line(text, '\t\tsubMenu = tractionTimingTableTbl,    "Traction Control Timing Adjustment"', ENGINE)
    text = gate_line(text, '\t\tsubMenu = tractionIgnitionSkipDialog,    "Traction Control Skip Ignition"', ENGINE)
    text = once(
        text,
        '\t\tsubMenu = tractionIgnitionSkipDialog,    "Traction Control Skip Ignition"@@if_ts_show_engine_control\n\t\tsubMenu = std_separator\n',
        '\t\tsubMenu = tractionIgnitionSkipDialog,    "Traction Control Skip Ignition"@@if_ts_show_engine_control\n\t\tsubMenu = std_separator' + ENGINE + '\n',
    )
    text = gate_line(text, '\t\tsubMenu = idleFlowEstimate, "Idle flow estimate", { modeledFlowIdle }', ENGINE)
    text = gate_line(
        text,
        '\t\tsubMenu = idleAirmassTimingEquivalence, "Idle airmass/timing equivalence", { modeledFlowIdle }',
        ENGINE,
    )

    # View's header is already @@if_ts_show_live_data. The generated live-data
    # list is not, so it would attach to Help when the header is dropped.
    text = once(
        text,
        '@@if_ts_show_live_data\n@@LIVE_DATA_MENU_FROM_FILE@@\n',
        '@@if_ts_show_live_data\n@@if_block ts_show_live_data\n@@LIVE_DATA_MENU_FROM_FILE@@\n@@endif_block\n',
    )

    # Battery page is PDM VBATT. Alternator settings are engine chrome on that dialog.
    text = gate_line(text, '\t\tpanel = alternatorSettings', "@@if_ts_show_alternator")
    text = once(
        text,
        'dialog = energySystems, "Battery and Alternator Settings"',
        'dialog = energySystems, "Battery / VBATT"',
    )
    text = gate_line(text, '\t\tfield = "Displacement",\t\t\t\t\t\tdisplacement', ENGINE)

    # Front-page engine indicators. Config Error / burn / warning stay.
    for line in (
        '\tindicator =\t\t\t\t\t{ checkEngine },\t"No Check Engine",\t\t"Check Engine",  white,  black,\tred,  black',
        '\tindicator =\t\t\t\t\t{ isTriggerError},\t\t\t"Trigger OK",\t\t"Trigger ERR",  white,  black,\tred,  black',
        '\tindicator =\t\t\t{ isIdling },\t\t\t"Not idling",\t\t\t\t"Idling",  white,  black,  green,  black',
        '\tindicator =\t\t\t\t{ isIdleCoasting },\t\t"Not coasting",\t\t\t"Coasting",  white,  black,  green,  black',
        '\tindicator =\t\t\t\t\t{ dfcoActive },  "Not decel fuel cut",\t"Decel fuel cut",  white,  black, yellow,  black',
        '\tindicator =\t\t\t\t{ isAboveAccelThreshold },\t\t"No TPS accel",  "TPS accel active",  white,  black, yellow,  black',
        '\tindicator =\t\t\t\t\t{ isTpsError },\t\t\t\t"TPS OK",\t\t\t"TPS error",\twhite, black,\tred,  black',
        '\tindicator =\t\t\t\t\t{ isCltError },\t\t\t\t"CLT OK",\t\t\t"CLT error",  white,  black,\tred,  black',
        '\tindicator =\t\t\t\t\t{ isFlexError },\t\t\t\t"Flex OK",\t\t\t"Flex error",  white,  black,\tred,  black',
        '\tindicator =\t\t{ isTuningNow },\t\t"",\t\t"Tuning Detected",  white,  black,  green,  black',
        '\tindicator =\t\t\t{ etb1etbRevLimitActive },\t"No ETB RPM Limit",\t"ETB RPM Limit",  white,  black, yellow,  black',
        '\tindicator = { wb1hasFault }, "WBO0: Ok", { WBO0: bitStringValue(wboStateList, wb1stateCode) }, white, black, red, black',
    ):
        text = gate_line(text, line, ENGINE)

    for line in (
        '\tindicator =\t\t\t\t\t{ @#OUTPUT_CHANNEL_SD_PRESENT#@ },\t\t\t"No SD card",\t"SD card present",  white,  black,  green,  black',
        '\tindicator =\t\t{ @#OUTPUT_CHANNEL_SD_LOGGING_INTERNAL#@ },\t\t"",\t\t"ECU Mode",  white,  black,  green,  black',
        '\tindicator =\t\t\t\t\t\t\t{ @#OUTPUT_CHANNEL_SD_MSD#@ },\t\t\t"",\t\t\t\t"PC Mode",  white,  black,  green,  black',
    ):
        text = gate_line(text, line, SD)

    path.write_text(text)


if __name__ == "__main__":
    main()
