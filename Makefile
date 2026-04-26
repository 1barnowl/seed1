# SEED CORE — Makefile
# Target: Kali Linux / Debian / Ubuntu
# Deps:   sudo apt install build-essential libssl-dev zlib1g-dev

CC      := gcc
TARGET  := seed
PREFIX  := $(HOME)/.local/bin

# ── Sources ──────────────────────────────────────────────
SRC_DIR   := src
PRIM_DIR  := src/primitives
INC_DIR   := include

SRCS := \
	$(SRC_DIR)/seed.c         \
	$(SRC_DIR)/canonical.c    \
	$(SRC_DIR)/keymgmt.c      \
	$(SRC_DIR)/genome.c       \
	$(SRC_DIR)/store.c        \
	$(SRC_DIR)/verifier.c     \
	$(SRC_DIR)/growth.c       \
	$(PRIM_DIR)/prim_registry.c \
	$(PRIM_DIR)/prim_impls.c

OBJS := $(SRCS:.c=.o)

# ── Flags ────────────────────────────────────────────────
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic \
           -O2 -g \
           -I$(INC_DIR) \
           -D_POSIX_C_SOURCE=200809L \
           -D_GNU_SOURCE

LDFLAGS := -lssl -lcrypto -lz -lm

# ── Sanitizer target ─────────────────────────────────────
CFLAGS_ASAN := $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer

# ── Rules ────────────────────────────────────────────────
.PHONY: all clean install uninstall check asan

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Run:   ./$(TARGET) -h"
	@echo ""

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

asan:
	$(CC) $(CFLAGS_ASAN) -o $(TARGET)_asan $(SRCS) $(LDFLAGS)
	@echo "  Built ASAN binary: $(TARGET)_asan"

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_asan

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/$(TARGET)
	@echo "  Installed to $(PREFIX)/$(TARGET)"
	@echo "  Make sure $(PREFIX) is in your PATH"

uninstall:
	rm -f $(PREFIX)/$(TARGET)

# ── Dependency check ─────────────────────────────────────
check:
	@echo "Checking build dependencies..."
	@which gcc    >/dev/null 2>&1 && echo "  [OK] gcc"    || echo "  [MISSING] gcc — sudo apt install build-essential"
	@pkg-config --exists openssl 2>/dev/null \
	  && echo "  [OK] openssl"   \
	  || (echo "  [MISSING] openssl — sudo apt install libssl-dev" && false)
	@pkg-config --exists zlib   2>/dev/null \
	  && echo "  [OK] zlib"     \
	  || (echo "  [MISSING] zlib — sudo apt install zlib1g-dev" && false)
	@echo "  All dependencies satisfied."
