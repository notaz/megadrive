CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size

TOOLSPATH = tools

TARGET = test

# CPPFLAGS += -DUSB_SERIAL -DLAYOUT_US_ENGLISH
CPPFLAGS += -D__MK20DX256__ -DF_CPU=48000000
CPPFLAGS += -DUSB_RAWHID
CPPFLAGS += -Wall -g -Os -mcpu=cortex-m4 -mthumb -nostdlib # -MMD
CXXFLAGS += -std=gnu++0x -felide-constructors -fno-exceptions -fno-rtti
LDFLAGS = -Os -Wl,--gc-sections -mcpu=cortex-m4 -mthumb -Tteensy3/mk20dx256.ld
LDLIBS += -lm

C_FILES := $(wildcard *.c)
CT_FILES := $(wildcard teensy3/*.c)
OBJS += $(C_FILES:.c=.o) $(CT_FILES:.c=.o)

all: $(TARGET).hex

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o "$@" $(OBJS) $(LDLIBS)

%.hex: %.elf
	$(SIZE) "$<"
	$(OBJCOPY) -O ihex -R .eeprom "$<" "$@"

clean:
	$(RM) $(TARGET).hex $(TARGET).elf $(OBJS)

up: $(TARGET).hex
	teensy_loader_cli -mmcu=mk20dx128 -w $<
