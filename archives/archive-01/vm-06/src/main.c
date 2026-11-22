#include "vm.h"
#include <stdio.h>

/* Emission macros assuming `uint16_t *prog` and `int idx` are in scope. */
#define vm_jr(a)          do { prog[idx++] = enc_rrr(OP_JR, (a), 0, 0); } while (0)
#define vm_movi(a, imm8)  do { prog[idx++] = enc_ri8(OP_MOVI, (a), (uint8_t)(imm8)); } while (0)
#define vm_mov(a, b)      do { prog[idx++] = enc_rrr(OP_MOV, (a), (b), 0); } while (0)
#define vm_add(a, b, c)   do { prog[idx++] = enc_rrr(OP_ADD, (a), (b), (c)); } while (0)
#define vm_sub(a, b, c)   do { prog[idx++] = enc_rrr(OP_SUB, (a), (b), (c)); } while (0)
#define vm_and(a, b, c)   do { prog[idx++] = enc_rrr(OP_AND, (a), (b), (c)); } while (0)
#define vm_or(a, b, c)    do { prog[idx++] = enc_rrr(OP_OR, (a), (b), (c)); } while (0)
#define vm_xor(a, b, c)   do { prog[idx++] = enc_rrr(OP_XOR, (a), (b), (c)); } while (0)
#define vm_shl(a, imm8)   do { prog[idx++] = enc_ri8(OP_SHL, (a), (uint8_t)(imm8)); } while (0)
#define vm_shr(a, imm8)   do { prog[idx++] = enc_ri8(OP_SHR, (a), (uint8_t)(imm8)); } while (0)
#define vm_cmplt(a, b, c) do { prog[idx++] = enc_rrr(OP_CMPLT, (a), (b), (c)); } while (0)
#define vm_jmp(imm12)     do { prog[idx++] = enc_i12(OP_JMP, (int16_t)(imm12)); } while (0)
#define vm_jz(a, imm8)    do { prog[idx++] = enc_ri8(OP_JZ, (a), (uint8_t)(imm8)); } while (0)
#define vm_ld32(a, b, c)  do { prog[idx++] = enc_ri8(OP_LD32, (a), (uint8_t)(((b) << 4) | ((c) & 0xF))); } while (0)
#define vm_st32(a, b, c)  do { prog[idx++] = enc_ri8(OP_ST32, (a), (uint8_t)(((b) << 4) | ((c) & 0xF))); } while (0)
#define vm_call(imm12)    do { prog[idx++] = enc_i12(OP_CALL, (int16_t)(imm12)); } while (0)

/*
 * Shifts left by repeated addition so recursion works for any non-negative
 * multiplier without needing a dedicated MUL opcode.
 * Inputs:  r[4] = multiplicand, r[5] = multiplier
 * Output:  r[6] = r[4] * r[5]
 * Clobbers: r[5], r[6], r[7]
 */
void emit_mul_helper(uint16_t *prog, int *idx, int zero_reg) {
    int start = *idx;
    prog[(*idx)++] = enc_rrr(OP_MOV, 6, zero_reg, zero_reg); /* r6 = 0 */

    int loop_top = *idx;
    prog[(*idx)++] = enc_ri8(OP_JZ, 5, 0);                   /* patched below: if r5 == 0, jump out */
    int jz_out_pos = *idx - 1;

    prog[(*idx)++] = enc_rrr(OP_ADD, 6, 6, 4);               /* r6 += r4 */
    prog[(*idx)++] = enc_rrr(OP_SUB, 5, 5, 1);               /* r5 -= 1 */

    int jmp_back_pos = *idx;
    prog[(*idx)++] = enc_i12(OP_JMP, (int16_t)(loop_top - (jmp_back_pos + 1)));

    int loop_end = *idx;
    /* JZ branches relative to pc after JZ (jz_out_pos + 1) */
    int8_t jz_off = (int8_t)(loop_end - (jz_out_pos + 1));
    prog[jz_out_pos] = enc_ri8(OP_JZ, 5, (uint8_t)jz_off);
    (void)start;
}

