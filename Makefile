# ─────────────────────────────────────────────────────────────────────────────
# Makefile  –  OS-Level Chat Server Using Thread Pools
# CS2006 Operating Systems  |  Spring 2026
#
# Targets:
#   make           → build chat_sim (default)
#   make clean     → remove all build artifacts
#   make run       → build + run with default parameters
#   make helgrind  → run under Valgrind Helgrind (race-condition detector)
#   make memcheck  → run under Valgrind Memcheck  (memory leak checker)
#
# Prerequisites:
#   - Raylib development package:
#       Ubuntu/Debian: sudo apt install libraylib-dev
#       Arch Linux:    sudo pacman -S raylib
#   - raygui.h must be placed in src/ (single‑header, no separate install)
# ─────────────────────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -g \
          -D_POSIX_C_SOURCE=200809L \
          -I./src

# Try to obtain Raylib flags via pkg-config (works on both Ubuntu and Arch)
RAYLIB_CFLAGS  := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LDFLAGS := $(shell pkg-config --libs raylib 2>/dev/null)

# Fallback manual flags if pkg-config fails (e.g., on systems without .pc file)
ifeq ($(RAYLIB_LDFLAGS),)
    RAYLIB_CFLAGS  =
    RAYLIB_LDFLAGS = -lraylib -lm -lpthread -lGL -ldl -lrt -lX11
endif

CFLAGS  += $(RAYLIB_CFLAGS)
LDFLAGS = $(RAYLIB_LDFLAGS) -lpthread -lm

# Source files
SRCS = src/main.c \
       src/threadpool.c \
       src/rooms.c \
       src/logger.c \
       src/client_gen.c \
       src/gui.c \
       src/utils.c \
       src/privmsg.c \
       src/ratelimit.c

TARGET = chat_sim

# ── Default target ────────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build successful →  ./$(TARGET)"
	@echo "Run:  ./$(TARGET) --threads 8 --clients 30 --duration 60"
	@echo ""

# ── Quick run ─────────────────────────────────────────────────────────────
run: all
	./$(TARGET) --threads 8 --clients 30 --duration 60

# ── Race-condition detection (Helgrind) ──────────────────────────────────
helgrind: all
	valgrind --tool=helgrind --log-file=helgrind.log \
	    ./$(TARGET) --threads 5 --clients 20 --duration 10 &
	@echo "Helgrind running in background. Output → helgrind.log"

# ── Memory leak check ─────────────────────────────────────────────────────
memcheck: all
	valgrind --tool=memcheck --leak-check=full --log-file=memcheck.log \
	    ./$(TARGET) --threads 5 --clients 10 --duration 5 &
	@echo "Memcheck running in background. Output → memcheck.log"

# ── Clean ─────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) *.log *.log.txt

.PHONY: all run helgrind memcheck clean