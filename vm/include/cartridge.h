#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CART_TITLE_SIZE 32u
#define CART_CODE_CAPACITY 32768u
#define CART_SPRITE_COUNT 256u
#define CART_SPRITE_BYTES 32u
#define CART_TILE_MAP_SIZE 256u
#define CART_SFX_COUNT 16u
#define CART_MUSIC_STEPS 16u
#define CART_MUSIC_CHANNELS 4u

typedef struct {
    uint8_t note;
    uint8_t waveform;
    uint8_t duration;
    uint8_t volume;
} cart_sfx_t;

typedef struct {
    char title[CART_TITLE_SIZE];
    char code[CART_CODE_CAPACITY + 1u];
    uint8_t sprites[CART_SPRITE_COUNT * CART_SPRITE_BYTES];
    uint8_t tiles[CART_TILE_MAP_SIZE];
    cart_sfx_t sfx[CART_SFX_COUNT];
    uint8_t music[CART_MUSIC_STEPS * CART_MUSIC_CHANNELS];
} cartridge_t;

void cart_init(cartridge_t *cart);
int cart_save(FILE *fp, const cartridge_t *cart);
int cart_load(FILE *fp, cartridge_t *cart);
uint8_t cart_sprite_pixel(const cartridge_t *cart, unsigned sprite,
                          unsigned x, unsigned y);
bool cart_set_sprite_pixel(cartridge_t *cart, unsigned sprite,
                           unsigned x, unsigned y, uint8_t color);
bool cart_set_tile(cartridge_t *cart, unsigned x, unsigned y, uint8_t tile);
bool cart_set_music_note(cartridge_t *cart, unsigned step, unsigned channel,
                         uint8_t note);

#endif
