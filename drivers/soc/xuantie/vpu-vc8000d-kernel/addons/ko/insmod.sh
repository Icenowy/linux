#!/bin/sh
KERNEL_VER=$(uname -r)
BASE_PATH=/lib/modules/${KERNEL_VER}/extra

insmod $BASE_PATH/hantrodec.ko
