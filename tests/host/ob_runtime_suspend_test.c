/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_lifecycle.h"

struct fake {
	u32 maccontrol, status;
	int reads, writes, delays, fail_read;
	u16 off[16];
	u32 val[16];
};

static int rd(void *p, u16 off, u32 *v)
{
	struct fake *f = p;
	if (f->fail_read && ++f->reads == f->fail_read)
		return -5;
	if (off == OB_D11_REG_MACCONTROL) *v = f->maccontrol;
	else *v = f->status;
	return 0;
}
static int wr(void *p, u16 off, u32 v)
{
	struct fake *f = p;
	if (f->writes < 16) { f->off[f->writes] = off; f->val[f->writes] = v; }
	f->writes++;
	if (off == OB_D11_REG_MACCONTROL) f->maccontrol = v;
	else f->status &= ~v;
	return 0;
}
static void delay(void *p, u32 us) { (void)us; ((struct fake *)p)->delays++; }
static int fails;
static void chk(const char *n, int a, int b) { if (a != b) { printf("FAIL %s %d/%d\n",n,a,b); fails++; } }

int main(void)
{
	const struct ob_d11_runtime_io io = { rd, wr, delay };
	bool transitioned = true;
	struct fake f = { .maccontrol = 1, .status = OB_D11_MI_MACSSPNDD };
	u32 polls = 99;
	chk("suspend", ob_d11_runtime_suspend_script(&io, &f, &polls), 0);
	chk("clear EN_MAC", f.val[0], 0);
	chk("no delay", f.delays, 0);
	f = (struct fake){ .maccontrol = 1, .status = 0 };
	chk("timeout", ob_d11_runtime_suspend_script(&io, &f, 0), -110);
	chk("timeout writes", f.writes, 1);
	f = (struct fake){ .maccontrol = 1, .status = OB_D11_MI_MACSSPNDD };
	chk("resume", ob_d11_runtime_resume_script(&io, &f), 0);
	chk("resume writes", f.writes, 2);
	chk("resume enable", f.val[0], 1);
	f = (struct fake){ .maccontrol = 0xffffffffu, .status = 0 };
	chk("all ones", ob_d11_runtime_suspend_script(&io, &f, 0), -5);
	/* Hardware-observed POST_UCODE_READY state from the class-0 run. */
	f = (struct fake){ .maccontrol = 0x04020402u, .status = 0 };
	transitioned = true;
	polls = 99;
	chk("already quiesced", ob_d11_runtime_quiesce_script(&io, &f,
							  &polls, &transitioned), 0);
	chk("already quiesced polls", polls, 0);
	chk("already quiesced owns no suspend", transitioned, false);
	chk("already quiesced no write", f.writes, 0);
	chk("already quiesced no delay", f.delays, 0);
	f = (struct fake){ .maccontrol = 0x04020400u, .status = 0 };
	chk("unknown disabled state", ob_d11_runtime_quiesce_script(&io, &f,
							 0, &transitioned), -22);
	f = (struct fake){ .maccontrol = 1, .status = OB_D11_MI_MACSSPNDD };
	transitioned = false;
	chk("running quiesce", ob_d11_runtime_quiesce_script(&io, &f,
							 0, &transitioned), 0);
	chk("running quiesce owns suspend", transitioned, true);
	if (fails) return 1;
	puts("ob_runtime_suspend_test: PASS");
	return 0;
}
