/**
 * @file pdm_can.cpp
 * @brief Consume rusEFI ECU verbose CAN and publish PowerCore channel status.
 *
 * RX: custom_board_can_rx at pdmCanConsumeBaseId (Status / Speeds / Sensors1).
 * Drive: OutputPin on PROTECTED_PIN_* so the e-fuse SM still latches/trips.
 * TX: custom_board_update_dash at pdmCanStatusBaseId (see powercore_pdm.dbc).
 */

#include "pch.h"

#include "pdm_can.h"
#include "pdm_can_logic.h"
#include "pdm_efuse.h"

#if EFI_CAN_SUPPORT
#include "can.h"
#include "can_msg_tx.h"
#endif

static constexpr size_t kOwnedCount = 5;

static const size_t kOwnedChannels[kOwnedCount] = {
	kPdmCanChHp1Pump,
	kPdmCanChHp2Fan,
	kPdmCanChHp3Fan2,
	kPdmCanChAdio1O2,
	kPdmCanChAdio5Relay,
};

static const Gpio kOwnedPins[kOwnedCount] = {
	Gpio::PROTECTED_PIN_0,
	Gpio::PROTECTED_PIN_1,
	Gpio::PROTECTED_PIN_2,
	Gpio::PROTECTED_PIN_4,
	Gpio::PROTECTED_PIN_8,
};

static const char* kOwnedNames[kOwnedCount] = {
	"HP1-pump",
	"HP2-fan",
	"HP3-fan2",
	"ADIO1-ego",
	"ADIO5-relay",
};

static OutputPin s_ownedPins[kOwnedCount];
static bool s_pinsClaimed = false;

static PdmCanEcuSignals s_ecu;
static bool s_haveRx = false;
static uint32_t s_lastRxMs = 0;
static uint8_t s_txSeq = 0;

#if EFI_CAN_SUPPORT
static const uint8_t* pdmCanFrameData(const CANRxFrame& frame) {
#ifdef STM32H7XX
	return frame.data.u8;
#else
	return frame.data8;
#endif
}

static uint8_t pdmCanFrameDlc(const CANRxFrame& frame) {
#ifdef STM32H7XX
	return frame.DLC;
#else
	return frame.DLC;
#endif
}
#endif

static void pdmCanReleasePins() {
	if (!s_pinsClaimed) {
		return;
	}
	for (size_t i = 0; i < kOwnedCount; i++) {
		s_ownedPins[i].deInit();
	}
	s_pinsClaimed = false;
}

static void pdmCanClaimPins() {
	if (s_pinsClaimed) {
		return;
	}
	for (size_t i = 0; i < kOwnedCount; i++) {
		s_ownedPins[i].initPin(kOwnedNames[i], kOwnedPins[i]);
		s_ownedPins[i].setValue(false);
	}
	s_pinsClaimed = true;
}

static void pdmCanUnassignConflictingOutputs() {
	engineConfiguration->luaOutputPins[0] = Gpio::Unassigned;
	engineConfiguration->luaOutputPins[1] = Gpio::Unassigned;
	engineConfiguration->luaOutputPins[2] = Gpio::Unassigned;
	engineConfiguration->luaOutputPins[4] = Gpio::Unassigned;
	engineConfiguration->gppwm[0].pin = Gpio::Unassigned;
}

static void pdmCanRestoreDefaultOutputsIfClear() {
	if (engineConfiguration->luaOutputPins[0] == Gpio::Unassigned) {
		engineConfiguration->luaOutputPins[0] = Gpio::PROTECTED_PIN_0;
	}
	if (engineConfiguration->luaOutputPins[1] == Gpio::Unassigned) {
		engineConfiguration->luaOutputPins[1] = Gpio::PROTECTED_PIN_1;
	}
	if (engineConfiguration->luaOutputPins[2] == Gpio::Unassigned) {
		engineConfiguration->luaOutputPins[2] = Gpio::PROTECTED_PIN_2;
	}
	if (engineConfiguration->luaOutputPins[4] == Gpio::Unassigned) {
		engineConfiguration->luaOutputPins[4] = Gpio::PROTECTED_PIN_4;
	}
	if (engineConfiguration->gppwm[0].pin == Gpio::Unassigned) {
		engineConfiguration->gppwm[0].pin = Gpio::PROTECTED_PIN_8;
	}
}

void pdmCan_configOverrides() {
	if (engineConfiguration->pdmCanConsumeEnable) {
		pdmCanUnassignConflictingOutputs();
	}
}

void pdmCan_onConfigurationChange(const engine_configuration_s* previousConfiguration) {
	const bool wasOn = previousConfiguration && previousConfiguration->pdmCanConsumeEnable;
	const bool nowOn = engineConfiguration->pdmCanConsumeEnable;
	if (wasOn && !nowOn) {
		s_haveRx = false;
		s_ecu = {};
	}
}

