#!/bin/sh

KDIR=/mnt/export/Testing/Board_Version_Control/SW_Common/SOCLE_MDK-3D/openlinux/2.6.29/v0_5/android_linux-2.6.29

echo
echo "KDIR=$KDIR"
echo "CROSS=$CROSS"

make KDIR=$KDIR $1

