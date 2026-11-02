// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — durable DMA resource owner (Phase 33).
 *
 * One independently allocated object per admitted device, held by a
 * fixed-capacity module-level registry. It records which DMA resource classes
 * are live, keeps the opaque handles of a device whose DMA quiescence could
 * not be verified, and refuses every release that is not backed by independent
 * quiescence evidence.
 *
 * The object never allocates, maps, unmaps or frees hardware-visible memory.
 * That stays in ob_dma.c / ob_d3a0.c; this file owns accountability and
 * lifetime only, which is what makes it safe to retain across the devres
 * release of `struct ob_hw`.
 *
 * The same translation unit builds in the kernel and on the host (with the
 * tests/host shim) so the lifetime semantics are exercised by real code.
 */
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>
#else
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "ob_dma_owner.h"

/* ---- portability layer ------------------------------------------------- */

#ifdef __KERNEL__
static DEFINE_MUTEX(ob_dma_owner_lock);
#define OB_OWNER_LOCK()		mutex_lock(&ob_dma_owner_lock)
#define OB_OWNER_UNLOCK()	mutex_unlock(&ob_dma_owner_lock)
#define OB_OWNER_ZALLOC(sz)	kzalloc((sz), GFP_KERNEL)
#define OB_OWNER_FREE(p)	kfree(p)
#define OB_OWNER_PIN()		__module_get(THIS_MODULE)
#define OB_OWNER_LOG(fmt, ...)	pr_crit("openbrcm: dma-owner: " fmt, ##__VA_ARGS__)
#else
static pthread_mutex_t ob_dma_owner_lock = PTHREAD_MUTEX_INITIALIZER;
#define OB_OWNER_LOCK()		pthread_mutex_lock(&ob_dma_owner_lock)
#define OB_OWNER_UNLOCK()	pthread_mutex_unlock(&ob_dma_owner_lock)
#define OB_OWNER_ZALLOC(sz)	calloc(1, (sz))
#define OB_OWNER_FREE(p)	free(p)
#define OB_OWNER_PIN()		do { } while (0)
#define OB_OWNER_LOG(fmt, ...)						\
	do {								\
		if (getenv("OB_DMA_OWNER_LOG"))				\
			fprintf(stderr, "dma-owner: " fmt "\n", ##__VA_ARGS__); \
	} while (0)
#endif

/* ---- internal state ---------------------------------------------------- */

struct ob_dma_owner {
	struct ob_dma_owner_identity	id;
	enum ob_dma_owner_family	family;
	enum ob_dma_owner_state		state;
	enum ob_dma_owner_retain_reason	reason;
	char				where[OB_DMA_OWNER_WHERE_LEN];

	u32				live[OB_DMA_OWNER_CLASS_COUNT];
	/* Opaque, never dereferenced. Only the adopting caller knew what these
	 * were; the owner keeps them reachable so the retained state has an
	 * accountable holder after `struct ob_hw` is gone.
	 */
	const void			*handles[OB_DMA_OWNER_CLASS_COUNT]
						 [OB_DMA_OWNER_MAX_HANDLES];
	u32				n_handles[OB_DMA_OWNER_CLASS_COUNT];
	bool				adopted[OB_DMA_OWNER_CLASS_COUNT];

	u32				retain_calls;
	u32				adopt_conflicts;
	u32				refs;
	bool				module_pinned;
};

static struct ob_dma_owner *ob_dma_owner_slots[OB_DMA_OWNER_REGISTRY_CAP];
static bool ob_dma_owner_poisoned;
static u32 ob_dma_owner_pins;

/* ---- small helpers ----------------------------------------------------- */

static bool ob_dma_owner_identity_valid(const struct ob_dma_owner_identity *id)
{
	return id && id->pci_present && id->chip_id != 0 && id->d11_rev != 0;
}

static bool ob_dma_owner_identity_equal(const struct ob_dma_owner_identity *a,
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

/* Same physical slot. A replacement board keeps the address, so it must not
 * inherit (or clear) the previous device's retained record.
 */
static bool ob_dma_owner_same_slot(const struct ob_dma_owner_identity *a,
				   const struct ob_dma_owner_identity *b)
{
	if (!a || !b)
		return false;
	return a->pci_domain == b->pci_domain &&
	       a->pci_bus == b->pci_bus &&
	       a->pci_devfn == b->pci_devfn;
}

static bool ob_dma_owner_any_resource(const struct ob_dma_owner *o)
{
	unsigned int c;

	for (c = 0; c < OB_DMA_OWNER_CLASS_COUNT; c++)
		if (o->live[c] != 0 || o->n_handles[c] != 0)
			return true;
	return false;
}

static unsigned int ob_dma_owner_slot_count_locked(void)
{
	unsigned int i, n = 0;

	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++)
		if (ob_dma_owner_slots[i])
			n++;
	return n;
}

static void ob_dma_owner_copy_where(char *dst, const char *src)
{
	size_t n = 0;

	if (!dst)
		return;
	if (src) {
		while (src[n] && n + 1 < OB_DMA_OWNER_WHERE_LEN) {
			dst[n] = src[n];
			n++;
		}
	}
	dst[n] = '\0';
}

const char *ob_dma_owner_state_name(enum ob_dma_owner_state state)
{
	switch (state) {
	case OB_DMA_OWNER_FREE:
		return "FREE";
	case OB_DMA_OWNER_LIVE:
		return "LIVE";
	case OB_DMA_OWNER_RETAINED:
		return "RETAINED";
	case OB_DMA_OWNER_RELEASED:
		return "RELEASED";
	}
	return "INVALID";
}

const char *ob_dma_owner_reason_name(enum ob_dma_owner_retain_reason reason)
{
	switch (reason) {
	case OB_DMA_OWNER_REASON_NONE:
		return "NONE";
	case OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED:
		return "QUIESCE_UNCONFIRMED";
	case OB_DMA_OWNER_REASON_DEVICE_LOST:
		return "DEVICE_LOST";
	case OB_DMA_OWNER_REASON_FATAL_TEARDOWN:
		return "FATAL_TEARDOWN";
	case OB_DMA_OWNER_REASON_REMOVE_RETAINED:
		return "REMOVE_RETAINED";
	}
	return "INVALID";
}

const char *ob_dma_owner_class_name(enum ob_dma_owner_class cls)
{
	switch (cls) {
	case OB_DMA_OWNER_CLASS_POOL:
		return "pool";
	case OB_DMA_OWNER_CLASS_RING_DESC:
		return "ring_desc";
	case OB_DMA_OWNER_CLASS_RING_SLOT:
		return "ring_slot";
	case OB_DMA_OWNER_CLASS_RX_BUFFER:
		return "rx_buffer";
	case OB_DMA_OWNER_CLASS_TX_BUFFER:
		return "tx_buffer";
	case OB_DMA_OWNER_CLASS_COUNT:
		break;
	}
	return "INVALID";
}

/* ---- lifecycle --------------------------------------------------------- */

int ob_dma_owner_create(const struct ob_dma_owner_identity *id,
			enum ob_dma_owner_family family,
			struct ob_dma_owner **out)
{
	struct ob_dma_owner *o;
	unsigned int i, slot = OB_DMA_OWNER_REGISTRY_CAP;
	int ret = 0;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!ob_dma_owner_identity_valid(id))
		return -ENODEV;
	if (family != OB_DMA_OWNER_FAMILY_M32 &&
	    family != OB_DMA_OWNER_FAMILY_D3A0)
		return -EINVAL;

	OB_OWNER_LOCK();
	if (ob_dma_owner_poisoned) {
		ret = -EBUSY;
		goto out_unlock;
	}
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++) {
		struct ob_dma_owner *cur = ob_dma_owner_slots[i];

		if (!cur)
			continue;
		if (cur->state != OB_DMA_OWNER_RETAINED)
			continue;
		/*
		 * Fail closed: an exact identity, or the same PCI slot with an
		 * unproven replacement, must not receive a fresh owner. The
		 * retained record is never re-keyed or cleared.
		 */
		if (ob_dma_owner_identity_equal(&cur->id, id) ||
		    ob_dma_owner_same_slot(&cur->id, id)) {
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++) {
		if (!ob_dma_owner_slots[i]) {
			slot = i;
			break;
		}
	}
	if (slot == OB_DMA_OWNER_REGISTRY_CAP) {
		/* Saturation is a fail-closed condition, like the FIFO registry:
		 * a later probe cannot be proven unrelated to a retained owner.
		 */
		ob_dma_owner_poisoned = true;
		ret = -ENOSPC;
		goto out_unlock;
	}

	o = OB_OWNER_ZALLOC(sizeof(*o));
	if (!o) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	o->id = *id;
	o->family = family;
	o->state = OB_DMA_OWNER_LIVE;
	o->reason = OB_DMA_OWNER_REASON_NONE;
	/* One reference for the caller, one for the registry entry. */
	o->refs = 2;
	ob_dma_owner_slots[slot] = o;
	*out = o;

out_unlock:
	OB_OWNER_UNLOCK();
	return ret;
}

