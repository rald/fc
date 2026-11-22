#include "vm.h"
#include <string.h>

static inline int32_t load32_le(const uint8_t *mem, uint32_t addr) {
    int32_t val;
    memcpy(&val, mem + addr, sizeof(val));
    return val;
}

static inline void store32_le(uint8_t *mem, uint32_t addr, int32_t val) {
    memcpy(mem + addr, &val, sizeof(val));
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
