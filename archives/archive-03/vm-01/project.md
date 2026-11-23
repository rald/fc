``` title="./Makefile"
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

BUILD_DIR = build
TARGET = vm
SRCS = src/main.c src/vm.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c include/vm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) asm/arkanoid.asm

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all run clean
```
```h title="./include/vm.h"
#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

/* Memory Map Constants */
#define VM_SCREEN_W      128
#define VM_SCREEN_H      128
#define VM_BACK_FB_ADDR  0x0000u /* 8192 bytes: VM draws here (128x128 4bpp) */
#define VM_FRONT_FB_ADDR 0x2000u /* 8192 bytes: SDL2 reads here (128x128 4bpp) */
#define VM_FB_ADDR       VM_BACK_FB_ADDR
#define VM_FB_SIZE       8192u   /* 128 * 128 / 2 (4bpp packed: low nibble = even x, high nibble = odd x) */
#define VM_INPUT_ADDR    0x4000u /* 32-bit bitmask of pressed buttons */
#define VM_SYS_ADDR      0x4004u /* 32-bit system register (bit 0 = swap back->front & yield frame) */
#define VM_RAM_ADDR      0x4010u /* General purpose RAM start */
#define VM_FONT_ADDR     0x4400u /* 8x8 DOS BIOS Font (128 ASCII chars * 8 bytes = 1024 bytes) */
#define VM_MEM_SIZE      0x6000u /* 24 KB total address space */

/* Button Bitmasks */
#define BTN_UP           (1u << 0)
#define BTN_DOWN         (1u << 1)
#define BTN_LEFT         (1u << 2)
#define BTN_RIGHT        (1u << 3)
#define BTN_X            (1u << 4)
#define BTN_O            (1u << 5)
#define BTN_L            (1u << 6)
#define BTN_R            (1u << 7)
#define BTN_SELECT       (1u << 8)
#define BTN_START        (1u << 9)

typedef struct {
    int32_t r[16];
    int32_t pc;
    uint8_t *mem;
    size_t mem_size;
    bool halted;
    bool frame_ready;
} vm_env_t;

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

void vm_env_init(vm_env_t *env, uint8_t *mem, size_t mem_size,
                 int32_t init_pc, int32_t init_lr, int32_t init_sp);

int32_t vm_run_env(vm_env_t *env, const uint16_t *code, size_t code_len,
                   size_t max_steps);

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp);

/* Simple two-pass assembler for VM assembly files */
int vm_assemble_file(const char *path, uint16_t *out_code, size_t max_code, size_t *out_len);

#endif /* VM_H */
```
```c title="./src/vm.c"
#include "vm.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static inline int32_t load32_le(const uint8_t *mem, uint32_t addr) {
    int32_t val;
    memcpy(&val, mem + addr, sizeof(val));
    return val;
}

static inline void store32_le(uint8_t *mem, uint32_t addr, int32_t val) {
    memcpy(mem + addr, &val, sizeof(val));
}

void vm_env_init(vm_env_t *env, uint8_t *mem, size_t mem_size,
                 int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    memset(env, 0, sizeof(*env));
    env->mem = mem;
    env->mem_size = mem_size;
    env->pc = init_pc;
    env->r[14] = init_lr;
    env->r[15] = init_sp;
    env->halted = false;
    env->frame_ready = false;
}

int32_t vm_run_env(vm_env_t *env, const uint16_t *code, size_t code_len,
                   size_t max_steps) {
    if (!env || env->halted) return env ? env->r[0] : 0;

    int32_t *r = env->r;
    uint8_t *mem = env->mem;
    size_t mem_size = env->mem_size;
    const uint16_t *pc = code + env->pc;
    const uint16_t *code_end = code + code_len;
    size_t steps = 0;

    static const void *dispatch[] = {
        &&L_JR,    &&L_MOVI,  &&L_MOV,   &&L_ADD,
        &&L_SUB,   &&L_AND,   &&L_OR,    &&L_XOR,
        &&L_SHL,   &&L_SHR,   &&L_CMPLT, &&L_JMP,
        &&L_JZ,    &&L_LD32,  &&L_ST32,  &&L_CALL
    };

    uint8_t a = 0, b = 0, c = 0, i8 = 0;

#define NEXT() do {                                                     \
        if (pc < code || pc >= code_end) {                              \
            env->pc = (int32_t)(pc - code);                             \
            env->halted = true;                                         \
            return r[0];                                                \
        }                                                               \
        if (max_steps > 0 && steps++ >= max_steps) {                    \
            env->pc = (int32_t)(pc - code);                             \
            return r[0];                                                \
        }                                                               \
        uint16_t ins = *pc++;                                           \
        uint8_t op = (uint8_t)(ins >> 12);                              \
        a  = (uint8_t)((ins >> 8) & 0xF);                               \
        b  = (uint8_t)((ins >> 4) & 0xF);                               \
        c  = (uint8_t)(ins & 0xF);                                      \
        i8 = (uint8_t)(ins & 0xFF);                                     \
        goto *dispatch[op];                                             \
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
            if (addr == VM_SYS_ADDR && (r[a] & 1)) {
                if (VM_FRONT_FB_ADDR + VM_FB_SIZE <= mem_size &&
                    VM_BACK_FB_ADDR + VM_FB_SIZE <= mem_size) {
                    memcpy(mem + VM_FRONT_FB_ADDR, mem + VM_BACK_FB_ADDR, VM_FB_SIZE);
                }
                env->frame_ready = true;
                env->pc = (int32_t)(pc - code);
                return r[0];
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

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    vm_env_t env;
    vm_env_init(&env, mem, mem_size, init_pc, init_lr, init_sp);
    return vm_run_env(&env, code, code_len, 0);
}

/* --- Two-Pass Assembler --- */
typedef struct {
    char name[64];
    int32_t addr;
} asm_label_t;

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int parse_reg(const char *tok) {
    if (!tok) return -1;
    if ((tok[0] == 'r' || tok[0] == 'R') && isdigit((unsigned char)tok[1])) {
        char *end = NULL;
        long v = strtol(tok + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v <= 15) return (int)v;
    }
    if (ascii_strcasecmp(tok, "lr") == 0) return 14;
    if (ascii_strcasecmp(tok, "sp") == 0) return 15;
    return -1;
}

static int parse_imm(const char *tok, const asm_label_t *labels, int label_count, int32_t *out) {
    if (!tok) return -1;
    for (int i = 0; i < label_count; i++) {
        if (strcmp(tok, labels[i].name) == 0) {
            *out = labels[i].addr;
            return 0;
        }
    }
    char *end = NULL;
    long v = strtol(tok, &end, 0);
    if (end && *end == '\0') {
        *out = (int32_t)v;
        return 0;
    }
    return -1;
}

static void strip_comment(char *line) {
    for (char *p = line; *p; p++) {
        if (*p == ';' || *p == '#' || (*p == '/' && *(p + 1) == '/')) {
            *p = '\0';
            break;
        }
    }
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e - 1))) *--e = '\0';
    return s;
}

int vm_assemble_file(const char *path, uint16_t *out_code, size_t max_code, size_t *out_len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    asm_label_t labels[512];
    int label_count = 0;
    char line[256];

    /* Pass 1: Collect labels */
    int32_t pc = 0;
    while (fgets(line, sizeof(line), fp)) {
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0') continue;

        char *colon = strchr(s, ':');
        if (colon) {
            *colon = '\0';
            char *lbl = trim(s);
            if (*lbl && label_count < 512) {
                strncpy(labels[label_count].name, lbl, 63);
                labels[label_count].name[63] = '\0';
                labels[label_count].addr = pc;
                label_count++;
            }
            s = trim(colon + 1);
            if (*s == '\0') continue;
        }
        pc++;
    }

    /* Pass 2: Encode instructions */
    rewind(fp);
    pc = 0;
    int line_no = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0') continue;

        char *colon = strchr(s, ':');
        if (colon) {
            s = trim(colon + 1);
            if (*s == '\0') continue;
        }

        if ((size_t)pc >= max_code) {
            fprintf(stderr, "Assembler error: program exceeds max_code\n");
            fclose(fp);
            return -1;
        }

        char *toks[5] = {0};
        int ntok = 0;
        char *p = strtok(s, " ,\t\r\n");
        while (p && ntok < 5) {
            toks[ntok++] = p;
            p = strtok(NULL, " ,\t\r\n");
        }
        if (ntok == 0) continue;

        uint16_t ins = 0;
        int ok = 0;
        const char *op = toks[0];

        if (ascii_strcasecmp(op, "jr") == 0 && ntok == 2) {
            int a = parse_reg(toks[1]);
            if (a >= 0) { ins = enc_rrr(OP_JR, (uint8_t)a, 0, 0); ok = 1; }
        } else if (ascii_strcasecmp(op, "movi") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_MOVI, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (ascii_strcasecmp(op, "mov") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            if (a >= 0 && b >= 0) { ins = enc_rrr(OP_MOV, (uint8_t)a, (uint8_t)b, 0); ok = 1; }
        } else if (ascii_strcasecmp(op, "add") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_ADD, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "sub") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_SUB, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "and") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_AND, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "or") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_OR, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "xor") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_XOR, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "shl") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_SHL, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (ascii_strcasecmp(op, "shr") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_SHR, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (ascii_strcasecmp(op, "cmplt") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_CMPLT, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (ascii_strcasecmp(op, "jmp") == 0 && ntok == 2) {
            int32_t target = 0;
            if (parse_imm(toks[1], labels, label_count, &target) == 0) {
                int32_t off = target - (pc + 1);
                if (off >= -2048 && off <= 2047) {
                    ins = enc_i12(OP_JMP, (int16_t)off);
                    ok = 1;
                }
            }
        } else if (ascii_strcasecmp(op, "jz") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t target = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &target) == 0) {
                int32_t off = target - (pc + 1);
                if (off >= -128 && off <= 127) {
                    ins = enc_ri8(OP_JZ, (uint8_t)a, (uint8_t)(int8_t)off);
                    ok = 1;
                } else {
                    fprintf(stderr, "Assembler error line %d: jz target out of range (%d)\n", line_no, off);
                }
            }
        } else if (ascii_strcasecmp(op, "ld32") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            int32_t c = 0;
            if (a >= 0 && b >= 0 && parse_imm(toks[3], labels, label_count, &c) == 0 && c >= 0 && c <= 15) {
                ins = enc_rrr(OP_LD32, (uint8_t)a, (uint8_t)b, (uint8_t)c);
                ok = 1;
            }
        } else if (ascii_strcasecmp(op, "st32") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            int32_t c = 0;
            if (a >= 0 && b >= 0 && parse_imm(toks[3], labels, label_count, &c) == 0 && c >= 0 && c <= 15) {
                ins = enc_rrr(OP_ST32, (uint8_t)a, (uint8_t)b, (uint8_t)c);
                ok = 1;
            }
        } else if (ascii_strcasecmp(op, "call") == 0 && ntok == 2) {
            int32_t target = 0;
            if (parse_imm(toks[1], labels, label_count, &target) == 0) {
                int32_t off = target - (pc + 1);
                if (off >= -2048 && off <= 2047) {
                    ins = enc_i12(OP_CALL, (int16_t)off);
                    ok = 1;
                }
            }
        }

        if (!ok) {
            fprintf(stderr, "Assembler error at line %d\n", line_no);
            fclose(fp);
            return -1;
        }
        out_code[pc++] = ins;
    }

    fclose(fp);
    *out_len = (size_t)pc;
    return 0;
}
```
```c title="./src/main.c"
#include "vm.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Sweetie-16 Palette (0xAARRGGBB for SDL_PIXELFORMAT_ARGB8888) */
static const uint32_t SWEETIE16_PALETTE[16] = {
    0xFF1A1C2Cu, /* 0: Dark Void */
    0xFF5D275Du, /* 1: Deep Purple */
    0xFFB13E53u, /* 2: Crimson Red */
    0xFFEF7D57u, /* 3: Vibrant Orange */
    0xFFFFCD75u, /* 4: Warm Yellow */
    0xFFA7F070u, /* 5: Lime Green */
    0xFF38B764u, /* 6: Emerald Green */
    0xFF257179u, /* 7: Deep Teal */
    0xFF29366Fu, /* 8: Navy Blue */
    0xFF3B5DC9u, /* 9: Royal Blue */
    0xFF41A6F6u, /* 10: Sky Blue */
    0xFF73EFF7u, /* 11: Cyan Ice */
    0xFFF4F4F4u, /* 12: Pure White */
    0xFF94B0C2u, /* 13: Light Slate */
    0xFF566C86u, /* 14: Medium Slate */
    0xFF333C57u  /* 15: Dark Slate */
};

/* Standard IBM PC / DOS BIOS 8x8 Font (ASCII 0..127, MSB = leftmost pixel) */
static const uint8_t DOS_FONT_8X8[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x00 */
    {0x7E,0x81,0xA5,0x81,0xBD,0x99,0x81,0x7E}, /* 0x01 */
    {0x7E,0xFF,0xDB,0xFF,0xC3,0xE7,0xFF,0x7E}, /* 0x02 */
    {0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00}, /* 0x03 (Heart) */
    {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00}, /* 0x04 */
    {0x38,0x7C,0x38,0xFE,0xFE,0x7C,0x38,0x7C}, /* 0x05 */
    {0x10,0x10,0x38,0x7C,0xFE,0x7C,0x38,0x7C}, /* 0x06 */
    {0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00}, /* 0x07 */
    {0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF}, /* 0x08 */
    {0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00}, /* 0x09 */
    {0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF}, /* 0x0A */
    {0x0F,0x07,0x0F,0x7D,0xCC,0xCC,0xCC,0x78}, /* 0x0B */
    {0x3C,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18}, /* 0x0C */
    {0x3F,0x33,0x3F,0x30,0x30,0x70,0xF0,0xE0}, /* 0x0D */
    {0x7F,0x63,0x7F,0x63,0x63,0x67,0xE6,0xC0}, /* 0x0E */
    {0x99,0x5A,0x3C,0xE7,0xE7,0x3C,0x5A,0x99}, /* 0x0F */
    {0x80,0xE0,0xF8,0xFE,0xF8,0xE0,0x80,0x00}, /* 0x10 */
    {0x02,0x0E,0x3E,0xFE,0x3E,0x0E,0x02,0x00}, /* 0x11 */
    {0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18}, /* 0x12 */
    {0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x66}, /* 0x13 */
    {0x7F,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x00}, /* 0x14 */
    {0x3E,0x63,0x38,0x6C,0x6C,0x38,0xCC,0x78}, /* 0x15 */
    {0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x00}, /* 0x16 */
    {0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0xFF}, /* 0x17 */
    {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00}, /* 0x18 */
    {0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00}, /* 0x19 */
    {0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00}, /* 0x1A */
    {0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00}, /* 0x1B */
    {0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00}, /* 0x1C */
    {0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00}, /* 0x1D */
    {0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00,0x00}, /* 0x1E */
    {0x00,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00}, /* 0x1F */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 ' ' */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 0x21 '!' */
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, /* 0x22 '"' */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* 0x23 '#' */
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, /* 0x24 '$' */
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, /* 0x25 '%' */
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, /* 0x26 '&' */
    {0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00}, /* 0x27 ''' */
    {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00}, /* 0x28 '(' */
    {0x60,0x30,0x18,0x18,0x18,0x30,0x60,0x00}, /* 0x29 ')' */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 0x2A '*' */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* 0x2B '+' */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* 0x2C ',' */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* 0x2D '-' */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* 0x2E '.' */
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, /* 0x2F '/' */
    {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00}, /* 0x30 '0' */
    {0x30,0x70,0x30,0x30,0x30,0x30,0xFC,0x00}, /* 0x31 '1' */
    {0x78,0xCC,0x0C,0x38,0x60,0xCC,0xFC,0x00}, /* 0x32 '2' */
    {0x78,0xCC,0x0C,0x38,0x0C,0xCC,0x78,0x00}, /* 0x33 '3' */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, /* 0x34 '4' */
    {0xFC,0xC0,0xF8,0x0C,0x0C,0xCC,0x78,0x00}, /* 0x35 '5' */
    {0x38,0x60,0xC0,0xF8,0xCC,0xCC,0x78,0x00}, /* 0x36 '6' */
    {0xFC,0xCC,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 0x37 '7' */
    {0x78,0xCC,0xCC,0x78,0xCC,0xCC,0x78,0x00}, /* 0x38 '8' */
    {0x78,0xCC,0xCC,0x7C,0x0C,0x18,0x70,0x00}, /* 0x39 '9' */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* 0x3A ':' */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* 0x3B ';' */
    {0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x00}, /* 0x3C '<' */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* 0x3D '=' */
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, /* 0x3E '>' */
    {0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00}, /* 0x3F '?' */
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00}, /* 0x40 '@' */
    {0x30,0x78,0xCC,0xCC,0xFC,0xCC,0xCC,0x00}, /* 0x41 'A' */
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, /* 0x42 'B' */
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, /* 0x43 'C' */
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, /* 0x44 'D' */
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, /* 0x45 'E' */
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, /* 0x46 'F' */
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, /* 0x47 'G' */
    {0xCC,0xCC,0xCC,0xFC,0xCC,0xCC,0xCC,0x00}, /* 0x48 'H' */
    {0x78,0x30,0x30,0x30,0x30,0x30,0x78,0x00}, /* 0x49 'I' */
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, /* 0x4A 'J' */
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, /* 0x4B 'K' */
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, /* 0x4C 'L' */
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, /* 0x4D 'M' */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* 0x4E 'N' */
    {0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* 0x4F 'O' */
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, /* 0x50 'P' */
    {0x78,0xCC,0xCC,0xCC,0xDC,0x78,0x1C,0x00}, /* 0x51 'Q' */
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, /* 0x52 'R' */
    {0x78,0xCC,0xE0,0x70,0x1C,0xCC,0x78,0x00}, /* 0x53 'S' */
    {0xFC,0xB4,0x30,0x30,0x30,0x30,0x78,0x00}, /* 0x54 'T' */
    {0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xFC,0x00}, /* 0x55 'U' */
    {0xCC,0xCC,0xCC,0xCC,0xCC,0x78,0x30,0x00}, /* 0x56 'V' */
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, /* 0x57 'W' */
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, /* 0x58 'X' */
    {0xCC,0xCC,0xCC,0x78,0x30,0x30,0x78,0x00}, /* 0x59 'Y' */
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, /* 0x5A 'Z' */
    {0x78,0x60,0x60,0x60,0x60,0x60,0x78,0x00}, /* 0x5B '[' */
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, /* 0x5C '\' */
    {0x78,0x18,0x18,0x18,0x18,0x18,0x78,0x00}, /* 0x5D ']' */
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* 0x5E '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 0x5F '_' */
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00}, /* 0x60 '`' */
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, /* 0x61 'a' */
    {0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00}, /* 0x62 'b' */
    {0x00,0x00,0x78,0xCC,0xC0,0xCC,0x78,0x00}, /* 0x63 'c' */
    {0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00}, /* 0x64 'd' */
    {0x00,0x00,0x78,0xCC,0xFC,0xC0,0x78,0x00}, /* 0x65 'e' */
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00}, /* 0x66 'f' */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, /* 0x67 'g' */
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, /* 0x68 'h' */
    {0x30,0x00,0x70,0x30,0x30,0x30,0x78,0x00}, /* 0x69 'i' */
    {0x0C,0x00,0x0C,0x0C,0x0C,0xCC,0xCC,0x78}, /* 0x6A 'j' */
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, /* 0x6B 'k' */
    {0x70,0x30,0x30,0x30,0x30,0x30,0x78,0x00}, /* 0x6C 'l' */
    {0x00,0x00,0xCC,0xFE,0xFE,0xD6,0xC6,0x00}, /* 0x6D 'm' */
    {0x00,0x00,0xF8,0xCC,0xCC,0xCC,0xCC,0x00}, /* 0x6E 'n' */
    {0x00,0x00,0x78,0xCC,0xCC,0xCC,0x78,0x00}, /* 0x6F 'o' */
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, /* 0x70 'p' */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, /* 0x71 'q' */
    {0x00,0x00,0xDC,0x76,0x66,0x60,0xF0,0x00}, /* 0x72 'r' */
    {0x00,0x00,0x7C,0xC0,0x78,0x0C,0xF8,0x00}, /* 0x73 's' */
    {0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0x00}, /* 0x74 't' */
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, /* 0x75 'u' */
    {0x00,0x00,0xCC,0xCC,0xCC,0x78,0x30,0x00}, /* 0x76 'v' */
    {0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x6C,0x00}, /* 0x77 'w' */
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, /* 0x78 'x' */
    {0x00,0x00,0xCC,0xCC,0xCC,0x7C,0x0C,0xF8}, /* 0x79 'y' */
    {0x00,0x00,0xFC,0x98,0x30,0x64,0xFC,0x00}, /* 0x7A 'z' */
    {0x1C,0x30,0x30,0xE0,0x30,0x30,0x1C,0x00}, /* 0x7B '{' */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 0x7C '|' */
    {0xE0,0x30,0x30,0x1C,0x30,0x30,0xE0,0x00}, /* 0x7D '}' */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x7E '~' */
    {0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0x00}  /* 0x7F */
};

