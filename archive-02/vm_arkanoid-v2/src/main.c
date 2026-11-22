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

    vm_env_t env;
    vm_env_init(&env, mem, sizeof(mem), 0, (int32_t)code_len, (int32_t)VM_MEM_SIZE);

    uint32_t argb_pixels[VM_SCREEN_W * VM_SCREEN_H];
    bool running = true;

    while (running && !env.halted) {
        uint32_t frame_start = SDL_GetTicks();

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

        /* Clear frame sync flag and execute VM until it signals frame_ready */
        uint32_t sys_val = 0;
        memcpy(mem + VM_SYS_ADDR, &sys_val, sizeof(sys_val));
        env.frame_ready = false;

        vm_run_env(&env, code, code_len, 250000);

        /* Convert 4bpp framebuffer using Sweetie-16 palette & present */
        render_framebuffer(mem + VM_FB_ADDR, argb_pixels);
        SDL_UpdateTexture(texture, NULL, argb_pixels, VM_SCREEN_W * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* Cap at ~60 FPS if vsync is unavailable */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 16) {
            SDL_Delay(16 - elapsed);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
