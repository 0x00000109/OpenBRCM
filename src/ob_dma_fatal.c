// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — per-device DMA failure evidence (Phase 34).
 *
 * One fixed-capacity registry of identities whose DMA quiescence could not be
 * verified. It is the per-device replacement for the module-wide boolean that
 * used to refuse every later DMA test on every adapter.
 *
 * The same translation unit builds in the kernel and on the host (with the
 * tests/host shim), so the admission semantics are exercised by real code.
 */
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#endif

#include "ob_dma_fatal.h"

/* ---- portability layer ------------------------------------------------- */

#ifdef __KERNEL__
static DEFINE_SPINLOCK(ob_dma_fatal_lock);
#define OB_FATAL_LOCK(flags)		spin_lock_irqsave(&ob_dma_fatal_lock, flags)
#define OB_FATAL_UNLOCK(flags)		spin_unlock_irqrestore(&ob_dma_fatal_lock, flags)
#define OB_FATAL_PIN()			__module_get(THIS_MODULE)
#else
static pthread_mutex_t ob_dma_fatal_lock = PTHREAD_MUTEX_INITIALIZER;
#define OB_FATAL_LOCK(flags)						\
	do {								\
		(void)(flags);						\
		pthread_mutex_lock(&ob_dma_fatal_lock);			\
	} while (0)
#define OB_FATAL_UNLOCK(flags)						\
	do {								\
		(void)(flags);						\
		pthread_mutex_unlock(&ob_dma_fatal_lock);		\
	} while (0)
#define OB_FATAL_PIN()			do { } while (0)
#endif

/* ---- internal state ---------------------------------------------------- */

struct ob_dma_fatal_slot {
	bool				used;
	struct ob_dma_owner_identity	id;
	struct ob_dma_fatal_evidence	ev;
	char				where[OB_DMA_FATAL_WHERE_LEN];
	u32				latch_calls;
};

static struct ob_dma_fatal_slot ob_dma_fatal_slots[OB_DMA_FATAL_CAP];
static unsigned int ob_dma_fatal_used;
static bool ob_dma_fatal_pinned;
static u32 ob_dma_fatal_pins;

/* ---- helpers ----------------------------------------------------------- */

/* Same key requirement as the owner: an unkeyed device cannot be told apart
 * from any other device, so it is never admitted.
 */
static bool ob_dma_fatal_identity_valid(const struct ob_dma_owner_identity *id)
{
	return id && id->pci_present && id->chip_id != 0 && id->d11_rev != 0;
}

static bool ob_dma_fatal_identity_equal(const struct ob_dma_owner_identity *a,
					const struct ob_dma_owner_identity *b)
{
	if (!a || !b)
		return false;
	return a->pci_domain == b->pci_domain &&
	       a->pci_bus == b->pci_bus &&
	       a->pci_devfn == b->pci_devfn &&
	       a->chip_id == b->chip_id &&
	       a->chip_rev == b->chip_rev &&
	       a->d11_rev == b->d11_rev &&
	       a->pci_present == b->pci_present;
}

/* A replacement board keeps the PCI address. It must inherit the refusal: the
 * previous device's DMA quiescence was never proven.
 */
static bool ob_dma_fatal_same_slot(const struct ob_dma_owner_identity *a,
				   const struct ob_dma_owner_identity *b)
{
	if (!a || !b)
		return false;
	return a->pci_domain == b->pci_domain &&
	       a->pci_bus == b->pci_bus &&
	       a->pci_devfn == b->pci_devfn;
}

static struct ob_dma_fatal_slot *
ob_dma_fatal_match_locked(const struct ob_dma_owner_identity *id)
{
	unsigned int i;

	for (i = 0; i < OB_DMA_FATAL_CAP; i++) {
		struct ob_dma_fatal_slot *s = &ob_dma_fatal_slots[i];

		if (!s->used)
			continue;
		if (ob_dma_fatal_identity_equal(&s->id, id) ||
		    ob_dma_fatal_same_slot(&s->id, id))
			return s;
	}
	return NULL;
}

static void ob_dma_fatal_copy_where(char *dst, const char *src)
{
	size_t n = 0;

	if (!dst)
		return;
	if (src) {
		while (src[n] && n + 1 < OB_DMA_FATAL_WHERE_LEN) {
			dst[n] = src[n];
			n++;
		}
	}
	dst[n] = '\0';
}

/* ---- public API -------------------------------------------------------- */

