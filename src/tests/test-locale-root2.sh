#!/bin/bash

root=tlr.dir

pt() {
	for code in fr fr-FR en en-US en-GB fr-CA fr-BE es es-PE es-SP; do
		dir=$root/locales/${code,,?}
		lan=${code%-*}
		mkdir -p $dir
		echo $code > $dir/a
		echo $code > $dir/$code
		echo $code > $dir/$lan
		echo $code > $root/$code
		if [ $code = $lan ]; then
			echo $code > $dir/b
		fi
	done
	echo xxx >  $root/c
	find $root -type f
}

ct() {
	rm -r $root
}

tf() {
	cat << EOC

###########################################################
###########################################################
#	WITH_OPENAT=${1:-0}
#	WITH_DIRENT=${2:-0}
#	WITH_LOCALE_FOLDER=${3:-0}
#	WITH_LOCALE_SEARCH_NODE=${4:-0}
########
EOC
	gcc -DTEST_locale_root=1 -o $root/tlr ../locale-root.c  ../subpath.c \
		-DWITH_OPENAT=${1:-0} \
		-DWITH_DIRENT=${2:-0} \
		-DWITH_LOCALE_FOLDER=${3:-0} \
		-DWITH_LOCALE_SEARCH_NODE=${4:-0}

#	strace -s 100 -o str-${1:-0}-${2:-0}-${3:-0}-${4:-0} \
	$root/tlr \
		@$root \
		+fr-FR,fi-FI,es-PE,en \
		a b c fr fr-FR es en \
		-fi-FI,fr-BE,en-US,es-MX \
		a b c fr fr-BE es en

}

pt

tf 0 0 0 0
tf 0 1 0 0
tf 0 1 1 0
tf 0 1 1 1

tf 1 0 0 0
tf 1 1 0 0
tf 1 1 1 0
tf 1 1 1 1

ct

