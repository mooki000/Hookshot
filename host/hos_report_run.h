/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* First call at normal startup, never first called from a fault handler. Returns
   128 system-random bits as 32 lowercase hex digits, or "unknown" on failure.
   Cached for this process; contains no pid, time, machine or user information. */
const char *hos_report_run_id(void);
