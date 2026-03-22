# Makefile
# --------
# Build system for the project. GNU Make.
#
# Compiler flags to use:
#    CC = gcc
#    CFLAGS = -std=c11 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L -I./src
#    LDFLAGS = -lraylib -lm -lpthread -lGL -ldl -lrt -lX11
#
# Source files (all of them):
#    src/main.c src/threadpool.c src/rooms.c src/logger.c
#    src/client_gen.c src/gui.c src/utils.c src/privmsg.c src/ratelimit.c
#
# Target binary name: chat_sim
#
# Targets to implement:
#
#    make           --> builds chat_sim (default target)
#                      after successful build print:
#                      "Build successful --> ./chat_sim"
#                      "Run: ./chat_sim --threads 8 --clients 30 --duration 60"
#
#    make run       --> builds then runs with default parameters
#
#    make helgrind  --> runs under valgrind --tool=helgrind
#                      logs output to helgrind.log
#                      use short duration (10s) for this
#
#    make memcheck  --> runs under valgrind --tool=memcheck --leak-check=full
#                      logs output to memcheck.log
#
#    make clean     --> removes chat_sim binary and all .log .log.txt files
#
# NOTE: raylib wont be in apt on Ubuntu 22.04 -- run setup.sh first
# to build it from source. After that make should work fine.
#
# .PHONY targets: all run helgrind memcheck clean