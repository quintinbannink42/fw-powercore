/**
 * @file pdm_efuse.cpp
 * @brief PowerCore electronic-fuse gpio chip on the rusEFI protected_gpio pattern.
 *
 * rusEFI protected_gpio.cpp still has "// TODO: smarter state machine". This
 * board chip is that SM: same OutputPin + adcGetRawVoltage + gpiochip_register
 * at PROTECTED_PIN_*, reading TunerStudio pdmChannelTrip[] instead of a compile
 * time MaximumAllowedCurrent. Not a parallel GPIO layer.
 */

#include "pch.h"

#include "pdm_efuse.h"
#include "gpio/gpio_ext.h"

static constexpr size_t kPdmEfuseChannels = 12;

static const char* kPdmChannelNames[kPdmEfuseChannels] = {
	"HP1", "HP2", "HP3", "HP4",
	"ADIO1", "ADIO2", "ADIO3", "ADIO4",
	"ADIO5", "ADIO6", "ADIO7", "ADIO8",
};

class PdmEfuseGpio {
public:
	int setPadMode(iomode_t mode);
	int set(bool value);
	int get() const;
	brain_pin_diag_e getDiag() const;

	void configure(size_t index, const ProtectedGpioConfig& pinCfg);
	void check(uint32_t nowMs);

	float currentA() const { return m_amps; }
	PdmEfuseState state() const { return m_sm.state(); }
	PdmEfuseTripReason reason() const { return m_sm.reason(); }

private:
	PdmEfuseTrip loadTrip() const;
	bool apply(uint32_t nowMs);

	OutputPin m_output;
	PdmEfuseChannel m_sm;
	const ProtectedGpioConfig* m_config = nullptr;
	size_t m_index = 0;
	bool m_requestedOn = false;
	float m_amps = 0;
	bool m_loggedTrip = false;
};

void PdmEfuseGpio::configure(size_t index, const ProtectedGpioConfig& pinCfg) {
	m_index = index;
	m_config = &pinCfg;
	m_sm.reset();
	m_requestedOn = false;
	m_amps = 0;
	m_loggedTrip = false;
}

int PdmEfuseGpio::setPadMode(iomode_t mode) {
	if (!m_config) {
		return -1;
	}

	if (mode == PAL_MODE_OUTPUT_PUSHPULL) {
		m_output.initPin(kPdmChannelNames[m_index], m_config->Pin);
	} else {
		m_output.deInit();
	}

	return 0;
}

int PdmEfuseGpio::set(bool value) {
	if (!m_config) {
		return -1;
	}

	m_requestedOn = value;
	apply(getTimeNowMs());
	return 0;
}

int PdmEfuseGpio::get() const {
	return m_output.getLogicValue();
}

void PdmEfuseGpio::check(uint32_t nowMs) {
	if (!m_config) {
		return;
	}
	apply(nowMs);
}

PdmEfuseTrip PdmEfuseGpio::loadTrip() const {
	PdmEfuseTrip trip;
	if (m_index >= efi::size(engineConfiguration->pdmChannelTrip)) {
		return trip;
	}

	const auto& src = engineConfiguration->pdmChannelTrip[m_index];
	trip.inrushLimitA = src.inrushLimitA;
	trip.ocLimitA = src.ocLimitA;
	trip.inrushWindowMs = src.inrushWindowMs;
	trip.tripTimeMs = src.tripTimeMs;
	trip.retryCount = src.retryCount;
	trip.latchOnFault = src.latchOnFault;

	// Compile-time class (MaximumAllowedCurrent) is the last-resort fast-short
	// ceiling if the tune left inrush at 0.
	if (trip.inrushLimitA <= 0 && m_config) {
		trip.inrushLimitA = m_config->MaximumAllowedCurrent;
	}
	return trip;
}

bool PdmEfuseGpio::apply(uint32_t nowMs) {
	const bool hasSense = isAdcChannelValid(m_config->SenseChannel);
	bool senseValid = true;
	float amps = 0;

	if (hasSense) {
		auto senseVolts = adcGetRawVoltage("pdm_efuse", m_config->SenseChannel);
		if (senseVolts) {
			amps = senseVolts.value_or(0) * m_config->AmpsPerVolt;
		} else {
			senseValid = false;
		}
	}

	m_amps = amps;
	const bool drive = m_sm.tick(m_requestedOn, amps, senseValid, loadTrip(), nowMs);
	m_output.setValue(drive);

	if (m_sm.isFaulted()) {
		if (!m_loggedTrip) {
			efiPrintf("PDM e-fuse %s trip reason=%u retries=%u",
				kPdmChannelNames[m_index],
				static_cast<unsigned>(m_sm.reason()),
				static_cast<unsigned>(m_sm.retriesUsed()));
			m_loggedTrip = true;
		}
	} else {
		m_loggedTrip = false;
	}

	return drive;
}

