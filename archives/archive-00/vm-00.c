#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    OP_JR    = 0x0, /* pc = code + (uint32_t)r[a]           */
    OP_MOVI  = 0x1, /* r[a] = (int8_t)i8                    */
    OP_MOV   = 0x2, /* r[a] = r[b]                          */
    OP_ADD   = 0x3, /* r[a] = r[b] + r[c]                   */
    OP_SUB   = 0x4, /* r[a] = r[b] - r[c]                   */
    OP_AND   = 0x5, /* r[a] = r[b] & r[c]                   */
    OP_OR    = 0x6, /* r[a] = r[b] | r[c]                   */
    OP_XOR   = 0x7, /* r[a] = r[b] ^ r[c]                   */
    OP_SHL   = 0x8, /* r[a] = (uint32_t)r[a] << (i8 & 31)   */
    OP_SHR   = 0x9, /* r[a] = (uint32_t)r[a] >> (i8 & 31)   */
    OP_CMPLT = 0xA, /* r[a] = (r[b] < r[c]) ? 1 : 0         */
    OP_JMP   = 0xB, /* pc += sext12(ins)                    */
    OP_JZ    = 0xC, /* if (r[a] == 0) pc += (int8_t)i8      */
    OP_LD32  = 0xD, /* r[a] = mem32[r[b] + c*4]             */
    OP_ST32  = 0xE, /* mem32[r[b] + c*4] = r[a]             */
    OP_CALL  = 0xF  /* r[14] = next_pc_idx; pc += sext12(ins) */
};

static inline uint16_t enc_rrr(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) |
                      ((b & 0xF) << 4) | (c & 0xF));
}

static inline uint16_t enc_ri8(uint8_t op, uint8_t a, uint8_t imm8) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) | imm8);
}

static inline uint16_t enc_i12(uint8_t op, int16_t imm12) {
    return (uint16_t)(((op & 0xF) << 12) | ((uint16_t)imm12 & 0x0FFFu));
}

static inline int32_t sext12(uint16_t w) {
    uint16_t v = w & 0x0FFFu;
    return (v & 0x0800u) ? (int32_t)(v | 0xFFFFF000u) : (int32_t)v;
}

static inline int32_t load32_le(const uint8_t *mem, uint32_t addr) {
    int32_t val;
    memcpy(&val, mem + addr, sizeof(val));
    return val;
}

static inline void store32_le(uint8_t *mem, uint32_t addr, int32_t val) {
    memcpy(mem + addr, &val, sizeof(val));
}

/*
 * Shifts left by repeated addition so recursion works for any non-negative
 * multiplier without needing a dedicated MUL opcode.
 * Inputs:  r[4] = multiplicand, r[5] = multiplier
 * Output:  r[6] = r[4] * r[5]
 * Clobbers: r[5], r[6], r[7]
 */
static void emit_mul_helper(uint16_t *prog, int *idx, int zero_reg) {
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

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    int32_t r[16] = {0};
    r[14] = init_lr;
    r[15] = init_sp;

    const uint16_t *pc = code + init_pc;
    const uint16_t *code_end = code + code_len;

    static const void *dispatch[] = {
        &&L_JR,    &&L_MOVI,  &&L_MOV,   &&L_ADD,
        &&L_SUB,   &&L_AND,   &&L_OR,    &&L_XOR,
        &&L_SHL,   &&L_SHR,   &&L_CMPLT, &&L_JMP,
        &&L_JZ,    &&L_LD32,  &&L_ST32,  &&L_CALL
    };

    uint8_t a = 0, b = 0, c = 0, i8 = 0;

#define NEXT() do {                                             \
        if (pc < code || pc >= code_end) return r[0];           \
        uint16_t ins = *pc++;                                   \
        uint8_t op = (uint8_t)(ins >> 12);                      \
        a  = (uint8_t)((ins >> 8) & 0xF);                       \
        b  = (uint8_t)((ins >> 4) & 0xF);                       \
        c  = (uint8_t)(ins & 0xF);                              \
        i8 = (uint8_t)(ins & 0xFF);                             \
        goto *dispatch[op];                                     \
    } while (0)

    NEXT();

L_JR:    pc = code + (uint32_t)r[a]; NEXT();
L_MOVI:  r[a] = (int8_t)i8; NEXT();
L_MOV:   r[a] = r[b]; NEXT();
L_ADD:   r[a] = r[b] + r[c]; NEXT();
L_SUB:   r[a] = r[b] - r[c]; NEXT();
L_AND:   r[a] = r[b] & r[c]; NEXT();
L_OR:    r[a] = r[b] | r[c]; NEXT();
L_XOR:   r[a] = r[b] ^ r[c]; NEXT();
L_SHL:   r[a] = (int32_t)((uint32_t)r[a] << (i8 & 31)); NEXT();
L_SHR:   r[a] = (int32_t)((uint32_t)r[a] >> (i8 & 31)); NEXT();
L_CMPLT: r[a] = (r[b] < r[c]) ? 1 : 0; NEXT();
L_JMP: {
        int32_t off = sext12(code[(size_t)(pc - code) - 1]);
        pc += off;
        NEXT();
    }
