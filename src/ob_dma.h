/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — DMA64 software model (M3.2).
 *
 * Pure software representation of the recovered BCM4352 / D11 rev42 DMA64
 * descriptor and rings. This header deliberately depends only on the basic
 * kernel types (plus the byteorder helpers) so that the descriptor encoding and
 * the ring index arithmetic can be unit-tested on the host; allocation and
 * teardown live in ob_dma.c.
 *
 * M3.2 is software-only: nothing in this file (or ob_dma.c) programs the D11
 * DMA register blocks (FIFO0 RX @ 0x220, FIFO3 TX @ 0x2c0), publishes a ring
 * base address to hardware, enables an engine or takes an IRQ.
 *
 * Provenance: docs/dma_architecture.md (M3.1).
 */
#ifndef _OB_DMA_H_
#define _OB_DMA_H_

#include <linux/types.h>
#include "ob_dma_owner.h"
#ifdef __KERNEL__
#include <linux/byteorder/generic.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#else
#include <errno.h>
#include <stdatomic.h>
#endif

/* ---- DMA64 descriptor (16 bytes, little-endian on the wire) ---- */
#define OB_DMA_DESC_SIZE	16

/* control word 1 */
#define OB_DMA_CTRL1_EOT	0x10000000u	/* end of descriptor table */
#define OB_DMA_CTRL1_IOC	0x20000000u	/* interrupt on completion */
#define OB_DMA_CTRL1_EOF	0x40000000u	/* end of frame */
#define OB_DMA_CTRL1_SOF	0x80000000u	/* start of frame */

/* control word 2 */
#define OB_DMA_CTRL2_BC_MASK	0x00007fffu	/* buffer byte count */
#define OB_DMA_CTRL2_AE		0x00030000u	/* address extension */
#define OB_DMA_CTRL2_AE_SHIFT	16
#define OB_DMA_CTRL2_PARITY	0x00040000u

/*
 * Descriptor layout. Deliberately NOT a C bitfield: the hardware format is a
 * fixed sequence of 32-bit words and the fields above are applied explicitly
 * with masks/shifts.
 */
struct ob_dma_desc {
	__le32	ctrl1;		/* flags + core-specific control bits */
	__le32	ctrl2;		/* buffer byte count + AE + parity    */
	__le32	addrlow;	/* buffer DMA address [31:0]          */
	__le32	addrhigh;	/* buffer DMA address [63:32]         */
};

_Static_assert(sizeof(struct ob_dma_desc) == OB_DMA_DESC_SIZE,
	       "ob_dma_desc must be exactly 16 bytes");

/* ---- ring geometry (asymmetric: blob-proven RX 256 / TX 512) ---- */
#define OB_DMA_RING_DESC_COUNT_RX	256
#define OB_DMA_RING_DESC_COUNT_TX	512
#define OB_DMA_RING_ACTIVE_BYTES_RX	(OB_DMA_RING_DESC_COUNT_RX * OB_DMA_DESC_SIZE)
#define OB_DMA_RING_ACTIVE_BYTES_TX	(OB_DMA_RING_DESC_COUNT_TX * OB_DMA_DESC_SIZE)
/*
 * The coherent allocation block and the required descriptor-table base
 * alignment remain 8 KiB for BOTH rings (C3: dmadesc_align = 13 whenever any
 * FIFO uses >= D64MAXDD/2 descriptors). The RX ring only activates the first
 * 256 descriptors (4096 B) of its 8 KiB block; see docs/rx_path.md.
 */
#define OB_DMA_RING_BYTES	OB_DMA_RING_ACTIVE_BYTES_TX	/* 8192 */
#define OB_DMA_RING_ALIGN	8192
#define OB_DMA_RX_POST_INIT	64	/* blob nrxpost */
#define OB_DMA_RX_BUFSZ		2048	/* blob rxbufsize (no extra headroom) */

/* Blob: PCIe descriptor/ring high word is dataoffsethigh (0x80000000). */
#define OB_DMA_PCIE_H32		0x80000000u

struct sk_buff;

