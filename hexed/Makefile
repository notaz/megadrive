CROSS = m68k-elf-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

ASFLAGS += -m68000 --register-prefix-optional --bitwise-or -pic
CPPFLAGS += -Wa,-m68000 -Wa,--register-prefix-optional -Wa,--bitwise-or -Wa,-pic

TARGET = hexed.bin
OBJS = sega_gcc.o hexed.o transfer.o font.o

all: $(TARGET)

$(TARGET): a.out
	$(OBJCOPY) -I elf32-m68k -O binary $^ $@

a.out: $(OBJS)
	$(LD) -Tmd.ld -Map $(TARGET).map $^

clean:
	$(RM) $(TARGET) $(OBJS) $(TARGET).map a.out pc_transfer


pc_transfer: pc_transfer.c
	gcc -Wall -O2 -ggdb -o $@ $^ -lz

%.o: %.S
	$(CC) -c -o $@ $^ $(CPPFLAGS)

%.bin: %.o
	$(OBJCOPY) -I elf32-m68k -O binary $^ $@


# ----------- release -----------
ifneq ($(findstring rel,$(MAKECMDGOALS)),)
ifeq ($(VER),)
$(error need VER)
endif
endif

rel: hexed.bin readme.txt src
	zip -9 -r hexed_r$(VER).zip $^
	rm -rf src

src: hexed.s font.s sega_gcc.s md.ld Makefile
	mkdir src
	cp $^ src/
