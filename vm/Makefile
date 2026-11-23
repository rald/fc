CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

BUILD_DIR = build
TARGET = vm
SRCS = src/main.c src/vm.c src/cartridge.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRCS))
TEST_TARGET = $(BUILD_DIR)/test_cartridge

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c include/vm.h include/cartridge.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

$(TEST_TARGET): tests/test_cartridge.c src/cartridge.c src/vm.c include/cartridge.h include/vm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/test_cartridge.c src/cartridge.c src/vm.c -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all run test clean
