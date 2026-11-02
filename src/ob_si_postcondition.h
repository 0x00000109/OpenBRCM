/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_SI_POSTCONDITION_H_
#define _OB_SI_POSTCONDITION_H_

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#endif

/*
 * BCMA core-enable API success is not the enabled-state postcondition.
 * Preserve transport/API errors, and fail closed when the required state is
 * still absent.  -EIO denotes an unusable device state, not a BCMA API error.
 */
static inline int ob_si_core_enable_postcondition(int enable_ret,
						  bool enabled)
{
	if (enable_ret)
		return enable_ret;
	return enabled ? 0 : -EIO;
}

/*
 * Caller-visible postcondition for a BCMA-owned attach reset.  The wrapper
 * writes remain exclusively inside BCMA; the caller supplies the result of
 * guarded, read-only wrapper observations and the Linux enabled predicate.
 */
static inline int ob_attach_core_reset_postcondition(int enable_ret,
						      bool wrapper_valid,
						      bool enabled)
{
	if (enable_ret)
		return enable_ret;
	return (wrapper_valid && enabled) ? 0 : -EIO;
}

/*
 * Validate the object graph before any SI/BCMA access.  BCMA owns window
 * selection; this check only ensures the per-device context still refers to
 * the same bus/core and profile that probe admitted.  Keep the decision pure
 * so host tests can verify fail-closed behavior without a BCMA backend.
 */
static inline int ob_si_core_context_precondition(bool device_present,
						  bool bus_present,
						  bool core_present,
						  bool profile_present,
						  bool core_bus_matches,
						  bool profile_matches,
						  bool device_lost)
{
	if (!device_present || !bus_present || !core_present ||
	    !profile_present)
		return -EINVAL;
	if (device_lost)
		return -EIO;
	if (!core_bus_matches || !profile_matches)
		return -ENODEV;
	return 0;
}

#endif /* _OB_SI_POSTCONDITION_H_ */
