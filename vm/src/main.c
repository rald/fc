#include "vm.h"
#include "cartridge.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef enum {
    SCREEN_MENU, SCREEN_CODE, SCREEN_SPRITE, SCREEN_TILE, SCREEN_SFX,
    SCREEN_MUSIC, SCREEN_GAME
} screen_t;

typedef enum { PROMPT_NONE, PROMPT_SAVE, PROMPT_LOAD } prompt_t;

static const char *MENU_ITEMS[] = {
    "RUN CARTRIDGE", "CODE EDITOR", "SPRITE EDITOR", "TILE EDITOR",
    "SFX MAKER", "MUSIC MAKER", "SAVE CARTRIDGE", "LOAD CARTRIDGE"
};

static cartridge_t cartridge;
static uint8_t memory[VM_MEM_SIZE];
static uint16_t vm_code[4096];
static size_t vm_code_len;
static vm_env_t vm;
static screen_t screen = SCREEN_MENU;
static prompt_t prompt = PROMPT_NONE;
static unsigned menu_choice, selected_sprite, selected_tile, selected_sfx;
static unsigned sprite_x, sprite_y, sprite_color, tile_x, tile_y;
static unsigned music_step, music_channel, sfx_field;
static size_t code_line;
static size_t code_cursor;
static bool code_editing;
static char code_line_buffer[256];
static char file_path[256];
static char status_message[96] = "ARKANOID READY";
static SDL_AudioDeviceID audio_device;

static void set_status(const char *message) {
    snprintf(status_message, sizeof(status_message), "%s", message);
}

static void put_pixel(uint8_t *fb, int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= VM_SCREEN_W || y >= VM_SCREEN_H) return;
    size_t index = (size_t)y * (VM_SCREEN_W / 2) + (unsigned)x / 2u;
    unsigned shift = ((unsigned)x & 1u) * 4u;
    fb[index] = (uint8_t)((fb[index] & ~(0x0Fu << shift)) |
                          ((color & 0x0Fu) << shift));
}

static void fill_rect(uint8_t *fb, int x, int y, int width, int height,
                      uint8_t color) {
    for (int py = y; py < y + height; py++)
        for (int px = x; px < x + width; px++)
            put_pixel(fb, px, py, color);
}

static void outline_rect(uint8_t *fb, int x, int y, int width, int height,
                         uint8_t color) {
    for (int px = x; px < x + width; px++) {
        put_pixel(fb, px, y, color);
        put_pixel(fb, px, y + height - 1, color);
    }
    for (int py = y; py < y + height; py++) {
        put_pixel(fb, x, py, color);
        put_pixel(fb, x + width - 1, py, color);
    }
}

static void draw_text(uint8_t *fb, int x, int y, const char *text, uint8_t color) {
    for (size_t i = 0; text[i] && x + (int)i * 8 < VM_SCREEN_W; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch >= 128) ch = '?';
        for (int row = 0; row < 8; row++) {
            uint8_t bits = DOS_FONT_8X8[ch][row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80u >> col))
                    put_pixel(fb, x + (int)i * 8 + col, y + row, color);
        }
    }
}

static void draw_centered(uint8_t *fb, int y, const char *text, uint8_t color) {
    int x = (VM_SCREEN_W - (int)strlen(text) * 8) / 2;
    draw_text(fb, x < 0 ? 0 : x, y, text, color);
}

static bool read_source_file(const char *path, char *out, size_t capacity) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    size_t length = fread(out, 1, capacity - 1, fp);
    bool ok = !ferror(fp) && feof(fp);
    fclose(fp);
    if (!ok) return false;
    out[length] = '\0';
    return true;
}

static bool line_bounds(const char *source, size_t wanted, size_t *start,
                        size_t *length) {
    size_t line = 0, begin = 0, total = strlen(source);
    for (size_t pos = 0; pos <= total; pos++) {
        if (pos == total || source[pos] == '\n') {
            if (line == wanted) {
                *start = begin;
                *length = pos - begin;
                return true;
            }
            line++;
            begin = pos + 1;
        }
    }
    return false;
}