/* `dma_attach` selects dma64proc exactly when si_core_sflags() bit 12 is 1.
 * This decodes an explicit runtime observation; it does not infer the bit
 * from PCI identity or enable a production backend by itself.
 */
#define OB_SI_SFLAGS_DMA64	(1u << 12)

static inline bool ob_dma64_backend_selected(u32 sflags)
{
	return (sflags & OB_SI_SFLAGS_DMA64) != 0;
}

/* Proven wlc_bmac_attach target and its four TX queue attach sites. */
#define OB_DMA_BACKEND_D11_CORE_ID	0x812u
#define OB_DMA_BACKEND_D11_REVISION	42u

enum ob_dma_attach_context {
	OB_DMA_ATTACH_UNKNOWN = 0,
	OB_DMA_ATTACH_TX_FIFO0,
	OB_DMA_ATTACH_TX_FIFO1,
	OB_DMA_ATTACH_TX_FIFO2,
	OB_DMA_ATTACH_TX_FIFO3,
};

enum ob_dma_backend_choice {
	OB_DMA_BACKEND_UNKNOWN = 0,
	OB_DMA_BACKEND_DMA32PROC,
	OB_DMA_BACKEND_DMA64PROC,
};

/* Interpret a single validated wrapper snapshot using the historical bit-12
 * predicate. This is only the backend that the predicate would choose; it is
 * not evidence that dma_attach ran or installed that callback.
 */
static inline enum ob_dma_backend_choice
ob_sflags_snapshot_classify(bool sample_valid, u32 sflags)
{
	if (!sample_valid)
		return OB_DMA_BACKEND_UNKNOWN;
	return ob_dma64_backend_selected(sflags) ?
		OB_DMA_BACKEND_DMA64PROC : OB_DMA_BACKEND_DMA32PROC;
}

/* Caller-supplied association for an already-read SFLAGS word. This is a
 * pure classifier; it performs no core selection or hardware access. It
 * refuses to classify unless the SFLAGS read and dma_attach context identify
 * the same BCM4352 D11 rev42 core instance.
 */
struct ob_dma_backend_input {
	u32 sflags;
	u16 selected_core_id;
	u8 selected_core_revision;
	u16 selected_core_index;
	u16 sflags_core_id;
	u8 sflags_core_revision;
	u16 sflags_core_index;
	enum ob_dma_attach_context attach_context;
	bool sflags_from_selected_wrapper;
};

static inline enum ob_dma_backend_choice
ob_dma_backend_classify(const struct ob_dma_backend_input *input)
{
	if (!input || !input->sflags_from_selected_wrapper ||
	    input->selected_core_id != OB_DMA_BACKEND_D11_CORE_ID ||
	    input->selected_core_revision != OB_DMA_BACKEND_D11_REVISION ||
	    input->sflags_core_id != input->selected_core_id ||
	    input->sflags_core_revision != input->selected_core_revision ||
	    input->sflags_core_index != input->selected_core_index ||
	    input->attach_context < OB_DMA_ATTACH_TX_FIFO0 ||
	    input->attach_context > OB_DMA_ATTACH_TX_FIFO3)
		return OB_DMA_BACKEND_UNKNOWN;

	return ob_dma64_backend_selected(input->sflags) ?
		OB_DMA_BACKEND_DMA64PROC : OB_DMA_BACKEND_DMA32PROC;
}

/*
 * The recovered BCM4352 model discards the DMA address high dword, so the
 * device can only address a 32-bit host window. Every coherent/streaming DMA
 * address must therefore have a zero high dword; the code re-checks this
 * defensively before encoding a descriptor even though the 32-bit DMA mask
 * should already enforce it.
 */
static inline u32 ob_dma_addr_high32(dma_addr_t addr)
{
	return (u32)(((u64)addr) >> 32);
}

static inline bool ob_dma_addr_in_window(dma_addr_t addr)
{
	return ob_dma_addr_high32(addr) == 0;
}


_Static_assert(OB_DMA_RING_DESC_COUNT_RX == 256,
	       "RX ring must hold 256 descriptors");
_Static_assert(OB_DMA_RING_DESC_COUNT_TX == 512,
	       "TX ring must hold 512 descriptors");
