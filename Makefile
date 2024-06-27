CC := gcc
# g - debug symbols MD - write source dependancies to .d
CFLAGS := -g -MD
BUILD_DIR := ./build

LIBS := -l wiringPi -l X11
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
# pull in object depenedencies
-include $(OBJS:.o=.d)

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