static uint32_t poll_inputs(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint32_t mask = 0;

    /* ARROWS or WASD */
    if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) mask |= BTN_UP;
    if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) mask |= BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) mask |= BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) mask |= BTN_RIGHT;

    /* X -> Z or . */
    if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_PERIOD]) mask |= BTN_X;

    /* O -> X or / */
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_SLASH])  mask |= BTN_O;

    /* L -> A or L */
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_L])      mask |= BTN_L;

    /* R -> S or ; */
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_SEMICOLON]) mask |= BTN_R;

    /* SELECT -> R-SHIFT */
    if (keys[SDL_SCANCODE_RSHIFT]) mask |= BTN_SELECT;

    /* START -> ENTER */
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) mask |= BTN_START;

    return mask;
}

static void render_framebuffer(const uint8_t *fb, uint32_t *argb_pixels) {
    for (size_t i = 0; i < VM_FB_SIZE; i++) {
        uint8_t packed = fb[i];
        uint8_t c0 = packed & 0x0Fu;
        uint8_t c1 = (packed >> 4) & 0x0Fu;
        argb_pixels[i * 2 + 0] = SWEETIE16_PALETTE[c0];
        argb_pixels[i * 2 + 1] = SWEETIE16_PALETTE[c1];
    }
}

int main(int argc, char **argv) {
    const char *asm_path = (argc > 1) ? argv[1] : "asm/arkanoid.asm";

    uint16_t code[4096];
    size_t code_len = 0;
    if (vm_assemble_file(asm_path, code, 4096, &code_len) != 0) {
        fprintf(stderr, "Failed to assemble '%s'\n", asm_path);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    const int scale = 5;
    SDL_Window *window = SDL_CreateWindow(
        "VM 128x128 Sweetie-16 - Arkanoid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VM_SCREEN_W * scale, VM_SCREEN_H * scale,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(renderer, VM_SCREEN_W, VM_SCREEN_H);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VM_SCREEN_W, VM_SCREEN_H
    );

    uint8_t mem[VM_MEM_SIZE];
    memset(mem, 0, sizeof(mem));

    /* Load 8x8 DOS BIOS Font into VM memory at VM_FONT_ADDR (0x4400) */
    if (VM_FONT_ADDR + sizeof(DOS_FONT_8X8) <= VM_MEM_SIZE) {
        memcpy(mem + VM_FONT_ADDR, DOS_FONT_8X8, sizeof(DOS_FONT_8X8));
    }

    vm_env_t env;
    vm_env_init(&env, mem, sizeof(mem), 0, (int32_t)code_len, (int32_t)VM_MEM_SIZE);

    uint32_t argb_pixels[VM_SCREEN_W * VM_SCREEN_H];
    bool running = true;
    const double target_dt = 1.0 / 60.0;
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;

    while (running && !env.halted) {
        uint64_t now_counter = SDL_GetPerformanceCounter();
        double delta_time = (double)(now_counter - last_counter) / (double)perf_freq;
        last_counter = now_counter;

        /* Clamp delta_time to avoid spiral of death after window drag/pause */
        if (delta_time > 0.1) {
            delta_time = 0.1;
        }
        accumulator += delta_time;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }

        /* Write polled input state into memory-mapped input register */
        uint32_t btn_mask = poll_inputs();
        memcpy(mem + VM_INPUT_ADDR, &btn_mask, sizeof(btn_mask));

        /* Step VM at fixed 60 Hz intervals driven by measured delta_time */
        while (accumulator >= target_dt && !env.halted) {
            uint32_t sys_val = 0;
            memcpy(mem + VM_SYS_ADDR, &sys_val, sizeof(sys_val));
            env.frame_ready = false;

            vm_run_env(&env, code, code_len, 250000);
            accumulator -= target_dt;
        }

        /* Convert 4bpp framebuffer using Sweetie-16 palette & present */
        render_framebuffer(mem + VM_FRONT_FB_ADDR, argb_pixels);
        SDL_UpdateTexture(texture, NULL, argb_pixels, VM_SCREEN_W * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```
```asm title="./asm/arkanoid.asm"
; ============================================================================
; ARKANOID FOR 128x128 4BPP SWEETIE-16 VM (WITH 8x8 DOS FONT HUD & BANNERS)
; ============================================================================
; Memory Map:
;   0x0000 .. 0x1FFF : 128x128 4bpp Back Framebuffer (8192 bytes, 64 bytes/row)
;   0x2000 .. 0x3FFF : 128x128 4bpp Front Framebuffer (8192 bytes)
;   0x4000           : Input register (32-bit bitmask)
;   0x4004           : System register (write 1 to yield frame to SDL2)
;   0x4010           : Game State Base (r13 points here)
;     [r13 + 0]  (0) : pad_x      (paddle left X, 4..100)
;     [r13 + 4]  (1) : ball_x     (ball X, 0..124)
;     [r13 + 8]  (2) : ball_y     (ball Y, 12..124)
;     [r13 + 12] (3) : ball_vx    (ball X velocity: -2, -1, 1, 2)
;     [r13 + 16] (4) : ball_vy    (ball Y velocity: -1, 1)
;     [r13 + 20] (5) : ball_stuck (1 = resting on paddle, 0 = active)
;     [r13 + 24] (6) : lives      (remaining lives, starts at 3)
;     [r13 + 28] (7) : bricks_left(active bricks count, starts at 32)
;     [r13 + 32] (8) : game_over  (0 = playing, 1 = lost, 2 = won)
;     [r13 + 36] (9) : score      (player score, +10 per brick)
;   0x4040           : Brick Array Base (32 bricks: 4 rows x 8 cols, 4 bytes each)
;                      Value = Sweetie-16 color index (0 = destroyed)
;   0x4400 .. 0x47FF : 8x8 DOS BIOS Font (128 ASCII chars x 8 bytes)
; ============================================================================

init:
    ; Set r13 = 0x4010 (Game State Base)
    movi r13, 0x40
    shl  r13, 8
    movi r0, 0x10
    add  r13, r13, r0

reset_game:
    ; Initialize lives = 3, game_over = 0, score = 0
    movi r0, 3
    st32 r0, r13, 6         ; lives = 3
    movi r0, 0
    st32 r0, r13, 8         ; game_over = 0
    st32 r0, r13, 9         ; score = 0

    ; Initialize 32 bricks (4 rows x 8 cols) at 0x4040
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer (0x4040)
    movi r9, 0              ; r9 = row (0..3)
    movi r10, 4             ; max rows
    movi r11, 8             ; cols per row
    movi r12, 1             ; constant 1

init_brick_row:
    cmplt r0, r9, r10
    jz    r0, init_bricks_done

    ; Row color: row 0 -> 2 (Red), row 1 -> 3 (Orange), row 2 -> 4 (Yellow), row 3 -> 5 (Lime)
    movi r1, 2
    add  r1, r1, r9         ; r1 = color for this row

    movi r2, 0              ; col = 0
init_brick_col:
    cmplt r0, r2, r11
    jz    r0, next_brick_row
    st32  r1, r8, 0         ; store brick color
    movi  r0, 4
    add   r8, r8, r0        ; brick_ptr += 4
    add   r2, r2, r12       ; col++
    jmp   init_brick_col

next_brick_row:
    add  r9, r9, r12        ; row++
    jmp  init_brick_row

init_bricks_done:
    movi r0, 32
    st32 r0, r13, 7         ; bricks_left = 32

reset_ball:
    movi r0, 52             ; center paddle X (128/2 - 24/2 = 52)
    st32 r0, r13, 0         ; pad_x = 52
    movi r0, 62
    st32 r0, r13, 1         ; ball_x = 62
    movi r0, 112
    st32 r0, r13, 2         ; ball_y = 112
    movi r0, 1
    st32 r0, r13, 3         ; ball_vx = 1
    movi r0, -1
    st32 r0, r13, 4         ; ball_vy = -1
    movi r0, 1
    st32 r0, r13, 5         ; ball_stuck = 1

; ============================================================================
; MAIN GAME LOOP (Runs once per video frame)
; ============================================================================
main_loop:
    ; Read input register at 0x4000
    movi r8, 0x40
    shl  r8, 8              ; r8 = 0x4000
    ld32 r9, r8, 0          ; r9 = input bitmask

    ; Check if game_over != 0
    ld32 r0, r13, 8
    jz   r0, handle_playing

    ; Game Over or Win state: check START (bit 9 = 512) to restart
    movi r1, 1
    shl  r1, 9              ; r1 = 512 (BTN_START)
    and  r0, r9, r1
    jz   r0, skip_restart
    jmp  reset_game
skip_restart:
    jmp  render_frame

handle_playing:
    ; --- Paddle Movement ---
    ld32 r1, r13, 0         ; r1 = pad_x
    movi r2, 3              ; paddle speed = 3 px/frame

    ; Check LEFT (bit 2 = 4)
    movi r3, 4
    and  r0, r9, r3
    jz   r0, check_right
    sub  r1, r1, r2
    movi r4, 4              ; min X = 4
    cmplt r0, r1, r4
    jz   r0, check_right
    mov  r1, r4

check_right:
    ; Check RIGHT (bit 3 = 8)
    movi r3, 8
    and  r0, r9, r3
    jz   r0, store_paddle
    add  r1, r1, r2
    movi r4, 100            ; max X = 128 - 4 - 24 = 100
    cmplt r0, r4, r1
    jz   r0, store_paddle
    mov  r1, r4

store_paddle:
    st32 r1, r13, 0         ; save updated pad_x

    ; --- Ball State ---
    ld32 r0, r13, 5         ; r0 = ball_stuck
    jz   r0, move_ball

    ; Ball is stuck on paddle: follow pad_x + 10
    movi r2, 10
    add  r2, r1, r2
    st32 r2, r13, 1         ; ball_x = pad_x + 10
    movi r2, 112
    st32 r2, r13, 2         ; ball_y = 112

    ; Launch ball on X (16), O (32), UP (1), or START (512)
    movi r3, 49             ; 16 | 32 | 1 = 49
    movi r4, 1
    shl  r4, 9              ; 512
    or   r3, r3, r4
    and  r0, r9, r3
    jz   r0, skip_launch

    movi r0, 0
    st32 r0, r13, 5         ; ball_stuck = 0
skip_launch:
    jmp  render_frame

move_ball:
    ld32 r1, r13, 1         ; r1 = ball_x
    ld32 r2, r13, 2         ; r2 = ball_y
    ld32 r3, r13, 3         ; r3 = ball_vx
    ld32 r4, r13, 4         ; r4 = ball_vy

    add  r1, r1, r3         ; ball_x += ball_vx
    add  r2, r2, r4         ; ball_y += ball_vy

    ; Left wall bounce (x < 4)
    movi r5, 4
    cmplt r0, r1, r5
    jz   r0, check_wall_right
    mov  r1, r5
    movi r5, 0
    sub  r3, r5, r3         ; ball_vx = -ball_vx

check_wall_right:
    ; Right wall bounce (x > 120, ball width = 4 -> right edge 124)
    movi r5, 120
    cmplt r0, r5, r1
    jz   r0, check_wall_top
    mov  r1, r5
    movi r5, 0
    sub  r3, r5, r3         ; ball_vx = -ball_vx

check_wall_top:
    ; Top playfield ceiling bounce below 8x8 HUD (y < 12)
    movi r5, 12
    cmplt r0, r2, r5
    jz   r0, check_bottom
    mov  r2, r5
    movi r5, 0
    sub  r4, r5, r4         ; ball_vy = -ball_vy

check_bottom:
    ; Bottom out-of-bounds (y > 124)
    movi r5, 124
    cmplt r0, r5, r2
    jz   r0, check_paddle_hit

    ; Lost a life
    ld32 r6, r13, 6         ; lives
    movi r7, 1
    sub  r6, r6, r7
    st32 r6, r13, 6
    jz   r6, set_game_lost
    jmp  reset_ball

set_game_lost:
    movi r0, 1
    st32 r0, r13, 8         ; game_over = 1 (lost)
    jmp  render_frame

check_paddle_hit:
    ; Only collide if ball moving down (ball_vy > 0 -> 0 < ball_vy)
    movi r5, 0
    cmplt r0, r5, r4
    jz   r0, check_bricks

    ; Paddle Y range: 116..119 (ball bottom is r2 + 4)
    movi r5, 112            ; ball_y >= 112 (so bottom >= 116)
    cmplt r0, r2, r5
    jz   r0, check_pad_y2
    jmp  check_bricks
check_pad_y2:
    movi r5, 119
    cmplt r0, r5, r2        ; ball_y > 119 -> missed paddle
    jz   r0, check_pad_x
    jmp  check_bricks

check_pad_x:
    ld32 r6, r13, 0         ; r6 = pad_x (width = 24)
    ; Ball right edge (r1 + 4) > pad_x -> pad_x < r1 + 4
    movi r7, 4
    add  r7, r1, r7
    cmplt r0, r6, r7
    jz   r0, check_bricks

    ; Ball left edge (r1) < pad_x + 24
    movi r7, 24
    add  r7, r6, r7
    cmplt r0, r1, r7
    jz   r0, check_bricks

    ; Paddle hit! Bounce up and angle based on hit offset
    movi r2, 112
    movi r4, -1

    ; Offset from pad_x: diff = (r1 + 2) - (r6 + 12) = r1 - r6 - 10
    sub  r7, r1, r6
    movi r5, 10
    sub  r7, r7, r5         ; negative = left side, positive = right side

    ; If diff < -4 -> vx = -2; else if diff < 0 -> vx = -1; else if diff < 5 -> vx = 1; else vx = 2
    movi r5, -4
    cmplt r0, r7, r5
    jz   r0, pad_ang_1
    movi r3, -2
    jmp  check_bricks
pad_ang_1:
    movi r5, 0
    cmplt r0, r7, r5
    jz   r0, pad_ang_2
    movi r3, -1
    jmp  check_bricks
pad_ang_2:
    movi r5, 5
    cmplt r0, r7, r5
    jz   r0, pad_ang_3
    movi r3, 1
    jmp  check_bricks
pad_ang_3:
    movi r3, 2

check_bricks:
    ; Save ball_x, ball_y, ball_vx, ball_vy temporarily
    st32 r1, r13, 1
    st32 r2, r13, 2
    st32 r3, r13, 3
    st32 r4, r13, 4

    ; Check brick grid: 4 rows (y = 18, 25, 32, 39; height = 5)
    ;                   8 cols (x = 8 + col*14; width = 12)
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer
    movi r9, 0              ; r9 = row (0..3)

brick_row_loop:
    movi r5, 4
    cmplt r0, r9, r5
    jz   r0, bricks_done
    jmp  brick_row_body
bricks_done:
    jmp  render_frame
brick_row_body:

    ; brick_y = 18 + row * 7
    mov  r6, r9
    shl  r6, 3              ; row * 8
    sub  r6, r6, r9         ; row * 7
    movi r5, 18
    add  r6, r6, r5         ; r6 = brick_y

    movi r10, 0             ; r10 = col (0..7)
brick_col_loop:
    movi r5, 8
    cmplt r0, r10, r5
    jz   r0, next_b_row

    ; Check if brick is active
    ld32 r7, r8, 0
    jz   r7, advance_brick

    ; brick_x = 8 + col * 14
    mov  r7, r10
    shl  r7, 4              ; col * 16
    sub  r7, r7, r10        ; col * 15
    sub  r7, r7, r10        ; col * 14
    movi r5, 8
    add  r7, r7, r5         ; r7 = brick_x

    ; AABB check between ball (r1, r2, 4x4) and brick (r7, r6, 12x5)
    ; Check brick_x < ball_x + 4
    movi r5, 4
    add  r5, r1, r5
    cmplt r0, r7, r5
    jz   r0, advance_brick

    ; Check ball_x < brick_x + 12
    movi r5, 12
    add  r5, r7, r5
    cmplt r0, r1, r5
    jz   r0, advance_brick

    ; Check brick_y < ball_y + 4
    movi r5, 4
    add  r5, r2, r5
    cmplt r0, r6, r5
    jz   r0, advance_brick

    ; Check ball_y < brick_y + 5
    movi r5, 5
    add  r5, r6, r5
    cmplt r0, r2, r5
    jz   r0, advance_brick

    ; Brick Hit! Destroy brick
    movi r0, 0
    st32 r0, r8, 0

    ; Invert ball_vy
    ld32 r4, r13, 4
    movi r5, 0
    sub  r4, r5, r4
    st32 r4, r13, 4

    ; Add 10 to score
    ld32 r5, r13, 9
    movi r0, 10
    add  r5, r5, r0
    st32 r5, r13, 9

    ; Decrement bricks_left
    ld32 r5, r13, 7
    movi r0, 1
    sub  r5, r5, r0
    st32 r5, r13, 7
    jz   r5, do_game_won
    jmp  render_frame
do_game_won:
    jmp  set_game_won

set_game_won:
    movi r0, 2
    st32 r0, r13, 8         ; game_over = 2 (won)
    jmp  render_frame

advance_brick:
    movi r5, 4
    add  r8, r8, r5
    movi r5, 1
    add  r10, r10, r5
    jmp  brick_col_loop

next_b_row:
    movi r5, 1
    add  r9, r9, r5
    jmp  brick_row_loop

; ============================================================================
; RENDER FRAME TO 128x128 4BPP FRAMEBUFFER (0x0000 .. 0x1FFF)
; ============================================================================
render_frame:
    ; 1. Clear framebuffer to color 0 (Dark Void) - 2048 32-bit words
    movi r8, 0              ; r8 = fb ptr
    movi r9, 0x20
    shl  r9, 8              ; r9 = 0x2000 (end of fb)
    movi r0, 0
    movi r1, 4
clear_loop:
    st32  r0, r8, 0
    add   r8, r8, r1
    cmplt r2, r8, r9
    jz    r2, draw_borders
    jmp   clear_loop

draw_borders:
    ; Playfield top line below 8x8 DOS HUD (x=0..127, y=10..11, color=14)
    movi r1, 0
    movi r2, 10
    movi r3, 128
    movi r4, 2
    movi r5, 14
    call fill_rect

    ; Left border (x=0..2, y=10..127, color=14)
    movi r1, 0
    movi r2, 10
    movi r3, 3
    movi r4, 118
    movi r5, 14
    call fill_rect

    ; Right border (x=125..127, y=10..127, color=14)
    movi r1, 125
    movi r2, 10
    movi r3, 3
    movi r4, 118
    movi r5, 14
    call fill_rect

draw_hud:
    ; Top 8x8 DOS Font HUD (y=1): "SCORE:" + 3 digits on left, lives hearts on right
    movi r1, 2
    movi r2, 1
    movi r3, 83             ; 'S'
    movi r4, 12             ; Pure White
    call draw_char_8x8

    movi r1, 10
    movi r2, 1
    movi r3, 67             ; 'C'
    movi r4, 12
    call draw_char_8x8

    movi r1, 18
    movi r2, 1
    movi r3, 79             ; 'O'
    movi r4, 12
    call draw_char_8x8

    movi r1, 26
    movi r2, 1
    movi r3, 82             ; 'R'
    movi r4, 12
    call draw_char_8x8

    movi r1, 34
    movi r2, 1
    movi r3, 69             ; 'E'
    movi r4, 12
    call draw_char_8x8

    movi r1, 42
    movi r2, 1
    movi r3, 58             ; ':'
    movi r4, 12
    call draw_char_8x8

    ; Render 3-digit score at x = 50, 58, 66 (color = 4 Warm Yellow)
    ld32 r8, r13, 9         ; r8 = score (0..320)

    ; Hundreds digit
    movi r9, 0              ; hundreds count
    movi r10, 100
hud_hundreds_loop:
    cmplt r0, r8, r10
    jz    r0, hud_sub_100
    jmp   hud_draw_100
hud_sub_100:
    sub   r8, r8, r10
    movi  r0, 1
    add   r9, r9, r0
    jmp   hud_hundreds_loop
hud_draw_100:
    movi  r1, 50
    movi  r2, 1
    movi  r0, 48            ; '0'
    add   r3, r9, r0
    movi  r4, 4             ; Warm Yellow
    call  draw_char_8x8

    ; Tens digit
    movi r9, 0              ; tens count
    movi r10, 10
hud_tens_loop:
    cmplt r0, r8, r10
    jz    r0, hud_sub_10
    jmp   hud_draw_10
hud_sub_10:
    sub   r8, r8, r10
    movi  r0, 1
    add   r9, r9, r0
    jmp   hud_tens_loop
hud_draw_10:
    movi  r1, 58
    movi  r2, 1
    movi  r0, 48            ; '0'
    add   r3, r9, r0
    movi  r4, 4             ; Warm Yellow
    call  draw_char_8x8

    ; Ones digit (remainder in r8)
    movi  r1, 66
    movi  r2, 1
    movi  r0, 48            ; '0'
    add   r3, r8, r0
    movi  r4, 4             ; Warm Yellow
    call  draw_char_8x8

    ; Draw remaining lives using DOS 8x8 heart glyph (ASCII 0x03) on top-right
    ld32 r8, r13, 6         ; lives (0..3)
    movi r9, 102            ; start x = 102 (102, 110, 118)
draw_lives_loop:
    jz   r8, draw_bricks
    mov  r1, r9
    movi r2, 1
    movi r3, 3              ; DOS Heart glyph (0x03)
    movi r4, 2              ; Crimson Red
    call draw_char_8x8
    movi r0, 8
    add  r9, r9, r0
    movi r0, 1
    sub  r8, r8, r0
    jmp  draw_lives_loop

draw_bricks:
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer
    movi r9, 0              ; row = 0

render_b_row:
    movi r0, 4
    cmplt r1, r9, r0
    jz   r1, render_b_done
    jmp  render_b_body
render_b_done:
    jmp  draw_entities
render_b_body:

    movi r10, 0             ; col = 0
render_b_col:
    movi r0, 8
    cmplt r1, r10, r0
    jz   r1, render_next_row

    ld32 r5, r8, 0          ; brick color
    jz   r5, skip_render_brick

    ; r1 = x = 8 + col * 14
    mov  r1, r10
    shl  r1, 4
    sub  r1, r1, r10
    sub  r1, r1, r10
    movi r0, 8
    add  r1, r1, r0

    ; r2 = y = 18 + row * 7
    mov  r2, r9
    shl  r2, 3
    sub  r2, r2, r9
    movi r0, 18
    add  r2, r2, r0

    movi r3, 12             ; width = 12
    movi r4, 5              ; height = 5
    call fill_rect

skip_render_brick:
    movi r0, 4
    add  r8, r8, r0
    movi r0, 1
    add  r10, r10, r0
    jmp  render_b_col

render_next_row:
    movi r0, 1
    add  r9, r9, r0
    jmp  render_b_row

draw_entities:
    ; Draw Paddle (pad_x, 116, 24x4, color=10 Sky Blue)
    ld32 r1, r13, 0
    movi r2, 116
    movi r3, 24
    movi r4, 4
    movi r5, 10
    call fill_rect

    ; Paddle highlight (pad_x+2, 116, 20x1, color=12 Pure White)
    ld32 r1, r13, 0
    movi r0, 2
    add  r1, r1, r0
    movi r2, 116
    movi r3, 20
    movi r4, 1
    movi r5, 12
    call fill_rect

    ; Draw Ball (ball_x, ball_y, 4x4, color=12 Pure White)
    ld32 r1, r13, 1
    ld32 r2, r13, 2
    movi r3, 4
    movi r4, 4
    movi r5, 12
    call fill_rect

    ; If game_over != 0, draw status text in center using 8x8 DOS font
    ld32 r0, r13, 8
    jz   r0, do_sync_frame
    jmp  prep_banner
do_sync_frame:
    jmp  sync_frame
prep_banner:

    ; Check if lost (1) or won (2)
    movi r1, 1
    cmplt r2, r1, r0        ; if 1 < game_over (game_over == 2 -> WON)
    jz   r2, draw_lost_text
    jmp  draw_won_text

draw_lost_text:
    ; Clear center strip for text readability
    movi r1, 24
    movi r2, 56
    movi r3, 80
    movi r4, 16
    movi r5, 0
    call fill_rect

    ; Draw "GAME OVER" (9 chars * 8px = 72px, centered at x=28, y=60, color=2 Red)
    movi r1, 28
    movi r2, 60
    movi r3, 71             ; 'G'
    movi r4, 2
    call draw_char_8x8

    movi r1, 36
    movi r2, 60
    movi r3, 65             ; 'A'
    movi r4, 2
    call draw_char_8x8

    movi r1, 44
    movi r2, 60
    movi r3, 77             ; 'M'
    movi r4, 2
    call draw_char_8x8

    movi r1, 52
    movi r2, 60
    movi r3, 69             ; 'E'
    movi r4, 2
    call draw_char_8x8

    movi r1, 68
    movi r2, 60
    movi r3, 79             ; 'O'
    movi r4, 2
    call draw_char_8x8

    movi r1, 76
    movi r2, 60
    movi r3, 86             ; 'V'
    movi r4, 2
    call draw_char_8x8

    movi r1, 84
    movi r2, 60
    movi r3, 69             ; 'E'
    movi r4, 2
    call draw_char_8x8

    movi r1, 92
    movi r2, 60
    movi r3, 82             ; 'R'
    movi r4, 2
    call draw_char_8x8
    jmp  sync_frame

draw_won_text:
    ; Clear center strip for text readability
    movi r1, 28
    movi r2, 56
    movi r3, 72
    movi r4, 16
    movi r5, 0
    call fill_rect

    ; Draw "YOU WIN!" (8 chars * 8px = 64px, centered at x=32, y=60, color=6 Emerald)
    movi r1, 32
    movi r2, 60
    movi r3, 89             ; 'Y'
    movi r4, 6
    call draw_char_8x8

    movi r1, 40
    movi r2, 60
    movi r3, 79             ; 'O'
    movi r4, 6
    call draw_char_8x8

    movi r1, 48
    movi r2, 60
    movi r3, 85             ; 'U'
    movi r4, 6
    call draw_char_8x8

    movi r1, 64
    movi r2, 60
    movi r3, 87             ; 'W'
    movi r4, 6
    call draw_char_8x8

    movi r1, 72
    movi r2, 60
    movi r3, 73             ; 'I'
    movi r4, 6
    call draw_char_8x8

    movi r1, 80
    movi r2, 60
    movi r3, 78             ; 'N'
    movi r4, 6
    call draw_char_8x8

    movi r1, 88
    movi r2, 60
    movi r3, 33             ; '!'
    movi r4, 6
    call draw_char_8x8

sync_frame:
    ; Write 1 to VM_SYS_ADDR (0x4004) to swap back->front & present frame
    movi r8, 0x40
    shl  r8, 8              ; 0x4000
    movi r0, 1
    st32 r0, r8, 1          ; mem32[0x4004] = 1
    jmp  main_loop

; ============================================================================
; SUBROUTINE: draw_char_8x8
; Renders an 8x8 DOS BIOS glyph from 0x4400 + ascii*8 onto the framebuffer.
; Inputs: r1 = x, r2 = y, r3 = ascii_code (0..127), r4 = color (0..15)
; Clobbers: r0, r1, r2, r3, r4, r5, r6, r7
; Preserves: r8, r9, r10, r11, r12, r13, r14, r15
; ============================================================================
draw_char_8x8:
    movi r0, 24
    sub  r15, r15, r0
    st32 r8,  r15, 0
    st32 r9,  r15, 1
    st32 r10, r15, 2
    st32 r11, r15, 3
    st32 r12, r15, 4
    st32 r14, r15, 5

    mov  r8, r1             ; base_x
    mov  r9, r2             ; base_y
    mov  r12, r4            ; color

    ; Compute glyph address = 0x4400 + (ascii_code & 0x7F) * 8
    movi r0, 0x7F
    and  r3, r3, r0
    shl  r3, 3
    movi r0, 0x44
    shl  r0, 8              ; 0x4400
    add  r10, r0, r3        ; r10 = glyph base byte address

    movi r11, 0             ; r11 = row (0..7)
dc_row_loop:
    movi r0, 8
    cmplt r0, r11, r0
    jz   r0, dc_done

    ; Read byte at (r10 + r11) via aligned 32-bit load
    add  r6, r10, r11       ; byte_addr
    movi r0, -4
    and  r7, r6, r0         ; word_addr = byte_addr & ~3
    ld32 r5, r7, 0          ; 32-bit word containing glyph row byte

    movi r0, 3
    and  r6, r6, r0         ; byte_offset (0..3)
    shl  r6, 3              ; bit_shift = byte_offset * 8
dc_shift_byte:
    jz   r6, dc_byte_ready
    shr  r5, 1
    movi r0, 1
    sub  r6, r6, r0
    jmp  dc_shift_byte
dc_byte_ready:
    movi r0, 0xFF
    and  r5, r5, r0         ; r5 = 8-bit row bitmap (bit 7 = left, bit 0 = right)

    movi r6, 0              ; r6 = col (0..7)
dc_col_loop:
    movi r0, 8
    cmplt r0, r6, r0
    jz   r0, dc_next_row

    ; Check bit (7 - col) in r5
    movi r0, 7
    sub  r7, r0, r6         ; shift amount = 7 - col
    mov  r4, r5
dc_bit_shift:
    jz   r7, dc_bit_ready
    shr  r4, 1
    movi r0, 1
    sub  r7, r7, r0
    jmp  dc_bit_shift
dc_bit_ready:
    movi r0, 1
    and  r4, r4, r0
    jz   r4, dc_next_col    ; transparent background if bit == 0

    ; Plot pixel at (base_x + col, base_y + row) with color r12
    add  r1, r8, r6         ; px = base_x + col
    add  r2, r9, r11        ; py = base_y + row

    ; Bounds check 0 <= px < 128 and 0 <= py < 128
    movi r0, 0
    cmplt r0, r1, r0
    jz   r0, dc_px_ok1
    jmp  dc_next_col
dc_px_ok1:
    movi r0, 127
    cmplt r0, r0, r1
    jz   r0, dc_px_ok2
    jmp  dc_next_col
dc_px_ok2:
    movi r0, 0
    cmplt r0, r2, r0
    jz   r0, dc_py_ok1
    jmp  dc_next_col
dc_py_ok1:
    movi r0, 127
    cmplt r0, r0, r2
    jz   r0, dc_py_ok2
    jmp  dc_next_col
dc_py_ok2:
    ; Compute byte_addr = (py * 64) + (px >> 1)
    mov  r3, r2
    shl  r3, 6
    mov  r4, r1
    shr  r4, 1
    add  r3, r3, r4

    movi r0, -4
    and  r7, r3, r0         ; aligned word_addr

    ; nibble bit shift = (byte_addr & 3) * 8 + (px & 1) * 4
    movi r0, 3
    and  r3, r3, r0
    shl  r3, 3
    movi r0, 1
    and  r0, r1, r0
    shl  r0, 2
    add  r3, r3, r0         ; r3 = nibble bit shift (0..28)

    movi r0, 0x0F
    mov  r4, r3
dc_mask_loop:
    jz   r4, dc_mask_done
    shl  r0, 1
    movi r1, 1
    sub  r4, r4, r1
    jmp  dc_mask_loop
dc_mask_done:
    movi r1, -1
    xor  r0, r0, r1         ; ~(0xF << shift)

    movi r1, 0x0F
    and  r1, r12, r1
    mov  r4, r3
dc_col_shift:
    jz   r4, dc_col_done
    shl  r1, 1
    movi r2, 1
    sub  r4, r4, r2
    jmp  dc_col_shift
dc_col_done:
    ld32 r2, r7, 0
    and  r2, r2, r0
    or   r2, r2, r1
    st32 r2, r7, 0

dc_next_col:
    movi r0, 1
    add  r6, r6, r0
    jmp  dc_col_loop

dc_next_row:
    movi r0, 1
    add  r11, r11, r0
    jmp  dc_row_loop

dc_done:
    ld32 r8,  r15, 0
    ld32 r9,  r15, 1
    ld32 r10, r15, 2
    ld32 r11, r15, 3
    ld32 r12, r15, 4
    ld32 r14, r15, 5
    movi r0, 24
    add  r15, r15, r0
    jr   r14

; ============================================================================
; SUBROUTINE: fill_rect
; Inputs: r1 = x, r2 = y, r3 = w, r4 = h, r5 = color (0..15)
; Clobbers: r0, r1, r2, r3, r4, r6, r7
; Preserves: r8, r9, r10, r11, r12, r13, r14, r15
; ============================================================================
fill_rect:
    ; Save registers on stack (sp = r15)
    movi r0, 20
    sub  r15, r15, r0
    st32 r8,  r15, 0
    st32 r9,  r15, 1
    st32 r10, r15, 2
    st32 r11, r15, 3
    st32 r14, r15, 4

    mov  r8, r1             ; start_x
    add  r9, r1, r3         ; end_x
    mov  r10, r2            ; cur_y
    add  r11, r2, r4        ; end_y

fr_y_loop:
    cmplt r0, r10, r11
    jz    r0, fr_done

    ; Bounds check y (0 <= cur_y < 128)
    movi  r0, 0
    cmplt r0, r10, r0
    jz    r0, fr_y_ok1
    jmp   fr_next_y
fr_y_ok1:
    movi  r0, 127
    cmplt r0, r0, r10
    jz    r0, fr_y_ok2
    jmp   fr_next_y

fr_y_ok2:
    mov  r1, r8             ; cur_x = start_x
fr_x_loop:
    cmplt r0, r1, r9
    jz    r0, fr_next_y

    ; Bounds check x (0 <= cur_x < 128)
    movi  r0, 0
    cmplt r0, r1, r0
    jz    r0, fr_x_ok1
    jmp   fr_next_x
fr_x_ok1:
    movi  r0, 127
    cmplt r0, r0, r1
    jz    r0, fr_x_ok2
    jmp   fr_next_x

fr_x_ok2:
    ; Compute byte address = (cur_y * 64) + (cur_x >> 1)
    mov  r6, r10
    shl  r6, 6              ; cur_y * 64
    mov  r7, r1
    shr  r7, 1              ; cur_x >> 1
    add  r6, r6, r7         ; byte_addr

    ; Aligned 32-bit word address = byte_addr & ~3
    movi r0, -4
    and  r7, r6, r0         ; word_addr

    ; Bit shift within 32-bit word = (byte_addr & 3) * 8 + (cur_x & 1) * 4
    movi r0, 3
    and  r6, r6, r0         ; byte_offset (0..3)
    shl  r6, 3              ; byte_offset * 8
    movi r0, 1
    and  r0, r1, r0         ; cur_x & 1
    shl  r0, 2              ; * 4
    add  r6, r6, r0         ; r6 = total bit shift (0, 4, 8, ..., 28)

    ; Build nibble mask = 0xF << r6
    movi r0, 0x0F
    mov  r2, r6
fr_shift_mask:
    jz   r2, fr_mask_ready
    shl  r0, 1
    movi r3, 1
    sub  r2, r2, r3
    jmp  fr_shift_mask

fr_mask_ready:
    ; Invert mask: ~mask = mask ^ -1
    movi r3, -1
    xor  r0, r0, r3         ; r0 = ~(0xF << shift)

    ; Shift color into position: (color & 0xF) << r6
    movi r3, 0x0F
    and  r3, r5, r3
    mov  r2, r6
fr_shift_col:
    jz   r2, fr_col_ready
    shl  r3, 1
    movi r4, 1
    sub  r2, r2, r4
    jmp  fr_shift_col

fr_col_ready:
    ; Read-Modify-Write 32-bit word at r7
    ld32 r2, r7, 0
    and  r2, r2, r0         ; clear target nibble
    or   r2, r2, r3         ; set new color nibble
    st32 r2, r7, 0

fr_next_x:
    movi r0, 1
    add  r1, r1, r0
    jmp  fr_x_loop

fr_next_y:
    movi r0, 1
    add  r10, r10, r0
    jmp  fr_y_loop

fr_done:
    ld32 r8,  r15, 0
    ld32 r9,  r15, 1
    ld32 r10, r15, 2
    ld32 r11, r15, 3
    ld32 r14, r15, 4
    movi r0, 20
    add  r15, r15, r0
    jr   r14
```
