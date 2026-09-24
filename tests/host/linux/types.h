/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal host shim so the pure-math sources build outside the kernel. */
#ifndef _OB_FAKE_LINUX_TYPES_H
#define _OB_FAKE_LINUX_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef uint64_t dma_addr_t;

/* Little-endian on-wire types: the host is little-endian, so the byteorder
 * helpers below are identity. They exist so ob_dma.h builds unmodified. */
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

#define cpu_to_le16(x) ((__le16)(uint16_t)(x))
#define cpu_to_le32(x) ((__le32)(uint32_t)(x))
#define cpu_to_le64(x) ((__le64)(uint64_t)(x))
#define le16_to_cpu(x) ((uint16_t)(x))
#define le32_to_cpu(x) ((uint32_t)(x))
#define le64_to_cpu(x) ((uint64_t)(x))

#endif /* _OB_FAKE_LINUX_TYPES_H */
