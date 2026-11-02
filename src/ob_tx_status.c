// SPDX-License-Identifier: GPL-2.0-only
/*
 * Offline TX-status translation; no MMIO. The only ownership this file
 * describes is the helper predicate ob_tx_status_skb_action(), which never
 * frees anything itself.
 */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_tx_status.h"

int ob_tx_status_translate(const struct ob_tx_status_input *input,
			   const struct ob_tx_status_evidence *evidence,
			   struct ob_tx_status_report *report)
{
	struct ob_tx_status_report clean;

	if (!report)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*report = clean;
	if (!input || !evidence)
		return -EINVAL;
	if (evidence->proven & ~OB_TX_STATUS_EV_KNOWN_MASK)
		return -EINVAL;
	if (!input->record_present)
		return -ENODATA;
	/* Without a proven association the record cannot be attributed. */
	if (!(evidence->proven & OB_TX_STATUS_EV_FRAME_ID))
		return -ENODATA;

	if (input->reported_frame_id != input->expected_frame_id) {
		clean.disposition = OB_TX_STATUS_DROP_FRAME_ID;
		*report = clean;
		return 0;
	}

	if (evidence->proven & OB_TX_STATUS_EV_ACK) {
		clean.disposition = evidence->ack ? OB_TX_STATUS_ACKED :
			OB_TX_STATUS_NOT_ACKED;
		clean.report_to_mac80211 = 1;
		clean.acked = evidence->ack ? 1 : 0;
	} else {
		/* Association alone is not an acknowledgement. */
		clean.disposition = OB_TX_STATUS_DELIVERED_UNCONFIRMED;
		clean.report_to_mac80211 = 0;
		clean.acked = 0;
	}

	if (evidence->proven & OB_TX_STATUS_EV_RATE) {
		if (input->rate_index >= OB_TX_STATUS_RATE_INDEX_LIMIT)
			return -ERANGE;
		clean.rate_known = 1;
		clean.rate_index = input->rate_index;
	}
	if (evidence->proven & OB_TX_STATUS_EV_RETRIES) {
		clean.retries_known = 1;
		clean.retries = input->retries;
	}
	/* A signal value is plausible only in the 8-bit signed range. */
	if (evidence->proven & OB_TX_STATUS_EV_SIGNAL) {
		clean.signal_known = 1;
		clean.signal_dbm = input->signal_dbm;
	}
	*report = clean;
	return 0;
}

enum ob_tx_skb_owner ob_tx_status_skb_action(bool reporting_enabled,
					     const struct ob_tx_status_report *report)
{
	/*
	 * Preconditions (non-NULL skb, non-NULL hw) are enforced by the
	 * wrapper, so every value here consumes the skb exactly once. A closed
	 * gate, a missing report or an unreportable one drops instead.
	 */
	if (!reporting_enabled || !report || !report->report_to_mac80211)
		return OB_TX_STATUS_SKB_CONSUMED_DROP;
	return OB_TX_STATUS_SKB_CONSUMED_REPORT;
}

int ob_tx_status_handoff(void *skb,
			 const struct ob_tx_status_report *report,
			 bool reporting_enabled,
			 const struct ob_tx_status_skb_ops *ops, void *ctx)
{
	/*
	 * Argument errors are the only negative returns and they precede every
	 * ownership effect, so "negative" always means "caller still owns".
	 */
	if (!skb || !ops || !ops->drop || !ops->report)
		return -EINVAL;

	if (ob_tx_status_skb_action(reporting_enabled, report) ==
	    OB_TX_STATUS_SKB_CONSUMED_REPORT) {
		ops->report(ctx, skb, report);
		return 0;
	}

	ops->drop(ctx, skb);
	return OB_TX_STATUS_CONSUMED_UNREPORTED;
}

void ob_tx_status_mac80211_view(const struct ob_tx_status_report *report,
				ob_tx_status_u32 prior_flags,
				struct ob_tx_status_tx_view *view)
{
	unsigned int attempts;
	bool reportable;

	if (!view)
		return;
	memset(view, 0, sizeof(*view));

	/*
	 * Mask first, then decide: a pre-existing ACK bit (for example from an
	 * earlier transmission of a reused skb) is never forwarded on its own.
	 */
	view->flags = prior_flags & ~OB_TX_STATUS_MAC80211_FLAG_ACK;
	view->rate_idx = OB_TX_STATUS_MAC80211_RATE_UNKNOWN;
	view->rate_count = 0;

	reportable = report && report->report_to_mac80211;
	if (!reportable)
		return;
	if (report->acked)
		view->flags |= OB_TX_STATUS_MAC80211_FLAG_ACK;
	if (!report->rate_known ||
	    report->rate_index >= OB_TX_STATUS_RATE_INDEX_LIMIT)
		return;

	view->rate_idx = (ob_tx_status_s8)report->rate_index;
	attempts = report->retries_known ?
		(unsigned int)report->retries + 1u : 1u;
	/* count is a 5-bit field: saturate instead of wrapping to 0. */
	if (attempts > OB_TX_STATUS_MAC80211_RATE_MAX_ATTEMPTS)
		attempts = OB_TX_STATUS_MAC80211_RATE_MAX_ATTEMPTS;
	view->rate_count = (ob_tx_status_u8)attempts;
}

bool ob_tx_status_live_reporting_enabled(void)
{
	return false;
}
