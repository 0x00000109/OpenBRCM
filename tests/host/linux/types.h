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

#endif /* _OB_FAKE_LINUX_TYPES_H */
