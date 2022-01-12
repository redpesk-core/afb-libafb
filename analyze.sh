#!/bin/sh

doit() {

	echo
	figlet cppcheck 2>/dev/null || echo C P P C H E C K
	echo
	cppcheck src/libafb

	echo
	figlet clang 2>/dev/null || echo C L A N G
	echo
	clang --analyze --analyzer-output text -I src/libafb -I build/src/libafb src/libafb/*/*.c
}

doit 2>&1 | tee analyze.output
