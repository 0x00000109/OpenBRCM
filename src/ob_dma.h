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
#ifdef __KERNEL__
#include <linux/byteorder/generic.h>
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

/* ---- ring geometry ---- */
#define OB_DMA_RING_DESC_COUNT	512
#define OB_DMA_RING_BYTES	(OB_DMA_RING_DESC_COUNT * OB_DMA_DESC_SIZE)
#define OB_DMA_RING_ALIGN	8192

_Static_assert(OB_DMA_RING_DESC_COUNT == 512,
	       "descriptor ring must hold 512 descriptors");
_Static_assert(OB_DMA_RING_BYTES == 8192,
	       "descriptor ring must be exactly 8 KiB");
_Static_assert(OB_DMA_RING_BYTES == OB_DMA_RING_ALIGN,
	       "descriptor ring size must equal the 8 KiB alignment requirement");
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

/*
 * Per-slot ownership metadata. A slot owns at most one buffer mapping at a
 * time; @mapped and @skb are the guards used by ob_dma.c so a mapping is
 * unmapped exactly once and an skb is freed/consumed exactly once.
 */
struct ob_dma_slot {
	struct sk_buff	*skb;	/* consumed/freed exactly once */
	dma_addr_t	dma;	/* streaming mapping, 0 when unmapped */
	bool		mapped;	/* true iff @dma must be dma_unmap'd */
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
	bool		mask64;		/* 64-bit DMA mask accepted */
};

/* ---- pure descriptor helpers (host-testable) ---- */

static inline void ob_dma_desc_zero(struct ob_dma_desc *d)
{
	d->ctrl1 = 0;
	d->ctrl2 = 0;
	d->addrlow = 0;
	d->addrhigh = 0;
}

static inline void ob_dma_desc_encode(struct ob_dma_desc *d, dma_addr_t addr,
				      u32 ctrl1, u32 len)
{
	d->ctrl1 = cpu_to_le32(ctrl1);
	d->ctrl2 = cpu_to_le32(len & OB_DMA_CTRL2_BC_MASK);
	d->addrlow = cpu_to_le32((u32)(addr & 0xffffffffu));
	d->addrhigh = cpu_to_le32((u32)((u64)addr >> 32));
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

static inline void ob_dma_slot_init(struct ob_dma_slot *s)
{
	s->skb = NULL;
	s->dma = 0;
	s->mapped = false;
}

/* ---- pure ring index helpers (host-testable) ---- */

static inline u16 ob_dma_next(u16 n, u16 i)
{
	return (u16)((i + 1 == n) ? 0 : (i + 1));
}

static inline u16 ob_dma_prev(u16 n, u16 i)
{
	return (u16)((i == 0) ? (n - 1) : (i - 1));
}

static inline bool ob_dma_index_valid(u16 n, u16 i)
{
	return i < n;
}

static inline bool ob_dma_empty(u16 n, u16 head, u16 tail)
{
	(void)n;
	return head == tail;
}

static inline bool ob_dma_full(u16 n, u16 head, u16 tail)
{
	return ob_dma_next(n, head) == tail;
}

static inline u16 ob_dma_used(u16 n, u16 head, u16 tail)
{
	return (u16)((head + n - tail) % n);
}

static inline u16 ob_dma_avail(u16 n, u16 head, u16 tail)
{
	return (u16)(n - 1 - ob_dma_used(n, head, tail));
}

static inline u32 ob_dma_desc_offset(u16 index)
{
	return (u32)index * OB_DMA_DESC_SIZE;
}

static inline u32 ob_dma_ring_eot(u16 n, u16 index)
{
	return (index == n - 1) ? OB_DMA_CTRL1_EOT : 0;
}

static inline struct ob_dma_desc *ob_dma_desc_at(const struct ob_dma_ring *r,
						 u16 index)
{
	return (struct ob_dma_desc *)
		((u8 *)r->desc_cpu + ob_dma_desc_offset(index));
}

/* ---- kernel allocation API (ob_dma.c) ---- */
struct ob_hw;

int  ob_dma_init(struct ob_hw *hw);
void ob_dma_free(struct ob_hw *hw);

#endif /* _OB_DMA_H_ */
