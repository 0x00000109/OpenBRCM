/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — mac80211 source-compatibility layer.
 *
 * Only the API generations consumed by ob_mac80211.c are gated here, each by
 * the release that introduced its current form. Every boundary below was read
 * from the real include/net/mac80211.h of the releases on both sides of it
 * (5.10, 5.15, 5.17, 6.0, 6.1, 6.3-6.12.114, 6.13-6.18, 7.0.0-38):
 *
 *   feature                  old form                    new form           since
 *   .stop                    stop(hw)                    stop(hw, bool)      6.11
 *   .bss_info_changed        (... info, u32 changed)      (..., u64 changed)  6.0
 *   TX status entry point    ieee80211_tx_status()       ieee80211_tx_status_skb()  6.7
 *   .config                  config(hw, u32)             (+int radio_idx)    6.17
 *   chanctx emulation        absent                      ieee80211_emulate_* 6.9
 *
 * The .wake_tx_queue helper (ieee80211_handle_wake_tx_queue, present since
 * 6.2) is enabled in the same group, so it is available wherever the group
 * applies. Build-verified targets: 6.12.114 and 7.0.0-38. 5.10 (the original
 * stand kernel) keeps the old branches; it is not built in this phase.
 */
#ifndef _OB_MAC80211_COMPAT_H_
#define _OB_MAC80211_COMPAT_H_

#include <linux/version.h>

/* struct ieee80211_ops.stop(hw, bool suspend) */
#define OB_MAC80211_STOP_HAS_SUSPEND \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))

/* bss_info_changed(): 64-bit changed mask instead of u32 */
#define OB_MAC80211_BSS_INFO_CHANGED_U64 \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0))

/* TX status entry point renamed from ieee80211_tx_status() to _skb() */
#define OB_MAC80211_TX_STATUS_SKB \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0))

/* struct ieee80211_ops.config(hw, int radio_idx, u32 changed) */
#define OB_MAC80211_CONFIG_HAS_RADIO_IDX \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0))

/* ieee80211_emulate_{add,remove,change,switch_vif}_chanctx() exist, and
 * ieee80211_handle_wake_tx_queue() (>= 6.2) is available with them.
 */
#define OB_MAC80211_HAS_CHANCTX_EMULATION \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0))

#if OB_MAC80211_TX_STATUS_SKB
#define ob_ieee80211_tx_status ieee80211_tx_status_skb
#else
#define ob_ieee80211_tx_status ieee80211_tx_status
#endif

#endif /* _OB_MAC80211_COMPAT_H_ */