_Static_assert(OB_DMA_RING_ACTIVE_BYTES_RX == 4096,
	       "RX active descriptor table must be 4096 bytes");
_Static_assert(OB_DMA_RING_ACTIVE_BYTES_TX == 8192,
	       "TX active descriptor table must be 8192 bytes");
_Static_assert(OB_DMA_RING_BYTES == 8192,
	       "descriptor allocation block must be exactly 8 KiB");
_Static_assert(OB_DMA_RING_BYTES == OB_DMA_RING_ALIGN,
	       "descriptor block size must equal the 8 KiB alignment requirement");
_Static_assert((OB_DMA_RING_ALIGN & (OB_DMA_RING_ALIGN - 1)) == 0,
	       "ring alignment must be a power of two");

/*
 * Ring roles. The hardware targets are recorded here only as documentation for
 * the later milestones; M3.2 must not program them.
 */
enum ob_dma_ring_role {
	OB_DMA_RING_RX = 0,	/* future D11 FIFO0 RX @ 0x220 */
	OB_DMA_RING_TX,		/* future D11 FIFO3 TX @ 0x2c0 */
};

/* Independent descriptor capacity per role. */
static inline bool ob_dma_ring_role_valid(enum ob_dma_ring_role role)
{
	return role == OB_DMA_RING_RX || role == OB_DMA_RING_TX;
}

static inline u16 ob_dma_ring_count(enum ob_dma_ring_role role)
{
	switch (role) {
	case OB_DMA_RING_RX:
		return OB_DMA_RING_DESC_COUNT_RX;
	case OB_DMA_RING_TX:
		return OB_DMA_RING_DESC_COUNT_TX;
	default:
		return 0;
	}
}

static inline u16 ob_dma_ring_active_bytes(enum ob_dma_ring_role role)
{
	switch (role) {
	case OB_DMA_RING_RX:
		return OB_DMA_RING_ACTIVE_BYTES_RX;
	case OB_DMA_RING_TX:
		return OB_DMA_RING_ACTIVE_BYTES_TX;
	default:
		return 0;
	}
}

/* Require the role-specific descriptor capacity before indexing a ring. */
static inline bool ob_dma_ring_geometry_valid(enum ob_dma_ring_role role,
						     u16 n)
{
	return ob_dma_ring_role_valid(role) && n != 0 &&
	       n == ob_dma_ring_count(role);
}

/* Validate state before any wraparound, occupancy, or slot calculation. */
static inline bool ob_dma_ring_indices_valid(u16 n, u16 head, u16 tail)
{
	return n != 0 && head < n && tail < n;
}

/*
 * Per-slot ownership metadata. A slot owns at most one buffer mapping at a
 * time; @mapped and @skb are the guards used by ob_dma.c so a mapping is
 * unmapped exactly once and an skb is freed/consumed exactly once.
 */
struct ob_dma_slot {
	struct sk_buff	*skb;	/* consumed/freed exactly once */
	dma_addr_t	dma;	/* streaming mapping, 0 when unmapped */
	bool		mapped;	/* true iff @dma must be dma_unmap'd */
	/* Software TX ownership metadata. No field below publishes to the D11. */
	u32		tx_generation;
	u32		tx_sw_generation;
	u16		tx_sw_slot;
	u8		tx_queue;
	u8		tx_state;
};

enum ob_dma_tx_slot_state {
	OB_DMA_TX_SLOT_FREE = 0,
	OB_DMA_TX_SLOT_RESERVED,
	OB_DMA_TX_SLOT_PREPARED,
	OB_DMA_TX_SLOT_SUBMITTED,
	OB_DMA_TX_SLOT_COMPLETED,
	OB_DMA_TX_SLOT_RECLAIMED,
	OB_DMA_TX_SLOT_CANCELED,
};

