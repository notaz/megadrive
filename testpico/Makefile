CROSS = m68k-elf-
HOSTCC = gcc
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

ASFLAGS += -m68000 --register-prefix-optional --bitwise-or -pic
ASFLAGS_CC += -Wa,-m68000 -Wa,--register-prefix-optional -Wa,--bitwise-or -Wa,-pic
CFLAGS += -Wall -g -O2 -m68000 -fomit-frame-pointer
#CFLAGS += -DPICO
LDLIBS += $(shell $(CC) -print-file-name=libgcc.a)

TARGET = testpico
OBJS = sega_gcc.o main.o asmtools.o data.o

all: $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(LD) -o $@ -Tsega.ld -Map $(TARGET).map $^ $(LDLIBS)

clean:
	$(RM) $(TARGET).bin $(OBJS) $(TARGET).elf $(TARGET).map fill
	$(RM) *.lst *.bin80

$(TARGET).bin: $(TARGET).elf fill
	$(OBJCOPY) -I elf32-m68k -O binary $< $@
	./fill $@

fill: fill.c
	$(HOSTCC) -o $@ $^ -Wall -O2

%.o: %.S
	$(CC) -c -o $@ $^ $(CFLAGS) $(ASFLAGS_CC)

%.bin80: %.s80
	sjasm $< $@

# manual deps
data.o: z80_test.bin80

.PHONY: all clean
