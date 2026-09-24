/**
 * @file pdm_can.cpp
 * @brief Consume rusEFI ECU verbose CAN and publish PowerCore channel status.
 *
 * RX: custom_board_can_rx at pdmCanConsumeBaseId (Status / Speeds / Sensors1).
 * Drive: the ECU output pin for Fuel pump / Fan / Fan 2 / O2 heater / Main relay
 * when that pin is assigned (e-fuse SM still owns PROTECTED_PIN_*). If the pin
 * is Unassigned, the legacy protected pin is driven instead.
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
static bool s_roleClaimed[kOwnedCount] = {};

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

static int pdmProtectedBase() {
	return static_cast<int>(Gpio::PROTECTED_PIN_0);
}

static PdmRolePins pdmReadRolePins() {
	PdmRolePins roles;
	roles.fuelPump = static_cast<int>(engineConfiguration->fuelPumpPin);
	roles.fan = static_cast<int>(engineConfiguration->fanPin);
	roles.fan2 = static_cast<int>(engineConfiguration->fan2Pin);
	roles.o2Heater = static_cast<int>(engineConfiguration->o2heaterPin);
	roles.mainRelay = static_cast<int>(engineConfiguration->mainRelayPin);
	return roles;
}

static void pdmCanReleasePins() {
	for (size_t i = 0; i < kOwnedCount; i++) {
		if (!s_roleClaimed[i]) {
			continue;
		}
		s_ownedPins[i].deInit();
		s_roleClaimed[i] = false;
	}
}

static void pdmCanClaimLegacyPins() {
	if (!engineConfiguration->pdmCanConsumeEnable) {
		pdmCanReleasePins();
		return;
	}
	const int base = pdmProtectedBase();
	const PdmRolePins roles = pdmReadRolePins();
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, base);
	for (size_t i = 0; i < kOwnedCount; i++) {
		if (!plan.legacy[i]) {
			if (s_roleClaimed[i]) {
				s_ownedPins[i].deInit();
				s_roleClaimed[i] = false;
			}
			continue;
		}
		if (s_roleClaimed[i]) {
			continue;
		}
		s_ownedPins[i].initPin(kOwnedNames[i], kOwnedPins[i]);
		s_ownedPins[i].setValue(false);
		s_roleClaimed[i] = true;
	}
}

template <typename PinT>
static void pdmClearPinIfTaken(PinT& pin, const PdmCanOutputPlan& plan, const PdmRolePins& roles, bool consume) {
	const int base = pdmProtectedBase();
	if (pdmOutputPinTaken(static_cast<int>(pin), plan, roles, base, consume)) {
		pin = Gpio::Unassigned;
	}
}

static void pdmCanUnassignConflictingOutputs() {
	const bool consume = engineConfiguration->pdmCanConsumeEnable;
	const PdmRolePins roles = pdmReadRolePins();
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, pdmProtectedBase());
	for (size_t i = 0; i < efi::size(engineConfiguration->luaOutputPins); i++) {
		pdmClearPinIfTaken(engineConfiguration->luaOutputPins[i], plan, roles, consume);
	}
	for (size_t i = 0; i < efi::size(engineConfiguration->gppwm); i++) {
		pdmClearPinIfTaken(engineConfiguration->gppwm[i].pin, plan, roles, consume);
	}
}

template <typename PinT>
static void pdmRestoreIfClear(PinT& pin, Gpio fallback, const PdmCanOutputPlan& plan, const PdmRolePins& roles) {
	if (static_cast<int>(pin) != static_cast<int>(Gpio::Unassigned)) {
		return;
	}
	if (pdmOutputPinTaken(static_cast<int>(fallback), plan, roles, pdmProtectedBase(), false)) {
		return;
	}
	pin = fallback;
}

static void pdmCanRestoreDefaultOutputsIfClear() {
	if (engineConfiguration->pdmCanConsumeEnable) {
		return;
	}
	const PdmRolePins roles = pdmReadRolePins();
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, pdmProtectedBase());
	pdmRestoreIfClear(engineConfiguration->luaOutputPins[0], Gpio::PROTECTED_PIN_0, plan, roles);
	pdmRestoreIfClear(engineConfiguration->luaOutputPins[1], Gpio::PROTECTED_PIN_1, plan, roles);
	pdmRestoreIfClear(engineConfiguration->luaOutputPins[2], Gpio::PROTECTED_PIN_2, plan, roles);
	pdmRestoreIfClear(engineConfiguration->luaOutputPins[4], Gpio::PROTECTED_PIN_4, plan, roles);
	pdmRestoreIfClear(engineConfiguration->gppwm[0].pin, Gpio::PROTECTED_PIN_8, plan, roles);
}

void pdmCan_configOverrides() {
	// Boot path: startPins has not run yet. Drop Lua / GP PWM that share an ECU pin.
	pdmCanUnassignConflictingOutputs();
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
	// Burn restarts pins after this hook. Clear conflicts before startPins.
	pdmCanUnassignConflictingOutputs();
}

void pdmCan_onStartHardware() {
	pdmCanClaimLegacyPins();
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

	const uint32_t nowMs = getTimeNowMs();
	const bool alive = pdmCanEcuIsAlive(nowMs);
	bool on[kPdmCanRoleCount] = {};
	pdmCanRoleLevels(s_ecu, true, alive, on);

	for (size_t i = 0; i < kOwnedCount; i++) {
		if (s_roleClaimed[i]) {
			s_ownedPins[i].setValue(on[i]);
		}
	}
}

static void pdmCanApplyLogical(const PdmCanOutputPlan& plan, const bool on[kPdmCanRoleCount]) {
	// Fan PWM mode owns the pin via startSimplePwm. Leave that curve alone.
	if (plan.logical[0]) {
		enginePins.fuelPumpRelay.setValue(on[0]);
	}
	if (plan.logical[1] && !engineConfiguration->fan1PwmEnabled) {
		enginePins.fanRelay.setValue(on[1]);
	}
	if (plan.logical[2] && !engineConfiguration->fan2PwmEnabled) {
		enginePins.fanRelay2.setValue(on[2]);
	}
	if (plan.logical[3]) {
		enginePins.o2heater.setValue(on[3]);
	}
	if (plan.logical[4]) {
		enginePins.mainRelay.setValue(on[4]);
	}
}

void pdmCan_periodicSlow() {
	if (!engineConfiguration->pdmCanConsumeEnable) {
		return;
	}
	const uint32_t nowMs = getTimeNowMs();
	const bool alive = pdmCanEcuIsAlive(nowMs);
	bool on[kPdmCanRoleCount] = {};
	pdmCanRoleLevels(s_ecu, true, alive, on);
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(pdmReadRolePins(), pdmProtectedBase());
	// After engine modules, so CAN wins over local pump/fan/main-relay logic.
	pdmCanApplyLogical(plan, on);
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
