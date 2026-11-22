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

static int parse_reg(const char *tok) {
    if (!tok) return -1;
    if ((tok[0] == 'r' || tok[0] == 'R') && isdigit((unsigned char)tok[1])) {
        char *end = NULL;
        long v = strtol(tok + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v <= 15) return (int)v;
    }
    if (strcasecmp(tok, "lr") == 0) return 14;
    if (strcasecmp(tok, "sp") == 0) return 15;
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

        if (strcasecmp(op, "jr") == 0 && ntok == 2) {
            int a = parse_reg(toks[1]);
            if (a >= 0) { ins = enc_rrr(OP_JR, (uint8_t)a, 0, 0); ok = 1; }
        } else if (strcasecmp(op, "movi") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_MOVI, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (strcasecmp(op, "mov") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            if (a >= 0 && b >= 0) { ins = enc_rrr(OP_MOV, (uint8_t)a, (uint8_t)b, 0); ok = 1; }
        } else if (strcasecmp(op, "add") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_ADD, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "sub") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_SUB, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "and") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_AND, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "or") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_OR, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "xor") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_XOR, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "shl") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_SHL, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (strcasecmp(op, "shr") == 0 && ntok == 3) {
            int a = parse_reg(toks[1]);
            int32_t imm = 0;
            if (a >= 0 && parse_imm(toks[2], labels, label_count, &imm) == 0) {
                ins = enc_ri8(OP_SHR, (uint8_t)a, (uint8_t)(imm & 0xFF));
                ok = 1;
            }
        } else if (strcasecmp(op, "cmplt") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]), c = parse_reg(toks[3]);
            if (a >= 0 && b >= 0 && c >= 0) { ins = enc_rrr(OP_CMPLT, (uint8_t)a, (uint8_t)b, (uint8_t)c); ok = 1; }
        } else if (strcasecmp(op, "jmp") == 0 && ntok == 2) {
            int32_t target = 0;
            if (parse_imm(toks[1], labels, label_count, &target) == 0) {
                int32_t off = target - (pc + 1);
                if (off >= -2048 && off <= 2047) {
                    ins = enc_i12(OP_JMP, (int16_t)off);
                    ok = 1;
                }
            }
        } else if (strcasecmp(op, "jz") == 0 && ntok == 3) {
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
        } else if (strcasecmp(op, "ld32") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            int32_t c = 0;
            if (a >= 0 && b >= 0 && parse_imm(toks[3], labels, label_count, &c) == 0 && c >= 0 && c <= 15) {
                ins = enc_rrr(OP_LD32, (uint8_t)a, (uint8_t)b, (uint8_t)c);
                ok = 1;
            }
        } else if (strcasecmp(op, "st32") == 0 && ntok == 4) {
            int a = parse_reg(toks[1]), b = parse_reg(toks[2]);
            int32_t c = 0;
            if (a >= 0 && b >= 0 && parse_imm(toks[3], labels, label_count, &c) == 0 && c >= 0 && c <= 15) {
                ins = enc_rrr(OP_ST32, (uint8_t)a, (uint8_t)b, (uint8_t)c);
                ok = 1;
            }
        } else if (strcasecmp(op, "call") == 0 && ntok == 2) {
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
