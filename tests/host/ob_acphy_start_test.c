#include <stdio.h>
#include <string.h>
#include "../../src/ob_channel.h"
#include "../../src/ob_phy.h"
#include "../../src/ob_irq.h"

enum event {
	EV_SHM,
	EV_D11_READ,
	EV_D11_WRITE,
	EV_AXI_RMW,
};

struct fake_hw {
	enum event events[16];
	unsigned int count;
	int fail_event;
	int lose_event;
	bool lost;
	u32 wrapper;
	u16 shm_value;
	u32 read_value;
	u16 d11_write_value;
	unsigned int wrapper_calls;
};

static int record(struct fake_hw *f, enum event ev)
{
	f->events[f->count++] = ev;
	if (f->lose_event == (int)ev + 1)
		f->lost = true;
	if (f->fail_event == (int)ev + 1)
		return -5;
	return 0;
}

static int fake_shm(void *ctx, u16 off, u16 value)
{
	struct fake_hw *f = ctx;
	if (off != 0x00a0)
		return -22;
	f->shm_value = value;
	return record(f, EV_SHM);
}

static int fake_d11_read(void *ctx, u16 off, u32 *value)
{
	struct fake_hw *f = ctx;
	if (off != 0x0120 || !value)
		return -22;
	*value = f->read_value;
	return record(f, EV_D11_READ);
}

static int fake_d11_write(void *ctx, u16 off, u16 value)
{
	struct fake_hw *f = ctx;
	if (off != 0x03e6 || value != 0)
		return -22;
	f->d11_write_value = value;
	return record(f, EV_D11_WRITE);
}

static int fake_axi_rmw(void *ctx, u16 off, u32 mask, u32 value,
			u32 *new_value)
{
	struct fake_hw *f = ctx;
	int ret;

	if (off != 0x0408 || mask != 0xc0 || !new_value)
		return -22;
	f->wrapper_calls++;
	*new_value = (f->wrapper & ~mask) | value;
	ret = record(f, EV_AXI_RMW);
	if (!ret)
		f->wrapper = *new_value;
	return ret;
}

static bool fake_lost(void *ctx)
{
	return ((struct fake_hw *)ctx)->lost;
}

static int failures;

