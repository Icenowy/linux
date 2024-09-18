#!/bin/sh
#dmesg -C
module="vc8000"
device="/dev/vc8000"
mode="666"
#Used to setup default parameters
DefaultParameter(){
    vcmd=1
    #default value can be added to here
}
echo

if [ ! -e /dev ]
then
    mkdir /dev/
fi
echo "Help information:"
echo "Input format should be like as below"
echo "./driver_load.sh vcmd=0(default) or (1)"
if [ $# -eq 0 ]
then
    DefaultParameter
    echo " Default vcmd_supported value = $vcmd"
else
    para_1="$1"
    vcmd_input=${para_1##*=}
    vcmd=$vcmd_input
    if [ $vcmd -ne 0 ] && [ $vcmd -ne 1 ]
    then
        echo "Invalid vcmd_supported value, which = $vcmd"
        echo "vcmd_supported should be 0 or 1"
    fi
    echo "vcmd_supported = $vcmd"
fi
#vcmd_supported = 0(default) or 1
#insert module
insmod $module.ko vcmd_supported=$vcmd || exit 1
#insmod $module.ko vcmd_supported=1 || exit 1

echo "module $module inserted"

#remove old nod
rm -f $device

#read the major asigned at loading time
major=`cat /proc/devices | grep $module | cut -c1-3`

echo "$module major = $major"

#create dev node
mknod $device c $major 0

echo "node $device created"

#give all 'rw' access
chmod $mode $device

echo "set node access to $mode"

#the end
echo