void ob_dma_owner_get(struct ob_dma_owner *owner)
{
	if (!owner)
		return;
	OB_OWNER_LOCK();
	owner->refs++;
	OB_OWNER_UNLOCK();
}

void ob_dma_owner_put(struct ob_dma_owner *owner)
{
	bool free_it = false;

	if (!owner)
		return;
	OB_OWNER_LOCK();
	if (owner->refs == 0) {
		/* Refcount underflow: refuse to free rather than double-free. */
		OB_OWNER_LOG("put on zero refs (state=%s)",
			     ob_dma_owner_state_name(owner->state));
	} else if (--owner->refs == 0) {
		free_it = true;
	}
	OB_OWNER_UNLOCK();
	if (free_it)
		OB_OWNER_FREE(owner);
}

bool ob_dma_owner_probe_refused(const struct ob_dma_owner_identity *id)
{
	unsigned int i;
	bool refused = false;

	if (!ob_dma_owner_identity_valid(id))
		return true;

	OB_OWNER_LOCK();
	if (ob_dma_owner_poisoned ||
	    ob_dma_owner_slot_count_locked() >= OB_DMA_OWNER_REGISTRY_CAP) {
		refused = true;
		goto out_unlock;
	}
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++) {
		struct ob_dma_owner *cur = ob_dma_owner_slots[i];

		if (!cur || cur->state != OB_DMA_OWNER_RETAINED)
			continue;
		if (ob_dma_owner_identity_equal(&cur->id, id) ||
		    ob_dma_owner_same_slot(&cur->id, id)) {
			refused = true;
			break;
		}
	}

