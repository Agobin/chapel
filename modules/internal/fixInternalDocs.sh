#!/bin/bash


# NOTE:
# This script _should_ eventually go away entirely as chpldoc improves.
# For now, we'll use common unix utilities to achieve a cleaner result.

if [ $# -ne 1 ]; then
  echo "Error: This script takes one argument, the path of the intermediate sphinx project"
  exit 1
fi

if [ ! -d "$1" ]; then
  echo "Error: Unable to find directory $1."
  exit 2
fi

TEMPDIR="$1"

cd "${TEMPDIR}/source/modules/internal/"

function removePattern() {
  sed "/$1/ { N; d; }" $2 > $2.tmp
  mv $2.tmp $2
}

function replace() {
  sed "s/$1/$2/g" $3 > $3.tmp
  mv $3.tmp $3
}

function fixTitle() {
  local base="$(basename $2 .rst)"
  replace "modules:: $base" "modules:: $1" $2

  # Replace the section header so the internal modules don't show up like this:
  # Module: ChapelFoo
  local titleLen=${#1}
  local header="$(printf '=%.0s' $(seq 1 $titleLen))"
  perl -0777 -i -pe "s/Module: $base\n=+\n/$1\n$header\n/g" $2
}

function removePrefixFunctions() {
  removePattern "proc [^a-zA-Z]" $1
  removePattern "proc chpl_" $1
}

## ChapelSyncvar ##

file="./ChapelSyncvar.rst"
replace "_syncvar" "sync" $file
replace "_singlevar" "single" $file
removePrefixFunctions $file
fixTitle "Synchronization Variables" $file

## End ChapelSyncvar ##


## Atomics ##

file="./Atomics.rst"

removePattern "type atomic_" $file
removePattern "proc atomic_" $file
removePattern "proc create_" $file
removePrefixFunctions $file

replace "atomic_int64" "atomic\(T\)" $file
replace "int(64)" "T" $file
fixTitle "Atomics" $file

## End Atomics ##
