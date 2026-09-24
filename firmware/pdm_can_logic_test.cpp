/**
 * Host unit tests for firmware/pdm_can_logic.h (no rusEFI).
 * Run: firmware/run_pdm_can_logic_test.sh
 */

#include "pdm_can_logic.h"

#include <cstdio>
#include <cstring>

static int g_fails = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_fails++; \
	} \
} while (0)

static void test_decode_status_pump_fan() {
	uint8_t d[8] = {};
	// byte 4: bit2 pump, bit6 fan, bit7 fan2, bit1 main relay, bit4 o2
	d[4] = (1u << 2) | (1u << 6);
	PdmCanEcuSignals s;
	CHECK(pdmCanDecodeStatus(d, 8, s));
	CHECK(s.validStatus);
	CHECK(s.fuelPump);
	CHECK(s.fan);
	CHECK(!s.fan2);
	CHECK(!s.mainRelay);
	CHECK(!s.o2Heater);

	d[4] = (1u << 7) | (1u << 1) | (1u << 4);
	CHECK(pdmCanDecodeStatus(d, 8, s));
	CHECK(!s.fuelPump);
	CHECK(!s.fan);
	CHECK(s.fan2);
	CHECK(s.mainRelay);
	CHECK(s.o2Heater);
}

static void test_decode_rejects_short_frame() {
	uint8_t d[4] = {0, 0, 0, 0xFF};
	PdmCanEcuSignals s;
	CHECK(!pdmCanDecodeStatus(d, 4, s));
	CHECK(!s.validStatus);
}

static void test_decode_rpm_and_clt() {
	uint8_t speeds[8] = {0xE8, 0x03}; // 1000 RPM LE
	PdmCanEcuSignals s;
	CHECK(pdmCanDecodeSpeeds(speeds, 8, s));
	CHECK(s.haveRpm);
	CHECK(s.rpm == 1000);

	uint8_t sensors[8] = {0, 0, 40 + 90}; // 90 °C
	CHECK(pdmCanDecodeSensors1(sensors, 8, s));
	CHECK(s.haveClt);
	CHECK(s.cltC == 90);
}

static void test_map_pump_fan_examples() {
	PdmCanEcuSignals s;
	s.validStatus = true;
	s.fuelPump = true;
	s.fan = true;
	const uint16_t bits = pdmCanMapEnables(s);
	CHECK(pdmCanBit(bits, kPdmCanChHp1Pump));
	CHECK(pdmCanBit(bits, kPdmCanChHp2Fan));
	CHECK(!pdmCanBit(bits, kPdmCanChHp3Fan2));
	CHECK((bits & ~kPdmCanOwnedMask) == 0);
}

static void test_timeout_failsafe_clears_outputs() {
	PdmCanEcuSignals s;
	s.validStatus = true;
	s.fuelPump = true;
	s.fan = true;
	const uint16_t mapped = pdmCanMapEnables(s);
	CHECK(pdmCanApplyTimeout(mapped, true, true) == mapped);
	CHECK(pdmCanApplyTimeout(mapped, true, false) == 0);
	CHECK(pdmCanApplyTimeout(mapped, false, true) == 0);
	CHECK(!pdmCanEcuAlive(false, 1000, 0, kPdmCanEcuTimeoutMs));
	CHECK(pdmCanEcuAlive(true, 1000, 600, kPdmCanEcuTimeoutMs));
	CHECK(!pdmCanEcuAlive(true, 1000, 400, kPdmCanEcuTimeoutMs));
}

static void test_pack_status_and_currents() {
	uint8_t f[8];
	pdmCanPackStatus0(f, 0x0ABC, 0x0123, 0x0005, true, true, true, false, 42);
	CHECK(f[0] == 0xBC);
	CHECK(f[1] == 0x0A);
	CHECK(f[2] == 0x23);
	CHECK(f[3] == 0x01);
	CHECK(f[4] == 0x05);
	CHECK(f[5] == 0x00);
	CHECK((f[6] & 0x07) == 0x07);
	CHECK((f[6] & 0x08) == 0);
	CHECK(f[7] == 42);

	float amps[4] = {1.50f, 0, 12.34f, 0.01f};
	pdmCanPackCurrents(f, amps);
	CHECK(f[0] == 150 && f[1] == 0);
	CHECK(f[2] == 0 && f[3] == 0);
	const uint16_t a2 = static_cast<uint16_t>(f[4] | (f[5] << 8));
	CHECK(a2 == 1234);
	CHECK(f[6] == 1 && f[7] == 0);
}