struct ob_dma_ring {
	void			*desc_cpu;	/* CPU descriptor base        */
	dma_addr_t		desc_dma;	/* DMA descriptor base, 8 KiB */
	void			*alloc_cpu;	/* pool block base to free    */
	dma_addr_t		alloc_dma;	/* pool block DMA base        */
	u16			n;		/* descriptor count           */
	u16			head;		/* producer index (0..n-1)    */
	u16			tail;		/* consumer index (0..n-1)    */
	struct ob_dma_slot	*slot;		/* ownership array            */
	enum ob_dma_ring_role	role;
	bool			allocated;	/* fully constructed          */
};

struct ob_dma {
	struct dma_pool	*pool;		/* 8 KiB-aligned coherent pool */
	struct ob_dma_ring rx;		/* future FIFO0 RX */
	struct ob_dma_ring tx;		/* future FIFO3 TX */
	u32		h32;		/* PCIe descriptor/ring high word */
	bool		mask_ok;	/* coherent/streaming DMA mask set */
};

/* ---- pure descriptor helpers (host-testable) ---- */

static inline void ob_dma_desc_zero(struct ob_dma_desc *d)
{
	d->ctrl1 = 0;
	d->ctrl2 = 0;
	d->addrlow = 0;
	d->addrhigh = 0;
}

/*
 * Encode a descriptor with an EXPLICIT high word. On BCM4352 PCIe the blob
 * discards the DMA address high dword and writes addrhigh = dataoffsethigh =
 * OB_DMA_PCIE_H32, with addrlow = (u32)addr. addr must therefore fit in 32
 * bits (see docs/rx_path.md).
 */
static inline void ob_dma_desc_encode(struct ob_dma_desc *d, dma_addr_t addr,
				      u32 addrhigh, u32 ctrl1, u32 len)
{
	d->ctrl1 = cpu_to_le32(ctrl1);
	d->ctrl2 = cpu_to_le32(len & OB_DMA_CTRL2_BC_MASK);
	d->addrlow = cpu_to_le32((u32)(addr & 0xffffffffu));
	d->addrhigh = cpu_to_le32(addrhigh);
}

/*
 * Encode the four words written by the pinned vendor's sub_dcf8 @ 0xdcf8.
 * This is deliberately separate from ob_dma_desc_encode(): the vendor's
 * word1 is byte-count/address-extension/parity, word2 is dataoffset plus the
 * DMA low address, and word3 is dataoffsethigh. It is a software-only
 * instruction-derived encoder and does not imply that dma64proc is selected
 * for a particular device.
 *
 * Inputs correspond to sub_dcf8(di, desc, dma_low, index, flags, length):
 * dataoffset = di+0xfc, dataoffsethigh = di+0x100, parity_enabled =
 * (di+0x0c)&1. Arithmetic is intentionally modulo 2^32 like the 32-bit
 * instructions. The index is a descriptor-slot index, not a packet ID.
 */
static inline u32 ob_dma64_tx_desc_parity(u32 w0, u32 w1, u32 w2, u32 w3)
{
	u32 x = w0 ^ w1 ^ w2 ^ w3;

	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1u;
}

static inline void ob_dma64_tx_desc_encode(struct ob_dma_desc *d,
					   u32 dataoffset,
					   u32 dma_low,
					   u32 flags,
					   u32 length,
					   u32 dataoffsethigh,
					   bool parity_enabled)
{
	u32 word1 = length & OB_DMA_CTRL2_BC_MASK;
	u32 word2;

	if (dataoffset && (dma_low & 0xc0000000u)) {
		word1 |= ((dma_low >> 30) << 16);
		word2 = dataoffset + (dma_low & 0x3fffffffu);
	} else {
		word2 = dataoffset + dma_low;
	}

	d->ctrl1 = cpu_to_le32(flags);
	d->ctrl2 = cpu_to_le32(word1);
	d->addrlow = cpu_to_le32(word2);
	d->addrhigh = cpu_to_le32(dataoffsethigh);

	if (parity_enabled &&
	    ob_dma64_tx_desc_parity(flags, word1, word2, dataoffsethigh))
		d->ctrl2 = cpu_to_le32(word1 | OB_DMA_CTRL2_PARITY);

}

static inline u32 ob_dma_desc_ctrl1(const struct ob_dma_desc *d)
{
	return le32_to_cpu(d->ctrl1);
}

