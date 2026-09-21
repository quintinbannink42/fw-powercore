/**
 * Host unit tests for firmware/pdm_efuse_sm.h (no rusEFI).
 * Run: firmware/run_pdm_efuse_sm_test.sh
 */

#include "pdm_efuse_sm.h"

#include <cstdio>
#include <cstdlib>

static int g_fails = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_fails++; \
	} \
} while (0)

static PdmEfuseTrip hpTrip() {
	PdmEfuseTrip t;
	t.inrushLimitA = 80;
	t.ocLimitA = 60;
	t.inrushWindowMs = 50;
	t.tripTimeMs = 20;
	t.retryCount = 3;
	t.latchOnFault = false;
	return t;
}

static void test_inrush_allows_oc_until_window() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	CHECK(ch.tick(true, 70.0f, true, t, 0));
	CHECK(ch.state() == PdmEfuseState::Inrush);
	CHECK(ch.tick(true, 70.0f, true, t, 49));
	CHECK(ch.state() == PdmEfuseState::Inrush);
	CHECK(ch.tick(true, 70.0f, true, t, 50));
	CHECK(ch.state() == PdmEfuseState::Steady);
}

static void test_fast_short_trips_during_inrush() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	CHECK(ch.tick(true, 10.0f, true, t, 0));
	CHECK(!ch.tick(true, 81.0f, true, t, 5));
	CHECK(ch.reason() == PdmEfuseTripReason::FastShort);
	CHECK(ch.state() == PdmEfuseState::WaitRetry);
}

static void test_oc_needs_trip_time_after_window() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	CHECK(ch.tick(true, 70.0f, true, t, 0));
	CHECK(ch.tick(true, 70.0f, true, t, 50));
	CHECK(ch.state() == PdmEfuseState::Steady);
	CHECK(ch.tick(true, 70.0f, true, t, 69));
	CHECK(ch.state() == PdmEfuseState::Steady);
	CHECK(!ch.tick(true, 70.0f, true, t, 70));
	CHECK(ch.reason() == PdmEfuseTripReason::Overcurrent);
}

static void test_oc_does_not_accumulate_below_limit() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	ch.tick(true, 10.0f, true, t, 0);
	ch.tick(true, 10.0f, true, t, 50);
	ch.tick(true, 70.0f, true, t, 60);
	CHECK(ch.tick(true, 10.0f, true, t, 70));
	CHECK(ch.tick(true, 70.0f, true, t, 80));
	CHECK(ch.state() == PdmEfuseState::Steady);
}

static void test_retry_then_latch() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	t.retryCount = 2;
	CHECK(!ch.tick(true, 90.0f, true, t, 0));
	CHECK(ch.retriesUsed() == 1);
	CHECK(ch.state() == PdmEfuseState::WaitRetry);
	CHECK(!ch.tick(true, 90.0f, true, t, 999));
	CHECK(ch.tick(true, 10.0f, true, t, 1000));
	CHECK(ch.state() == PdmEfuseState::Inrush);
	CHECK(!ch.tick(true, 90.0f, true, t, 1001));
	CHECK(ch.retriesUsed() == 2);
	CHECK(ch.state() == PdmEfuseState::WaitRetry);
	CHECK(ch.tick(true, 10.0f, true, t, 2001));
	CHECK(!ch.tick(true, 90.0f, true, t, 2002));
	CHECK(ch.state() == PdmEfuseState::Latched);
	CHECK(!ch.tick(true, 10.0f, true, t, 5000));
}

static void test_latch_on_fault_skips_retry() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	t.latchOnFault = true;
	CHECK(!ch.tick(true, 90.0f, true, t, 0));
	CHECK(ch.state() == PdmEfuseState::Latched);
	CHECK(ch.retriesUsed() == 0);
}

static void test_off_cycle_clears_latch() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	t.latchOnFault = true;
	ch.tick(true, 90.0f, true, t, 0);
	CHECK(ch.state() == PdmEfuseState::Latched);
	CHECK(!ch.tick(false, 0.0f, true, t, 10));
	CHECK(ch.state() == PdmEfuseState::Latched);
	CHECK(!ch.tick(false, 0.0f, true, t, 10 + kPdmEfuseOffResetMs));
	CHECK(ch.state() == PdmEfuseState::Off);
	CHECK(ch.tick(true, 10.0f, true, t, 100));
	CHECK(ch.state() == PdmEfuseState::Inrush);
}

static void test_pwm_short_off_does_not_reset_inrush() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	CHECK(ch.tick(true, 70.0f, true, t, 0));
	CHECK(!ch.tick(false, 0.0f, true, t, 5));
	CHECK(ch.tick(true, 70.0f, true, t, 10));
	CHECK(ch.state() == PdmEfuseState::Inrush);
	CHECK(ch.tick(true, 70.0f, true, t, 50));
	CHECK(ch.state() == PdmEfuseState::Steady);
}

static void test_sense_fail_trips() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	CHECK(ch.tick(true, 0.0f, true, t, 0));
	CHECK(!ch.tick(true, 0.0f, false, t, 1));
	CHECK(ch.reason() == PdmEfuseTripReason::SenseFail);
}

static void test_zero_window_goes_steady() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	t.inrushWindowMs = 0;
	CHECK(ch.tick(true, 10.0f, true, t, 0));
	CHECK(ch.state() == PdmEfuseState::Steady);
}

static void test_zero_retry_latches() {
	PdmEfuseChannel ch;
	auto t = hpTrip();
	t.retryCount = 0;
	t.latchOnFault = false;
	CHECK(!ch.tick(true, 90.0f, true, t, 0));
	CHECK(ch.state() == PdmEfuseState::Latched);
}

int main() {
	test_inrush_allows_oc_until_window();
	test_fast_short_trips_during_inrush();
	test_oc_needs_trip_time_after_window();
	test_oc_does_not_accumulate_below_limit();
	test_retry_then_latch();
	test_latch_on_fault_skips_retry();
	test_off_cycle_clears_latch();
	test_pwm_short_off_does_not_reset_inrush();
	test_sense_fail_trips();
	test_zero_window_goes_steady();
	test_zero_retry_latches();

	if (g_fails) {
		std::printf("%d check(s) failed\n", g_fails);
		return 1;
	}
	std::printf("pdm_efuse_sm_test: all passed\n");
	return 0;
}