L_JZ:    if (r[a] == 0) pc += (int8_t)i8; NEXT();
L_LD32: {
        uint32_t addr = (uint32_t)r[b] + (uint32_t)c * 4u;
        if (addr + 4u <= mem_size) r[a] = load32_le(mem, addr);
        NEXT();
    }
L_ST32: {
        uint32_t addr = (uint32_t)r[b] + (uint32_t)c * 4u;
        if (addr + 4u <= mem_size) store32_le(mem, addr, r[a]);
        NEXT();
    }
L_CALL: {
        int32_t off = sext12(code[(size_t)(pc - code) - 1]);
        r[14] = (int32_t)(pc - code);
        pc += off;
        NEXT();
    }
#undef NEXT
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
    prog[idx++] = enc_ri8(OP_MOVI, 0, 5);       /* r0 = 5 (input n) */
    prog[idx++] = enc_ri8(OP_MOVI, 1, 1);       /* r1 = 1 */
    prog[idx++] = enc_ri8(OP_MOVI, 2, 4);       /* r2 = 4 (slot size) */
    prog[idx++] = enc_ri8(OP_MOVI, 8, 0);       /* r8 = 0 */
    prog[idx++] = enc_ri8(OP_MOVI, 9, 8);       /* r9 = 8 (2 slots = 8 bytes) */
    prog[idx++] = enc_rrr(OP_MOV, 13, 14, 8);   /* save initial exit_pc (r14) into r13 */
    int call_fact_pos = idx++;                  /* CALL fact */
    prog[idx++] = enc_rrr(OP_JR, 13, 0, 0);     /* return to r13 (exit_pc) */

    /* fact(r0 = n) -> returns r0 = n! */
    int fact_idx = idx;
    prog[call_fact_pos] = enc_i12(OP_CALL, (int16_t)(fact_idx - (call_fact_pos + 1)));

    /* Base case check: if n < 2 (i.e. n <= 1), return 1 */
    prog[idx++] = enc_ri8(OP_MOVI, 10, 2);      /* r10 = 2 */
    prog[idx++] = enc_rrr(OP_CMPLT, 3, 0, 10);  /* r3 = (r0 < 2) ? 1 : 0 */
    prog[idx++] = enc_ri8(OP_JZ, 3, 2);         /* if r3 == 0 (n >= 2), skip base case */
    prog[idx++] = enc_rrr(OP_MOV, 0, 1, 8);     /* r0 = 1 */
    prog[idx++] = enc_rrr(OP_JR, 14, 0, 0);     /* return 1 */

    /* Recursive case: allocate 8 bytes on r15 stack: [sp+0]=n, [sp+4]=r14 */
    prog[idx++] = enc_rrr(OP_SUB, 15, 15, 9);   /* r15 -= 8 */
    prog[idx++] = enc_ri8(OP_ST32, 0, 0xF0);    /* a=0 (n), b=15 (SP), c=0 -> [sp + 0] */
    prog[idx++] = enc_ri8(OP_ST32, 14, 0xF1);   /* a=14 (LR), b=15 (SP), c=1 -> [sp + 4] */

    /* r0 = fact(n - 1) */
    prog[idx++] = enc_rrr(OP_SUB, 0, 0, 1);    /* r0 = n - 1 */
    int recur_call_pos = idx++;                 /* CALL fact */
    prog[recur_call_pos] = enc_i12(OP_CALL, (int16_t)(fact_idx - (recur_call_pos + 1)));

    /* Restore saved n into r4, saved LR into r14, free stack frame */
    prog[idx++] = enc_ri8(OP_LD32, 4, 0xF0);    /* r4 = [sp + 0] (saved n) */
    prog[idx++] = enc_ri8(OP_LD32, 14, 0xF1);   /* r14 = [sp + 4] (saved LR) */
    prog[idx++] = enc_rrr(OP_ADD, 15, 15, 9);   /* r15 += 8 */

    /* Multiply: r5 = r0 (fact(n-1)), r4 = saved n -> r6 = n * fact(n-1) */
    prog[idx++] = enc_rrr(OP_MOV, 5, 0, 8);     /* r5 = fact(n - 1) */
    emit_mul_helper(prog, &idx, 8);             /* r6 = r4 * r5 */
    prog[idx++] = enc_rrr(OP_MOV, 0, 6, 8);     /* r0 = r6 (return value) */
    prog[idx++] = enc_rrr(OP_JR, 14, 0, 0);     /* return to caller */

    uint8_t mem[512] = {0};
    int32_t init_sp = (int32_t)sizeof(mem);
    int32_t exit_pc = idx;

    /* Pass exit_pc in r14 via init_lr = exit_pc; main saves it to r13 before CALL */
    int32_t ans = vm_run(prog, (size_t)idx, mem, sizeof(mem),
                         main_idx, exit_pc, init_sp);
    printf("factorial(5) = %d\n", ans);
    return 0;
}
