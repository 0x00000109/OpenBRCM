// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host-only public ABI checks against the retained Broadcom header snapshot.
 * This validates compiler layout under the selected typedefs.h test mode; it
 * does not validate a wl binary or any private driver/firmware structures.
 */
#include "typedefs.h"
#include "pcicfg.h"
#include "wlioctl.h"
#include "bcmwifi_channels.h"

#define ABI_ASSERT(expr, message) _Static_assert((expr), message)
#define ABI_OFFSETOF(type, member) __builtin_offsetof(type, member)

ABI_ASSERT(__CHAR_BIT__ == 8, "compiler target has 8-bit bytes");
ABI_ASSERT(sizeof(void *) == __SIZEOF_POINTER__, "compiler pointer width matches target macro");
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "compiler must expose byte-order target macros"
#endif
#if defined(__x86_64__) || defined(__i386__)
ABI_ASSERT(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "x86 ABI fixture is little-endian");
#else
#error "public ABI fixture is validated only for x86-64 and i386 targets"
#endif

ABI_ASSERT(sizeof(uint) == 4, "Broadcom uint width");
ABI_ASSERT(sizeof(uint32) == 4, "Broadcom uint32 width");
ABI_ASSERT(sizeof(chanspec_t) == 2, "public chanspec_t width");
ABI_ASSERT(_Alignof(chanspec_t) == 2, "public chanspec_t alignment");

ABI_ASSERT(sizeof(wl_rateset_t) == 20, "public wl_rateset_t size");
ABI_ASSERT(_Alignof(wl_rateset_t) == 4, "public wl_rateset_t alignment");
ABI_ASSERT(ABI_OFFSETOF(wl_rateset_t, count) == 0, "wl_rateset_t.count offset");
ABI_ASSERT(ABI_OFFSETOF(wl_rateset_t, rates) == 4, "wl_rateset_t.rates offset");

ABI_ASSERT(PCI_BAR0_WIN == 0x80, "PCI primary BAR0 window selector");
ABI_ASSERT(PCIE2_BAR0_WIN2 == 0x70, "PCIe2 second BAR0 window selector");
ABI_ASSERT(PCI_BAR0_WIN2 == 0xac, "legacy PCI second BAR0 selector");
ABI_ASSERT(PCI_BAR0_WINSZ == 16384, "BAR0 selector aperture size");
ABI_ASSERT(WLC_GET_REVINFO == 98, "public WLC_GET_REVINFO ioctl number");
ABI_ASSERT(WL_CHANSPEC_BW_MASK == 0x3800, "modern chanspec bandwidth mask");
ABI_ASSERT(WL_LCHANSPEC_BW_MASK == 0x0c00, "legacy chanspec bandwidth mask");

#if __SIZEOF_POINTER__ == 8
ABI_ASSERT(sizeof(wl_ioctl_t) == 32, "64-bit wl_ioctl_t size");
ABI_ASSERT(_Alignof(wl_ioctl_t) == 8, "64-bit wl_ioctl_t alignment");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, cmd) == 0, "64-bit wl_ioctl_t.cmd offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, buf) == 8, "64-bit wl_ioctl_t.buf offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, len) == 16, "64-bit wl_ioctl_t.len offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, set) == 20, "64-bit wl_ioctl_t.set offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, used) == 24, "64-bit wl_ioctl_t.used offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, needed) == 28, "64-bit wl_ioctl_t.needed offset");
#elif __SIZEOF_POINTER__ == 4
ABI_ASSERT(sizeof(wl_ioctl_t) == 24, "32-bit wl_ioctl_t size");
ABI_ASSERT(_Alignof(wl_ioctl_t) == 4, "32-bit wl_ioctl_t alignment");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, cmd) == 0, "32-bit wl_ioctl_t.cmd offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, buf) == 4, "32-bit wl_ioctl_t.buf offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, len) == 8, "32-bit wl_ioctl_t.len offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, set) == 12, "32-bit wl_ioctl_t.set offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, used) == 16, "32-bit wl_ioctl_t.used offset");
ABI_ASSERT(ABI_OFFSETOF(wl_ioctl_t, needed) == 20, "32-bit wl_ioctl_t.needed offset");
#else
#error "ABI checks support only 32-bit and 64-bit pointer targets"
#endif
