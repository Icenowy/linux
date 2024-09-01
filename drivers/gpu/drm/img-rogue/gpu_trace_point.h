/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_trace_point

#if !defined(_TRACE_GPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_H

#include <linux/sched/numa_balancing.h>
#include <linux/tracepoint.h>
#include <linux/binfmts.h>

TRACE_EVENT(gpu_interrupt,

       TP_PROTO(unsigned int IRQStatusReg, unsigned int IRQStatus),

       TP_ARGS(IRQStatusReg, IRQStatus),

       TP_STRUCT__entry(
               __field(        unsigned int,   IRQStatusReg)
               __field(        unsigned int,   IRQStatus)
       ),

       TP_fast_assign(
               __entry->IRQStatusReg = IRQStatusReg;
               __entry->IRQStatus = IRQStatus;
       ),

       TP_printk("IRQStatusReg=%d IRQStatus=%d", __entry->IRQStatusReg, __entry->IRQStatus)
);

#endif /* _TRACE_GPU_H */

/* We don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	gpu_trace_point
/* This part must be outside protection */
#include <trace/define_trace.h>
