/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef __ASM_RISCV_VECTOR_H
#define __ASM_RISCV_VECTOR_H

#include <linux/types.h>
#include <uapi/asm-generic/errno.h>

#ifdef CONFIG_RISCV_ISA_V

#include <linux/stringify.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/ptrace.h>
#include <asm/hwcap.h>
#include <asm/csr.h>
#include <asm/asm.h>
#include <asm/errata_list.h>

extern unsigned long riscv_v_vsize;
int riscv_v_setup_vsize(void);
bool riscv_v_first_use_handler(struct pt_regs *regs);

static __always_inline bool has_vector(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_v);
}

static inline void __riscv_v_vstate_clean(struct pt_regs *regs)
{
	unsigned long sr_vs, sr_vs_clean;

	ALT_SR_VS(sr_vs, SR_VS);
	ALT_SR_VS(sr_vs_clean, SR_VS_CLEAN);

	regs->status = (regs->status & ~sr_vs) | sr_vs_clean;
}

static inline void __riscv_v_vstate_dirty(struct pt_regs *regs)
{
	unsigned long sr_vs, sr_vs_dirty;

	ALT_SR_VS(sr_vs, SR_VS);
	ALT_SR_VS(sr_vs_dirty, SR_VS_DIRTY);

	regs->status = (regs->status & ~sr_vs) | sr_vs_dirty;
}

static inline void riscv_v_vstate_off(struct pt_regs *regs)
{
	regs->status = (regs->status & ~SR_VS) | SR_VS_OFF;
	regs->status = (regs->status & ~SR_VS_THEAD) | SR_VS_OFF_THEAD;
}

static inline void riscv_v_vstate_on(struct pt_regs *regs)
{
	unsigned long sr_vs, sr_vs_initial;

	ALT_SR_VS(sr_vs, SR_VS);
	ALT_SR_VS(sr_vs_initial, SR_VS_INITIAL);

	regs->status = (regs->status & ~sr_vs) | sr_vs_initial;
}

static inline bool riscv_v_vstate_query(struct pt_regs *regs)
{
	unsigned long sr_vs;

	ALT_SR_VS(sr_vs, SR_VS);

	return (regs->status & sr_vs) != 0;
}

static __always_inline void riscv_v_enable(void)
{
	unsigned long sr_vs;

	ALT_SR_VS(sr_vs, SR_VS);

	csr_set(CSR_SSTATUS, sr_vs);
}

static __always_inline void riscv_v_disable(void)
{
	csr_clear(CSR_SSTATUS, SR_VS | SR_VS_THEAD);
}

static __always_inline void __vstate_csr_save(struct __riscv_v_ext_state *dest)
{
	register u32 t1 asm("t1") = (SR_FS);

	asm volatile (ALTERNATIVE(
		"csrr	%0, " __stringify(CSR_VSTART) "\n\t"
		"csrr	%1, " __stringify(CSR_VTYPE) "\n\t"
		"csrr	%2, " __stringify(CSR_VL) "\n\t"
		"csrr	%3, " __stringify(CSR_VCSR) "\n\t"
		"csrr	%4, " __stringify(CSR_VLENB) "\n\t"
		__nops(4),
		"csrs	sstatus, t1\n\t"
		"csrr	%0, " __stringify(CSR_VSTART) "\n\t"
		"csrr	%1, " __stringify(CSR_VTYPE) "\n\t"
		"csrr	%2, " __stringify(CSR_VL) "\n\t"
		"csrr	%3, " __stringify(THEAD_C9XX_CSR_VXRM) "\n\t"
		"slliw	%3, %3, " __stringify(VCSR_VXRM_SHIFT) "\n\t"
		"csrr	t4, " __stringify(THEAD_C9XX_CSR_VXSAT) "\n\t"
		"or	%3, %3, t4\n\t"
		"csrc	sstatus, t1\n\t",
		THEAD_VENDOR_ID,
		ERRATA_THEAD_VECTOR, CONFIG_ERRATA_THEAD_VECTOR)
		: "=r" (dest->vstart), "=r" (dest->vtype), "=r" (dest->vl),
		  "=r" (dest->vcsr), "=r" (dest->vlenb) : "r"(t1) : "t4");
}

