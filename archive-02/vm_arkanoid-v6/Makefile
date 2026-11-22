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
