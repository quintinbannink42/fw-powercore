/**
 * @file pdm_can_logic.h
 * @brief Host-testable rusEFI ECU broadcast consume + PDM status packing.
 *
 * Consumes rusEFI verbose CAN (can_verbose.cpp / rusEFI_CAN_verbose.dbc)
 * starting at pdmCanConsumeBaseId (default 0x200):
 *   +0 Status  — FuelPumpAct / Fan / Fan2 / MainRelayAct / EGOHeatAct
 *   +1 Speeds  — RPM
 *   +3 Sensors1 — CLT (byte2, offset -40 °C)
 *
 * Maps those signals onto protected PDM channels (pump / fan examples):
 *   HP1  PROTECTED_PIN_0  FuelPumpAct
 *   HP2  PROTECTED_PIN_1  Fan
 *   HP3  PROTECTED_PIN_2  Fan2
 *   ADIO1 PROTECTED_PIN_4  EGOHeatAct
 *   ADIO5 PROTECTED_PIN_8  MainRelayAct
 *
 * Status TX at pdmCanStatusBaseId (default 0x240) is packed here; see
 * firmware/powercore_pdm.dbc and docs/CAN.md.
 *
 * No rusEFI types — compiled by firmware/pdm_can_logic_test.cpp with host g++.
 */

#pragma once

#include <cstdint>
#include <cstddef>

static constexpr size_t kPdmCanChannels = 12;
static constexpr uint32_t kPdmCanEcuTimeoutMs = 500;
static constexpr uint8_t kPdmCanCltOffset = 40;

// Channel indices matching e-fuse bank / PROTECTED_PIN_n
static constexpr size_t kPdmCanChHp1Pump = 0;
static constexpr size_t kPdmCanChHp2Fan = 1;
static constexpr size_t kPdmCanChHp3Fan2 = 2;
static constexpr size_t kPdmCanChAdio1O2 = 4;
static constexpr size_t kPdmCanChAdio5Relay = 8;

static constexpr uint16_t kPdmCanOwnedMask =
	(1u << kPdmCanChHp1Pump) |
	(1u << kPdmCanChHp2Fan) |
	(1u << kPdmCanChHp3Fan2) |
	(1u << kPdmCanChAdio1O2) |
	(1u << kPdmCanChAdio5Relay);

// rusEFI verbose offsets relative to verboseCanBaseAddress / pdmCanConsumeBaseId
static constexpr uint16_t kPdmCanEcuStatusOff = 0;
static constexpr uint16_t kPdmCanEcuSpeedsOff = 1;
static constexpr uint16_t kPdmCanEcuSensors1Off = 3;

// PDM status TX offsets relative to pdmCanStatusBaseId
static constexpr uint16_t kPdmCanTxStatus0Off = 0;
static constexpr uint16_t kPdmCanTxCurrHpOff = 1;
static constexpr uint16_t kPdmCanTxCurrAdio14Off = 2;
static constexpr uint16_t kPdmCanTxCurrAdio58Off = 3;
static constexpr uint16_t kPdmCanTxReasonsOff = 4;

struct PdmCanEcuSignals {
	bool validStatus = false;
	bool fuelPump = false;
	bool fan = false;
	bool fan2 = false;
	bool mainRelay = false;
	bool o2Heater = false;
	uint16_t rpm = 0;
	bool haveRpm = false;
	int16_t cltC = 0;
	bool haveClt = false;
};

inline bool pdmCanBit(uint16_t mask, size_t channel) {
	return channel < 16 && (mask & (1u << channel)) != 0;
}

/**
 * Decode rusEFI BASE0 / Status (FuelPumpAct bit 34 = byte 4 bit 2, Intel).
 */
inline bool pdmCanDecodeStatus(const uint8_t* d, size_t dlc, PdmCanEcuSignals& s) {
	if (d == nullptr || dlc < 5) {
		return false;
	}
	const uint8_t b = d[4];
	s.mainRelay = (b & (1u << 1)) != 0;
	s.fuelPump = (b & (1u << 2)) != 0;
	s.o2Heater = (b & (1u << 4)) != 0;
	s.fan = (b & (1u << 6)) != 0;
	s.fan2 = (b & (1u << 7)) != 0;
	s.validStatus = true;
	return true;
}

/** Decode rusEFI BASE1 / Speeds: RPM uint16 LE at bytes 0-1. */
inline bool pdmCanDecodeSpeeds(const uint8_t* d, size_t dlc, PdmCanEcuSignals& s) {
	if (d == nullptr || dlc < 2) {
		return false;
	}
	s.rpm = static_cast<uint16_t>(d[0] | (static_cast<uint16_t>(d[1]) << 8));
	s.haveRpm = true;
	return true;
}