out_unlock:
	OB_OWNER_UNLOCK();
	return refused;
}

/* ---- resource accounting ----------------------------------------------- */

int ob_dma_owner_add(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		     u32 n)
{
	int ret = 0;

	if (!owner || cls >= OB_DMA_OWNER_CLASS_COUNT)
		return -EINVAL;
	OB_OWNER_LOCK();
	if (owner->state != OB_DMA_OWNER_LIVE)
		ret = -EBUSY;
	else if (owner->live[cls] > 0xffffffffu - n)
		ret = -EOVERFLOW;
	else
		owner->live[cls] += n;
	OB_OWNER_UNLOCK();
	return ret;
}

int ob_dma_owner_sub(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		     u32 n)
{
	int ret = 0;

	if (!owner || cls >= OB_DMA_OWNER_CLASS_COUNT)
		return -EINVAL;
	OB_OWNER_LOCK();
	if (owner->state != OB_DMA_OWNER_LIVE)
		ret = -EBUSY;
	else if (owner->live[cls] < n)
		ret = -EINVAL;
	else
		owner->live[cls] -= n;
	OB_OWNER_UNLOCK();
	return ret;
}

bool ob_dma_owner_has_resources(const struct ob_dma_owner *owner)
{
	bool has = false;

	if (!owner)
		return false;
	OB_OWNER_LOCK();
	has = ob_dma_owner_any_resource(owner);
	OB_OWNER_UNLOCK();
	return has;
}

int ob_dma_owner_adopt(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		       const void *const *handles, unsigned int n)
{
	unsigned int i;
	int ret = 0;

	if (!owner || cls >= OB_DMA_OWNER_CLASS_COUNT ||
	    n > OB_DMA_OWNER_MAX_HANDLES || (n && !handles))
		return -EINVAL;

	OB_OWNER_LOCK();
	if (owner->state == OB_DMA_OWNER_RELEASED ||
	    owner->state == OB_DMA_OWNER_FREE) {
		ret = -EINVAL;
	} else if (owner->n_handles[cls] != 0 || owner->adopted[cls]) {
		/* Exactly one accountable owner per resource class. */
		owner->adopt_conflicts++;
		ret = -EEXIST;
	} else {
		for (i = 0; i < n; i++)
			owner->handles[cls][i] = handles[i];
		owner->n_handles[cls] = n;
		owner->adopted[cls] = true;
	}
	OB_OWNER_UNLOCK();
	return ret;
}

