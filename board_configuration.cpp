/**
 * @file board_configuration.cpp
 * @brief PowerCore — standalone Hellen mega-mcu144 PDM (STM32F767ZI)
 *
 * Product: PowerCore (not an engine ECU). Former stub name: hellen-pdm-razor.
 * I/O target: Link Razor PDM parity
 *   - 4x high-power (25 A cont / 80 A peak class)
 *   - 8x ADIO (8 A high-side + analog/digital)
 *
 * Protection: rusEFI protected_gpio pattern + PowerCore e-fuse SM
 * (inrush window / delayed OC / fast short / retry / latch) reading
 * engineConfiguration->pdmChannelTrip[]. Do NOT invent a parallel GPIO layer.
 *
 * Pin map: hellen-pdm-razor/PINMAP.md (authoritative)
 */

#include "pch.h"

#include "hellen_meta.h"
#include "pdm_efuse.h"
#include "board_overrides.h"

// LED pins: provided by hellen_leds_144.cpp (hellen-common144.mk)

// BOM starters from HARDWARE_BOM.md (kILIS/RIS). Bench cal waits for first silicon.
// HP: BTS50010-1TAD, kILIS≈52100, RIS=2.7k → ≈19.3 A/V
static constexpr float HP_AMPS_PER_VOLT = 19.3f;   // TODO: two-point calibrate on first silicon
// ADIO: BTS7004-1EPP, kILIS≈20000, RIS=4.7k → ≈4.26 A/V
static constexpr float ADIO_AMPS_PER_VOLT = 4.26f; // TODO: calibrate per channel on first silicon

static constexpr float HP_MAX_CURRENT_A = 60.0f;   // Razor measurable class (OC default)
static constexpr float ADIO_MAX_CURRENT_A = 10.0f; // Razor ADIO trip class
static constexpr float HP_FAST_SHORT_A = 80.0f;    // fallback if tune inrush is 0
static constexpr float ADIO_FAST_SHORT_A = 20.0f;

