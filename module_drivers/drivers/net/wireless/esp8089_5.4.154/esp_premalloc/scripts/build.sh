#!/bin/bash
# 
#  Copyright (c) 2013 Espressif system
#

TARGETS="
a33
a23
rk2
a33_tengzhong
rk3026_tengzhong
rk3026_yingka
"

TARGETS_NUM=6

function help {
	echo "Please check build target! $TARGETS "
}

if [ ! $# = 1 ]; then
	help
	exit
fi

let "loops = 0"
for TARGET in $TARGETS; do
	if [ $1 == $TARGET ]; then
		echo "buid $TARGET"
		break
	fi
	let "loops++"
done

if [ $loops -ge $TARGETS_NUM ]; then
	help
	exit
fi

#make clean
source configs/$TARGET.env
make clean
make
