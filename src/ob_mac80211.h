/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — mac80211 registration glue.
 *
 * Exposes only capabilities backed by recovered/provenance-tracked data.
 */
#ifndef _OB_MAC80211_H_
#define _OB_MAC80211_H_

#include "ob_rx_mac80211.h"
#include "ob_tx_status.h"

struct ieee80211_hw;
struct sk_buff;
struct ob_hw;

/*
 * Keep the mac80211 filter-mask contract independently testable. A flag can
 * be reported as supported only when the device can pass that frame class to
 * the stack; the current OpenBRCM RX path supports no filter flags yet.
 */
static inline unsigned int ob_mac80211_filter_flags_supported(
	unsigned int requested, unsigned int supported)
{
	return requested & supported;
}

int  ob_mac80211_register(struct ob_hw *hw);
void ob_mac80211_unregister(struct ob_hw *hw);

/*
 * Gated mac80211 handoffs. Both return -EOPNOTSUPP before consuming @skb while
 * their compile-time policy is closed. No production caller exists; the live
 * RX/TX paths must stay disabled until the outstanding hardware facts are
 * proven (see audit/openbrcm_phase35/).
 */
int  ob_mac80211_rx_deliver(struct ieee80211_hw *ieee, struct sk_buff *skb,
			    const struct ob_rx_mac80211_report *report);
int  ob_mac80211_tx_status(struct ieee80211_hw *ieee, struct sk_buff *skb,
			   const struct ob_tx_status_report *report);

#endif /* _OB_MAC80211_H_ */
