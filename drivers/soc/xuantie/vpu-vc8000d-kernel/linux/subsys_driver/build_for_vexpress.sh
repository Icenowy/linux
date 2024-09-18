#!/bin/sh

KDIR=/mnt/export/Testing/Board_Version_Control/SW_Common/VExpress/linux-linaro-3.2-2012.01-0

echo
echo "KDIR=$KDIR"
echo "CROSS=$CROSS"

make KDIR=$KDIR $1

