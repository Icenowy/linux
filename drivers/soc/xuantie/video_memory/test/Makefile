CFLAGS = -Wall -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE -O2 -Werror -Wno-unused -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-strict-overflow -Wno-array-bounds -Wno-shift-negative-value -Wempty-body -Wtype-limits -Wno-unused-result -fPIC -Wmissing-field-initializers -std=gnu99

INCLUDE += -I../lib

SRCS = video_memory_test.c
OBJS = $(SRCS:.c=.o)

#source search path
vpath %.c

# name of the outputfile (library)
TARGET = vidmem_test

#Here are rules for building codes and generating object library.
all:    tags
	@echo ---------------------------------------
	@echo "Usage: make [ system | testdata | versatile | integrator | android]"
	@echo "system     - PC system model (== pclinux)"
	@echo "testdata   - PC system model for test data creation"
	@echo "eval       - PC system model for evaluation with frame limit"
	@echo "versatile  - ARM versatile with FPGA HW"
	@echo "integrator - ARM integrator with FPGA HW"
	@echo "android    - ARM android"
	@echo "NOTE! Make sure to do 'make clean'"
	@echo "between compiling to different targets!"
	@echo ---------------------------------------

.PHONY: system_lib system testdata clean depend

evaluation: eval
eval: system

system_static: testdata
system: testdata

# for libva
system_lib: system

testdata: .depend


.PHONY: hwlinux
hwlinux:
hwlinux: .depend $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -L../output/rootfs/bsp/vidmem/lib -lvmem -lm -lpthread -o $(TARGET)



%.o: %.c
	$(CC) -c $(CFLAGS) $(INCLUDE) $< -o $@

clean:
	$(RM) $(TARGET)
	$(RM) .depend
	$(RM) *.o *.gcno *.gcda

tags:
	ctags ../kernel_mode/*h ./*[ch]

depend .depend: $(SRCS)
	@echo "[CC] $@"
	@$(CC) -M $(DEBFLAGS) $(INCLUDE) $^ > .depend

ifneq (clean, $(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
