CC := gcc
CFLAGS := -DNDEBUG -s -Os -flto -Wall -Wextra

# Target platform, specify with TARGET= on the command line, linux64 is default.
# Currently supported: linux64, linux32, win32
TARGET ?= linux64

ifeq ($(TARGET),linux32)
	TARGET_CFLAGS := -m32
else ifeq ($(TARGET),win32)
# If using a cross compiler, specify the compiler executable on the command line.
# make TARGET=win32 CC=~/c/mxe/usr/bin/i686-w64-mingw32.static-gcc
	TARGET_LIBS := -mconsole -municode
else ifneq ($(TARGET),linux64)
	$(error Supported targets: linux64, linux32, win32)
endif

OBJ_DIR := o/$(TARGET)

$(OBJ_DIR)/src/decoder/%.o: CFLAGS := -DNDEBUG -s -Ofast -flto -Wall -Wextra

SRC_DIRS := $(shell find src -type d)
C_FILES  := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
O_FILES  := $(foreach f,$(C_FILES:.c=.o),$(OBJ_DIR)/$f)

# Make build directories
$(shell mkdir -p $(foreach dir,$(SRC_DIRS),$(OBJ_DIR)/$(dir)))

.PHONY: all clean

all: z64decompress

z64decompress: $(O_FILES)
	$(CC) $(TARGET_CFLAGS) $(CFLAGS) $(O_FILES) -lm $(TARGET_LIBS) -o z64decompress

$(OBJ_DIR)/%.o: %.c
	$(CC) -c $(TARGET_CFLAGS) $(CFLAGS) $< -o $@

clean:
	$(RM) -rf z64compress bin o
