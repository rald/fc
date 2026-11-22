#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>

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

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp);

#endif /* VM_H */
