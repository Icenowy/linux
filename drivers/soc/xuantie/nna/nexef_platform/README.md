3NX-F base platform driver
==========================

This document is here to explain how to build the linux drivers for the PCI FPGA based version of the 3NX-F.

It also expect you having basic knowledge on how to build the normal GPU driver and NNA driver.

### Building the drivers

##### 1. Getting the sources

First you need to have the NNA source tree (which you probably already have done as you read this file).
You will also need the NNPU (a GPU without graphic) driver that you can find here:

//powervr/swgraphics/rogueddk/MAIN/

##### 2. Building the base driver and NNA

Then you first build the NNA side driver which include the base 3NX-F driver: nexef_plat.ko

> To build the driver you will need first to locate the `tc_drv.h` file inside the GPU driver. It is normally located in
> `kernel/drivers/staging/imgtec/tc` and copy the full path of the folder containing it.

Start by building the NNA driver as usual:
- Creating a build folder
- Run cmake inside that build folder and configure as your needs
- run `make` inside the folder to build the userland part

Then we will build the three needed kernel modules:
- Go inside the `source/kernel/linux` folder
- run `make` with the proper arguments: 

```
make -f Makefile.testing CONFIG_VHA_NEXEF=y CONFIG_HW_AX3=y CONFIG_NEXEF_NNPU_INCLUDE=/path/to/tc_drv/folder
```

It should results with three `.ko` files: `nexef_platform/nexef_plat.ko`, `img_mem/img_mem.ko` and `vha/vha.ko`.
Copy them to a handy location for later use.

##### 3. Building the NNPU driver

You now need to build the NNPU driver. This document is not an extensive explanation of how to build this driver.
Please refer to the GPU/NNPU documentation on the dependencies you need and how to build it.

So the main thing you will need is to set the proper environment variable to build the NNPU driver:

```
MIPS_ELF_ROOT=/path/to/mips/toolchain
RGX_BVNC=32.6.52.603
EXCLUDED_APIS=vulkan renderscript openrl opengles1 opengles3 camerahal composerhal memtrackhal sensorhal
PVR_BUILD_DIR=tc_linux
CLDNN=1
KERNELDIR=/path/to/linux/kernel/sources
ROGUEDDK_FOLDER=/path/to/nnpu/sources
NPU_BUILD_FOLDER=binary_${PVR_BUILD_DIR}_${BUILD}
NNPU_BUILD_FOLDER_PATH=/path/to/nnpu/sources/${NNPU_BUILD_FOLDER}
NNPU_FIRMWARE_FILE=${NNPU_BUILD_FOLDER}/target_neutral/rgx.fw.${RGX_BVNC}
NNPU_PVRSRVKM_MODULE=${NNPU_BUILD_FOLDER}/target_x86_64/pvrsrvkm.ko
SUPPORT_KMS=1
```

You would also probably need these variable to be set:
```
PVRVERSION_WITHOUT_P4=1
DRIVER_BRANCH=MAIN
PDUMP=0
SUPPORT_OPENCL_2_X=1
```

Then from the root NNPU driver you would just need to run `make -j8 imgdnn imgdnn_test build`

Once the NNPU driver is build you will need to install the required files, you can use the provided install.sh shell.
**Be careful** to not install the kernel modules and the rc script with it!



### Loading the drivers

To load the kernel drivers, you will need to load them in order.
But before loading the kernel modules, you need to set the DUT clock on the FPGA. For that you need the dbg_py tool
to be installed on the system you are using, and run the python script `set_fpga_freq.py` to set the FPGA clock.

Then you will be able to load the driver:

First the base 3NX-F driver:
`sudo insmod nexef_plat.ko`

Then load the NNA and NNPU driver:
```
sudo insmod img_mem.ko
sudo insmod vha.ko
sudo insmod pvrsrvkm.ko
```

**You must not** load the `tc.ko` as it will conflict with the base 3NX-F driver.
You also don't need the `drm_pdp.ko`file as there is no support for it in the 3NX-F base driver.

Info: The 3NX-F base driver basically replace the `tc.ko` module in this configuration.


##### What does NeXeF mean?

(*Or why does the base driver is called nexef?*)  

*NeX* / *NeXeF* were coined by the GPT-2 Network (a transformer neural network) while trying to make it talking about
the 3NX-F. It added in the middle of a sentence "the 3NX (pronounced 3-Nex`)"

> **Imagination Technologies just released the 3NX-F**, a new "3NX" (pronounced "3-Nex") laser-based MEMS camera
> that has been designed to be "small, power-efficient, th1520-weight and with a compact size to be able to fit in a
> smartphone." The 3NX-F is an upgrade to the 1.5 million pixel 3NX that debuted in March.