static void check_int(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

static void check_bool(const char *name, bool got, bool expected)
{
	check_int(name, got, expected);
}

int main(void)
{
	static const struct ob_acphy_pre_radio_ops ops = {
		.shm_write16 = fake_shm,
		.d11_read32_trusted = fake_d11_read,
		.d11_write16 = fake_d11_write,
		.axi_rmw32_checked = fake_axi_rmw,
		.device_lost = fake_lost,
	};
	struct ob_acphy_pre_radio_state phy = { 0 };
	struct fake_hw f = { .wrapper = 0xa5a50031u, .read_value = 0x12345678u };
	u16 encoded = 0xbeefu;
	enum ob_acphy_start_state start = OB_ACPHY_START_NOT_STARTED;
	struct ob_phy_init_lifecycle lifecycle = { 0 };
	int ret;

	check_int("encode channel 1 20 MHz", ob_acphy_encode_2g_20mhz(1, 2412,
			&encoded), 0);
	check_int("encoded channel 1", encoded, 0x1001);
	encoded = 0xbeef;
	check_int("reject invalid channel without default",
		ob_acphy_encode_2g_20mhz(0, 2412, &encoded), -22);
	check_int("failed encoder leaves output untouched", encoded, 0xbeef);
	check_int("reject frequency mismatch",
		ob_acphy_encode_2g_20mhz(6, 2412, &encoded), -22);
	check_int("null output rejected",
		ob_acphy_encode_2g_20mhz(1, 2412, NULL), -22);

	check_bool("irq service gate closed", ob_d11_irq_service_allowed(false), false);
	check_bool("irq service gate open", ob_d11_irq_service_allowed(true), true);

	check_int("claim first start", ob_acphy_first_up_begin(&start), 0);
	check_int("start in progress", start, OB_ACPHY_START_IN_PROGRESS);
	check_int("reject repeated start", ob_acphy_first_up_begin(&start), -16);
	ob_acphy_first_up_finish(&start, true);
	check_int("ready state", start, OB_ACPHY_START_READY_FOR_RADIO_ON);
	check_int("reject restart after ready", ob_acphy_first_up_begin(&start), -16);
	check_int("ready state can fail later radio stage",
		  ob_acphy_first_up_fail(&start), 0);
	check_int("later stage failure is terminal", start, OB_ACPHY_START_FAILED);
	start = OB_ACPHY_START_NOT_STARTED;
	check_int("claim failed start", ob_acphy_first_up_begin(&start), 0);
	ob_acphy_first_up_finish(&start, false);
	check_int("failed state is terminal", start, OB_ACPHY_START_FAILED);
	check_int("reject retry after failure", ob_acphy_first_up_begin(&start), -16);

	/* The PHY lifecycle cannot promote the accepted pre-radio prefix to
	 * radio/calibration/channel/operational readiness. */
	check_int("lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check_int("lifecycle starts at pre-radio", lifecycle.phase,
		  OB_PHY_INIT_PRE_RADIO_IN_PROGRESS);
	/* The production gate stops before the pre-radio transaction while the
	 * complete initialization block is not admitted. */
	check_int("production gate blocks before pre-radio hardware",
		  ob_phy_init_block(&lifecycle,
				    OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP), 0);
	check_int("production gate phase", lifecycle.phase, OB_PHY_INIT_BLOCKED);
	check_int("production gate blocker", lifecycle.blocker,
		  OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP);
	check_int("blocked path did not call fake hardware", f.count, 0);
	lifecycle = (struct ob_phy_init_lifecycle){ 0 };
	check_int("restart test lifecycle", ob_phy_init_begin(&lifecycle), 0);
	check_int("lifecycle pre-radio ready", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_PRE_RADIO_READY), 0);
	check_int("pre-radio is not operational", ob_phy_init_is_operational(&lifecycle), 0);
	check_int("radio prefix may begin before LPF", ob_phy_init_begin_radio(&lifecycle), 0);
	check_int("radio prefix remains non-operational",
		  ob_phy_init_is_operational(&lifecycle), 0);
	check_int("radio completion needs prefix, LPF and sequence tokens",
		  ob_phy_init_advance(&lifecycle, OB_PHY_INIT_RADIO_COMPLETE), -95);
	check_int("post-LPF lookup can block radio-in-progress",
		  ob_phy_init_block(&lifecycle,
				    OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP), 0);
	check_int("blocked phase retained", lifecycle.phase, OB_PHY_INIT_BLOCKED);
	check_int("blocker retained", lifecycle.blocker,
		  OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP);
	check_int("blocker error retained", lifecycle.error, -95);
	check_int("blocked is not operational", ob_phy_init_is_operational(&lifecycle), 0);
	check_int("cannot advance after block", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_RADIO_COMPLETE), -95);
	check_int("cannot restart blocked lifecycle", ob_phy_init_begin(&lifecycle), -16);

	/* The later milestones are reachable only in strict order; only the final
	 * transition can publish operational readiness. */
	lifecycle = (struct ob_phy_init_lifecycle){ 0 };
	check_int("second lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check_int("cannot skip pre-radio", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_RADIO_IN_PROGRESS), -22);
	check_int("advance pre-radio", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_PRE_RADIO_READY), 0);
	check_int("advance radio in progress", ob_phy_init_begin_radio(&lifecycle), 0);
	check_int("publish bounded radio-prefix completion",
		  ob_phy_init_radio_prefix_complete(&lifecycle), 0);
	/* Synthetic tokens below exercise only downstream state transitions. */
	lifecycle.analog_tx_lpf_complete = true;
	check_int("radio cannot be marked complete without sequence result",
		  ob_phy_init_advance(&lifecycle, OB_PHY_INIT_RADIO_COMPLETE), -95);
	lifecycle.radio_sequence_complete = true;
	check_int("advance radio complete", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_RADIO_COMPLETE), 0);
	check_int("radio complete not operational", ob_phy_init_is_operational(&lifecycle), 0);
	check_int("advance calibration in progress", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CALIBRATION_IN_PROGRESS), 0);
	check_int("calibration completion requires result token",
		  ob_phy_init_advance(&lifecycle,
				      OB_PHY_INIT_CALIBRATION_COMPLETE), -95);
	lifecycle.calibration_complete = true;
	check_int("advance calibration complete", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CALIBRATION_COMPLETE), 0);
	check_int("advance channel programming", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CHANNEL_PROGRAMMING), 0);
	check_int("channel completion requires result token",
		  ob_phy_init_advance(&lifecycle,
				      OB_PHY_INIT_CHANNEL_PROGRAMMED), -95);
	lifecycle.channel_programmed = true;
	check_int("advance channel programmed", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CHANNEL_PROGRAMMED), 0);
	check_int("channel programmed not operational", ob_phy_init_is_operational(&lifecycle), 0);
	lifecycle.channel_programmed = false;
	check_int("operational requires all successful stage tokens",
		  ob_phy_init_advance(&lifecycle, OB_PHY_INIT_OPERATIONAL), -95);
	lifecycle = (struct ob_phy_init_lifecycle){
		.phase = OB_PHY_INIT_CHANNEL_PROGRAMMED,
		.radio_prefix_complete = true,
		.analog_tx_lpf_complete = true,
		.radio_sequence_complete = true,
		.calibration_complete = true,
		.channel_programmed = true,
	};
	check_int("advance operational", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_OPERATIONAL), 0);
	check_int("operational published only at terminal state",
		  ob_phy_init_is_operational(&lifecycle), 1);
	check_int("operational state cannot regress", ob_phy_init_fail(&lifecycle, -5), -16);

	lifecycle = (struct ob_phy_init_lifecycle){ 0 };
	check_int("calibration boundary lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check_int("calibration boundary pre-radio ready", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_PRE_RADIO_READY), 0);
	check_int("calibration boundary radio begin", ob_phy_init_begin_radio(&lifecycle), 0);
	check_int("calibration boundary prefix complete",
		  ob_phy_init_radio_prefix_complete(&lifecycle), 0);
	lifecycle.analog_tx_lpf_complete = true;
	lifecycle.radio_sequence_complete = true;
	check_int("calibration boundary radio complete", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_RADIO_COMPLETE), 0);
	check_int("calibration boundary begin", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CALIBRATION_IN_PROGRESS), 0);
	check_int("calibration input blocker", ob_phy_init_block(&lifecycle,
		  OB_PHY_BLOCK_CALIBRATION_INPUTS), 0);
	check_int("calibration does not imply operational", ob_phy_init_is_operational(&lifecycle), 0);

	lifecycle = (struct ob_phy_init_lifecycle){ 0 };
	check_int("channel boundary lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check_int("channel boundary pre-radio ready", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_PRE_RADIO_READY), 0);
	check_int("channel boundary radio begin", ob_phy_init_begin_radio(&lifecycle), 0);
	check_int("channel boundary prefix complete",
		  ob_phy_init_radio_prefix_complete(&lifecycle), 0);
	lifecycle.analog_tx_lpf_complete = true;
	lifecycle.radio_sequence_complete = true;
	check_int("channel boundary radio complete", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_RADIO_COMPLETE), 0);
	check_int("channel boundary calibration begin", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CALIBRATION_IN_PROGRESS), 0);
	lifecycle.calibration_complete = true;
	check_int("channel boundary calibration complete", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CALIBRATION_COMPLETE), 0);
	check_int("channel boundary programming begin", ob_phy_init_advance(&lifecycle,
		  OB_PHY_INIT_CHANNEL_PROGRAMMING), 0);
	check_int("channel lookup blocker", ob_phy_init_block(&lifecycle,
		  OB_PHY_BLOCK_CHANNEL_LOOKUP), 0);
	check_int("channel blocker is not operational", ob_phy_init_is_operational(&lifecycle), 0);

	memset(&phy, 0, sizeof(phy));
	memset(&f.events, 0, sizeof(f.events));
	f.count = 0;
	ret = ob_acphy_pre_radio_run(&ops, &f, 0x1006, &phy);
	check_int("pre-radio succeeds", ret, 0);
	check_int("unconditional SHM chanspec", f.shm_value, 0x1006);
	check_int("one wrapper transaction for unknown initial bandwidth",
		f.wrapper_calls, 1);
	check_int("wrapper outside bits preserved", f.wrapper,
		  (0xa5a50031u & ~0xc0u) | 0x40u);
	check_int("D11 write value", f.d11_write_value, 0);
	check_int("ordered hardware event count", f.count, 4);
	check_int("event 1 SHM", f.events[0], EV_SHM);
	check_int("event 2 D11 preread", f.events[1], EV_D11_READ);
	check_int("event 3 anacore write", f.events[2], EV_D11_WRITE);
	check_int("event 4 wrapper RMW", f.events[3], EV_AXI_RMW);
	check_int("bandwidth state committed", phy.bandwidth_bits, 0x1000);
	check_bool("bandwidth state valid", phy.bandwidth_valid, true);
	check_int("first local state gate", phy.state_offset_465, 0);
	check_int("second local state gate", phy.state_offset_f88, 0);

	/* Same width skips only the wrapper RMW; all other historical steps run. */
	f.count = 0;
	f.wrapper_calls = 0;
	ret = ob_acphy_pre_radio_run(&ops, &f, 0x100b, &phy);
	check_int("same-width transaction succeeds", ret, 0);
	check_int("same-width wrapper skipped", f.wrapper_calls, 0);
	check_int("same-width events", f.count, 3);

	/* Exercise all mapped bandwidth cflags and the unknown fallback. */
	check_int("20 MHz cflags", ob_acphy_bw_cflags(0x1000), 0x40);
	check_int("40 MHz cflags", ob_acphy_bw_cflags(0x1800), 0x80);
	check_int("80 MHz cflags", ob_acphy_bw_cflags(0x2000), 0xc0);
	check_int("other cflags", ob_acphy_bw_cflags(0), 0);
	{
		struct ob_acphy_pre_radio_state blocked_state = { 0 };
		struct fake_hw blocked = { .lost = true };
		ret = ob_acphy_pre_radio_run(&ops, &blocked, 0x1001,
					     &blocked_state);
		check_int("initial device loss fails closed", ret, -19);
		check_int("initial device loss performs no operation", blocked.count, 0);
	}

	/* Every failing stage returns immediately and leaves local gates untouched. */
	for (int event = EV_SHM; event <= EV_AXI_RMW; event++) {
		struct ob_acphy_pre_radio_state failed_state = { 0 };
		struct fake_hw failed = {
			.fail_event = event + 1,
			.wrapper = 0x80000000u,
		};
		ret = ob_acphy_pre_radio_run(&ops, &failed, 0x1001,
					     &failed_state);
		check_int("injected failure propagated", ret, -5);
		check_int("no later callback after failure", failed.count, event + 1);
		check_int("failed sequence does not set gate 465",
			failed_state.state_offset_465, 0);
		check_int("failed sequence does not set gate f88",
			failed_state.state_offset_f88, 0);
		if (event == EV_AXI_RMW)
			check_bool("failed RMW does not commit bandwidth",
				   failed_state.bandwidth_valid, false);
	}
	{
		struct ob_acphy_pre_radio_state lost_state = { 0 };
		struct fake_hw lost = { .lose_event = EV_D11_READ + 1 };
		ret = ob_acphy_pre_radio_run(&ops, &lost, 0x1001, &lost_state);
		check_int("device loss stops sequence", ret, -19);
		check_int("device loss prevents D11 write", lost.count, 2);
		check_int("device loss leaves later state gates clear",
			lost_state.state_offset_465, 0);
	}

	if (failures) {
		printf("ob_acphy_start_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_acphy_start_test: PASS");
	return 0;
}
