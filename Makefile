CC := gcc
CFLAGS := -g
BUILD_DIR := ./build

LIBS := -l wiringPi
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

EXEC := $(BUILD_DIR)/display

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

.PHONY: clean
clean:
	rm -r $(BUILD_DIR)

.PHONY: test
test: $(EXEC)
	./$(BUILD_DIR)/display
