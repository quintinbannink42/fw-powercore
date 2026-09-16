/**
 * @file board_configuration.cpp
 * @brief PowerCore — standalone Hellen mega-mcu144 PDM (STM32F767ZI)
 *
 * Product: PowerCore (not an engine ECU). Former stub name: hellen-pdm-razor.
 * I/O target: Link Razor PDM parity
 *   - 4x high-power (25 A cont / 80 A peak class)
 *   - 8x ADIO (8 A high-side + analog/digital)
 *
 * Protection: rusEFI protected_gpio (instantaneous trip). Full Razor
 * inrush/retry/latch state machine is TODO on top of this — do NOT invent
 * a parallel protection layer.
 *
 * Pin map: /workspace/hellen-pdm-razor/PINMAP.md (authoritative)
 */

#include "pch.h"

#include "hellen_meta.h"
#include "protected_gpio.h"
#include "board_overrides.h"

Gpio getCommsLedPin() {
	return H144_LED2_GREEN;  // Gpio::G1
}

Gpio getWarningLedPin() {
	return H144_LED3_BLUE;   // Gpio::E7
}

Gpio getRunningLedPin() {
	return H144_LED4_YELLOW; // Gpio::E8
}

// BOM starters from HARDWARE_BOM.md (kILIS/RIS). Bench cal waits for first silicon.
// HP: BTS50010-1TAD, kILIS≈52100, RIS=2.7k → ≈19.3 A/V
static constexpr float HP_AMPS_PER_VOLT = 19.3f;   // TODO: two-point calibrate on first silicon
// ADIO: BTS7004-1EPP, kILIS≈20000, RIS=4.7k → ≈4.26 A/V
static constexpr float ADIO_AMPS_PER_VOLT = 4.26f; // TODO: calibrate per channel on first silicon

static constexpr float HP_MAX_CURRENT_A = 60.0f;   // Razor measurable class
static constexpr float ADIO_MAX_CURRENT_A = 10.0f; // Razor ADIO trip class

// protected_gpio: 8 channels = HP1-4 + ADIO1-4 (current sense) — PINMAP freeze
// ADIO5-8: plain outs in rev1; V-sense ADCs still wired for input mode / logging
static const ProtectedGpioConfig kProtectedCfgs[] = {
	{ Gpio::H144_OUT_PWM1, H144_IN_AUX1_ANALOG, HP_AMPS_PER_VOLT, HP_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM2, H144_IN_AUX2_ANALOG, HP_AMPS_PER_VOLT, HP_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM3, H144_IN_AUX3_ANALOG, HP_AMPS_PER_VOLT, HP_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM4, H144_IN_AUX4_ANALOG, HP_AMPS_PER_VOLT, HP_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM5, H144_IN_MAP1, ADIO_AMPS_PER_VOLT, ADIO_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM6, H144_IN_MAP2, ADIO_AMPS_PER_VOLT, ADIO_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM7, H144_IN_MAP3, ADIO_AMPS_PER_VOLT, ADIO_MAX_CURRENT_A },
	{ Gpio::H144_OUT_PWM8, H144_IN_O2S,  ADIO_AMPS_PER_VOLT, ADIO_MAX_CURRENT_A },
};

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

	protectedGpio_add(Gpio::PROTECTED_PIN_0, kProtectedCfgs);

	for (size_t i = 0; i < efi::size(adioPullUp); i++) {
		char name[16];
		chsnprintf(name, sizeof(name), "ADIO%u-PU", (unsigned)(i + 1));
		adioPullUp[i].initPin(name, kAdioPullUpPins[i]);
		adioPullUp[i].setValue(false);
	}
}

static void pdmBoardDefaultConfiguration() {
	engineConfiguration->analogInputDividerCoefficient = 1.0f;
	engineConfiguration->adcVcc = 3.3f;

	// Carrier R1/R2 100k/10k → 11:1 (HARDWARE_BOM.md)
	engineConfiguration->vbattDividerCoeff = 11.0f;
	engineConfiguration->vbattAdcChannel = H144_IN_VBATT;

	engineConfiguration->canTxPin = Gpio::H144_CAN_TX;
	engineConfiguration->canRxPin = Gpio::H144_CAN_RX;

	// Protected: HP1-4, ADIO1-4
	engineConfiguration->auxOutputPins[0] = Gpio::PROTECTED_PIN_0;
	engineConfiguration->auxOutputPins[1] = Gpio::PROTECTED_PIN_1;
	engineConfiguration->auxOutputPins[2] = Gpio::PROTECTED_PIN_2;
	engineConfiguration->auxOutputPins[3] = Gpio::PROTECTED_PIN_3;
	engineConfiguration->auxOutputPins[4] = Gpio::PROTECTED_PIN_4;
	engineConfiguration->auxOutputPins[5] = Gpio::PROTECTED_PIN_5;
	engineConfiguration->auxOutputPins[6] = Gpio::PROTECTED_PIN_6;
	engineConfiguration->auxOutputPins[7] = Gpio::PROTECTED_PIN_7;

	// ADIO5-8 enables (protection bank TODO)
	engineConfiguration->auxOutputPins[8] = Gpio::H144_OUT_IO5;
	engineConfiguration->auxOutputPins[9] = Gpio::H144_OUT_IO6;
	engineConfiguration->auxOutputPins[10] = Gpio::H144_OUT_IO7;
	engineConfiguration->auxOutputPins[11] = Gpio::H144_OUT_IO8;

	// Optional HP DIR: H144_OUT_IO1..IO4 — assign when bridge stage is fitted
}

void setup_custom_board_overrides() {
	custom_board_InitHardware = pdmBoardInitHardware;
	custom_board_DefaultConfiguration = pdmBoardDefaultConfiguration;
}
