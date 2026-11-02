/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — durable DMA resource owner (Phase 33).
 *
 * Why this exists
 * ---------------
 * Up to Phase 32 every DMA object (dma_pool, descriptor rings, slot arrays,
 * mapped RX skbs) was reachable only through `struct ob_hw`, which probe
 * allocates with devm_kzalloc(&core->dev).  A fatal path deliberately retains
 * those objects instead of freeing memory the device may still own, but
 * devres_release_all() then releases `struct ob_hw` after ob_remove()
 * returns.  The retained allocation survives while its only owner becomes
 * unreachable: an UNSAFE state (Phase 32 DMA_OWNERSHIP_IMPLEMENTATION_REPORT).
 *
 * This object is NOT a field of devm-managed `struct ob_hw`.  It is allocated
 * on its own, held by a module-level registry, and it retains the opaque
 * resource handles when quiescence cannot be proven.  `struct ob_hw` keeps at
 * most a non-owning handle (`hw->dma_owner`), which is cleared on transfer.
 *
 * Hard rules encoded here
 * -----------------------
 * - Retention is idempotent and never pins the module twice.
 * - A retained owner is never released, never freed and never re-keyed.
 * - Resources may be declared released only with independent quiescence
 *   evidence; the API cannot free hardware-visible memory itself.
 * - A retained identity refuses a new owner for the same identity, including
 *   a replacement device at the same PCI address.
 * - The registry is fixed-capacity and fails closed on exhaustion.
 *
 * This object owns accountability and lifetime, not the DMA programming
 * mechanism: ob_dma.c / ob_d3a0.c still perform every map/unmap/free.
 */

#ifndef _OB_DMA_OWNER_H_
#define _OB_DMA_OWNER_H_

#include <linux/types.h>

/* Registry capacity. Small on purpose, like the FIFO failure registry: a
 * retention event is a reboot-required state, not an expected steady state.
 */
#define OB_DMA_OWNER_REGISTRY_CAP	8u

/* Opaque pointer slots kept per resource class. Only ever stored, never
 * dereferenced: the owner must not touch hardware-visible memory.
 */
#define OB_DMA_OWNER_MAX_HANDLES	16u

#define OB_DMA_OWNER_WHERE_LEN		40u

/* ---- stable device identity ------------------------------------------- */
/*
 * The key is the full admission identity, not the PCI address alone: a
 * replacement board in the same slot has the same bus/devfn but keeps its own
 * chip/D11 identity, and a retained record must not be inherited by a device
 * that was never proven to be the same silicon.
 */
struct ob_dma_owner_identity {
	u32	pci_domain;
	u8	pci_bus;
	u8	pci_devfn;
	u16	chip_id;
	u8	chip_rev;
	u8	d11_rev;
	bool	pci_present;
};

/*
 * Which producer owns the allocation. M3.2 (normal probe) and D3A0
 * (dma_test_only diagnostics) are mutually exclusive for one `struct ob_hw`:
 * they program different engines over the same D11 core, and neither path has
 * a quiescence proof that would make the other family's resources safe to
 * touch. An owner therefore belongs to exactly one family; every clear or
 * release must name it, so a teardown can never erase another family's
 * accounting.
 */
enum ob_dma_owner_family {
	OB_DMA_OWNER_FAMILY_NONE = 0,
	OB_DMA_OWNER_FAMILY_M32,	/* src/ob_dma.c RX/TX descriptor rings   */
	OB_DMA_OWNER_FAMILY_D3A0,	/* src/ob_d3a0.c four TX + FIFO0 RX      */
};

enum ob_dma_owner_state {
	OB_DMA_OWNER_FREE = 0,	/* registry slot unused                  */
	OB_DMA_OWNER_LIVE,	/* created, resources may be live        */
	OB_DMA_OWNER_RETAINED,	/* quiescence unproven: never released   */
	OB_DMA_OWNER_RELEASED,	/* terminal: resources proven released   */
};

enum ob_dma_owner_class {
	OB_DMA_OWNER_CLASS_POOL = 0,	/* dma_pool object               */
	OB_DMA_OWNER_CLASS_RING_DESC,	/* coherent descriptor block(s)  */
	OB_DMA_OWNER_CLASS_RING_SLOT,	/* per-slot ownership array(s)   */
	OB_DMA_OWNER_CLASS_RX_BUFFER,	/* mapped RX skbs                */
	OB_DMA_OWNER_CLASS_TX_BUFFER,	/* reserved/mapped TX skbs       */
	OB_DMA_OWNER_CLASS_COUNT,
};

/* Why the owner was retained. Recorded once, at the first retention. */
enum ob_dma_owner_retain_reason {
	OB_DMA_OWNER_REASON_NONE = 0,
	OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED,
	OB_DMA_OWNER_REASON_DEVICE_LOST,
	OB_DMA_OWNER_REASON_FATAL_TEARDOWN,
	OB_DMA_OWNER_REASON_REMOVE_RETAINED,
};

struct ob_dma_owner;