/** Decode rusEFI BASE3 / Sensors1: CLT = byte2 - 40 °C. */
inline bool pdmCanDecodeSensors1(const uint8_t* d, size_t dlc, PdmCanEcuSignals& s) {
	if (d == nullptr || dlc < 3) {
		return false;
	}
	s.cltC = static_cast<int16_t>(d[2]) - static_cast<int16_t>(kPdmCanCltOffset);
	s.haveClt = true;
	return true;
}

/**
 * Map ECU commanded bits onto the owned PDM channel mask.
 * Pump/fan follow ECU outputs 1:1; O2 heater and main relay are ADIO examples.
 */
inline uint16_t pdmCanMapEnables(const PdmCanEcuSignals& s) {
	if (!s.validStatus) {
		return 0;
	}
	uint16_t bits = 0;
	if (s.fuelPump) {
		bits |= (1u << kPdmCanChHp1Pump);
	}
	if (s.fan) {
		bits |= (1u << kPdmCanChHp2Fan);
	}
	if (s.fan2) {
		bits |= (1u << kPdmCanChHp3Fan2);
	}
	if (s.o2Heater) {
		bits |= (1u << kPdmCanChAdio1O2);
	}
	if (s.mainRelay) {
		bits |= (1u << kPdmCanChAdio5Relay);
	}
	return bits;
}

inline bool pdmCanEcuAlive(bool haveRx, uint32_t nowMs, uint32_t lastRxMs, uint32_t timeoutMs) {
	if (!haveRx) {
		return false;
	}
	return (nowMs - lastRxMs) < timeoutMs;
}

/** Owned channels are forced off when consume is disabled or the ECU timed out. */
inline uint16_t pdmCanApplyTimeout(uint16_t mapped, bool consumeEnable, bool ecuAlive) {
	if (!consumeEnable || !ecuAlive) {
		return 0;
	}
	return mapped & kPdmCanOwnedMask;
}

inline uint16_t pdmCanPackBits(const bool flags[kPdmCanChannels]) {
	uint16_t bits = 0;
	for (size_t i = 0; i < kPdmCanChannels; i++) {
		if (flags[i]) {
			bits |= (1u << i);
		}
	}
	return bits;
}

inline void pdmCanPackStatus0(uint8_t out[8], uint16_t commanded, uint16_t actual, uint16_t fault,
		bool ecuAlive, bool consumeEn, bool statusEn, bool timeoutFailsafe, uint8_t seq) {
	out[0] = static_cast<uint8_t>(commanded & 0xff);
	out[1] = static_cast<uint8_t>((commanded >> 8) & 0x0f);
	out[2] = static_cast<uint8_t>(actual & 0xff);
	out[3] = static_cast<uint8_t>((actual >> 8) & 0x0f);
	out[4] = static_cast<uint8_t>(fault & 0xff);
	out[5] = static_cast<uint8_t>((fault >> 8) & 0x0f);
	out[6] = static_cast<uint8_t>(
		(ecuAlive ? 1u : 0u) |
		(consumeEn ? 2u : 0u) |
		(statusEn ? 4u : 0u) |
		(timeoutFailsafe ? 8u : 0u));
	out[7] = seq;
}

inline uint16_t pdmCanAmpsToRaw(float amps) {
	if (amps <= 0) {
		return 0;
	}
	const float scaled = amps * 100.0f;
	if (scaled >= 65535.0f) {
		return 65535;
	}
	return static_cast<uint16_t>(scaled + 0.5f);
}

inline void pdmCanPackCurrents(uint8_t out[8], const float amps4[4]) {
	for (int i = 0; i < 4; i++) {
		const uint16_t raw = pdmCanAmpsToRaw(amps4[i]);
		out[i * 2] = static_cast<uint8_t>(raw & 0xff);
		out[i * 2 + 1] = static_cast<uint8_t>(raw >> 8);
	}
}

inline void pdmCanPackReasons(uint8_t out[8], const uint8_t reasons[kPdmCanChannels], uint16_t rpm) {
	for (int i = 0; i < 6; i++) {
		const uint8_t lo = reasons[i * 2] & 0x0f;
		const uint8_t hi = reasons[i * 2 + 1] & 0x0f;
		out[i] = static_cast<uint8_t>(lo | (hi << 4));
	}
	out[6] = static_cast<uint8_t>(rpm & 0xff);
	out[7] = static_cast<uint8_t>(rpm >> 8);
}