static size_t source_line_count(const char *source) {
    size_t count = 1;
    for (const char *p = source; *p; p++)
        if (*p == '\n') count++;
    return count;
}

static bool replace_source_line(size_t wanted, const char *replacement) {
    size_t start, length;
    if (!line_bounds(cartridge.code, wanted, &start, &length)) return false;
    size_t source_size = strlen(cartridge.code);
    size_t replacement_size = strlen(replacement);
    size_t new_size = source_size - length + replacement_size;
    if (new_size >= sizeof(cartridge.code)) return false;
    size_t suffix = start + length;
    memmove(cartridge.code + start + replacement_size,
            cartridge.code + suffix, source_size - suffix + 1);
    memcpy(cartridge.code + start, replacement, replacement_size);
    return true;
}

static bool start_program(void) {
    if (vm_assemble_source(cartridge.code, vm_code, 4096, &vm_code_len) != 0) {
        set_status("ASSEMBLY ERROR - FIX CODE");
        return false;
    }
    memset(memory, 0, sizeof(memory));
    memcpy(memory + VM_FONT_ADDR, DOS_FONT_8X8, sizeof(DOS_FONT_8X8));
    vm_env_init(&vm, memory, sizeof(memory), 0, (int32_t)vm_code_len,
                (int32_t)VM_MEM_SIZE);
    screen = SCREEN_GAME;
    return true;
}

static void prompt_file(prompt_t next) {
    prompt = next;
    snprintf(file_path, sizeof(file_path), "cartridge.cart");
    SDL_StartTextInput();
}

static void save_cartridge(void) {
    FILE *fp = fopen(file_path, "wb");
    if (!fp) {
        set_status("SAVE FAILED - CHECK PATH");
        return;
    }
    int result = cart_save(fp, &cartridge);
    if (fclose(fp) != 0) result = -1;
    set_status(result == 0 ? "CARTRIDGE SAVED" : "SAVE FAILED");
}

static void load_cartridge(void) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        set_status("LOAD FAILED - CHECK PATH");
        return;
    }
    cartridge_t loaded;
    int result = cart_load(fp, &loaded);
    fclose(fp);
    if (result == 0) {
        cartridge = loaded;
        code_line = 0;
        set_status("CARTRIDGE LOADED");
    } else {
        set_status("INVALID CARTRIDGE");
    }
}

static double note_frequency(uint8_t note) {
    return 65.40639132514966 * pow(2.0, (double)note / 12.0);
}

static double wave_sample(unsigned waveform, double phase, unsigned noise) {
    if (waveform == 1) return phase < 0.5 ? 1.0 - 4.0 * phase : 4.0 * phase - 3.0;
    if (waveform == 2) return 2.0 * phase - 1.0;
    if (waveform == 3) return (noise & 1u) ? 1.0 : -1.0;
    return phase < 0.5 ? 1.0 : -1.0;
}

static void queue_sfx(const cart_sfx_t *sfx) {
    if (!sfx) return;
    if (!audio_device) {
        set_status("AUDIO DEVICE UNAVAILABLE");
        return;
    }
    const unsigned rate = 22050;
    size_t count = (size_t)sfx->duration * 367u;
    int16_t *samples = malloc(count * sizeof(*samples));
    if (!samples) return;
    double frequency = note_frequency(sfx->note);
    double volume = (double)sfx->volume / 15.0;
    for (size_t i = 0; i < count; i++) {
        double phase = fmod((double)i * frequency / rate, 1.0);
        double value = wave_sample(sfx->waveform, phase, (unsigned)i * 1103515245u);
        samples[i] = (int16_t)(value * volume * 12000.0);
    }
    SDL_ClearQueuedAudio(audio_device);
    if (SDL_QueueAudio(audio_device, samples,
                       (uint32_t)(count * sizeof(*samples))) != 0)
        set_status("AUDIO QUEUE FAILED");
    free(samples);
}