void pdmCan_onStopHardware() {
	pdmCanReleasePins();
	if (!engineConfiguration->pdmCanConsumeEnable) {
		pdmCanRestoreDefaultOutputsIfClear();
	}
}

void pdmCan_onStartHardware() {
	if (engineConfiguration->pdmCanConsumeEnable) {
		pdmCanClaimPins();
	} else {
		pdmCanReleasePins();
	}
}

void pdmCan_initHardware() {
	s_haveRx = false;
	s_ecu = {};
}

static bool pdmCanEcuIsAlive(uint32_t nowMs) {
	return pdmCanEcuAlive(s_haveRx, nowMs, s_lastRxMs, kPdmCanEcuTimeoutMs);
}

void pdmCan_periodicFast() {
	if (!engineConfiguration->pdmCanConsumeEnable) {
		return;
	}

	if (!s_pinsClaimed) {
		pdmCanClaimPins();
	}

	const uint32_t nowMs = getTimeNowMs();
	const bool alive = pdmCanEcuIsAlive(nowMs);
	const uint16_t enable = pdmCanApplyTimeout(pdmCanMapEnables(s_ecu), true, alive);

	for (size_t i = 0; i < kOwnedCount; i++) {
		s_ownedPins[i].setValue(pdmCanBit(enable, kOwnedChannels[i]));
	}
}

#if EFI_CAN_SUPPORT
void pdmCan_onRx(size_t /*busIndex*/, const CANRxFrame& frame, efitick_t /*nowNt*/) {
	if (!engineConfiguration->pdmCanConsumeEnable) {
		return;
	}
	if (CAN_ISX(frame)) {
		return;
	}

	const uint32_t id = CAN_ID(frame);
	const uint32_t base = engineConfiguration->pdmCanConsumeBaseId;
	if (id < base) {
		return;
	}
	const uint32_t off = id - base;
	const uint8_t* data = pdmCanFrameData(frame);
	const uint8_t dlc = pdmCanFrameDlc(frame);

	bool decoded = false;
	if (off == kPdmCanEcuStatusOff) {
		decoded = pdmCanDecodeStatus(data, dlc, s_ecu);
	} else if (off == kPdmCanEcuSpeedsOff) {
		decoded = pdmCanDecodeSpeeds(data, dlc, s_ecu);
	} else if (off == kPdmCanEcuSensors1Off) {
		decoded = pdmCanDecodeSensors1(data, dlc, s_ecu);
	}

	if (decoded) {
		s_haveRx = true;
		s_lastRxMs = getTimeNowMs();
	}
}

static void pdmCanSendFrame(uint32_t id, const uint8_t payload[8]) {
	CanTxMessage msg(CanCategory::NBC, id, 8);
	msg.setArray(payload, 8);
}

void pdmCan_updateDash(CanCycle cycle) {
	if (!engineConfiguration->pdmCanStatusEnable) {
		return;
	}
	if (!cycle.isInterval(CI::_50ms)) {
		return;
	}

	const uint32_t nowMs = getTimeNowMs();
	const bool alive = pdmCanEcuIsAlive(nowMs);
	const bool consume = engineConfiguration->pdmCanConsumeEnable;
	const bool timeout = consume && !alive;
	const uint32_t base = engineConfiguration->pdmCanStatusBaseId;

	bool commanded[kPdmCanChannels] = {};
	bool actual[kPdmCanChannels] = {};
	bool fault[kPdmCanChannels] = {};
	uint8_t reasons[kPdmCanChannels] = {};
	float amps[kPdmCanChannels] = {};

	for (size_t i = 0; i < kPdmCanChannels; i++) {
		commanded[i] = pdmEfuse_isRequestedOn(i);
		actual[i] = pdmEfuse_isDriven(i);
		fault[i] = pdmEfuse_isFaulted(i);
		reasons[i] = static_cast<uint8_t>(pdmEfuse_getReason(i));
		amps[i] = pdmEfuse_getCurrentA(i);
	}

	uint8_t payload[8];
	pdmCanPackStatus0(payload,
		pdmCanPackBits(commanded),
		pdmCanPackBits(actual),
		pdmCanPackBits(fault),
		alive, consume, true, timeout, s_txSeq++);
	pdmCanSendFrame(base + kPdmCanTxStatus0Off, payload);

	pdmCanPackCurrents(payload, amps);
	pdmCanSendFrame(base + kPdmCanTxCurrHpOff, payload);
	pdmCanPackCurrents(payload, amps + 4);
	pdmCanSendFrame(base + kPdmCanTxCurrAdio14Off, payload);
	pdmCanPackCurrents(payload, amps + 8);
	pdmCanSendFrame(base + kPdmCanTxCurrAdio58Off, payload);

	pdmCanPackReasons(payload, reasons, s_ecu.haveRpm ? s_ecu.rpm : 0);
	pdmCanSendFrame(base + kPdmCanTxReasonsOff, payload);
}
#endif
