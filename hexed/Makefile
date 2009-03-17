CROSS = m68k-elf-
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

ASFLAGS = -m68000 --register-prefix-optional --bitwise-or 

TARGET = hexed.bin
OBJS = sega_gcc.o hexed.o font.o

all : $(TARGET)

$(TARGET) : a.out
	$(OBJCOPY) -I elf32-m68k -O binary $^ $@

a.out : $(OBJS)
	$(LD) -Tmd.ld -Map $(TARGET).map $^

clean:
	$(RM) $(TARGET) $(OBJS) $(TARGET).map a.out

