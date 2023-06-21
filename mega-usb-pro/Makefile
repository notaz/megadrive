CFLAGS += -Wall -ggdb
ifndef DEBUG
CFLAGS += -O2
endif

TARGET = mega-usb-pro

all: $(TARGET)

clean:
	$(RM) $(TARGET)

up: $(TARGET)
	scp $< root@router:/tmp/
