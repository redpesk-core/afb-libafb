#!/bin/sh

doit() {

	cppcheck src/libafb

	clang --analyze --analyzer-output text -I src/libafb -I build/src/libafb src/libafb/*/*.c
}

doit |& tee analyze.output
