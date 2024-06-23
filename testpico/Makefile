CROSS = m68k-elf-
HOSTCC = gcc
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CROSS_SH = sh-elf-
CC_SH = $(CROSS_SH)gcc
LD_SH = $(CROSS_SH)ld
OBJCOPY_SH = $(CROSS_SH)objcopy

ASFLAGS += -m68000 --register-prefix-optional --bitwise-or -pic
ASFLAGS_CC += -Wa,-m68000 -Wa,--register-prefix-optional -Wa,--bitwise-or -Wa,-pic
CFLAGS += -Wall -g -O2 -m68000 -fomit-frame-pointer
LDLIBS_LIBGCC := $(shell $(CC) -print-file-name=libgcc.a)
LDLIBS += $(LDLIBS_LIBGCC)

ifeq ($(shell m68k-elf-gcc -dD -E -c - --param=min-pagesize=0 < /dev/null > /dev/null 2>&1 && echo ok),ok)
CFLAGS += --param=min-pagesize=0
endif

SUFFIX := $(shell git describe --always --dirty)
ifdef PICO
CFLAGS += -DPICO
SUFFIX := $(SUFFIX)-for-pd
endif
TARGET_BASE = testpico
TARGET = $(TARGET_BASE)-$(SUFFIX)
OBJS = sega_gcc.o main.o asmtools.o

all: $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(LD) -o $@ -Tsega.ld -Map $(TARGET_BASE).map $^ $(LDLIBS)

clean:
	$(RM) $(TARGET_BASE)*.bin $(OBJS) *.elf *.map fill
	$(RM) *.lst *.bin80 *.osh *.binsh

$(TARGET).bin: $(TARGET).elf fill
	$(OBJCOPY) -I elf32-m68k -O binary $< $@
	./fill $@

fill: fill.c
	$(HOSTCC) -o $@ $^ -Wall -O2

%.o: %.S
	$(CC) -c -o $@ $^ $(CFLAGS) $(ASFLAGS_CC)

%.bin80: %.s80
	sjasm $< $@

# sh2
TARGET_SH = sh2_test
OBJS_SH = sh2_test.osh sh2_main.osh

CFLAGS_SH = -m2 -mb -O2 -Wall

%.binsh: %.elf
	$(OBJCOPY_SH) -O binary $< $@

$(TARGET_SH).elf: $(OBJS_SH)
	$(LD_SH) -o $@ $^ -Tmars.ld -Map $(TARGET_SH).map -nostdlib

%.osh: %.sh2
	$(CC_SH) -o $@ -c -x assembler $< $(CFLAGS_SH)

%.osh: %.c
	$(CC_SH) -o $@ -c $< $(CFLAGS_SH)

# manual deps
sega_gcc.o: z80_test.bin80
sega_gcc.o: sh2_test.binsh

up: $(TARGET).bin
	scp $< root@router:/tmp/

.PHONY: all clean