static void queue_music(void) {
    if (!audio_device) {
        set_status("AUDIO DEVICE UNAVAILABLE");
        return;
    }
    const unsigned rate = 22050, samples_per_step = 2205;
    size_t count = CART_MUSIC_STEPS * samples_per_step;
    int16_t *samples = calloc(count, sizeof(*samples));
    if (!samples) {
        set_status("AUDIO BUFFER ALLOCATION FAILED");
        return;
    }
    for (unsigned step = 0; step < CART_MUSIC_STEPS; step++) {
        for (unsigned i = 0; i < samples_per_step; i++) {
            double mixed = 0.0;
            for (unsigned channel = 0; channel < CART_MUSIC_CHANNELS; channel++) {
                uint8_t note = cartridge.music[step * CART_MUSIC_CHANNELS + channel];
                if (!note) continue;
                double frequency = note_frequency(note);
                double phase = fmod((double)i * frequency / rate, 1.0);
                mixed += wave_sample(channel, phase, i + channel * 17u) * 0.15;
            }
            samples[step * samples_per_step + i] =
                (int16_t)(mixed * 16000.0);
        }
    }
    SDL_ClearQueuedAudio(audio_device);
    int result = SDL_QueueAudio(audio_device, samples,
                                (uint32_t)(count * sizeof(*samples)));
    free(samples);
    set_status(result == 0 ? "PLAYING MUSIC PATTERN" : "AUDIO QUEUE FAILED");
}

static void draw_footer(uint8_t *fb, const char *help) {
    fill_rect(fb, 0, 112, VM_SCREEN_W, 16, 0);
    draw_text(fb, 0, 112, status_message, 4);
    draw_text(fb, 0, 120, help, 13);
}

static void draw_menu(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    draw_centered(fb, 3, "VM FANTASY CONSOLE", 11);
    draw_centered(fb, 12, "128X128 4BPP / SWEETIE-16", 13);
    for (unsigned i = 0; i < sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]); i++) {
        int y = 27 + (int)i * 9;
        if (i == menu_choice) {
            fill_rect(fb, 8, y - 1, 112, 9, 8);
            draw_text(fb, 12, y, ">", 4);
        }
        draw_text(fb, 24, y, MENU_ITEMS[i], i == menu_choice ? 12 : 13);
    }
    draw_footer(fb, "UP/DN SELECT ENTER OPEN");
}

static void draw_code_editor(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    char heading[32];
    snprintf(heading, sizeof(heading), "CODE %zu/%zu", code_line + 1,
             source_line_count(cartridge.code));
    draw_text(fb, 0, 0, heading, 11);
    size_t count = source_line_count(cartridge.code);
    size_t first = code_line > 10 ? code_line - 10 : 0;
    for (size_t row = 0; row < 12 && first + row < count; row++) {
        size_t start, length;
        if (!line_bounds(cartridge.code, first + row, &start, &length)) break;
        char line[300];
        if (code_editing && first + row == code_line) {
            size_t visible_start = code_cursor > 11 ? code_cursor - 11 : 0;
            snprintf(line, sizeof(line), "%03zu>%.*s", first + row + 1, 58,
                     code_line_buffer + visible_start);
        } else {
            snprintf(line, sizeof(line), "%03zu %.*s", first + row + 1,
                     (int)(length > 58 ? 58 : length), cartridge.code + start);
        }
        if (first + row == code_line) fill_rect(fb, 0, 8 + (int)row * 8, 128, 8, 8);
        draw_text(fb, 0, 8 + (int)row * 8, line, first + row == code_line ? 12 : 13);
        if (code_editing && first + row == code_line) {
            size_t visible_start = code_cursor > 11 ? code_cursor - 11 : 0;
            int cursor_x = 32 + (int)(code_cursor - visible_start) * 8;
            if (cursor_x >= 128) cursor_x = 120;
            fill_rect(fb, cursor_x, 8 + (int)row * 8, 1, 8, 4);
        }
    }
    draw_footer(fb, code_editing ? "TYPE / ENTER OK / ESC NO" : "UP/DN LINE ENTER EDIT F5 RUN");
}

