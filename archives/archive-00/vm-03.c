#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Memory-Mapped VGA (128x128, 4bpp Sweetie-16 palette, 1 nibble per pixel)
 * ========================================================================= */
#define VGA_WIDTH           128u
#define VGA_HEIGHT          128u
#define VGA_SCALE           4u
#define VGA_BASE            0x0000u
#define VGA_FB_SIZE         ((VGA_WIDTH * VGA_HEIGHT) / 2u) /* 8192 bytes (0x2000) */
#define VM_MEM_SIZE         0x4000u                         /* 16 KiB total: 8K VRAM + 8K RAM/Stack */
#define VM_INSTRUCTIONS_PS  250000.0
#define VM_MAX_SLICE_STEPS  65536u
#define MAX_DELTA_TIME      0.1f

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

/* =========================================================================
 * GUI Layer (SDL2 Window, Framebuffer Upload & Event Handling)
 * ========================================================================= */
typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint32_t      pixels[VGA_WIDTH * VGA_HEIGHT];
    int           running;
    int           restart_requested;
} GuiContext;

/* Reads a 4-bit color index at (x, y). Low nibble = even x, High nibble = odd x. */
static inline uint8_t vga_get_pixel(const uint8_t *mem, uint32_t x, uint32_t y) {
    uint32_t idx = y * VGA_WIDTH + x;
    uint8_t byte = mem[VGA_BASE + (idx >> 1)];
    return (idx & 1u) ? (byte >> 4) : (byte & 0x0Fu);
}

static int gui_init(GuiContext *gui, const char *title, uint32_t scale) {
    memset(gui, 0, sizeof(*gui));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    int win_w = (int)(VGA_WIDTH * scale);
    int win_h = (int)(VGA_HEIGHT * scale);

    gui->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        win_w,
        win_h,
        SDL_WINDOW_SHOWN
    );
    if (!gui->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    gui->renderer = SDL_CreateRenderer(gui->window, -1, SDL_RENDERER_ACCELERATED);
    if (!gui->renderer) {
        gui->renderer = SDL_CreateRenderer(gui->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!gui->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(gui->window);
        SDL_Quit();
        return -1;
    }

    SDL_RenderSetLogicalSize(gui->renderer, (int)VGA_WIDTH, (int)VGA_HEIGHT);

    gui->texture = SDL_CreateTexture(
        gui->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        (int)VGA_WIDTH,
        (int)VGA_HEIGHT
    );
    if (!gui->texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(gui->renderer);
        SDL_DestroyWindow(gui->window);
        SDL_Quit();
        return -1;
    }

    gui->running = 1;
    return 0;
}

static void gui_poll_events(GuiContext *gui) {
    gui->restart_requested = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            gui->running = 0;
        } else if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                gui->running = 0;
            } else if (ev.key.keysym.sym == SDLK_r) {
                gui->restart_requested = 1;
            }
        }
    }
}

static void gui_render_vga(GuiContext *gui, const uint8_t *mem) {
    for (uint32_t y = 0; y < VGA_HEIGHT; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            uint8_t color_idx = vga_get_pixel(mem, x, y);
            gui->pixels[y * VGA_WIDTH + x] = SWEETIE16_PALETTE[color_idx & 0x0Fu];
        }
    }
    SDL_UpdateTexture(gui->texture, NULL, gui->pixels, (int)(VGA_WIDTH * sizeof(uint32_t)));
}

static void gui_present(GuiContext *gui) {
    SDL_RenderClear(gui->renderer);
    SDL_RenderCopy(gui->renderer, gui->texture, NULL, NULL);
    SDL_RenderPresent(gui->renderer);
}

static void gui_shutdown(GuiContext *gui) {
    if (gui->texture)  SDL_DestroyTexture(gui->texture);
    if (gui->renderer) SDL_DestroyRenderer(gui->renderer);
    if (gui->window)   SDL_DestroyWindow(gui->window);
    SDL_Quit();
    memset(gui, 0, sizeof(*gui));
}