static void test_output_plan_legacy_when_unassigned() {
	const int base = 289; // PROTECTED_PIN_0
	PdmRolePins roles;
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, base);
	CHECK(plan.legacy[0] && plan.channel[0] == 0);
	CHECK(plan.legacy[1] && plan.channel[1] == 1);
	CHECK(plan.legacy[2] && plan.channel[2] == 2);
	CHECK(plan.legacy[3] && plan.channel[3] == 4);
	CHECK(plan.legacy[4] && plan.channel[4] == 8);
	CHECK(!plan.logical[0]);
	CHECK(pdmOutputPinTaken(base + 0, plan, roles, base, true));
	CHECK(!pdmOutputPinTaken(base + 0, plan, roles, base, false));
	CHECK(!pdmOutputPinTaken(base + 3, plan, roles, base, true));
}

static void test_output_plan_follows_assigned_ecu_pins() {
	const int base = 289;
	PdmRolePins roles;
	roles.fuelPump = base + 1; // HP2
	roles.fan = base + 0;      // HP1
	roles.fan2 = base + 2;
	roles.o2Heater = base + 4;
	roles.mainRelay = base + 9; // ADIO6
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, base);
	CHECK(plan.logical[0] && !plan.legacy[0] && plan.channel[0] == 1);
	CHECK(plan.logical[1] && !plan.legacy[1] && plan.channel[1] == 0);
	CHECK(plan.logical[4] && plan.channel[4] == 9);
	CHECK(!plan.legacy[2] && !plan.legacy[3]);
	CHECK(pdmOutputPinTaken(base + 1, plan, roles, base, false));
	CHECK(!pdmOutputPinTaken(base + 8, plan, roles, base, true));
}

static void test_output_plan_suppresses_legacy_on_taken_channel() {
	const int base = 289;
	PdmRolePins roles;
	roles.fuelPump = base + 1; // occupies fan's legacy HP2
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, base);
	CHECK(plan.logical[0] && plan.channel[0] == 1);
	CHECK(!plan.legacy[1]);
	CHECK(plan.legacy[2] && plan.channel[2] == 2);
	CHECK(pdmOutputPinTaken(base + 1, plan, roles, base, true));
	CHECK(pdmOutputPinTaken(base + 1, plan, roles, base, false));
}

static void test_output_plan_non_protected_pin_skips_legacy() {
	const int base = 289;
	PdmRolePins roles;
	roles.fuelPump = 70; // on-chip pin, not the e-fuse bank
	const PdmCanOutputPlan plan = pdmCanPlanOutputs(roles, base);
	CHECK(plan.logical[0]);
	CHECK(!plan.legacy[0]);
	CHECK(plan.channel[0] == -1);
	CHECK(plan.legacy[1] && plan.channel[1] == 1);
}

static void test_role_levels_follow_timeout() {
	PdmCanEcuSignals s;
	s.validStatus = true;
	s.fuelPump = true;
	s.mainRelay = true;
	bool on[kPdmCanRoleCount] = {};
	pdmCanRoleLevels(s, true, true, on);
	CHECK(on[0] && !on[1] && on[4]);
	pdmCanRoleLevels(s, true, false, on);
	CHECK(!on[0] && !on[4]);
	pdmCanRoleLevels(s, false, true, on);
	CHECK(!on[0] && !on[4]);
}

static void test_pack_trip_reasons() {
	uint8_t reasons[kPdmCanChannels] = {
		1, 2, 3, 0, 1, 0, 0, 0, 2, 0, 0, 3
	};
	uint8_t f[8];
	pdmCanPackReasons(f, reasons, 2500);
	CHECK((f[0] & 0x0f) == 1);
	CHECK((f[0] >> 4) == 2);
	CHECK((f[1] & 0x0f) == 3);
	CHECK((f[4] & 0x0f) == 2);
	CHECK((f[5] >> 4) == 3);
	CHECK(f[6] == (2500 & 0xff));
	CHECK(f[7] == (2500 >> 8));
}

int main() {
	test_decode_status_pump_fan();
	test_decode_rejects_short_frame();
	test_decode_rpm_and_clt();
	test_map_pump_fan_examples();
	test_timeout_failsafe_clears_outputs();
	test_output_plan_legacy_when_unassigned();
	test_output_plan_follows_assigned_ecu_pins();
	test_output_plan_suppresses_legacy_on_taken_channel();
	test_output_plan_non_protected_pin_skips_legacy();
	test_role_levels_follow_timeout();
	test_pack_status_and_currents();
	test_pack_trip_reasons();

	if (g_fails) {
		std::printf("%d check(s) failed\n", g_fails);
		return 1;
	}
	std::printf("pdm_can_logic_test: all passed\n");
	return 0;
}
