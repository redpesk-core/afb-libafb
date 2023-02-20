#!/bin/sh

doit() {

	echo
	figlet cppcheck 2>/dev/null || echo C P P C H E C K
	echo
	cppcheck src/libafb

	echo
	figlet clang 2>/dev/null || echo C L A N G
	echo
	clang-14 --analyze --analyzer-output text -I src/libafb -I build/src/libafb -I ~/.locenv/afb/include src/libafb/*/*.c

	echo
	figlet gcc 2>/dev/null || echo G C C
	echo
	gcc -fanalyzer -I src/libafb -I build/src/libafb -I ~/.locenv/afb/include src/libafb/*/*.c

	echo
	figlet clang-tidy 2>/dev/null || echo C L A N G - T I D Y
	echo
	clang-tidy -p build/compile_commands.json -config-file .clang-tidy  src/libafb/*/*.c
}

doit 2>&1 | tee analyze.output