/*
 * Computes factorial(r0) recursively using r15 as stack pointer:
 *   if (n <= 1) return 1;
 *   save n at [sp + 0], lr at [sp + 4]
 *   r0 = fact(n - 1)
 *   restore n from [sp + 0], lr from [sp + 4]
 *   return n * r0
 */
int main(void) {
    uint16_t prog[128];
    int idx = 0;

    /* main: set constants, call fact(5), return to exit_pc via r13 */
    int main_idx = idx;
    vm_movi(0, 5);       /* r0 = 5 (input n) */
    vm_movi(1, 1);       /* r1 = 1 */
    vm_movi(2, 4);       /* r2 = 4 (slot size) */
    vm_movi(8, 0);       /* r8 = 0 */
    vm_movi(9, 8);       /* r9 = 8 (2 slots = 8 bytes) */
    vm_mov(13, 14);      /* save initial exit_pc (r14) into r13 */
    int call_fact_pos = idx;
    vm_call(0);          /* patched below: CALL fact */
    vm_jr(13);           /* return to r13 (exit_pc) */

    /* fact(r0 = n) -> returns r0 = n! */
    int fact_idx = idx;
    prog[call_fact_pos] = enc_i12(OP_CALL, (int16_t)(fact_idx - (call_fact_pos + 1)));

    /* Base case check: if n < 2 (i.e. n <= 1), return 1 */
    vm_movi(10, 2);      /* r10 = 2 */
    vm_cmplt(3, 0, 10);  /* r3 = (r0 < 2) ? 1 : 0 */
    vm_jz(3, 2);         /* if r3 == 0 (n >= 2), skip base case */
    vm_mov(0, 1);        /* r0 = 1 */
    vm_jr(14);           /* return 1 */

    /* Recursive case: allocate 8 bytes on r15 stack: [sp+0]=n, [sp+4]=r14 */
    vm_sub(15, 15, 9);   /* r15 -= 8 */
    vm_st32(0, 15, 0);   /* a=0 (n), b=15 (SP), c=0 -> [sp + 0] */
    vm_st32(14, 15, 1);  /* a=14 (LR), b=15 (SP), c=1 -> [sp + 4] */

    /* r0 = fact(n - 1) */
    vm_sub(0, 0, 1);     /* r0 = n - 1 */
    int recur_call_pos = idx;
    vm_call(0);          /* patched below: CALL fact */
    prog[recur_call_pos] = enc_i12(OP_CALL, (int16_t)(fact_idx - (recur_call_pos + 1)));

    /* Restore saved n into r4, saved LR into r14, free stack frame */
    vm_ld32(4, 15, 0);   /* r4 = [sp + 0] (saved n) */
    vm_ld32(14, 15, 1);  /* r14 = [sp + 4] (saved LR) */
    vm_add(15, 15, 9);   /* r15 += 8 */

    /* Multiply: r5 = r0 (fact(n-1)), r4 = saved n -> r6 = n * fact(n-1) */
    vm_mov(5, 0);        /* r5 = fact(n - 1) */
    emit_mul_helper(prog, &idx, 8); /* r6 = r4 * r5 */
    vm_mov(0, 6);        /* r0 = r6 (return value) */
    vm_jr(14);           /* return to caller */

    uint8_t mem[512] = {0};
    int32_t init_sp = (int32_t)sizeof(mem);
    int32_t exit_pc = idx;

    /* Pass exit_pc in r14 via init_lr = exit_pc; main saves it to r13 before CALL */
    int32_t ans = vm_run(prog, (size_t)idx, mem, sizeof(mem),
                         main_idx, exit_pc, init_sp);
    printf("factorial(5) = %d\n", ans);
    return 0;
}
