#!/bin/sh


KVER=$(uname -r)
KDIR=/lib/modules/$KVER/build

HLINA_START=0x30000000
HLINA_SIZE=512              #Size in megabytes

HLINA_END=$(($HLINA_START + $HLINA_SIZE*1024*1024))

echo
echo "KDIR=$KDIR"
echo "CROSS=$CROSS"
echo
echo   "Linear memory base   = $HLINA_START"
printf "Linear top-of-memory = 0x%x\n" $HLINA_END
echo   "Linear memory size   = ${HLINA_SIZE}MB"
echo

make KDIR=$KDIR CROSS=$CROSS HLINA_START=${HLINA_START}U HLINA_SIZE=${HLINA_SIZE}U $1
exit $?