static inline u32 ob_dma_desc_len(const struct ob_dma_desc *d)
{
	return le32_to_cpu(d->ctrl2) & OB_DMA_CTRL2_BC_MASK;
}

static inline dma_addr_t ob_dma_desc_addr(const struct ob_dma_desc *d)
{
	return (dma_addr_t)le32_to_cpu(d->addrlow) |
	       ((dma_addr_t)le32_to_cpu(d->addrhigh) << 32);
}

static inline u32 ob_dma_desc_addrhigh(const struct ob_dma_desc *d)
{
	return le32_to_cpu(d->addrhigh);
}

static inline u32 ob_dma_desc_addrlow(const struct ob_dma_desc *d)
{
	return le32_to_cpu(d->addrlow);
}

static inline void ob_dma_slot_init(struct ob_dma_slot *s)
{
	s->skb = NULL;
	s->dma = 0;
	s->mapped = false;
	s->tx_generation = 0;
	s->tx_sw_generation = 0;
	s->tx_sw_slot = 0;
	s->tx_queue = 0;
	s->tx_state = OB_DMA_TX_SLOT_FREE;
}

static inline bool ob_dma_full(u16 n, u16 head, u16 tail);
static inline u16 ob_dma_next(u16 n, u16 i);
static inline bool ob_dma_index_valid(u16 n, u16 i);
static inline bool ob_dma_slot_claim(struct ob_dma_slot *s,
				     struct sk_buff *skb, dma_addr_t dma);
static inline struct ob_dma_desc *ob_dma_desc_at(const struct ob_dma_ring *r,
						 u16 index);

/* A TX descriptor table uses the existing one-slot-empty producer/consumer
 * geometry. Out-of-order completions may free slots, but producer capacity
 * remains conservative until the consumer reaches contiguous free slots.
 */
static inline bool ob_dma_tx_ring_usable(const struct ob_dma_ring *ring)
{
	return ring && ring->allocated && ring->role == OB_DMA_RING_TX &&
		ob_dma_ring_geometry_valid(ring->role, ring->n) && ring->desc_cpu &&
		ring->slot && ob_dma_ring_indices_valid(ring->n, ring->head,
						       ring->tail);
}

static inline bool ob_dma_tx_ring_empty(const struct ob_dma_ring *ring)
{
	u16 i;

	if (!ob_dma_tx_ring_usable(ring) || ring->head != ring->tail)
		return false;
	for (i = 0; i < ring->n; i++) {
		if (ring->slot[i].tx_state != OB_DMA_TX_SLOT_FREE ||
		    ring->slot[i].skb || ring->slot[i].mapped)
			return false;
	}
	return true;
}

static inline int ob_dma_tx_ring_reserve(struct ob_dma_ring *ring,
					 struct sk_buff *skb, dma_addr_t dma,
					 u8 queue, u16 sw_slot,
					 u32 sw_generation, u16 *index,
					 u32 *generation)
{
	struct ob_dma_slot *slot;
	u32 next_generation;
	u16 reserved;

	if (!ob_dma_tx_ring_usable(ring) || !skb || !index || !generation)
		return -EINVAL;
	if (ob_dma_full(ring->n, ring->head, ring->tail))
		return -ENOSPC;
	reserved = ring->head;
	slot = &ring->slot[reserved];
	if (slot->tx_state != OB_DMA_TX_SLOT_FREE || slot->skb || slot->mapped)
		return -EUCLEAN;
	if (slot->tx_generation == (u32)~(u32)0)
		return -EOVERFLOW;
	next_generation = slot->tx_generation + 1;
	if (!ob_dma_slot_claim(slot, skb, dma))
		return -EUCLEAN;
	slot->tx_generation = next_generation;
	slot->tx_sw_generation = sw_generation;
	slot->tx_sw_slot = sw_slot;
	slot->tx_queue = queue;
	slot->tx_state = OB_DMA_TX_SLOT_RESERVED;
	ring->head = ob_dma_next(ring->n, reserved);
	*index = reserved;
	*generation = next_generation;
	return 0;
}

