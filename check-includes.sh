#!/bin/bash

cd $(dirname $0)

# checking that headers are "stand alone"

find src -type f -name '*.h' |
while read h; do
	echo checking $h ...
	echo "#include \"$h\"" |
	gcc -c -o /dev/null -x c -
done

# checking that any header is included

cd ./src/libafb
ls -1 */*.h |
while read h; do
	if ! grep -q -- "^#include \"$h\"" afb-*.h; then
		echo missing include to $h
	fi
done

ls -1 afb-*.h |
while read h; do
	if ! grep -q -- "^#include \"$h\"" libafb.h; then
		if [[ $h != afb-legacy.h ]]; then
			echo missing include to $h
		fi
	fi
done