int ob_dma_fatal_latch(const struct ob_dma_owner_identity *id,
		       const struct ob_dma_fatal_evidence *ev,
		       const char *where, bool *pin_now)
{
	struct ob_dma_fatal_slot *s;
	unsigned long flags;
	bool take_pin = false;
	int ret = 0;

	if (pin_now)
		*pin_now = false;
	if (!ob_dma_fatal_identity_valid(id))
		return -EINVAL;

	OB_FATAL_LOCK(flags);
	if (!ob_dma_fatal_pinned) {
		/* Exactly one module reference per module lifetime: it keeps the
		 * text that holds every retained record from being unloaded.
		 */
		ob_dma_fatal_pinned = true;
		ob_dma_fatal_pins++;
		take_pin = true;
		if (pin_now)
			*pin_now = true;
	}

	s = ob_dma_fatal_match_locked(id);
	if (s) {
		/* Idempotent: keep the first evidence and reason. */
		s->latch_calls++;
		goto out_unlock;
	}
	if (ob_dma_fatal_used >= OB_DMA_FATAL_CAP) {
		/* Cannot prove an unrecorded device unrelated to a retained one. */
		ret = -ENOSPC;
		goto out_unlock;
	}

	s = &ob_dma_fatal_slots[ob_dma_fatal_used++];
	s->used = true;
	s->id = *id;
	if (ev)
		s->ev = *ev;
	ob_dma_fatal_copy_where(s->where, where);
	s->latch_calls = 1;

out_unlock:
	OB_FATAL_UNLOCK(flags);
	/* The registry owns the single module reference, including on a full
	 * registry: the retained memory must not outlive the text even when the
	 * evidence for a new device cannot be stored.
	 */
	if (take_pin)
		OB_FATAL_PIN();
	return ret;
}

bool ob_dma_fatal_refused(const struct ob_dma_owner_identity *id)
{
	unsigned long flags;
	bool refused;

	if (!ob_dma_fatal_identity_valid(id))
		return true;

	OB_FATAL_LOCK(flags);
	if (ob_dma_fatal_used >= OB_DMA_FATAL_CAP)
		refused = true;	/* saturated: nothing can be proven unrelated */
	else
		refused = ob_dma_fatal_match_locked(id) != NULL;
	OB_FATAL_UNLOCK(flags);
	return refused;
}

bool ob_dma_fatal_is_latched(const struct ob_dma_owner_identity *id)
{
	unsigned long flags;
	bool latched;

	if (!ob_dma_fatal_identity_valid(id))
		return false;

	OB_FATAL_LOCK(flags);
	latched = ob_dma_fatal_match_locked(id) != NULL;
	OB_FATAL_UNLOCK(flags);
	return latched;
}

bool ob_dma_fatal_module_pinned(void)
{
	unsigned long flags;
	bool pinned;

	OB_FATAL_LOCK(flags);
	pinned = ob_dma_fatal_pinned;
	OB_FATAL_UNLOCK(flags);
	return pinned;
}

u32 ob_dma_fatal_pin_count(void)
{
	unsigned long flags;
	u32 n;

	OB_FATAL_LOCK(flags);
	n = ob_dma_fatal_pins;
	OB_FATAL_UNLOCK(flags);
	return n;
}

unsigned int ob_dma_fatal_count(void)
{
	unsigned long flags;
	unsigned int n;

	OB_FATAL_LOCK(flags);
	n = ob_dma_fatal_used;
	OB_FATAL_UNLOCK(flags);
	return n;
}

bool ob_dma_fatal_any(void)
{
	return ob_dma_fatal_count() != 0;
}

unsigned int ob_dma_fatal_snapshot(struct ob_dma_fatal_snapshot *out,
				   unsigned int max)
{
	unsigned long flags;
	unsigned int i, written = 0;

	if (!out || !max)
		return 0;

	OB_FATAL_LOCK(flags);
	for (i = 0; i < OB_DMA_FATAL_CAP && written < max; i++) {
		struct ob_dma_fatal_slot *s = &ob_dma_fatal_slots[i];
		struct ob_dma_fatal_snapshot *d = &out[written];

		if (!s->used)
			continue;
		memset(d, 0, sizeof(*d));
		d->id = s->id;
		d->ev = s->ev;
		memcpy(d->where, s->where, sizeof(d->where));
		d->latch_calls = s->latch_calls;
		written++;
	}
	OB_FATAL_UNLOCK(flags);
	return written;
}

#ifndef __KERNEL__
void ob_dma_fatal_reset_for_test(void)
{
	unsigned long flags;

	OB_FATAL_LOCK(flags);
	memset(ob_dma_fatal_slots, 0, sizeof(ob_dma_fatal_slots));
	ob_dma_fatal_used = 0;
	ob_dma_fatal_pinned = false;
	ob_dma_fatal_pins = 0;
	OB_FATAL_UNLOCK(flags);
}
#endif