static void draw_sprite_editor(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    char heading[32];
    snprintf(heading, sizeof(heading), "SPRITE %03u COLOR %02u", selected_sprite,
             sprite_color);
    draw_text(fb, 0, 0, heading, 11);
    const int cell = 10, origin_x = 24, origin_y = 16;
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) {
            uint8_t color = cart_sprite_pixel(&cartridge, selected_sprite, x, y);
            fill_rect(fb, origin_x + (int)x * cell, origin_y + (int)y * cell,
                      cell - 1, cell - 1, color);
        }
    }
    outline_rect(fb, origin_x + (int)sprite_x * cell, origin_y + (int)sprite_y * cell,
                 cell - 1, cell - 1, 12);
    for (unsigned i = 0; i < 16; i++) {
        fill_rect(fb, i * 8, 99, 7, 7, (uint8_t)i);
        if (i == sprite_color) outline_rect(fb, i * 8, 99, 7, 7, 12);
    }
    draw_footer(fb, "ARROWS Z PAINT X ERASE [ ] COLOR");
}

static void draw_tile_editor(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    char heading[32];
    snprintf(heading, sizeof(heading), "TILE %03u  MAP 16X16", selected_tile);
    draw_text(fb, 0, 0, heading, 11);
    const int cell = 6, origin_x = 16, origin_y = 12;
    for (unsigned y = 0; y < 16; y++) {
        for (unsigned x = 0; x < 16; x++) {
            uint8_t tile = cartridge.tiles[y * 16u + x];
            int px = origin_x + (int)x * cell, py = origin_y + (int)y * cell;
            fill_rect(fb, px, py, cell - 1, cell - 1, 0);
            for (unsigned sy = 0; sy < 2; sy++)
                for (unsigned sx = 0; sx < 2; sx++)
                    put_pixel(fb, px + 1 + (int)sx, py + 1 + (int)sy,
                              cart_sprite_pixel(&cartridge, tile, sx * 4u, sy * 4u));
        }
    }
    outline_rect(fb, origin_x + (int)tile_x * cell, origin_y + (int)tile_y * cell,
                 cell - 1, cell - 1, 12);
    draw_footer(fb, "ARROWS MOVE Z PLACE [ ] TILE ID");
}

static void draw_sfx_editor(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    cart_sfx_t *sfx = &cartridge.sfx[selected_sfx];
    char text[32];
    snprintf(text, sizeof(text), "SFX %02u FIELD %u/4", selected_sfx + 1, sfx_field + 1);
    draw_text(fb, 0, 0, text, 11);
    static const char *labels[] = {"NOTE", "WAVE", "LENGTH", "VOLUME"};
    unsigned values[] = {sfx->note, sfx->waveform, sfx->duration, sfx->volume};
    for (unsigned i = 0; i < 4; i++) {
        int y = 24 + (int)i * 16;
        draw_text(fb, 8, y, labels[i], i == sfx_field ? 4 : 13);
        snprintf(text, sizeof(text), "%03u", values[i]);
        draw_text(fb, 80, y, text, 12);
        fill_rect(fb, 8, y + 9, (int)(values[i] > 80 ? 80 : values[i]), 2,
                  (uint8_t)(i + 3));
    }
    draw_footer(fb, "LEFT/RIGHT SFX TAB FIELD UP/DN");
}

