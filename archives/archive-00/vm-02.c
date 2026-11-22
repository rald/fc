#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Memory-Mapped VGA (128x128, 4bpp Sweetie-16 palette, 1 nibble per pixel)
 * ========================================================================= */
#define VGA_WIDTH       128u
#define VGA_HEIGHT      128u
#define VGA_BASE        0x0000u
#define VGA_FB_SIZE     ((VGA_WIDTH * VGA_HEIGHT) / 2u) /* 8192 bytes (0x2000) */
#define VM_MEM_SIZE     0x4000u                         /* 16 KiB total: 8K VRAM + 8K RAM/Stack */

/* Sweetie-16 Palette (0x00RRGGBB) */
static const uint32_t SWEETIE16_PALETTE[16] = {
    0x1A1C2Cu, /* 0x0: Black / Deep Void */
    0x5D275Du, /* 0x1: Purple */
    0xB13E53u, /* 0x2: Red */
    0xEF7D57u, /* 0x3: Orange */
    0xFFCD75u, /* 0x4: Yellow */
    0xA7F070u, /* 0x5: Light Green */
    0x38B764u, /* 0x6: Green */
    0x257179u, /* 0x7: Dark Green / Teal */
    0x29366Fu, /* 0x8: Dark Blue */
    0x3B5DC9u, /* 0x9: Blue */
    0x41A6F6u, /* 0xA: Light Blue */
    0x73EFF7u, /* 0xB: Cyan */
    0xF4F4F4u, /* 0xC: White */
    0x94B0C2u, /* 0xD: Light Grey */
    0x566C86u, /* 0xE: Grey */
    0x333C57u  /* 0xF: Dark Grey */
};

/* Reads a 4-bit color index at (x, y). Low nibble = even x, High nibble = odd x. */
static inline uint8_t vga_get_pixel(const uint8_t *mem, uint32_t x, uint32_t y) {
    uint32_t idx = y * VGA_WIDTH + x;
    uint8_t byte = mem[VGA_BASE + (idx >> 1)];
    return (idx & 1u) ? (byte >> 4) : (byte & 0x0Fu);
}