static inline bool ob_dma_tx_slot_transition(struct ob_dma_ring *ring,
					     u16 index, u32 generation,
					     u8 from, u8 to)
{
	struct ob_dma_slot *slot;

	if (!ob_dma_tx_ring_usable(ring) || !ob_dma_index_valid(ring->n, index))
		return false;
	slot = &ring->slot[index];
	if (slot->tx_generation != generation || slot->tx_state != from)
		return false;
	slot->tx_state = to;
	return true;
}

static inline bool ob_dma_tx_slot_unmap(struct ob_dma_ring *ring,
					 u16 index, u32 generation)
{
	struct ob_dma_slot *slot;

	if (!ob_dma_tx_ring_usable(ring) || !ob_dma_index_valid(ring->n, index))
		return false;
	slot = &ring->slot[index];
	if (slot->tx_generation != generation || !slot->mapped || !slot->skb)
		return false;
	slot->mapped = false;
	slot->dma = 0;
	return true;
}

static inline bool ob_dma_tx_slot_release(struct ob_dma_ring *ring,
					  u16 index, u32 generation)
{
	struct ob_dma_slot *slot;
	u32 keep_generation;

	if (!ob_dma_tx_ring_usable(ring) || !ob_dma_index_valid(ring->n, index))
		return false;
	slot = &ring->slot[index];
	if (slot->tx_generation != generation || slot->mapped || !slot->skb ||
	    (slot->tx_state != OB_DMA_TX_SLOT_COMPLETED &&
	     slot->tx_state != OB_DMA_TX_SLOT_RECLAIMED &&
	     slot->tx_state != OB_DMA_TX_SLOT_CANCELED))
		return false;
	keep_generation = slot->tx_generation;
	ob_dma_desc_zero(ob_dma_desc_at(ring, index));
	ob_dma_slot_init(slot);
	slot->tx_generation = keep_generation;
	while (ring->tail != ring->head &&
	       ring->slot[ring->tail].tx_state == OB_DMA_TX_SLOT_FREE)
		ring->tail = ob_dma_next(ring->n, ring->tail);
	return true;
}

/*
 * Claim a slot for exactly one (skb, dma) mapping. Returns false if the slot
 * is already owned, which prevents a double-claim.
 */
static inline bool ob_dma_slot_claim(struct ob_dma_slot *s,
				     struct sk_buff *skb, dma_addr_t dma)
{
	if (s->mapped || s->skb)
		return false;
	s->skb = skb;
	s->dma = dma;
	s->mapped = true;
	return true;
}

/*
 * Release a slot exactly once. Returns false and leaves the slot untouched if
 * it is not owned, which prevents a double unmap/free.
 */
static inline bool ob_dma_slot_release(struct ob_dma_slot *s,
				       struct sk_buff **skb, dma_addr_t *dma)
{
	if (!s->mapped || !s->skb)
		return false;
	*skb = s->skb;
	*dma = s->dma;
	ob_dma_slot_init(s);
	return true;
}

/* ---- pure ring index helpers (host-testable) ---- */

static inline u16 ob_dma_next(u16 n, u16 i)
{
	if (!n || i >= n)
		return 0;
	return (u16)((i + 1 == n) ? 0 : (i + 1));
}

static inline u16 ob_dma_prev(u16 n, u16 i)
{
	if (!n || i >= n)
		return 0;
	return (u16)((i == 0) ? (n - 1) : (i - 1));
}

static inline bool ob_dma_index_valid(u16 n, u16 i)
{
	return n != 0 && i < n;
}

static inline bool ob_dma_empty(u16 n, u16 head, u16 tail)
{
	return ob_dma_ring_indices_valid(n, head, tail) && head == tail;
}

static inline bool ob_dma_full(u16 n, u16 head, u16 tail)
{
	return ob_dma_ring_indices_valid(n, head, tail) &&
	       ob_dma_next(n, head) == tail;
}

static inline u16 ob_dma_used(u16 n, u16 head, u16 tail)
{
	if (!ob_dma_ring_indices_valid(n, head, tail))
		return 0;
	return (u16)((head + n - tail) % n);
}

