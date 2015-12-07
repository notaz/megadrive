CFLAGS += -Wall -ggdb
ifndef DEBUG
CFLAGS += -O2
endif

TARGET = main
OBJS = main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS)

clean:
	$(RM) $(TARGET) $(OBJS)
