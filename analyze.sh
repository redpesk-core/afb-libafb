#!/bin/sh

cppcheck src/libafb

clang --analyze --analyzer-output text -I build/src -I src/libafb src/libafb/*/*.c
