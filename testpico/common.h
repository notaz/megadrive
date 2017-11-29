#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define noinline __attribute__((noinline))
#define unused   __attribute__((unused))
#define _packed  __attribute__((packed))

#define mem_barrier() \
    asm volatile("":::"memory")

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define MKLONG(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define read8(a) \
    *((volatile u8 *) (a))
#define read16(a) \
    *((volatile u16 *) (a))
#define read32(a) \
    *((volatile u32 *) (a))
#define write8(a, d) \
    *((volatile u8 *) (a)) = (d)
#define write16(a, d) \
    *((volatile u16 *) (a)) = (d)
#define write32(a, d) \
    *((volatile u32 *) (a)) = (d)

enum x32x_cmd {
    CMD_ECHO = 1,
    CMD_READ_FRT = 2, // read Free-Running Timer
    CMD_READ8 = 3,
    CMD_READ16 = 4,
    CMD_READ32 = 5,
    CMD_WRITE8 = 6,
    CMD_WRITE16 = 7,
    CMD_WRITE32 = 8,
};

// vim:ts=4:sw=4:expandtab
