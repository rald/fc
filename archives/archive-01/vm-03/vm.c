#include "vm.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t VM_ENV_MAGIC[8] = {'V', 'M', 'E', 'N', 'V', '0', '0', '1'};

static inline int32_t load32_le(const uint8_t *mem, uint32_t addr) {
    int32_t val;
    memcpy(&val, mem + addr, sizeof(val));
    return val;
}

static inline void store32_le(uint8_t *mem, uint32_t addr, int32_t val) {
    memcpy(mem + addr, &val, sizeof(val));
}

static void write_u32_le(uint8_t out[4], uint32_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t read_u32_le(const uint8_t in[4]) {
    return ((uint32_t)in[0]) |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static void write_u64_le(uint8_t out[8], uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
    }
}

static uint64_t read_u64_le(const uint8_t in[8]) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)in[i]) << (i * 8);
    }
    return v;
}

void vm_env_init(vm_env_t *env, uint8_t *mem, size_t mem_size,
                 int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    memset(env, 0, sizeof(*env));
    env->r[14] = init_lr;
    env->r[15] = init_sp;
    env->pc = init_pc;
    env->mem = mem;
    env->mem_size = mem_size;
    env->halted = false;
}

int32_t vm_run_env(vm_env_t *env, const uint16_t *code, size_t code_len,
                   size_t max_steps) {
    if (!env || !code || (!env->mem && env->mem_size > 0)) {
        return 0;
    }

    int32_t *r = env->r;
    const uint8_t *mem = env->mem;
    size_t mem_size = env->mem_size;
    const uint16_t *pc = code + (uint32_t)env->pc;
    const uint16_t *code_end = code + code_len;
    size_t steps = 0;

    static const void *dispatch[] = {
        &&L_JR,    &&L_MOVI,  &&L_MOV,   &&L_ADD,
        &&L_SUB,   &&L_AND,   &&L_OR,    &&L_XOR,
        &&L_SHL,   &&L_SHR,   &&L_CMPLT, &&L_JMP,
        &&L_JZ,    &&L_LD32,  &&L_ST32,  &&L_CALL
    };

    uint8_t a = 0, b = 0, c = 0, i8 = 0;

#define SYNC_PC() do { env->pc = (int32_t)(pc - code); } while (0)

#define NEXT() do {                                                         if (max_steps > 0 && steps >= max_steps) {                              SYNC_PC();                                                          return r[0];                                                    }                                                                   if (pc < code || pc >= code_end) {                                      SYNC_PC();                                                          env->halted = true;                                                 return r[0];                                                    }                                                                   steps++;                                                            uint16_t ins = *pc++;                                               uint8_t op = (uint8_t)(ins >> 12);                                  a  = (uint8_t)((ins >> 8) & 0xF);                                   b  = (uint8_t)((ins >> 4) & 0xF);                                   c  = (uint8_t)(ins & 0xF);                                          i8 = (uint8_t)(ins & 0xFF);                                         goto *dispatch[op];                                             } while (0)

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
        if (addr + 4u <= mem_size) r[a] = load32_le(env->mem, addr);
        NEXT();
    }
L_ST32: {
        uint32_t addr = (uint32_t)r[b] + (uint32_t)c * 4u;
        if (addr + 4u <= mem_size) store32_le(env->mem, addr, r[a]);
        NEXT();
    }
L_CALL: {
        int32_t off = sext12(code[(size_t)(pc - code) - 1]);
        r[14] = (int32_t)(pc - code);
        pc += off;
        NEXT();
    }
#undef NEXT
#undef SYNC_PC
    (void)mem;
    (void)mem_size;
}

int32_t vm_run(const uint16_t *code, size_t code_len,
               uint8_t *mem, size_t mem_size,
               int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    vm_env_t env;
    vm_env_init(&env, mem, mem_size, init_pc, init_lr, init_sp);
    return vm_run_env(&env, code, code_len, 0);
}

int vm_save_env_fp(const vm_env_t *env, FILE *fp) {
    if (!env || !fp || (!env->mem && env->mem_size > 0)) {
        return -1;
    }

    if (fwrite(VM_ENV_MAGIC, 1, sizeof(VM_ENV_MAGIC), fp) != sizeof(VM_ENV_MAGIC)) {
        return -1;
    }

    uint8_t hdr[13];
    write_u32_le(hdr, (uint32_t)env->pc);
    write_u64_le(hdr + 4, (uint64_t)env->mem_size);
    hdr[12] = env->halted ? 1u : 0u;
    if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return -1;
    }

    uint8_t reg_buf[16 * 4];
    for (int i = 0; i < 16; i++) {
        write_u32_le(reg_buf + (i * 4), (uint32_t)env->r[i]);
    }
    if (fwrite(reg_buf, 1, sizeof(reg_buf), fp) != sizeof(reg_buf)) {
        return -1;
    }

    if (env->mem_size > 0 && fwrite(env->mem, 1, env->mem_size, fp) != env->mem_size) {
        return -1;
    }

    return 0;
}

int vm_load_env_fp(vm_env_t *env, FILE *fp) {
    if (!env || !fp) {
        return -1;
    }

    uint8_t magic[sizeof(VM_ENV_MAGIC)];
    if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic) ||
        memcmp(magic, VM_ENV_MAGIC, sizeof(magic)) != 0) {
        return -1;
    }

    uint8_t hdr[13];
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return -1;
    }

    int32_t pc = (int32_t)read_u32_le(hdr);
    size_t saved_mem_size = (size_t)read_u64_le(hdr + 4);
    bool halted = hdr[12] != 0u;

    /* Require matching buffer or allocate if env->mem is NULL */
    if (env->mem != NULL) {
        if (env->mem_size != saved_mem_size) {
            return -1;
        }
    } else if (saved_mem_size > 0) {
        env->mem = (uint8_t *)malloc(saved_mem_size);
        if (!env->mem) {
            return -1;
        }
        env->mem_size = saved_mem_size;
    }

    uint8_t reg_buf[16 * 4];
    if (fread(reg_buf, 1, sizeof(reg_buf), fp) != sizeof(reg_buf)) {
        return -1;
    }

    for (int i = 0; i < 16; i++) {
        env->r[i] = (int32_t)read_u32_le(reg_buf + (i * 4));
    }

    if (saved_mem_size > 0 &&
        fread(env->mem, 1, saved_mem_size, fp) != saved_mem_size) {
        return -1;
    }

    env->pc = pc;
    env->halted = halted;
    return 0;
}

int vm_save_env(const vm_env_t *env, const char *path) {
    if (!env || !path) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    int rc = vm_save_env_fp(env, fp);
    if (fclose(fp) != 0) return -1;
    return rc;
}

int vm_load_env(vm_env_t *env, const char *path) {
    if (!env || !path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    int rc = vm_load_env_fp(env, fp);
    if (fclose(fp) != 0) return -1;
    return rc;
}