int ob_dma_owner_retain(struct ob_dma_owner *owner,
			enum ob_dma_owner_retain_reason reason,
			const char *where, bool module_pin_permitted)
{
	bool first = false;
	int ret = 0;

	if (!owner)
		return -EINVAL;

	OB_OWNER_LOCK();
	if (owner->state == OB_DMA_OWNER_RELEASED ||
	    owner->state == OB_DMA_OWNER_FREE) {
		ret = -EINVAL;
		goto out_unlock;
	}
	owner->retain_calls++;
	if (owner->state == OB_DMA_OWNER_RETAINED) {
		/* Idempotent: keep the first reason, never pin again. */
		goto out_unlock;
	}
	owner->state = OB_DMA_OWNER_RETAINED;
	owner->reason = reason;
	ob_dma_owner_copy_where(owner->where, where);
	if (module_pin_permitted && !ob_dma_owner_pins) {
		/* Module pin is module-wide and exactly once: it keeps the text
		 * that owns the retained handles from being unloaded. It is NOT
		 * evidence that the DMA memory has a live device owner.
		 */
		ob_dma_owner_pins++;
		OB_OWNER_PIN();
	}
	owner->module_pinned = module_pin_permitted || ob_dma_owner_pins != 0;
	first = true;

out_unlock:
	OB_OWNER_UNLOCK();
	if (first)
		OB_OWNER_LOG("%s: retained pci=%08x:%02x:%02x.%u chip=%04x/%u d11=%u reason=%s where=%s; module pinned; release refused",
			     ob_dma_owner_state_name(OB_DMA_OWNER_RETAINED),
			     owner->id.pci_domain, owner->id.pci_bus,
			     (owner->id.pci_devfn >> 3) & 0x1f,
			     owner->id.pci_devfn & 0x7,
			     owner->id.chip_id, owner->id.chip_rev,
			     owner->id.d11_rev,
			     ob_dma_owner_reason_name(owner->reason),
			     owner->where);
	return ret;
}

int ob_dma_owner_clear_resources(struct ob_dma_owner *owner,
				 enum ob_dma_owner_family family,
				 bool quiescence_verified)
{
	int ret = 0;

	if (!owner)
		return -EINVAL;
	OB_OWNER_LOCK();
	if (owner->state == OB_DMA_OWNER_RETAINED) {
		/* A retained allocation must never be declared released. */
		ret = -EBUSY;
	} else if (owner->family != family) {
		/* One family's teardown must never erase another's census. */
		ret = -EPERM;
	} else if (!quiescence_verified) {
		/* No unmap/free may be accounted when quiescence is unknown. */
		ret = -EPERM;
	} else if (owner->state != OB_DMA_OWNER_LIVE) {
		ret = -EINVAL;
	} else {
		memset(owner->live, 0, sizeof(owner->live));
	}
	OB_OWNER_UNLOCK();
	return ret;
}

int ob_dma_owner_release(struct ob_dma_owner *owner,
			 const struct ob_dma_owner_identity *claimant,
			 enum ob_dma_owner_family family,
			 bool quiescence_verified)
{
	unsigned int i;
	int ret = 0;

	if (!owner)
		return -EINVAL;

	OB_OWNER_LOCK();
	if (owner->state == OB_DMA_OWNER_RETAINED) {
		ret = -EBUSY;
		goto out_unlock;
	}
	if (owner->state != OB_DMA_OWNER_LIVE) {
		ret = -EINVAL;
		goto out_unlock;
	}
	if (claimant && !ob_dma_owner_identity_equal(claimant, &owner->id)) {
		/* Device A must never release resources belonging to B. */
		ret = -EPERM;
		goto out_unlock;
	}
	if (owner->family != family) {
		/* A D3A0 teardown must never release an M3.2 allocation. */
		ret = -EPERM;
		goto out_unlock;
	}
	if (!quiescence_verified) {
		ret = -EPERM;
		goto out_unlock;
	}
	if (ob_dma_owner_any_resource(owner)) {
		/* Refuse rather than drop the last reference to live memory. */
		ret = -EBUSY;
		goto out_unlock;
	}
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++)
		if (ob_dma_owner_slots[i] == owner)
			ob_dma_owner_slots[i] = NULL;
	owner->state = OB_DMA_OWNER_RELEASED;
	owner->refs--;	/* drop the registry reference */