static void draw_music_editor(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    char heading[32];
    snprintf(heading, sizeof(heading), "MUSIC STEP %02u CH %u",
             music_step + 1, music_channel + 1);
    draw_text(fb, 0, 0, heading, 11);
    const int cell_w = 7, cell_h = 17, origin_x = 8, origin_y = 21;
    for (unsigned channel = 0; channel < CART_MUSIC_CHANNELS; channel++) {
        for (unsigned step = 0; step < CART_MUSIC_STEPS; step++) {
            uint8_t note = cartridge.music[step * CART_MUSIC_CHANNELS + channel];
            int x = origin_x + (int)step * cell_w;
            int y = origin_y + (int)channel * cell_h;
            fill_rect(fb, x, y, cell_w - 1, cell_h - 1, note ? 3 + channel * 2 : 1);
            if (note)
                fill_rect(fb, x + 1, y + cell_h - 3, 4,
                          (note % 12u) + 1u, (uint8_t)(4 + channel * 2));
            if (step == music_step && channel == music_channel)
                outline_rect(fb, x, y, cell_w - 1, cell_h - 1, 12);
        }
    }
    draw_footer(fb, "ARROWS Z/X NOTE SPACE PLAY");
}

static void draw_prompt(uint8_t *fb) {
    fill_rect(fb, 0, 0, VM_SCREEN_W, VM_SCREEN_H, 0);
    draw_centered(fb, 24, prompt == PROMPT_SAVE ? "SAVE CARTRIDGE" : "LOAD CARTRIDGE", 11);
    draw_centered(fb, 42, "ENTER PATH:", 13);
    size_t length = strlen(file_path);
    const char *shown = length > 15 ? file_path + length - 15 : file_path;
    draw_centered(fb, 58, shown, 4);
    draw_footer(fb, "TYPE PATH ENTER OK ESC CANCEL");
}

static void handle_menu_choice(void) {
    switch (menu_choice) {
        case 0:
            if (!start_program()) screen = SCREEN_MENU;
            break;
        case 1: screen = SCREEN_CODE; code_editing = false; break;
        case 2: screen = SCREEN_SPRITE; break;
        case 3: screen = SCREEN_TILE; break;
        case 4: screen = SCREEN_SFX; break;
        case 5: screen = SCREEN_MUSIC; break;
        case 6: prompt_file(PROMPT_SAVE); break;
        case 7: prompt_file(PROMPT_LOAD); break;
    }
}

static void adjust_sfx(int direction) {
    cart_sfx_t *sfx = &cartridge.sfx[selected_sfx];
    uint8_t *field = sfx_field == 0 ? &sfx->note :
                     sfx_field == 1 ? &sfx->waveform :
                     sfx_field == 2 ? &sfx->duration : &sfx->volume;
    unsigned max = sfx_field == 0 ? 95 : sfx_field == 1 ? 3 :
                   sfx_field == 2 ? 60 : 15;
    unsigned min = sfx_field == 2 ? 1 : 0;
    int value = (int)*field + direction;
    if (value < (int)min) value = (int)min;
    if (value > (int)max) value = (int)max;
    *field = (uint8_t)value;
}

static void begin_code_edit(void) {
    size_t start, length;
    if (!line_bounds(cartridge.code, code_line, &start, &length)) return;
    if (length >= sizeof(code_line_buffer)) {
        set_status("CODE LINE TOO LONG TO EDIT");
        return;
    }
    memcpy(code_line_buffer, cartridge.code + start, length);
    code_line_buffer[length] = '\0';
    code_cursor = length;
    code_editing = true;
    SDL_StartTextInput();
}

static void finish_code_edit(bool commit) {
    if (commit) {
        char replacement[sizeof(code_line_buffer) + 1];
        snprintf(replacement, sizeof(replacement), "%s", code_line_buffer);
        if (code_line + 1 >= source_line_count(cartridge.code)) {
            size_t length = strlen(replacement);
            replacement[length] = '\n';
            replacement[length + 1] = '\0';
        }
        if (!replace_source_line(code_line, replacement))
            set_status("CODE BUFFER FULL");
        else
            set_status("CODE LINE UPDATED");
    }
    code_editing = false;
    SDL_StopTextInput();
}