/* =========================================================================
 * VM Opcodes, Encoding & Memory Helpers
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

typedef struct {
    const uint16_t *code;
    size_t          code_len;
    uint8_t        *mem;
    size_t          mem_size;
    int32_t         r[16];
    size_t          pc_idx;
    int32_t         init_pc;
    int32_t         init_lr;
    int32_t         init_sp;
    int             halted;
    int             vram_dirty;
    double          step_accum;
} VmState;

static inline uint16_t vm_enc_rrr(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) |
                      ((b & 0xF) << 4) | (c & 0xF));
}

static inline uint16_t vm_enc_ri8(uint8_t op, uint8_t a, uint8_t imm8) {
    return (uint16_t)(((op & 0xF) << 12) | ((a & 0xF) << 8) | imm8);
}

static inline uint16_t vm_enc_i12(uint8_t op, int16_t imm12) {
    return (uint16_t)(((op & 0xF) << 12) | ((uint16_t)imm12 & 0x0FFFu));
}

static inline int32_t vm_sext12(uint16_t w) {
    uint16_t v = w & 0x0FFFu;
    return (v & 0x0800u) ? (int32_t)(v | 0xFFFFF000u) : (int32_t)v;
}

static inline int32_t vm_load32_le(const uint8_t *mem, uint32_t addr) {
    int32_t val;
    memcpy(&val, mem + addr, sizeof(val));
    return val;
}

static inline void vm_store32_le(VmState *vm, uint32_t addr, int32_t val) {
    memcpy(vm->mem + addr, &val, sizeof(val));
    if (addr >= VGA_BASE && addr < VGA_BASE + VGA_FB_SIZE) {
        vm->vram_dirty = 1;
    }
}

/* =========================================================================
 * VM Core Lifecycle & Execution Functions
 * ========================================================================= */
static void vm_reset(VmState *vm) {
    memset(vm->r, 0, sizeof(vm->r));
    vm->r[14] = vm->init_lr;
    vm->r[15] = vm->init_sp;
    vm->pc_idx = (vm->init_pc >= 0) ? (size_t)vm->init_pc : vm->code_len;
    vm->halted = (vm->pc_idx >= vm->code_len) ? 1 : 0;
    vm->vram_dirty = 0;
    vm->step_accum = 0.0;
}

static void vm_init(VmState *vm, const uint16_t *code, size_t code_len,
                    uint8_t *mem, size_t mem_size,
                    int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    memset(vm, 0, sizeof(*vm));
    vm->code = code;
    vm->code_len = code_len;
    vm->mem = mem;
    vm->mem_size = mem_size;
    vm->init_pc = init_pc;
    vm->init_lr = init_lr;
    vm->init_sp = init_sp;
    vm_reset(vm);
}

static inline int32_t vm_read_reg(const VmState *vm, uint8_t reg) {
    return vm->r[reg & 0xFu];
}

static inline void vm_write_reg(VmState *vm, uint8_t reg, int32_t val) {
    vm->r[reg & 0xFu] = val;
}