out_unlock:
	OB_OWNER_UNLOCK();
	return ret;
}

bool ob_dma_owner_is_retained(const struct ob_dma_owner *owner)
{
	bool retained = false;

	if (!owner)
		return false;
	OB_OWNER_LOCK();
	retained = owner->state == OB_DMA_OWNER_RETAINED;
	OB_OWNER_UNLOCK();
	return retained;
}

enum ob_dma_owner_family ob_dma_owner_family(const struct ob_dma_owner *owner)
{
	enum ob_dma_owner_family family;

	if (!owner)
		return OB_DMA_OWNER_FAMILY_NONE;
	OB_OWNER_LOCK();
	family = owner->family;
	OB_OWNER_UNLOCK();
	return family;
}

enum ob_dma_owner_state ob_dma_owner_state(const struct ob_dma_owner *owner)
{
	enum ob_dma_owner_state state;

	if (!owner)
		return OB_DMA_OWNER_FREE;
	OB_OWNER_LOCK();
	state = owner->state;
	OB_OWNER_UNLOCK();
	return state;
}

u32 ob_dma_owner_live(const struct ob_dma_owner *owner,
		      enum ob_dma_owner_class cls)
{
	u32 n = 0;

	if (!owner || cls >= OB_DMA_OWNER_CLASS_COUNT)
		return 0;
	OB_OWNER_LOCK();
	n = owner->live[cls];
	OB_OWNER_UNLOCK();
	return n;
}

/* ---- registry / diagnostics -------------------------------------------- */

unsigned int ob_dma_owner_registry_capacity(void)
{
	return OB_DMA_OWNER_REGISTRY_CAP;
}

unsigned int ob_dma_owner_registry_count(void)
{
	unsigned int n;

	OB_OWNER_LOCK();
	n = ob_dma_owner_slot_count_locked();
	OB_OWNER_UNLOCK();
	return n;
}

unsigned int ob_dma_owner_registry_retained_count(void)
{
	unsigned int i, n = 0;

	OB_OWNER_LOCK();
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++)
		if (ob_dma_owner_slots[i] &&
		    ob_dma_owner_slots[i]->state == OB_DMA_OWNER_RETAINED)
			n++;
	OB_OWNER_UNLOCK();
	return n;
}

bool ob_dma_owner_registry_poisoned(void)
{
	bool poisoned;

	OB_OWNER_LOCK();
	poisoned = ob_dma_owner_poisoned;
	OB_OWNER_UNLOCK();
	return poisoned;
}

u32 ob_dma_owner_pin_count(void)
{
	u32 n;

	OB_OWNER_LOCK();
	n = ob_dma_owner_pins;
	OB_OWNER_UNLOCK();
	return n;
}

unsigned int ob_dma_owner_registry_snapshot(struct ob_dma_owner_snapshot *out,
					    unsigned int max)
{
	unsigned int i, written = 0;

	if (!out || !max)
		return 0;
	OB_OWNER_LOCK();
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP && written < max; i++) {
		struct ob_dma_owner *cur = ob_dma_owner_slots[i];
		struct ob_dma_owner_snapshot *s = &out[written];
		unsigned int c;

		if (!cur)
			continue;
		memset(s, 0, sizeof(*s));
		s->identity = cur->id;
		s->family = cur->family;
		s->state = cur->state;
		s->reason = cur->reason;
		memcpy(s->where, cur->where, sizeof(s->where));
		for (c = 0; c < OB_DMA_OWNER_CLASS_COUNT; c++) {
			s->live[c] = cur->live[c];
			s->adopted[c] = cur->adopted[c] ? 1u : 0u;
			s->handles[c] = cur->n_handles[c];
		}
		s->retain_calls = cur->retain_calls;
		s->adopt_conflicts = cur->adopt_conflicts;
		s->refs = cur->refs;
		s->module_pinned = cur->module_pinned;
		written++;
	}
	OB_OWNER_UNLOCK();
	return written;
}

#ifndef __KERNEL__
void ob_dma_owner_registry_reset_for_test(void)
{
	unsigned int i;

	OB_OWNER_LOCK();
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++)
		ob_dma_owner_slots[i] = NULL;
	ob_dma_owner_poisoned = false;
	ob_dma_owner_pins = 0;
	OB_OWNER_UNLOCK();
}
#endif
