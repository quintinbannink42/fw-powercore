#pragma once

#include <cstddef>
#include <cstdint>

/**
 * PowerCore CAN consume (rusEFI ECU verbose broadcast) + status publish.
 * Drives e-fuse protected pins for pump/fan examples when pdmCanConsumeEnable.
 */
void pdmCan_initHardware();
void pdmCan_onStartHardware();
void pdmCan_onStopHardware();
void pdmCan_onConfigurationChange(const struct engine_configuration_s* previousConfiguration);
void pdmCan_configOverrides();
void pdmCan_periodicFast();
void pdmCan_periodicSlow();

#if EFI_CAN_SUPPORT
#include "can.h"
void pdmCan_onRx(size_t busIndex, const CANRxFrame& frame, efitick_t nowNt);
void pdmCan_updateDash(CanCycle cycle);
#endif
