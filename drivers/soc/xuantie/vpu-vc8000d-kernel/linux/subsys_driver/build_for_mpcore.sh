#!/bin/sh

KDIR=/mnt/export/Testing/Board_Version_Control/SW_Common/ARM_realview_v6/2.6.28-arm1/v0_1-v6/linux-2.6.28-arm1

echo
echo "KDIR=$KDIR"
echo "CROSS=$CROSS"

make KDIR=$KDIR $1

