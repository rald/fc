#include "cartridge.h"

#include <string.h>

#define CART_VERSION 1u
#define CART_FIXED_PAYLOAD (CART_TITLE_SIZE + 4u + \
                            CART_SPRITE_COUNT * CART_SPRITE_BYTES + \
                            CART_TILE_MAP_SIZE + CART_SFX_COUNT * 4u + \
                            CART_MUSIC_STEPS * CART_MUSIC_CHANNELS)
#define CART_MAX_PAYLOAD (CART_FIXED_PAYLOAD + CART_CODE_CAPACITY)

static void put_u16le(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

void cart_init(cartridge_t *cart) {
    if (!cart) return;
    memset(cart, 0, sizeof(*cart));
    memcpy(cart->title, "UNTITLED", 8);
    for (unsigned i = 0; i < CART_SFX_COUNT; i++) {
        cart->sfx[i].note = (uint8_t)(36u + i);
        cart->sfx[i].waveform = 0;
        cart->sfx[i].duration = 12;
        cart->sfx[i].volume = 10;
    }
}

static size_t code_length(const cartridge_t *cart) {
    size_t length = 0;
    while (length <= CART_CODE_CAPACITY && cart->code[length] != '\0') length++;
    return length;
}

int cart_save(FILE *fp, const cartridge_t *cart) {
    if (!fp || !cart) return -1;
    size_t code_size = code_length(cart);
    if (code_size > CART_CODE_CAPACITY) return -1;
    for (unsigned i = 0; i < CART_SFX_COUNT; i++)
        if (cart->sfx[i].note > 95 || cart->sfx[i].waveform > 3 ||
            cart->sfx[i].duration == 0 || cart->sfx[i].volume > 15)
            return -1;
    for (size_t i = 0; i < sizeof(cart->music); i++)
        if (cart->music[i] > 95) return -1;

    size_t payload_size = CART_FIXED_PAYLOAD + code_size;
    uint8_t payload[CART_MAX_PAYLOAD];
    uint8_t header[14];
    size_t at = 0;
    memcpy(payload + at, cart->title, CART_TITLE_SIZE);
    at += CART_TITLE_SIZE;
    put_u32le(payload + at, (uint32_t)code_size);
    at += 4;
    memcpy(payload + at, cart->code, code_size);
    at += code_size;
    memcpy(payload + at, cart->sprites, sizeof(cart->sprites));
    at += sizeof(cart->sprites);
    memcpy(payload + at, cart->tiles, sizeof(cart->tiles));
    at += sizeof(cart->tiles);
    for (unsigned i = 0; i < CART_SFX_COUNT; i++) {
        payload[at++] = cart->sfx[i].note;
        payload[at++] = cart->sfx[i].waveform;
        payload[at++] = cart->sfx[i].duration;
        payload[at++] = cart->sfx[i].volume;
    }
    memcpy(payload + at, cart->music, sizeof(cart->music));
    at += sizeof(cart->music);
    if (at != payload_size) return -1;

    memcpy(header, "VMFC", 4);
    put_u16le(header + 4, CART_VERSION);
    put_u32le(header + 6, (uint32_t)payload_size);
    put_u32le(header + 10, crc32_bytes(payload, payload_size));
    return fwrite(header, 1, sizeof(header), fp) == sizeof(header) &&
           fwrite(payload, 1, payload_size, fp) == payload_size ? 0 : -1;
}

int cart_load(FILE *fp, cartridge_t *cart) {
    if (!fp || !cart) return -1;
    uint8_t header[14];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
        memcmp(header, "VMFC", 4) != 0 ||
        get_u16le(header + 4) != CART_VERSION)
        return -1;
    uint32_t payload_size = get_u32le(header + 6);
    if (payload_size < CART_FIXED_PAYLOAD || payload_size > CART_MAX_PAYLOAD)
        return -1;
    uint8_t payload[CART_MAX_PAYLOAD];
    if (fread(payload, 1, payload_size, fp) != payload_size ||
        get_u32le(header + 10) != crc32_bytes(payload, payload_size))
        return -1;

    cartridge_t loaded;
    cart_init(&loaded);
    size_t at = 0;
    memcpy(loaded.title, payload + at, CART_TITLE_SIZE);
    at += CART_TITLE_SIZE;
    uint32_t code_size = get_u32le(payload + at);
    at += 4;
    if (code_size > CART_CODE_CAPACITY ||
        payload_size != CART_FIXED_PAYLOAD + code_size)
        return -1;
    memcpy(loaded.code, payload + at, code_size);
    loaded.code[code_size] = '\0';
    at += code_size;
    if (memchr(loaded.code, '\0', code_size) != NULL) return -1;
    memcpy(loaded.sprites, payload + at, sizeof(loaded.sprites));
    at += sizeof(loaded.sprites);
    memcpy(loaded.tiles, payload + at, sizeof(loaded.tiles));
    at += sizeof(loaded.tiles);
    for (unsigned i = 0; i < CART_SFX_COUNT; i++) {
        loaded.sfx[i].note = payload[at++];
        loaded.sfx[i].waveform = payload[at++];
        loaded.sfx[i].duration = payload[at++];
        loaded.sfx[i].volume = payload[at++];
        if (loaded.sfx[i].note > 95 || loaded.sfx[i].waveform > 3 ||
            loaded.sfx[i].duration == 0 || loaded.sfx[i].volume > 15)
            return -1;
    }
    memcpy(loaded.music, payload + at, sizeof(loaded.music));
    for (size_t i = 0; i < sizeof(loaded.music); i++)
        if (loaded.music[i] > 95) return -1;
    *cart = loaded;
    return 0;
}

uint8_t cart_sprite_pixel(const cartridge_t *cart, unsigned sprite,
                          unsigned x, unsigned y) {
    if (!cart || sprite >= CART_SPRITE_COUNT || x >= 8 || y >= 8) return 0;
    size_t index = sprite * CART_SPRITE_BYTES + y * 4u + x / 2u;
    uint8_t packed = cart->sprites[index];
    return (uint8_t)((x & 1u) ? packed >> 4 : packed & 0x0Fu);
}

bool cart_set_sprite_pixel(cartridge_t *cart, unsigned sprite,
                           unsigned x, unsigned y, uint8_t color) {
    if (!cart || sprite >= CART_SPRITE_COUNT || x >= 8 || y >= 8 || color > 15)
        return false;
    size_t index = sprite * CART_SPRITE_BYTES + y * 4u + x / 2u;
    uint8_t shift = (uint8_t)((x & 1u) * 4u);
    cart->sprites[index] = (uint8_t)((cart->sprites[index] & ~(0x0Fu << shift)) |
                                     ((color & 0x0Fu) << shift));
    return true;
}

bool cart_set_tile(cartridge_t *cart, unsigned x, unsigned y, uint8_t tile) {
    if (!cart || x >= 16 || y >= 16) return false;
    cart->tiles[y * 16u + x] = tile;
    return true;
}

bool cart_set_music_note(cartridge_t *cart, unsigned step, unsigned channel,
                         uint8_t note) {
    if (!cart || step >= CART_MUSIC_STEPS || channel >= CART_MUSIC_CHANNELS ||
        note > 95)
        return false;
    cart->music[step * CART_MUSIC_CHANNELS + channel] = note;
    return true;
}
