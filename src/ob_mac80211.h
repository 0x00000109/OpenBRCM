/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — mac80211 registration glue.
 *
 * Exposes only capabilities backed by recovered/provenance-tracked data.
 */
#ifndef _OB_MAC80211_H_
#define _OB_MAC80211_H_

struct ob_hw;

int  ob_mac80211_register(struct ob_hw *hw);
void ob_mac80211_unregister(struct ob_hw *hw);

#endif /* _OB_MAC80211_H_ */