/* Executes a single instruction at vm->pc_idx; returns 1 if still active, 0 if halted. */
static int vm_exec_one(VmState *vm) {
    if (vm->halted || !vm->code || !vm->mem || vm->pc_idx >= vm->code_len) {
        vm->halted = 1;
        return 0;
    }

    uint16_t ins = vm->code[vm->pc_idx];
    size_t next_idx = vm->pc_idx + 1u;

    uint8_t op = (uint8_t)(ins >> 12);
    uint8_t a  = (uint8_t)((ins >> 8) & 0xF);
    uint8_t b  = (uint8_t)((ins >> 4) & 0xF);
    uint8_t c  = (uint8_t)(ins & 0xF);
    uint8_t i8 = (uint8_t)(ins & 0xFF);

    switch (op) {
        case OP_JR:
            next_idx = (uint32_t)vm->r[a];
            break;
        case OP_MOVI:
            vm->r[a] = (int8_t)i8;
            break;
        case OP_MOV:
            vm->r[a] = vm->r[b];
            break;
        case OP_ADD:
            vm->r[a] = vm->r[b] + vm->r[c];
            break;
        case OP_SUB:
            vm->r[a] = vm->r[b] - vm->r[c];
            break;
        case OP_AND:
            vm->r[a] = vm->r[b] & vm->r[c];
            break;
        case OP_OR:
            vm->r[a] = vm->r[b] | vm->r[c];
            break;
        case OP_XOR:
            vm->r[a] = vm->r[b] ^ vm->r[c];
            break;
        case OP_SHL:
            vm->r[a] = (int32_t)((uint32_t)vm->r[a] << (i8 & 31));
            break;
        case OP_SHR:
            vm->r[a] = (int32_t)((uint32_t)vm->r[a] >> (i8 & 31));
            break;
        case OP_CMPLT:
            vm->r[a] = (vm->r[b] < vm->r[c]) ? 1 : 0;
            break;
        case OP_JMP:
            next_idx = (size_t)((int64_t)next_idx + vm_sext12(ins));
            break;
        case OP_JZ:
            if (vm->r[a] == 0) {
                next_idx = (size_t)((int64_t)next_idx + (int8_t)i8);
            }
            break;
        case OP_LD32: {
            uint32_t addr = (uint32_t)vm->r[b] + (uint32_t)c * 4u;
            if (addr + 4u <= vm->mem_size) {
                vm->r[a] = vm_load32_le(vm->mem, addr);
            }
            break;
        }
        case OP_ST32: {
            uint32_t addr = (uint32_t)vm->r[b] + (uint32_t)c * 4u;
            if (addr + 4u <= vm->mem_size) {
                vm_store32_le(vm, addr, vm->r[a]);
            }
            break;
        }
        case OP_CALL:
            vm->r[14] = (int32_t)next_idx;
            next_idx = (size_t)((int64_t)next_idx + vm_sext12(ins));
            break;
        default:
            vm->halted = 1;
            return 0;
    }

    vm->pc_idx = next_idx;
    if (vm->pc_idx >= vm->code_len) {
        vm->halted = 1;
        return 0;
    }
    return 1;
}

/* Executes up to `max_steps` instructions and returns 1 if still running, 0 if halted. */
static int vm_step_slice(VmState *vm, size_t max_steps) {
    if (vm->halted || max_steps == 0) {
        return 0;
    }
    for (size_t i = 0; i < max_steps; i++) {
        if (!vm_exec_one(vm)) {
            return 0;
        }
    }
    return 1;
}

/* Advances the VM using `delta_time` (in seconds) at `VM_INSTRUCTIONS_PS`. */
static void vm_update(VmState *vm, float delta_time) {
    if (vm->halted || delta_time <= 0.0f) {
        return;
    }

    vm->step_accum += (double)delta_time * VM_INSTRUCTIONS_PS;
    size_t steps = (size_t)vm->step_accum;
    if (steps == 0) {
        return;
    }

    if (steps > VM_MAX_SLICE_STEPS) {
        steps = VM_MAX_SLICE_STEPS;
    }

    vm->step_accum -= (double)steps;
    vm_step_slice(vm, steps);
}

static int32_t vm_run(const uint16_t *code, size_t code_len,
                      uint8_t *mem, size_t mem_size,
                      int32_t init_pc, int32_t init_lr, int32_t init_sp) {
    VmState vm;
    vm_init(&vm, code, code_len, mem, mem_size, init_pc, init_lr, init_sp);
    while (vm_step_slice(&vm, VM_MAX_SLICE_STEPS)) {
    }
    return vm_read_reg(&vm, 0);
}

