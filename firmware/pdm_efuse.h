#pragma once

#include "protected_gpio.h"
#include "pdm_efuse_sm.h"

/**
 * PowerCore e-fuse gpio chip: rusEFI protected_gpio pattern (OutputPin + ISENSE
 * + gpiochip_register at PROTECTED_PIN_*) with the Razor-style SM filled in.
 *
 * 12 channels: HP1-4 + ADIO1-8. Reads engineConfiguration->pdmChannelTrip[i].
 * Do not also call protectedGpio_add() — this chip owns PROTECTED_PIN_0..11.
 */
int pdmEfuse_add(brain_pin_e base, const ProtectedGpioConfig* configs);
void pdmEfuse_check();
float pdmEfuse_getCurrentA(size_t channel);
PdmEfuseState pdmEfuse_getState(size_t channel);
PdmEfuseTripReason pdmEfuse_getReason(size_t channel);