static inline u16 ob_dma_avail(u16 n, u16 head, u16 tail)
{
	if (!ob_dma_ring_indices_valid(n, head, tail))
		return 0;
	return (u16)(n - 1 - ob_dma_used(n, head, tail));
}

static inline u32 ob_dma_desc_offset(u16 index)
{
	return (u32)index * OB_DMA_DESC_SIZE;
}

static inline u32 ob_dma_ring_eot(u16 n, u16 index)
{
	return n && index < n && index == n - 1 ? OB_DMA_CTRL1_EOT : 0;
}

static inline struct ob_dma_desc *ob_dma_desc_at(const struct ob_dma_ring *r,
						 u16 index)
{
	if (!r || !r->desc_cpu ||
	    !ob_dma_ring_geometry_valid(r->role, r->n) ||
	    !ob_dma_index_valid(r->n, index))
		return NULL;
	return (struct ob_dma_desc *)
		((u8 *)r->desc_cpu + ob_dma_desc_offset(index));
}

/* D11 rev>10 dma64 TX channel blocks. These are register offsets from the
 * selected D11 core window, not mac80211 queue numbers. The caller must make
 * an evidence-backed FIFO choice; this helper deliberately does not map ACs.
 */
#define OB_D11_DMA64_TX_FIFO_COUNT	4u
#define OB_D11_DMA64_TX_FIFO0_BASE	0x200u
#define OB_D11_DMA64_TX_FIFO_STRIDE	0x040u
#define OB_D11_DMA64_TX_PTR		0x004u

static inline int ob_dma64_tx_fifo_base(u8 fifo, u32 *base)
{
	if (!base || fifo >= OB_D11_DMA64_TX_FIFO_COUNT)
		return -EINVAL;
	*base = OB_D11_DMA64_TX_FIFO0_BASE +
		(u32)fifo * OB_D11_DMA64_TX_FIFO_STRIDE;
	return 0;
}

/* The current ob_tx_path maps one linear packet to one ring slot. This
 * instruction-derived sub_fafd/sub_dcf8 adapter is intentionally limited to
 * that single-segment case. It does not construct the D11 TX frame header.
 */
static inline int ob_dma64_tx_desc_prepare_single(
		struct ob_dma_ring *ring, u16 index, dma_addr_t dma, u32 length,
		u32 dataoffsetlow, u32 dataoffsethigh, bool parity_enabled)
{
	struct ob_dma_slot *slot;
	struct ob_dma_desc *desc;
	u32 flags = OB_DMA_CTRL1_SOF | OB_DMA_CTRL1_EOF | OB_DMA_CTRL1_IOC;

	if (!ob_dma_tx_ring_usable(ring) || !ob_dma_index_valid(ring->n, index) ||
	    !length || length > OB_DMA_CTRL2_BC_MASK ||
	    ob_dma_addr_high32(dma) != 0)
		return -EINVAL;
	slot = &ring->slot[index];
	if (slot->tx_state != OB_DMA_TX_SLOT_RESERVED || !slot->mapped ||
	    !slot->skb || slot->dma != dma)
		return -ESTALE;
	desc = ob_dma_desc_at(ring, index);
	if (!desc)
		return -EUCLEAN;
	flags |= ob_dma_ring_eot(ring->n, index);
	ob_dma64_tx_desc_encode(desc, dataoffsetlow, (u32)dma, flags, length,
				dataoffsethigh, parity_enabled);
	return 0;
}

struct ob_dma64_tx_publish_ops {
	/* Production implementations may use writel(); host tests use a recorder.
	 * The callback is invoked only after the ordering barriers below.
	 */
	void (*write32)(void *ctx, u32 register_offset, u32 value);
};

/* Publish the producer pointer used by sub_fafd: ring DMA base plus the
 * already-advanced producer index, written to the selected FIFO's PTR (+4).
 * Real OpenBRCM TX does not call this yet: FIFO selection, device readiness,
 * submission/completion serialization, and hardware enablement remain gated.
 */