/* =========================================================================
 * Demo Program & Entry Point
 * ========================================================================= */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint16_t prog[128];
    int idx = 0;

    int main_idx = idx;
    prog[idx++] = vm_enc_ri8(OP_MOVI, 0, 0);       /* r0 = 0 (VGA_BASE) */

    /* r1 = 8192 (0x2000 = VGA_FB_SIZE) */
    prog[idx++] = vm_enc_ri8(OP_MOVI, 1, 1);       /* r1 = 1 */
    prog[idx++] = vm_enc_ri8(OP_SHL,  1, 13);      /* r1 = 1 << 13 = 8192 */

    prog[idx++] = vm_enc_ri8(OP_MOVI, 2, 4);       /* r2 = 4 (bytes per 32-bit store) */
    prog[idx++] = vm_enc_ri8(OP_MOVI, 3, 0);       /* r3 = 0 (palette index 0) */
    prog[idx++] = vm_enc_ri8(OP_MOVI, 7, 1);       /* r7 = 1 */

    /* r8 = 128 (words per 8-row band: 8 rows * 16 words/row) */
    prog[idx++] = vm_enc_ri8(OP_MOVI, 8, 1);       /* r8 = 1 */
    prog[idx++] = vm_enc_ri8(OP_SHL,  8, 7);       /* r8 = 128 */

    /* Outer loop: one color band per iteration */
    int outer_loop = idx;
    prog[idx++] = vm_enc_rrr(OP_CMPLT, 9, 0, 1);   /* r9 = (r0 < 8192) */
    int jz_done_pos = idx++;

    /* Pack nibble r3 into all 8 nibbles of r4: r4 = r3 * 0x11111111 */
    prog[idx++] = vm_enc_rrr(OP_MOV, 4, 3, 0);
    prog[idx++] = vm_enc_rrr(OP_MOV, 5, 4, 0);
    prog[idx++] = vm_enc_ri8(OP_SHL, 5, 4);
    prog[idx++] = vm_enc_rrr(OP_OR,  4, 4, 5);
    prog[idx++] = vm_enc_rrr(OP_MOV, 5, 4, 0);
    prog[idx++] = vm_enc_ri8(OP_SHL, 5, 8);
    prog[idx++] = vm_enc_rrr(OP_OR,  4, 4, 5);
    prog[idx++] = vm_enc_rrr(OP_MOV, 5, 4, 0);
    prog[idx++] = vm_enc_ri8(OP_SHL, 5, 16);
    prog[idx++] = vm_enc_rrr(OP_OR,  4, 4, 5);

    /* Inner loop: write 128 words (8 rows of 128px) */
    prog[idx++] = vm_enc_rrr(OP_MOV, 6, 8, 0);
    int inner_loop = idx;
    int jz_next_band_pos = idx++;

    prog[idx++] = vm_enc_ri8(OP_ST32, 4, 0x00);
    prog[idx++] = vm_enc_rrr(OP_ADD,  0, 0, 2);
    prog[idx++] = vm_enc_rrr(OP_SUB,  6, 6, 7);
    int jmp_inner_pos = idx++;
    prog[jmp_inner_pos] = vm_enc_i12(OP_JMP, (int16_t)(inner_loop - (jmp_inner_pos + 1)));

    int after_inner = idx;
    prog[jz_next_band_pos] = vm_enc_ri8(OP_JZ, 6, (uint8_t)(after_inner - (jz_next_band_pos + 1)));

    prog[idx++] = vm_enc_rrr(OP_ADD, 3, 3, 7);
    int jmp_outer_pos = idx++;
    prog[jmp_outer_pos] = vm_enc_i12(OP_JMP, (int16_t)(outer_loop - (jmp_outer_pos + 1)));

    int done_idx = idx;
    prog[jz_done_pos] = vm_enc_ri8(OP_JZ, 9, (uint8_t)(done_idx - (jz_done_pos + 1)));
    prog[idx++] = vm_enc_rrr(OP_JR, 14, 0, 0);

    uint8_t mem[VM_MEM_SIZE] = {0};
    int32_t init_sp = (int32_t)sizeof(mem);
    int32_t exit_pc = idx;

    GuiContext gui;
    if (gui_init(&gui, "Sweetie-16 VM (4x)", VGA_SCALE) != 0) {
        return 1;
    }

    VmState vm;
    vm_init(&vm, prog, (size_t)idx, mem, sizeof(mem), main_idx, exit_pc, init_sp);

    gui_render_vga(&gui, mem);
    gui_present(&gui);

    uint64_t last_counter = SDL_GetPerformanceCounter();
    double   perf_freq    = (double)SDL_GetPerformanceFrequency();

    while (gui.running) {
        uint64_t now_counter = SDL_GetPerformanceCounter();
        float delta_time = (float)((double)(now_counter - last_counter) / perf_freq);
        last_counter = now_counter;

        if (delta_time > MAX_DELTA_TIME) {
            delta_time = MAX_DELTA_TIME;
        }

        gui_poll_events(&gui);
        if (gui.restart_requested) {
            memset(mem, 0, VGA_FB_SIZE);
            vm_reset(&vm);
            gui_render_vga(&gui, mem);
            gui_present(&gui);
        }

        vm_update(&vm, delta_time);

        if (vm.vram_dirty) {
            gui_render_vga(&gui, mem);
            gui_present(&gui);
            vm.vram_dirty = 0;
        }

        SDL_Delay(16);
    }

    gui_shutdown(&gui);
    return 0;
}
