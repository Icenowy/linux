/* SPDX-License-Identifier: GPL-2.0-or-later */
/* vha message transfer tracepoints
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vha_trace_point

#if !defined(_TRACE_VHA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VHA_H

#include <linux/sched/numa_balancing.h>
#include <linux/tracepoint.h>
#include <linux/binfmts.h>

TRACE_EVENT(vha_session_in,
    TP_PROTO(uint32_t session_id, uint64_t kicks),

    TP_ARGS(session_id, kicks),

    TP_STRUCT__entry(
        __field(    uint32_t,  session_id)
        __field(    uint64_t,  kicks)
    ),

    TP_fast_assign(
        __entry->session_id = session_id;
        __entry->kicks = kicks;
    ),

    TP_printk("session_id %d, kicks=%d", __entry->session_id, __entry->kicks)
);

TRACE_EVENT(vha_session_out,
    TP_PROTO(uint32_t session_id, uint64_t kicks),

    TP_ARGS(session_id, kicks),

    TP_STRUCT__entry(
        __field(    uint32_t,  session_id)
        __field(    uint64_t,  kicks)
    ),

    TP_fast_assign(
        __entry->session_id = session_id;
        __entry->kicks = kicks;
    ),

    TP_printk("session_id %d, kicks=%d", __entry->session_id, __entry->kicks)
);

TRACE_EVENT(vha_hwexec_in,
    TP_PROTO(uint32_t session_id, uint32_t subseg_current),

    TP_ARGS(session_id, subseg_current),

    TP_STRUCT__entry(
        __field(    uint32_t,  session_id)
        __field(    uint32_t,  subseg_current)
    ),

    TP_fast_assign(
        __entry->session_id = session_id;
        __entry->subseg_current = subseg_current;
    ),

    TP_printk("session_id %d, subseg_current=%d", __entry->session_id, __entry->subseg_current)
);

TRACE_EVENT(vha_irq,
    TP_PROTO(unsigned int dev_id, uint64_t status, uint8_t count, uint64_t last_proc_us),

    TP_ARGS(dev_id, status, count, last_proc_us),

    TP_STRUCT__entry(
        __field(    unsigned int,  dev_id)
        __field(    uint64_t,  status)
        __field(    uint8_t,  count)
        __field(    uint64_t,  last_proc_us)
    ),

    TP_fast_assign(
        __entry->dev_id = dev_id;
        __entry->status = status;
        __entry->count = count;
        __entry->last_proc_us = last_proc_us;
    ),

   TP_printk("dev_id=%d, status=%lld, count=%d, last_proc_us=%lld",
         __entry->dev_id, __entry->status,  __entry->count, __entry->last_proc_us)
);

#endif /* _TRACE_VHA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