brain_pin_diag_e PdmEfuseGpio::getDiag() const {
	if (m_sm.isFaulted()) {
		return PIN_OVERLOAD;
	}
	return PIN_OK;
}

class PdmEfuseGpios : public GpioChip {
public:
	int init() override { return 0; }
	int setPadMode(size_t pin, iomode_t mode) override;
	int writePad(size_t pin, int value) override;
	int readPad(size_t pin) override;
	brain_pin_diag_e getDiag(size_t pin) override;

	void configure(const ProtectedGpioConfig* configs);
	void check();

	float currentA(size_t pin) const;
	PdmEfuseState state(size_t pin) const;
	PdmEfuseTripReason reason(size_t pin) const;

private:
	PdmEfuseGpio m_channels[kPdmEfuseChannels];
};

int PdmEfuseGpios::setPadMode(size_t pin, iomode_t mode) {
	if (pin >= kPdmEfuseChannels) {
		return -1;
	}
	return m_channels[pin].setPadMode(mode);
}

int PdmEfuseGpios::writePad(size_t pin, int value) {
	if (pin >= kPdmEfuseChannels) {
		return -1;
	}
	return m_channels[pin].set(value != 0);
}

int PdmEfuseGpios::readPad(size_t pin) {
	if (pin >= kPdmEfuseChannels) {
		return -1;
	}
	return m_channels[pin].get();
}

brain_pin_diag_e PdmEfuseGpios::getDiag(size_t pin) {
	if (pin >= kPdmEfuseChannels) {
		return PIN_UNKNOWN;
	}
	return m_channels[pin].getDiag();
}

void PdmEfuseGpios::configure(const ProtectedGpioConfig* configs) {
	for (size_t i = 0; i < kPdmEfuseChannels; i++) {
		m_channels[i].configure(i, configs[i]);
	}
}

void PdmEfuseGpios::check() {
	const uint32_t nowMs = getTimeNowMs();
	for (size_t i = 0; i < kPdmEfuseChannels; i++) {
		m_channels[i].check(nowMs);
	}

	// ADIO1-8 currents feed the existing Lua gauges (TunerStudio PDM front page).
	for (size_t i = 0; i < LUA_GAUGE_COUNT && (i + 4) < kPdmEfuseChannels; i++) {
		engine->outputChannels.luaGauges[i] = m_channels[i + 4].currentA();
	}
}

float PdmEfuseGpios::currentA(size_t pin) const {
	if (pin >= kPdmEfuseChannels) {
		return 0;
	}
	return m_channels[pin].currentA();
}

PdmEfuseState PdmEfuseGpios::state(size_t pin) const {
	if (pin >= kPdmEfuseChannels) {
		return PdmEfuseState::Off;
	}
	return m_channels[pin].state();
}

PdmEfuseTripReason PdmEfuseGpios::reason(size_t pin) const {
	if (pin >= kPdmEfuseChannels) {
		return PdmEfuseTripReason::None;
	}
	return m_channels[pin].reason();
}

static PdmEfuseGpios s_efuse;
static bool s_didInit = false;

int pdmEfuse_add(brain_pin_e base, const ProtectedGpioConfig* configs) {
	s_efuse.configure(configs);

	int result = gpiochip_register(base, "protected", s_efuse, kPdmEfuseChannels);
	gpiochips_setPinNames(base, kPdmChannelNames);

	if (result == static_cast<int>(base)) {
		s_didInit = true;
	} else {
		efiPrintf("PDM e-fuse gpiochip_register failed: %d", result);
	}

	return result;
}

void pdmEfuse_check() {
	if (s_didInit) {
		s_efuse.check();
	}
}

float pdmEfuse_getCurrentA(size_t channel) {
	return s_efuse.currentA(channel);
}

PdmEfuseState pdmEfuse_getState(size_t channel) {
	return s_efuse.state(channel);
}

PdmEfuseTripReason pdmEfuse_getReason(size_t channel) {
	return s_efuse.reason(channel);
}