static __always_inline void __vstate_csr_restore(struct __riscv_v_ext_state *src)
{
	register u32 t1 asm("t1") = (SR_FS);

	asm volatile (ALTERNATIVE(
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvl	 x0, %2, %1\n\t"
		".option pop\n\t"
		"csrw	" __stringify(CSR_VSTART) ", %0\n\t"
		"csrw	" __stringify(CSR_VCSR) ", %3\n\t"
		__nops(6),
		"csrs	sstatus, t1\n\t"
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvl	 x0, %2, %1\n\t"
		".option pop\n\t"
		"csrw	" __stringify(CSR_VSTART) ", %0\n\t"
		"srliw	t4, %3, " __stringify(VCSR_VXRM_SHIFT) "\n\t"
		"andi	t4, t4, " __stringify(VCSR_VXRM_MASK) "\n\t"
		"csrw	" __stringify(THEAD_C9XX_CSR_VXRM) ", t4\n\t"
		"andi	%3, %3, " __stringify(VCSR_VXSAT_MASK) "\n\t"
		"csrw	" __stringify(THEAD_C9XX_CSR_VXSAT) ", %3\n\t"
		"csrc	sstatus, t1\n\t",
		THEAD_VENDOR_ID,
		ERRATA_THEAD_VECTOR, CONFIG_ERRATA_THEAD_VECTOR)
		: : "r" (src->vstart), "r" (src->vtype), "r" (src->vl),
		    "r" (src->vcsr), "r"(t1) : "t4");
}

static inline void __riscv_v_vstate_save(struct __riscv_v_ext_state *save_to,
					 void *datap)
{
	unsigned long vl;

	riscv_v_enable();
	__vstate_csr_save(save_to);
	asm volatile (ALTERNATIVE(
		"nop\n\t"
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
		"vse8.v		v0, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vse8.v		v8, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vse8.v		v16, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vse8.v		v24, (%1)\n\t"
		".option pop\n\t",
		"mv		t0, %1\n\t"
		THEAD_VSETVLI_T4X0E8M8D1
		THEAD_VSB_V_V0T0
		"addi		t0, t0, 128\n\t"
		THEAD_VSB_V_V8T0
		"addi		t0, t0, 128\n\t"
		THEAD_VSB_V_V16T0
		"addi		t0, t0, 128\n\t"
		THEAD_VSB_V_V24T0, THEAD_VENDOR_ID,
		ERRATA_THEAD_VECTOR, CONFIG_ERRATA_THEAD_VECTOR)
		: "=&r" (vl) : "r" (datap) : "t0", "t4", "memory");
	riscv_v_disable();
}

static inline void __riscv_v_vstate_restore(struct __riscv_v_ext_state *restore_from,
					    void *datap)
{
	unsigned long vl;

	riscv_v_enable();
	asm volatile (ALTERNATIVE(
		"nop\n\t"
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
		"vle8.v		v0, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vle8.v		v8, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vle8.v		v16, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vle8.v		v24, (%1)\n\t"
		".option pop\n\t",
		"mv		t0, %1\n\t"
		THEAD_VSETVLI_T4X0E8M8D1
		THEAD_VLB_V_V0T0
		"addi		t0, t0, 128\n\t"
		THEAD_VLB_V_V8T0
		"addi		t0, t0, 128\n\t"
		THEAD_VLB_V_V16T0
		"addi		t0, t0, 128\n\t"
		THEAD_VLB_V_V24T0, THEAD_VENDOR_ID,
		ERRATA_THEAD_VECTOR, CONFIG_ERRATA_THEAD_VECTOR)
		: "=&r" (vl) : "r" (datap) : "t0", "t4", "memory");
	__vstate_csr_restore(restore_from);
	riscv_v_disable();
}

