#include "cartridge.h"
#include "vm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_round_trip(void) {
    cartridge_t original, loaded;
    cart_init(&original);
    strcpy(original.title, "TEST CART");
    strcpy(original.code, "start:\n  movi r0, 7\n");
    assert(cart_set_sprite_pixel(&original, 3, 0, 0, 5));
    assert(cart_set_sprite_pixel(&original, 3, 7, 7, 12));
    assert(cart_set_tile(&original, 15, 15, 23));
    assert(cart_set_music_note(&original, 12, 2, 48));
    original.sfx[4] = (cart_sfx_t){48, 2, 20, 9};

    FILE *fp = tmpfile();
    assert(fp != NULL);
    assert(cart_save(fp, &original) == 0);
    rewind(fp);
    cart_init(&loaded);
    assert(cart_load(fp, &loaded) == 0);
    assert(strcmp(loaded.title, original.title) == 0);
    assert(strcmp(loaded.code, original.code) == 0);
    assert(memcmp(loaded.sprites, original.sprites, sizeof(original.sprites)) == 0);
    assert(memcmp(loaded.tiles, original.tiles, sizeof(original.tiles)) == 0);
    assert(memcmp(loaded.sfx, original.sfx, sizeof(original.sfx)) == 0);
    assert(memcmp(loaded.music, original.music, sizeof(original.music)) == 0);
    fclose(fp);
}

static void test_corruption_does_not_replace_cartridge(void) {
    cartridge_t original, loaded;
    cart_init(&original);
    strcpy(original.code, "movi r0, 1\n");
    FILE *fp = tmpfile();
    assert(fp != NULL);
    assert(cart_save(fp, &original) == 0);
    assert(fseek(fp, 14, SEEK_SET) == 0);
    int byte = fgetc(fp);
    assert(byte != EOF);
    assert(fseek(fp, 14, SEEK_SET) == 0);
    fputc(byte ^ 0x80, fp);
    rewind(fp);

    cart_init(&loaded);
    strcpy(loaded.title, "PRESERVED");
    assert(cart_load(fp, &loaded) == -1);
    assert(strcmp(loaded.title, "PRESERVED") == 0);
    fclose(fp);
}

static void test_editor_data_bounds(void) {
    cartridge_t cart;
    cart_init(&cart);
    cart.sfx[0].waveform = 4;
    FILE *invalid = tmpfile();
    assert(invalid != NULL);
    assert(cart_save(invalid, &cart) == -1);
    fclose(invalid);
    cart.sfx[0].waveform = 0;

    assert(cart_set_sprite_pixel(&cart, 0, 1, 2, 3));
    assert(cart_sprite_pixel(&cart, 0, 1, 2) == 3);
    assert(cart_sprite_pixel(&cart, 0, 0, 2) == 0);
    assert(cart_set_sprite_pixel(&cart, 0, 0, 2, 14));
    assert(cart_sprite_pixel(&cart, 0, 0, 2) == 14);
    assert(!cart_set_sprite_pixel(&cart, CART_SPRITE_COUNT, 0, 0, 1));
    assert(!cart_set_sprite_pixel(&cart, 0, 8, 0, 1));
    assert(!cart_set_sprite_pixel(&cart, 0, 0, 0, 16));

    assert(cart_set_tile(&cart, 15, 15, 255));
    assert(cart.tiles[255] == 255);
    assert(!cart_set_tile(&cart, 16, 0, 1));
    assert(cart_set_music_note(&cart, 15, 3, 95));
    assert(cart.music[63] == 95);
    assert(!cart_set_music_note(&cart, 16, 0, 1));
    assert(!cart_set_music_note(&cart, 0, 4, 1));
    assert(!cart_set_music_note(&cart, 0, 0, 96));
}

static void test_code_editor_assembly_input(void) {
    uint16_t code[4];
    size_t length = 0;
    assert(vm_assemble_source("start:\n  movi r0, 7\n  jmp start\n",
                              code, 4, &length) == 0);
    assert(length == 2);
    assert(code[0] == enc_ri8(OP_MOVI, 0, 7));
    assert(code[1] == enc_i12(OP_JMP, -2));

    uint16_t demo[4096];
    assert(vm_assemble_file("asm/arkanoid.asm", demo, 4096, &length) == 0);
    assert(length > 0);
}

int main(void) {
    test_round_trip();
    test_corruption_does_not_replace_cartridge();
    test_editor_data_bounds();
    test_code_editor_assembly_input();
    puts("cartridge tests passed");
    return 0;
}