/* Read-only view for diagnostics and tests. */
struct ob_dma_owner_snapshot {
	struct ob_dma_owner_identity	identity;
	enum ob_dma_owner_family	family;
	enum ob_dma_owner_state		state;
	enum ob_dma_owner_retain_reason	reason;
	char				where[OB_DMA_OWNER_WHERE_LEN];
	u32				live[OB_DMA_OWNER_CLASS_COUNT];
	u32				adopted[OB_DMA_OWNER_CLASS_COUNT];
	u32				handles[OB_DMA_OWNER_CLASS_COUNT];
	u32				retain_calls;
	u32				adopt_conflicts;
	u32				refs;
	bool				module_pinned;
};

/* ---- lifecycle -------------------------------------------------------- */
/*
 * Create and register an owner. Requires a PCI-keyed identity: an unkeyed
 * device cannot be distinguished from any other, so it is refused (-ENODEV)
 * rather than admitted into a shared namespace. @family is required and is
 * fixed for the object's lifetime. Returns -ENOSPC and poisons the registry
 * on exhaustion, -EBUSY on a poisoned registry or when a retained owner
 * already exists for the same (or a conflicting) identity.
 */
int ob_dma_owner_create(const struct ob_dma_owner_identity *id,
			enum ob_dma_owner_family family,
			struct ob_dma_owner **out);

/* Add a reference; the registry holds one until release. */
void ob_dma_owner_get(struct ob_dma_owner *owner);
void ob_dma_owner_put(struct ob_dma_owner *owner);

/* True when this identity must be refused: retained record, poisoned
 * registry, or an unkeyable identity. Does not take a reference.
 */
bool ob_dma_owner_probe_refused(const struct ob_dma_owner_identity *id);

/* ---- resource accounting --------------------------------------------- */
int ob_dma_owner_add(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		     u32 n);
int ob_dma_owner_sub(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		     u32 n);
bool ob_dma_owner_has_resources(const struct ob_dma_owner *owner);

/*
 * Copy the opaque handles of one resource class into the owner. This is the
 * ownership transfer: after it succeeds the caller must not free, unmap or
 * re-key what it handed over. Refuses duplicates (one accountable owner) and
 * refuses after release.
 */
int ob_dma_owner_adopt(struct ob_dma_owner *owner, enum ob_dma_owner_class cls,
		       const void *const *handles, unsigned int n);

/*
 * Retain the owner because quiescence is not proven. Idempotent: a second call
 * records the event but neither re-adopts nor pins the module again. A
 * retained owner keeps its registry reference for the rest of the module
 * lifetime and is never returned to the allocator.
 *
 * @module_pin_permitted is false when the caller knows the module is already
 * pinned for this same retention event (the pre-existing D3A0 fatal latch and
 * the FIFO evidence pin each take their own). It exists so one fatal event
 * never adds a second module reference.
 */
int ob_dma_owner_retain(struct ob_dma_owner *owner,
			enum ob_dma_owner_retain_reason reason,
			const char *where, bool module_pin_permitted);

/*
 * Declare every tracked resource of @family released. Requires independent
 * quiescence evidence, a non-retained owner and the owner's own family; this
 * call never frees memory itself. A different family is refused (-EPERM) so a
 * D3A0 teardown cannot erase the M3.2 census (or vice versa).
 */
int ob_dma_owner_clear_resources(struct ob_dma_owner *owner,
				 enum ob_dma_owner_family family,
				 bool quiescence_verified);

/*
 * Release the owner. Refused (-EPERM) without quiescence evidence, refused
 * (-EPERM) when @claimant names a different device, refused (-EPERM) when
 * @family is not the owner's family, refused (-EBUSY) while a resource is
 * still accounted, and refused (-EBUSY) once retained. On success the
 * registry slot is freed and the object drops its reference; a caller that
 * still holds a reference keeps the (empty, released) object alive.
 */
int ob_dma_owner_release(struct ob_dma_owner *owner,
			 const struct ob_dma_owner_identity *claimant,
			 enum ob_dma_owner_family family,
			 bool quiescence_verified);

bool ob_dma_owner_is_retained(const struct ob_dma_owner *owner);
enum ob_dma_owner_family ob_dma_owner_family(const struct ob_dma_owner *owner);
enum ob_dma_owner_state ob_dma_owner_state(const struct ob_dma_owner *owner);
u32 ob_dma_owner_live(const struct ob_dma_owner *owner,
		      enum ob_dma_owner_class cls);

/* ---- registry / diagnostics (read-only) ------------------------------- */
unsigned int ob_dma_owner_registry_capacity(void);
unsigned int ob_dma_owner_registry_count(void);
unsigned int ob_dma_owner_registry_retained_count(void);
bool ob_dma_owner_registry_poisoned(void);
u32 ob_dma_owner_pin_count(void);

/* Fills up to @max snapshots, returns the number written. */
unsigned int ob_dma_owner_registry_snapshot(struct ob_dma_owner_snapshot *out,
					    unsigned int max);

const char *ob_dma_owner_state_name(enum ob_dma_owner_state state);
const char *ob_dma_owner_reason_name(enum ob_dma_owner_retain_reason reason);
const char *ob_dma_owner_class_name(enum ob_dma_owner_class cls);

/* Host/oracle only: drop every entry and reset counters. Never compiled into
 * the kernel module, so a retained record cannot be cleared at runtime.
 */
#ifndef __KERNEL__
void ob_dma_owner_registry_reset_for_test(void);
#endif

#endif /* _OB_DMA_OWNER_H_ */
