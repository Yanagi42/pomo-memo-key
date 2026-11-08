CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS ?= -fsanitize=address,undefined

SRC := src/pose.c src/wav.c src/capsule.c src/selftest.c
BUILD := build

.PHONY: test clean

test: $(BUILD)/test_capsule
	./$(BUILD)/test_capsule

$(BUILD)/test_capsule: $(SRC) test/test_capsule.c src/capsule.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test/test_capsule.c $(SRC) $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