/* Exports the 128x128 framebuffer to a binary PPM image file */
static int vga_dump_ppm(const uint8_t *mem, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;

    fprintf(f, "P6\n%u %u\n255\n", VGA_WIDTH, VGA_HEIGHT);
    for (uint32_t y = 0; y < VGA_HEIGHT; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            uint8_t color_idx = vga_get_pixel(mem, x, y);
            uint32_t rgb = SWEETIE16_PALETTE[color_idx & 0x0Fu];
            uint8_t px[3] = {
                (uint8_t)((rgb >> 16) & 0xFFu),
                (uint8_t)((rgb >> 8)  & 0xFFu),
                (uint8_t)(rgb & 0xFFu)
            };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}

/* =========================================================================
 * VM Opcodes & Helpers
 * ========================================================================= */
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

/* Optional hook invoked whenever a 32-bit word is written inside VRAM */
static inline void vga_on_mmio_write(uint32_t addr, int32_t val) {
    (void)addr;
    (void)val;
    /* Plug in SDL2 / Raylib / terminal refresh logic here if desired */
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
        if (addr + 4u <= mem_size) {
            store32_le(mem, addr, r[a]);
            if (addr >= VGA_BASE && addr < VGA_BASE + VGA_FB_SIZE) {
                vga_on_mmio_write(addr, r[a]);
            }
        }
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
 * Demo VM program:
 * Fills the 128x128 memory-mapped VGA framebuffer (0x0000..0x1FFF) with
 * 16 horizontal color bands (each 8 rows tall), showcasing all 16 Sweetie-16 colors.
 */
int main(void) {
    uint16_t prog[128];
    int idx = 0;

    /*
     * Register usage:
     * r0 = current VRAM byte address (0 -> 8192)
     * r1 = VRAM end address (8192)
     * r2 = word stride (4)
     * r3 = current color index (0..15)
     * r4 = packed 8-pixel 32-bit word (color repeated in all 8 nibbles)
     * r5 = temporary / shift accumulator
     * r6 = row/word counter per band (128 words = 1024 pixels = 8 rows)
     * r7 = constant 1
     * r8 = constant 128 (words per 8-row band)
     * r9 = loop condition flag
     */
    int main_idx = idx;
    prog[idx++] = enc_ri8(OP_MOVI, 0, 0);       /* r0 = 0 (VGA_BASE) */

    /* r1 = 8192 (0x2000 = VGA_FB_SIZE) */
    prog[idx++] = enc_ri8(OP_MOVI, 1, 1);       /* r1 = 1 */
    prog[idx++] = enc_ri8(OP_SHL,  1, 13);      /* r1 = 1 << 13 = 8192 */

    prog[idx++] = enc_ri8(OP_MOVI, 2, 4);       /* r2 = 4 (bytes per 32-bit store) */
    prog[idx++] = enc_ri8(OP_MOVI, 3, 0);       /* r3 = 0 (palette index 0) */
    prog[idx++] = enc_ri8(OP_MOVI, 7, 1);       /* r7 = 1 */

    /* r8 = 128 (words per 8-row band: 8 rows * 16 words/row) */
    prog[idx++] = enc_ri8(OP_MOVI, 8, 1);       /* r8 = 1 */
    prog[idx++] = enc_ri8(OP_SHL,  8, 7);       /* r8 = 128 */

    /* Outer loop: one color band per iteration */
    int outer_loop = idx;
    prog[idx++] = enc_rrr(OP_CMPLT, 9, 0, 1);   /* r9 = (r0 < 8192) */
    int jz_done_pos = idx++;                    /* patched below: if r9 == 0, exit */

    /* Pack nibble r3 into all 8 nibbles of r4: r4 = r3 * 0x11111111 */
    prog[idx++] = enc_rrr(OP_MOV, 4, 3, 0);     /* r4 = color (0x0000000C) */
    prog[idx++] = enc_rrr(OP_MOV, 5, 4, 0);     /* r5 = r4 */
    prog[idx++] = enc_ri8(OP_SHL, 5, 4);        /* r5 <<= 4 */
    prog[idx++] = enc_rrr(OP_OR,  4, 4, 5);     /* r4 = 0x000000CC */
    prog[idx++] = enc_rrr(OP_MOV, 5, 4, 0);     /* r5 = r4 */
    prog[idx++] = enc_ri8(OP_SHL, 5, 8);        /* r5 <<= 8 */
    prog[idx++] = enc_rrr(OP_OR,  4, 4, 5);     /* r4 = 0x0000CCCC */
    prog[idx++] = enc_rrr(OP_MOV, 5, 4, 0);     /* r5 = r4 */
    prog[idx++] = enc_ri8(OP_SHL, 5, 16);       /* r5 <<= 16 */
    prog[idx++] = enc_rrr(OP_OR,  4, 4, 5);     /* r4 = 0xCCCCCCCC */

    /* Inner loop: write 128 words (8 rows of 128px) */
    prog[idx++] = enc_rrr(OP_MOV, 6, 8, 0);     /* r6 = 128 */
    int inner_loop = idx;
    int jz_next_band_pos = idx++;               /* patched below: if r6 == 0, next band */

    prog[idx++] = enc_ri8(OP_ST32, 4, 0x00);    /* mem32[r0 + 0] = r4 (writes 8 pixels) */
    prog[idx++] = enc_rrr(OP_ADD,  0, 0, 2);    /* r0 += 4 */
    prog[idx++] = enc_rrr(OP_SUB,  6, 6, 7);    /* r6 -= 1 */
    int jmp_inner_pos = idx++;
    prog[jmp_inner_pos] = enc_i12(OP_JMP, (int16_t)(inner_loop - (jmp_inner_pos + 1)));

    int after_inner = idx;
    prog[jz_next_band_pos] = enc_ri8(OP_JZ, 6, (uint8_t)(after_inner - (jz_next_band_pos + 1)));

    prog[idx++] = enc_rrr(OP_ADD, 3, 3, 7);     /* r3 += 1 (next Sweetie-16 color) */
    int jmp_outer_pos = idx++;
    prog[jmp_outer_pos] = enc_i12(OP_JMP, (int16_t)(outer_loop - (jmp_outer_pos + 1)));

    int done_idx = idx;
    prog[jz_done_pos] = enc_ri8(OP_JZ, 9, (uint8_t)(done_idx - (jz_done_pos + 1)));
    prog[idx++] = enc_rrr(OP_JR, 14, 0, 0);     /* return to exit_pc */

    uint8_t mem[VM_MEM_SIZE] = {0};
    int32_t init_sp = (int32_t)sizeof(mem);
    int32_t exit_pc = idx;

    vm_run(prog, (size_t)idx, mem, sizeof(mem), main_idx, exit_pc, init_sp);

    if (vga_dump_ppm(mem, "vga_output.ppm") == 0) {
        printf("Rendered 128x128 Sweetie-16 framebuffer to vga_output.ppm\n");
    }
    return 0;
}
