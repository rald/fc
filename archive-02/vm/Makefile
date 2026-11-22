CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude

BUILD_DIR = build
TARGET = vm
SRCS = src/main.c src/vm.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: %.c include/vm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