static void handle_keydown(const SDL_KeyboardEvent *key) {
    SDL_Scancode code = key->keysym.scancode;
    if (prompt != PROMPT_NONE) {
        if (code == SDL_SCANCODE_ESCAPE) {
            prompt = PROMPT_NONE;
            SDL_StopTextInput();
        } else if (code == SDL_SCANCODE_BACKSPACE) {
            size_t length = strlen(file_path);
            if (length) file_path[length - 1] = '\0';
        } else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
            if (*file_path) {
                if (prompt == PROMPT_SAVE) save_cartridge();
                else load_cartridge();
            }
            prompt = PROMPT_NONE;
            SDL_StopTextInput();
        }
        return;
    }
    if (code == SDL_SCANCODE_ESCAPE) {
        if (screen == SCREEN_GAME) {
            screen = SCREEN_MENU;
            set_status("RETURNED TO CONSOLE");
        } else if (screen == SCREEN_CODE && code_editing) {
            finish_code_edit(false);
        } else {
            screen = SCREEN_MENU;
        }
        return;
    }
    if (screen == SCREEN_MENU) {
        if (code == SDL_SCANCODE_UP && menu_choice) menu_choice--;
        if (code == SDL_SCANCODE_DOWN && menu_choice < 7) menu_choice++;
        if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
            handle_menu_choice();
    } else if (screen == SCREEN_CODE) {
        if (code_editing) {
            if (code == SDL_SCANCODE_BACKSPACE) {
                if (code_cursor) {
                    size_t length = strlen(code_line_buffer);
                    memmove(code_line_buffer + code_cursor - 1,
                            code_line_buffer + code_cursor, length - code_cursor + 1);
                    code_cursor--;
                }
            } else if (code == SDL_SCANCODE_DELETE) {
                size_t length = strlen(code_line_buffer);
                if (code_cursor < length)
                    memmove(code_line_buffer + code_cursor,
                            code_line_buffer + code_cursor + 1,
                            length - code_cursor);
            } else if (code == SDL_SCANCODE_LEFT && code_cursor) {
                code_cursor--;
            } else if (code == SDL_SCANCODE_RIGHT &&
                       code_cursor < strlen(code_line_buffer)) {
                code_cursor++;
            } else if (code == SDL_SCANCODE_HOME) {
                code_cursor = 0;
            } else if (code == SDL_SCANCODE_END) {
                code_cursor = strlen(code_line_buffer);
            } else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                finish_code_edit(true);
                if (code_line + 1 < source_line_count(cartridge.code)) code_line++;
            }
        } else if (code == SDL_SCANCODE_UP && code_line) {
            code_line--;
        } else if (code == SDL_SCANCODE_DOWN &&
                   code_line + 1 < source_line_count(cartridge.code)) {
            code_line++;
        } else if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
            begin_code_edit();
        } else if (code == SDL_SCANCODE_F5) {
            start_program();
        } else if (code == SDL_SCANCODE_S && (key->keysym.mod & KMOD_CTRL)) {
            prompt_file(PROMPT_SAVE);
        } else if (code == SDL_SCANCODE_O && (key->keysym.mod & KMOD_CTRL)) {
            prompt_file(PROMPT_LOAD);
        }
    } else if (screen == SCREEN_SPRITE) {
        if (code == SDL_SCANCODE_UP && sprite_y) sprite_y--;
        if (code == SDL_SCANCODE_DOWN && sprite_y < 7) sprite_y++;
        if (code == SDL_SCANCODE_LEFT && sprite_x) sprite_x--;
        if (code == SDL_SCANCODE_RIGHT && sprite_x < 7) sprite_x++;
        if (code == SDL_SCANCODE_Z)
            cart_set_sprite_pixel(&cartridge, selected_sprite, sprite_x, sprite_y,
                                  (uint8_t)sprite_color);
        if (code == SDL_SCANCODE_X)
            cart_set_sprite_pixel(&cartridge, selected_sprite, sprite_x, sprite_y, 0);
        if (code == SDL_SCANCODE_LEFTBRACKET && sprite_color) sprite_color--;
        if (code == SDL_SCANCODE_RIGHTBRACKET && sprite_color < 15) sprite_color++;
        if (code == SDL_SCANCODE_COMMA && selected_sprite) selected_sprite--;
        if (code == SDL_SCANCODE_PERIOD && selected_sprite < CART_SPRITE_COUNT - 1)
            selected_sprite++;
    } else if (screen == SCREEN_TILE) {
        if (code == SDL_SCANCODE_UP && tile_y) tile_y--;
        if (code == SDL_SCANCODE_DOWN && tile_y < 15) tile_y++;
        if (code == SDL_SCANCODE_LEFT && tile_x) tile_x--;
        if (code == SDL_SCANCODE_RIGHT && tile_x < 15) tile_x++;
        if (code == SDL_SCANCODE_Z)
            cart_set_tile(&cartridge, tile_x, tile_y, (uint8_t)selected_tile);
        if (code == SDL_SCANCODE_LEFTBRACKET && selected_tile) selected_tile--;
        if (code == SDL_SCANCODE_RIGHTBRACKET && selected_tile < 255) selected_tile++;
    } else if (screen == SCREEN_SFX) {
        if (code == SDL_SCANCODE_LEFT && selected_sfx) selected_sfx--;
        if (code == SDL_SCANCODE_RIGHT && selected_sfx < CART_SFX_COUNT - 1)
            selected_sfx++;
        if (code == SDL_SCANCODE_TAB) sfx_field = (sfx_field + 1) % 4;
        if (code == SDL_SCANCODE_UP) adjust_sfx(1);
        if (code == SDL_SCANCODE_DOWN) adjust_sfx(-1);
        if (code == SDL_SCANCODE_SPACE) queue_sfx(&cartridge.sfx[selected_sfx]);
    } else if (screen == SCREEN_MUSIC) {
        if (code == SDL_SCANCODE_UP && music_channel) music_channel--;
        if (code == SDL_SCANCODE_DOWN && music_channel < CART_MUSIC_CHANNELS - 1)
            music_channel++;
        if (code == SDL_SCANCODE_LEFT && music_step) music_step--;
        if (code == SDL_SCANCODE_RIGHT && music_step < CART_MUSIC_STEPS - 1)
            music_step++;
        size_t note_index = music_step * CART_MUSIC_CHANNELS + music_channel;
        if (code == SDL_SCANCODE_Z && cartridge.music[note_index] < 95)
            cart_set_music_note(&cartridge, music_step, music_channel,
                                (uint8_t)(cartridge.music[note_index] + 1));
        if (code == SDL_SCANCODE_X && cartridge.music[note_index])
            cart_set_music_note(&cartridge, music_step, music_channel,
                                (uint8_t)(cartridge.music[note_index] - 1));
        if (code == SDL_SCANCODE_SPACE) queue_music();
    }
}