static inline int ob_dma64_tx_publish_producer(
		const struct ob_dma_ring *ring, u8 fifo, u16 producer,
		const struct ob_dma64_tx_publish_ops *ops, void *ctx)
{
	u32 fifo_base, value;

	if (!ob_dma_tx_ring_usable(ring) || !ops || !ops->write32 ||
	    producer != ring->head || producer >= ring->n ||
	    (((u64)ring->desc_dma & (OB_DMA_RING_ALIGN - 1)) != 0) ||
	    ob_dma_addr_high32(ring->desc_dma) != 0 ||
	    ob_dma64_tx_fifo_base(fifo, &fifo_base))
		return -EINVAL;
	if ((u64)(u32)ring->desc_dma + ob_dma_desc_offset(producer) > 0xffffffffULL)
		return -ERANGE;
	value = (u32)ring->desc_dma + ob_dma_desc_offset(producer);

	/* Coherent descriptor stores must become visible before the MMIO doorbell;
	 * wmb orders the subsequent register write relative to prior CPU stores.
	 */
#ifdef __KERNEL__
	dma_wmb();
	wmb();
#else
	atomic_thread_fence(memory_order_release);
	atomic_thread_fence(memory_order_seq_cst);
#endif
	ops->write32(ctx, fifo_base + OB_D11_DMA64_TX_PTR, value);
	return 0;
}

/* ---- kernel allocation API (ob_dma.c) ---- */
struct ob_hw;
struct bcma_device;

int  ob_dma_init(struct ob_hw *hw);
void ob_dma_free(struct ob_hw *hw);

/*
 * Durable-owner glue (Phase 33).
 *
 * ob_dma_owner_ensure() creates the independent, registry-held owner for this
 * device once. It is idempotent and never replaces an existing owner, so a
 * retained record can never be reset by a later allocation call. Each owner
 * belongs to exactly one @family; a second family on the same owner is
 * refused (-EBUSY) because one finish() must never release both families.
 *
 * ob_dma_owner_core_probe_refused() is the production probe guard. It builds
 * the same registry key from BCMA-enumerated identity only (no MMIO) and is
 * called by ob_probe() before devm_kzalloc(), ob_si_probe() or any firmware,
 * ucode, D11 or PHY work.
 *
 * ob_dma_owner_finish() checks the family, clears that family's resource
 * census and releases the owner. Both steps require independently verified
 * quiescence; with @quiescence_verified == false nothing is released, and a
 * family mismatch leaves the owner attached and untouched.
 *
 * ob_dma_owner_retain_m32()/ob_dma_owner_retain_d3a0() transfer the opaque
 * handles of the M3.2 rings and of the D3A0 rings into the owner and retain
 * it. After adoption the caller must not free, unmap or re-key those
 * resources; the owner (and the module pin it takes) survives `struct ob_hw`.
 */
int  ob_dma_owner_ensure(struct ob_hw *hw, enum ob_dma_owner_family family);
bool ob_dma_owner_core_probe_refused(struct bcma_device *core);

/*
 * Fill @id from BCMA-enumerated identity only (PCI domain/bus/devfn, chip
 * id/rev, D11 rev). No config-space or MMIO access. Shared by the owner key
 * and the Phase 34 per-device fatal-evidence key so both always agree.
 */
void ob_dma_owner_core_identity(struct bcma_device *core,
				struct ob_dma_owner_identity *id);

/*
 * Per-device DMA-fatal admission guard (Phase 34). True only when THIS device
 * (exact identity or the same PCI slot) has a fatal DMA record, the identity
 * is unkeyable, or the evidence registry is saturated. A fatal record on a
 * different PCI function does not block this device.
 */
bool ob_dma_fatal_core_refused(struct bcma_device *core);
void ob_dma_owner_finish(struct ob_hw *hw, enum ob_dma_owner_family family,
			 bool quiescence_verified);
void ob_dma_owner_retain_m32(struct ob_hw *hw,
			     enum ob_dma_owner_retain_reason reason,
			     const char *where);
void ob_dma_owner_retain_d3a0(struct ob_hw *hw,
			      enum ob_dma_owner_retain_reason reason,
			      const char *where);
bool ob_dma_owner_active(const struct ob_hw *hw);

#endif /* _OB_DMA_H_ */
