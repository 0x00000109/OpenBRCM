// SPDX-License-Identifier: GPL-2.0-only
#ifndef _OB_SPROM_POLICY_H_
#define _OB_SPROM_POLICY_H_

/*
 * Normal child probe no longer rereads the raw SPROM window.  The argument is
 * retained for source compatibility with existing host contract tests; raw
 * SPROM access is confined to explicit isolated diagnostics/tests.
 */
static inline bool ob_si_sprom_read_required(bool bounded_radio2069)
{
	(void)bounded_radio2069;
	return false;
}

/* An all-ones first word is an inaccessible SPROM-window sentinel. */
static inline bool ob_si_sprom_first_word_accessible(unsigned short value)
{
	return value != 0xffffu;
}

#endif
