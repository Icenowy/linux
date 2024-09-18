CFLAGS = -Wall -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE -O2 -Werror -Wno-unused -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-strict-overflow -Wno-array-bounds -Wno-shift-negative-value -Wempty-body -Wtype-limits -Wno-unused-result -fPIC -Wmissing-field-initializers -std=gnu99

INCLUDE += -I../driver

SRCS = video_mem.c

OUT_PATH = ../output/rootfs/bsp/vidmem/lib
TARGET = $(OUT_PATH)/libvmem.so

$(TARGET):
	$(shell if [ ! -e $(OUT_PATH) ];then mkdir -p $(OUT_PATH); fi)
	$(CC) $(SRCS) $(CFLAGS) $(INCLUDE) -shared -o $(TARGET)

clean:
	$(RM) -r $(OUT_PATH)

