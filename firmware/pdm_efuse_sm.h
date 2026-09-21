/**
 * @file pdm_efuse_sm.h
 * @brief Host-testable Link Razor-style electronic-fuse state machine.
 *
 * Consumes TunerStudio pdm_channel_trip_s fields:
 *   inrushLimitA, ocLimitA, inrushWindowMs, tripTimeMs, retryCount, latchOnFault
 *
 * Mapping (high-side / ADIO output, not half-bridge):
 *   - Fast short: amps > inrushLimitA trips immediately (any stage).
 *   - Inrush window after commanded ON: ocLimitA is ignored; only fast short applies.
 *   - After the window: amps > ocLimitA for tripTimeMs trips (overcurrent).
 *   - latchOnFault: stay off until commanded off for kPdmEfuseOffResetMs, then on.
 *   - else: retry up to retryCount times with kPdmEfuseRetryDelayMs between attempts,
 *     then latch until the same off→on cycle.
 *
 * No rusEFI types — this header is compiled by firmware/pdm_efuse_sm_test.cpp
 * with host g++ as well as by the board gpio chip.
 */

#pragma once

#include <cstdint>

static constexpr uint32_t kPdmEfuseOffResetMs = 50;
static constexpr uint32_t kPdmEfuseRetryDelayMs = 1000;

enum class PdmEfuseState : uint8_t {
	Off = 0,
	Inrush,
	Steady,
	WaitRetry,
	Latched,
};

enum class PdmEfuseTripReason : uint8_t {
	None = 0,
	FastShort,
	Overcurrent,
	SenseFail,
};

struct PdmEfuseTrip {
	float inrushLimitA = 0;
	float ocLimitA = 0;
	uint16_t inrushWindowMs = 0;
	uint16_t tripTimeMs = 0;
	uint8_t retryCount = 0;
	bool latchOnFault = false;
};

class PdmEfuseChannel {
public:
	void reset() {
		m_state = PdmEfuseState::Off;
		m_reason = PdmEfuseTripReason::None;
		m_retriesUsed = 0;
		m_onMs = 0;
		m_overMs = 0;
		m_tripMs = 0;
		m_offMs = 0;
		m_haveOn = false;
		m_haveOver = false;
		m_haveOff = false;
		m_fp = 0;
		m_haveFp = false;
	}

	/**
	 * @return true if the physical high-side should be driven.
	 */
	bool tick(bool requestedOn, float amps, bool senseValid, const PdmEfuseTrip& trip,
			uint32_t nowMs) {
		const uint32_t fp = fingerprint(trip);
		if (!m_haveFp || fp != m_fp) {
			// TunerStudio burn / default load recovers like a Link setting change.
			reset();
			m_fp = fp;
			m_haveFp = true;
		}

		if (!requestedOn) {
			if (!m_haveOff) {
				m_offMs = nowMs;
				m_haveOff = true;
			}
			if (elapsed(nowMs, m_offMs) >= kPdmEfuseOffResetMs) {
				// Inactive long enough: clear latch / inrush window (Link OFF→ON reset).
				m_state = PdmEfuseState::Off;
				m_retriesUsed = 0;
				m_reason = PdmEfuseTripReason::None;
				m_haveOn = false;
				m_haveOver = false;
			}
			return false;
		}

		m_haveOff = false;

		if (m_state == PdmEfuseState::Latched) {
			return false;
		}

		if (m_state == PdmEfuseState::WaitRetry) {
			if (elapsed(nowMs, m_tripMs) < kPdmEfuseRetryDelayMs) {
				return false;
			}
			enterOn(nowMs, trip);
		}

		if (m_state == PdmEfuseState::Off) {
			enterOn(nowMs, trip);
		}

		if (m_state == PdmEfuseState::Inrush && m_haveOn
				&& elapsed(nowMs, m_onMs) >= trip.inrushWindowMs) {
			m_state = PdmEfuseState::Steady;
			m_haveOver = false;
		}

		if (!senseValid) {
			return tripNow(nowMs, trip, PdmEfuseTripReason::SenseFail);
		}

		if (trip.inrushLimitA > 0 && amps > trip.inrushLimitA) {
			return tripNow(nowMs, trip, PdmEfuseTripReason::FastShort);
		}

		if (m_state == PdmEfuseState::Steady && trip.ocLimitA > 0 && amps > trip.ocLimitA) {
			if (!m_haveOver) {
				m_overMs = nowMs;
				m_haveOver = true;
			}
			if (elapsed(nowMs, m_overMs) >= trip.tripTimeMs) {
				return tripNow(nowMs, trip, PdmEfuseTripReason::Overcurrent);
			}
		} else {
			m_haveOver = false;
		}

		return true;
	}

	PdmEfuseState state() const { return m_state; }
	PdmEfuseTripReason reason() const { return m_reason; }
	uint8_t retriesUsed() const { return m_retriesUsed; }
	bool isFaulted() const {
		return m_state == PdmEfuseState::WaitRetry || m_state == PdmEfuseState::Latched;
	}

private:
	static uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) {
		return nowMs - thenMs;
	}

	static uint32_t fingerprint(const PdmEfuseTrip& t) {
		uint32_t h = static_cast<uint32_t>(t.inrushWindowMs)
				^ (static_cast<uint32_t>(t.tripTimeMs) << 16)
				^ (static_cast<uint32_t>(t.retryCount) << 8)
				^ (t.latchOnFault ? 1u : 0u);
		// Quantize amps so float noise does not reset; 0.25 A steps.
		h ^= static_cast<uint32_t>(t.inrushLimitA * 4.0f);
		h ^= static_cast<uint32_t>(t.ocLimitA * 4.0f) << 10;
		return h;
	}

	void enterOn(uint32_t nowMs, const PdmEfuseTrip& trip) {
		m_onMs = nowMs;
		m_haveOn = true;
		m_haveOver = false;
		m_reason = PdmEfuseTripReason::None;
		m_state = (trip.inrushWindowMs == 0) ? PdmEfuseState::Steady : PdmEfuseState::Inrush;
	}

	bool tripNow(uint32_t nowMs, const PdmEfuseTrip& trip, PdmEfuseTripReason reason) {
		m_reason = reason;
		m_tripMs = nowMs;
		m_haveOver = false;
		m_haveOn = false;
		if (trip.latchOnFault || trip.retryCount == 0 || m_retriesUsed >= trip.retryCount) {
			m_state = PdmEfuseState::Latched;
		} else {
			m_retriesUsed++;
			m_state = PdmEfuseState::WaitRetry;
		}
		return false;
	}

	PdmEfuseState m_state = PdmEfuseState::Off;
	PdmEfuseTripReason m_reason = PdmEfuseTripReason::None;
	uint8_t m_retriesUsed = 0;
	uint32_t m_onMs = 0;
	uint32_t m_overMs = 0;
	uint32_t m_tripMs = 0;
	uint32_t m_offMs = 0;
	uint32_t m_fp = 0;
	bool m_haveOn = false;
	bool m_haveOver = false;
	bool m_haveOff = false;
	bool m_haveFp = false;
};