static inline void __riscv_v_vstate_discard(void)
{
	unsigned long vl, vtype_inval = 1UL << (BITS_PER_LONG - 1);

	riscv_v_enable();
	asm volatile (ALTERNATIVE(
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
		"vmv.v.i	v0, -1\n\t"
		"vmv.v.i	v8, -1\n\t"
		"vmv.v.i	v16, -1\n\t"
		"vmv.v.i	v24, -1\n\t"
		"vsetvl		%0, x0, %1\n\t"
		".option pop\n\t",
		/* FIXME: Do real vstate discard as above! */
		__nops(6), THEAD_VENDOR_ID,
		ERRATA_THEAD_VECTOR, CONFIG_ERRATA_THEAD_VECTOR)
		: "=&r" (vl) : "r" (vtype_inval) : "memory");
	riscv_v_disable();
}

static inline void riscv_v_vstate_discard(struct pt_regs *regs)
{
	unsigned long sr_vs;

	ALT_SR_VS(sr_vs, SR_VS);

	if ((regs->status & sr_vs) == SR_VS_OFF)
		return;

	__riscv_v_vstate_discard();
	__riscv_v_vstate_dirty(regs);
}

static inline void riscv_v_vstate_save(struct task_struct *task,
				       struct pt_regs *regs)
{
	unsigned long sr_vs, sr_vs_dirty;

	ALT_SR_VS(sr_vs, SR_VS);
	ALT_SR_VS(sr_vs_dirty, SR_VS_DIRTY);

	if ((regs->status & sr_vs) == sr_vs_dirty) {
		struct __riscv_v_ext_state *vstate = &task->thread.vstate;

		__riscv_v_vstate_save(vstate, vstate->datap);
		__riscv_v_vstate_clean(regs);
	}
}

static inline void riscv_v_vstate_restore(struct task_struct *task,
					  struct pt_regs *regs)
{
	unsigned long sr_vs;

	ALT_SR_VS(sr_vs, SR_VS);

	if ((regs->status & sr_vs) != SR_VS_OFF) {
		struct __riscv_v_ext_state *vstate = &task->thread.vstate;

		__riscv_v_vstate_restore(vstate, vstate->datap);
		__riscv_v_vstate_clean(regs);
	}
}

static inline void __switch_to_vector(struct task_struct *prev,
				      struct task_struct *next)
{
	struct pt_regs *regs;

	regs = task_pt_regs(prev);
	riscv_v_vstate_save(prev, regs);
	riscv_v_vstate_restore(next, task_pt_regs(next));
}

void riscv_v_vstate_ctrl_init(struct task_struct *tsk);
bool riscv_v_vstate_ctrl_user_allowed(void);

#else /* ! CONFIG_RISCV_ISA_V  */

struct pt_regs;

static inline int riscv_v_setup_vsize(void) { return -EOPNOTSUPP; }
static __always_inline bool has_vector(void) { return false; }
static inline bool riscv_v_first_use_handler(struct pt_regs *regs) { return false; }
static inline bool riscv_v_vstate_query(struct pt_regs *regs) { return false; }
static inline bool riscv_v_vstate_ctrl_user_allowed(void) { return false; }
#define riscv_v_vsize (0)
#define riscv_v_vstate_discard(regs)		do {} while (0)
#define riscv_v_vstate_save(task, regs)		do {} while (0)
#define riscv_v_vstate_restore(task, regs)	do {} while (0)
#define __switch_to_vector(__prev, __next)	do {} while (0)
#define riscv_v_vstate_off(regs)		do {} while (0)
#define riscv_v_vstate_on(regs)			do {} while (0)

#endif /* CONFIG_RISCV_ISA_V */

#endif /* ! __ASM_RISCV_VECTOR_H */
