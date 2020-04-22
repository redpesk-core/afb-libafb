#!/bin/bash

function fatal {
   echo "$*" 1>&2
   exit 1
}

function versofcmake {
   sed '/PROJECT_VERSION /!d;s/.*SION *\([0-9]*[.][0-9]*\).*/\1/' "${1:-CMakeLists.txt}"
}

function nameofgit {
   basename $(git -C ${1:-.} remote get-url origin) .git
}

# scan arguments
ba=
ha=
na=
oa=
pa=
va=
ha=
eval set -- $(getopt -ob:h:n:o:p:v: -lbase:head:name:output:prefix:version: -- "$@") || exit 1
while :; do
   case "$1" in
   -b|--base) ba="$2"; shift 2;;
   -h|--head) ha="$2"; shift 2;;
   -n|--name) na="$2"; shift 2;;
   -o|--output) oa="$2"; shift 2;;
   -p|--prefix) pa="$2"; shift 2;;
   -v|--version) va="$2"; shift 2;;
   --) shift; break;;
   esac
done

# get the base directory that must be git
test -z "${ba}" -a $# -ge 1 && ba="$1"
base=${ba:-$(dirname $0)}
test -d "${base}" || fatal "no directory ${base}"
test -d "${base}/.git" || fatal "no git in ${base}"

# get the root name of the tar file to create
name=${na:-$(nameofgit "${base}")}

# get the version (see how to use "git describe")
version=${va:-$(versofcmake "${base}/CMakeLists.txt")}

# get the prefix and the name of the tar output
prefix=${pa:-${name}-${version}}
output=$(realpath ${oa:-${prefix}.tar.gz})

# get the commit to archive
head=${ha:-HEAD}

# recapitulate
echo "base    $base"
echo "name    $name"
echo "version $version"
echo "prefix  $prefix"
echo "output  $output"
echo "head    $output"

# issue the command
git -C "${base}" archive --prefix="${prefix}/" --output="${output}" "${head}"