int main(int argc, char **argv) {
    cart_init(&cartridge);
    const char *extension = argc > 1 ? strrchr(argv[1], '.') : NULL;
    if (extension && strcmp(extension, ".cart") == 0) {
        FILE *fp = fopen(argv[1], "rb");
        if (!fp || cart_load(fp, &cartridge) != 0) {
            if (fp) fclose(fp);
            fprintf(stderr, "Failed to load cartridge '%s'\n", argv[1]);
            return 1;
        }
        fclose(fp);
        set_status("CARTRIDGE LOADED");
    } else {
        const char *source_path = argc > 1 ? argv[1] : "asm/arkanoid.asm";
        if (!read_source_file(source_path, cartridge.code, sizeof(cartridge.code)) &&
            !read_source_file("vm/asm/arkanoid.asm", cartridge.code,
                              sizeof(cartridge.code))) {
            fprintf(stderr, "Failed to read Arkanoid assembly '%s'\n", source_path);
            return 1;
        }
        snprintf(cartridge.title, sizeof(cartridge.title), "%s",
                 argc > 1 ? "USER CARTRIDGE" : "ARKANOID");
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec wanted = {0}, obtained = {0};
        wanted.freq = 22050;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 1;
        wanted.samples = 512;
        audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
        if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    }

    const int scale = 5;
    SDL_Window *window = SDL_CreateWindow(
        "VM Fantasy Console - Arkanoid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VM_SCREEN_W * scale, VM_SCREEN_H * scale, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        if (audio_device) SDL_CloseAudioDevice(audio_device);
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        if (audio_device) SDL_CloseAudioDevice(audio_device);
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, VM_SCREEN_W, VM_SCREEN_H);
    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        VM_SCREEN_W, VM_SCREEN_H);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (audio_device) SDL_CloseAudioDevice(audio_device);
        SDL_Quit();
        return 1;
    }

    uint8_t framebuffer[VM_FB_SIZE];
    uint32_t argb_pixels[VM_SCREEN_W * VM_SCREEN_H];
    bool running = true;
    const double target_dt = 1.0 / 60.0;
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_counter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        double delta = (double)(now - last_counter) / (double)perf_freq;
        last_counter = now;
        if (delta > 0.1) delta = 0.1;
        accumulator += delta;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                handle_keydown(&event.key);
            } else if (event.type == SDL_TEXTINPUT && prompt != PROMPT_NONE) {
                size_t used = strlen(file_path);
                size_t incoming = strlen(event.text.text);
                if (used + incoming < sizeof(file_path))
                    memcpy(file_path + used, event.text.text, incoming + 1);
            } else if (event.type == SDL_TEXTINPUT && screen == SCREEN_CODE &&
                       code_editing) {
                size_t used = strlen(code_line_buffer);
                size_t incoming = strlen(event.text.text);
                if (used + incoming < sizeof(code_line_buffer)) {
                    memmove(code_line_buffer + code_cursor + incoming,
                            code_line_buffer + code_cursor, used - code_cursor + 1);
                    memcpy(code_line_buffer + code_cursor, event.text.text, incoming);
                    code_cursor += incoming;
                }
            }
        }

        if (screen == SCREEN_GAME) {
            uint32_t buttons = poll_inputs();
            memcpy(memory + VM_INPUT_ADDR, &buttons, sizeof(buttons));
            while (accumulator >= target_dt && !vm.halted && screen == SCREEN_GAME) {
                uint32_t sys_val = 0;
                memcpy(memory + VM_SYS_ADDR, &sys_val, sizeof(sys_val));
                vm.frame_ready = false;
                vm_run_env(&vm, vm_code, vm_code_len, 250000);
                accumulator -= target_dt;
            }
            if (vm.halted) {
                screen = SCREEN_MENU;
                set_status("PROGRAM HALTED");
            }
            render_framebuffer(memory + VM_FRONT_FB_ADDR, argb_pixels);
        } else {
            memset(framebuffer, 0, sizeof(framebuffer));
            if (prompt != PROMPT_NONE) draw_prompt(framebuffer);
            else switch (screen) {
                case SCREEN_MENU: draw_menu(framebuffer); break;
                case SCREEN_CODE: draw_code_editor(framebuffer); break;
                case SCREEN_SPRITE: draw_sprite_editor(framebuffer); break;
                case SCREEN_TILE: draw_tile_editor(framebuffer); break;
                case SCREEN_SFX: draw_sfx_editor(framebuffer); break;
                case SCREEN_MUSIC: draw_music_editor(framebuffer); break;
                case SCREEN_GAME: break;
            }
            render_framebuffer(framebuffer, argb_pixels);
        }
        SDL_UpdateTexture(texture, NULL, argb_pixels, VM_SCREEN_W * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
