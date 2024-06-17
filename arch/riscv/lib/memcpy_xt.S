/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Regents of the University of California
 */

#include <linux/linkage.h>
#include <asm/asm.h>

#  define LABLE_ALIGN   \
        .balignw 16, 0x00000001

/* void *memcpy(void *, const void *, size_t) */
ENTRY(__memcpy)
WEAK(memcpy)
.L_to_memcpy:
	/* Test if len less than 8 bytes.  */
	mv      t6, a0
	sltiu   a3, a2, 8
	li      t3, 1
	bnez    a3, .L_copy_by_byte

	andi    a3, a0, 7
	li      t5, 8
	/* Test if dest is not 8 bytes aligned.  */
	bnez    a3, .L_dest_not_aligned
.L_dest_aligned:
	/* If dest is aligned, then copy.  */
	srli    t4, a2, 6
	/* Test if len less than 64 bytes.  */
	beqz    t4, .L_len_less_64bytes
	andi    a2, a2, 63

	LABLE_ALIGN
.L_len_larger_64bytes:
	ldd	a4, a5, (a1), 0, 4
	sdd	a4, a5, (a0), 0, 4
	ldd	a6, a7, (a1), 1, 4
	sdd	a6, a7, (a0), 1, 4
	ldd	a4, a5, (a1), 2, 4
	sdd	a4, a5, (a0), 2, 4
	ldd	a6, a7, (a1), 3, 4
	sub	t4, t4, t3
	addi    a1, a1, 64
	sdd	a6, a7, (a0), 3, 4
	addi    a0, a0, 64
	bnez	t4, .L_len_larger_64bytes

.L_len_less_64bytes:
	srli    t4, a2, 2
	beqz    t4, .L_copy_by_byte
	andi    a2, a2, 3
.L_len_less_64bytes_loop:
	lw      a4, 0(a1)
	sub	t4, t4, t3
	addi    a1, a1, 4
	sw      a4, 0(a0)
	addi    a0, a0, 4
	bnez    t4, .L_len_less_64bytes_loop

	/* Copy tail.  */
.L_copy_by_byte:
	andi    t4, a2, 7
	beqz    t4, .L_return
.L_copy_by_byte_loop:
	lb      a4, 0(a1)
	sub	t4, t4, t3
	addi    a1, a1, 1
	sb      a4, 0(a0)
	addi    a0, a0, 1
	bnez	t4, .L_copy_by_byte_loop

.L_return:
	mv      a0, t6
	ret

	/* If dest is not aligned, just copying some bytes makes the dest
	align.  */
.L_dest_not_aligned:
	sub     a3, t5, a3
	mv      t5, a3
.L_dest_not_aligned_loop:
	/* Makes the dest align.  */
	lb      a4, 0(a1)
	sub	a3, a3, t3
	addi    a1, a1, 1
	sb      a4, 0(a0)
	addi    a0, a0, 1
	bnez	a3, .L_dest_not_aligned_loop
	sub     a2, a2, t5
	sltiu	a3, a2, 8
	bnez    a3, .L_copy_by_byte
	/* Check whether the src is aligned.  */
	j		.L_dest_aligned
END(__memcpy)
SYM_FUNC_ALIAS(__pi_memcpy, __memcpy)
SYM_FUNC_ALIAS(__pi___memcpy, __memcpy)