// 12-channel e-fuse bank = HP1-4 + ADIO1-8 (PINMAP freeze)
// ADIO6-8 ISENSE (H144_IN_RES1/2/3) are not EFI_ADC yet — SM still owns the
// enable pins (retry/latch); software OC waits on ADC3 analog.
static const ProtectedGpioConfig kProtectedCfgs[] = {
	{ Gpio::H144_OUT_PWM1, H144_IN_AUX1_ANALOG, HP_AMPS_PER_VOLT, HP_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM2, H144_IN_AUX2_ANALOG, HP_AMPS_PER_VOLT, HP_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM3, H144_IN_AUX3_ANALOG, HP_AMPS_PER_VOLT, HP_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM4, H144_IN_AUX4_ANALOG, HP_AMPS_PER_VOLT, HP_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM5, H144_IN_MAP1, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM6, H144_IN_MAP2, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM7, H144_IN_MAP3, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_PWM8, H144_IN_O2S,  ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_IO5,  H144_IN_O2S2, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_IO6,  EFI_ADC_NONE, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_IO7,  EFI_ADC_NONE, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
	{ Gpio::H144_OUT_IO8,  EFI_ADC_NONE, ADIO_AMPS_PER_VOLT, ADIO_FAST_SHORT_A },
};
static_assert(efi::size(kProtectedCfgs) == 12);

static OutputPin adioPullUp[8];

static const Gpio kAdioPullUpPins[8] = {
	Gpio::H144_OUT_IO9,
	Gpio::H144_OUT_IO10,
	Gpio::H144_OUT_IO11,
	Gpio::H144_OUT_IO12,
	Gpio::H144_OUT_IO13,
	Gpio::H144_GP_IO1,
	Gpio::H144_GP_IO2,
	Gpio::H144_GP_IO3,
};

static void pdmBoardInitHardware() {
	setHellenEnPin(H144_GP8); // PE10 PWR_EN — required before analog reads

	pdmEfuse_add(Gpio::PROTECTED_PIN_0, kProtectedCfgs);

	for (size_t i = 0; i < efi::size(adioPullUp); i++) {
		char name[16];
		chsnprintf(name, sizeof(name), "ADIO%u-PU", (unsigned)(i + 1));
		adioPullUp[i].initPin(name, kAdioPullUpPins[i]);
		adioPullUp[i].setValue(false);
	}
}

static void pdmBoardPeriodicFast() {
	pdmEfuse_check();
}

static void pdmBoardDefaultConfiguration() {
	engineConfiguration->analogInputDividerCoefficient = 1.0f;
	engineConfiguration->adcVcc = 3.3f;

	// Carrier R1/R2 100k/10k → 11:1 (HARDWARE_BOM.md)
	engineConfiguration->vbattDividerCoeff = 11.0f;
	engineConfiguration->vbattAdcChannel = H144_IN_VBATT;

	// H144_CAN_* macros already include Gpio::
	engineConfiguration->canTxPin = H144_CAN_TX;
	engineConfiguration->canRxPin = H144_CAN_RX;

	// LUA_PWM_COUNT=8: PWM-capable protected bank HP1-4 + ADIO1-4
	engineConfiguration->luaOutputPins[0] = Gpio::PROTECTED_PIN_0; // HP1
	engineConfiguration->luaOutputPins[1] = Gpio::PROTECTED_PIN_1; // HP2
	engineConfiguration->luaOutputPins[2] = Gpio::PROTECTED_PIN_2; // HP3
	engineConfiguration->luaOutputPins[3] = Gpio::PROTECTED_PIN_3; // HP4
	engineConfiguration->luaOutputPins[4] = Gpio::PROTECTED_PIN_4; // ADIO1
	engineConfiguration->luaOutputPins[5] = Gpio::PROTECTED_PIN_5; // ADIO2
	engineConfiguration->luaOutputPins[6] = Gpio::PROTECTED_PIN_6; // ADIO3
	engineConfiguration->luaOutputPins[7] = Gpio::PROTECTED_PIN_7; // ADIO4

	// ADIO5-8 are on/off (not PWM on Razor). Default GPPWM 1-4 onto the second bank.
	engineConfiguration->gppwm[0].pin = Gpio::PROTECTED_PIN_8;  // ADIO5
	engineConfiguration->gppwm[1].pin = Gpio::PROTECTED_PIN_9;  // ADIO6
	engineConfiguration->gppwm[2].pin = Gpio::PROTECTED_PIN_10; // ADIO7
	engineConfiguration->gppwm[3].pin = Gpio::PROTECTED_PIN_11; // ADIO8

	// PDM, not an engine ECU — hide injection/ignition gated TunerStudio fields
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->isIgnitionEnabled = false;

	strcpy(engineConfiguration->engineMake, "PowerCore");
	strcpy(engineConfiguration->vehicleName, "PDM");

	// HP1-4 current via aux linear (volts * AmpsPerVolt). Cal blocked on first silicon.
	engineConfiguration->auxLinear1.hwChannel = H144_IN_AUX1_ANALOG;
	engineConfiguration->auxLinear1.v1 = 0;
	engineConfiguration->auxLinear1.value1 = 0;
	engineConfiguration->auxLinear1.v2 = 1;
	engineConfiguration->auxLinear1.value2 = HP_AMPS_PER_VOLT;
	engineConfiguration->auxLinear2.hwChannel = H144_IN_AUX2_ANALOG;
	engineConfiguration->auxLinear2.v1 = 0;
	engineConfiguration->auxLinear2.value1 = 0;
	engineConfiguration->auxLinear2.v2 = 1;
	engineConfiguration->auxLinear2.value2 = HP_AMPS_PER_VOLT;
	engineConfiguration->auxLinear3.hwChannel = H144_IN_AUX3_ANALOG;
	engineConfiguration->auxLinear3.v1 = 0;
	engineConfiguration->auxLinear3.value1 = 0;
	engineConfiguration->auxLinear3.v2 = 1;
	engineConfiguration->auxLinear3.value2 = HP_AMPS_PER_VOLT;
	engineConfiguration->auxLinear4.hwChannel = H144_IN_AUX4_ANALOG;
	engineConfiguration->auxLinear4.v1 = 0;
	engineConfiguration->auxLinear4.value1 = 0;
	engineConfiguration->auxLinear4.v2 = 1;
	engineConfiguration->auxLinear4.value2 = HP_AMPS_PER_VOLT;

	// ADIO current-sense ADCs (Lua analog). ADIO6-8 RES pins are not EFI_ADC yet.
	engineConfiguration->auxAnalogInputs[0] = H144_IN_MAP1;  // ADIO1
	engineConfiguration->auxAnalogInputs[1] = H144_IN_MAP2;  // ADIO2
	engineConfiguration->auxAnalogInputs[2] = H144_IN_MAP3;  // ADIO3
	engineConfiguration->auxAnalogInputs[3] = H144_IN_O2S;   // ADIO4
	engineConfiguration->auxAnalogInputs[4] = H144_IN_O2S2;  // ADIO5

	for (size_t i = 0; i < 4; i++) {
		engineConfiguration->pdmChannelTrip[i].inrushLimitA = 80.0f;
		engineConfiguration->pdmChannelTrip[i].ocLimitA = HP_MAX_CURRENT_A;
		engineConfiguration->pdmChannelTrip[i].inrushWindowMs = 50;
		engineConfiguration->pdmChannelTrip[i].tripTimeMs = 20;
		engineConfiguration->pdmChannelTrip[i].retryCount = 3;
		engineConfiguration->pdmChannelTrip[i].latchOnFault = false;
	}
	for (size_t i = 4; i < efi::size(engineConfiguration->pdmChannelTrip); i++) {
		engineConfiguration->pdmChannelTrip[i].inrushLimitA = 20.0f;
		engineConfiguration->pdmChannelTrip[i].ocLimitA = ADIO_MAX_CURRENT_A;
		engineConfiguration->pdmChannelTrip[i].inrushWindowMs = 20;
		engineConfiguration->pdmChannelTrip[i].tripTimeMs = 10;
		engineConfiguration->pdmChannelTrip[i].retryCount = 3;
		engineConfiguration->pdmChannelTrip[i].latchOnFault = false;
	}

	engineConfiguration->pdmCanConsumeEnable = false;
	engineConfiguration->pdmCanStatusEnable = false;
	engineConfiguration->pdmCanConsumeBaseId = 0x200;
	engineConfiguration->pdmCanStatusBaseId = 0x240;

	// Optional HP DIR: H144_OUT_IO1..IO4 — assign when bridge stage is fitted
}

void setup_custom_board_overrides() {
	custom_board_InitHardware = pdmBoardInitHardware;
	custom_board_DefaultConfiguration = pdmBoardDefaultConfiguration;
	custom_board_periodicFastCallback = pdmBoardPeriodicFast;
}
